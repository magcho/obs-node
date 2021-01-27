#include "source.h"
#include "callback.h"
#include <media-io/video-frame.h>
#include <util/platform.h>

#define MAX_AUDIO_BUFFER 500000000 // 500ms
#define AUDIO_SMOOTH_THRESHOLD 70000000

struct ts_info {
    uint64_t start;
    uint64_t end;
};

static inline uint64_t uint64_diff(uint64_t ts1, uint64_t ts2) {
    return (ts1 < ts2) ? (ts2 - ts1) : (ts1 - ts2);
}

SourceType Source::getSourceType(const std::string &sourceType) {
    if (sourceType == "Image") {
        return Image;
    } else if (sourceType == "MediaSource") {
        return MediaSource;
    } else {
        throw std::invalid_argument("Invalid sourceType: " + sourceType);
    }
}

std::string Source::getSourceTypeString(SourceType sourceType) {
    switch (sourceType) {
        case Image:
            return "Image";
        case MediaSource:
            return "MediaSource";
        default:
            throw std::invalid_argument("Invalid sourceType: " + std::to_string(sourceType));
    }
}

void Source::volmeter_callback(void *param, const float *magnitude, const float *peak, const float *input_peak) {
    auto source = static_cast<Source *>(param);
    auto callback = Callback::getVolmeterCallback();
    if (callback && source->obs_volmeter) {
        int channels = obs_volmeter_get_nr_channels(source->obs_volmeter);
        std::vector<float> vecMagnitude;
        std::vector<float> vecPeak;
        std::vector<float> vecInputPeak;
        for (size_t ch = 0; ch < channels; ch++) {
            vecMagnitude.push_back(magnitude[ch]);
            vecPeak.push_back(peak[ch]);
            vecInputPeak.push_back(input_peak[ch]);
        }
        callback(source->sceneId, source->id, channels, vecMagnitude, vecPeak, vecInputPeak);
    }
}

void Source::source_media_get_frame_callback(void *param, calldata_t *data) {
    auto source = (Source *) param;
    auto *frame = (obs_source_frame *) calldata_ptr(data, "frame");

    // create video scaler after first frame is received
    if (!source->video_scaler) {
        source->create_video_scaler(frame);
    }

    source->frame_buf_mutex.lock();

    obs_source_frame *new_frame = obs_source_frame_create(frame->format, frame->width, frame->height);
    obs_source_frame_copy(new_frame, frame);

    circlebuf_push_back(&source->frame_buf, &new_frame, sizeof(void *));

    source->frame_buf_mutex.unlock();
}

void Source::video_output_callback(void *param) {
    auto *source = (Source *) param;
    const struct video_output_info *voi = video_output_get_info(source->video);
    uint64_t frame_time_ns = util_mul_div64(1000000000UL, voi->fps_den, voi->fps_num);

    while (!source->video_stop) {

        source->frame_buf_mutex.lock();

        while (source->frame_buf.size) {

            if (!source->video_time) {
                source->video_time = os_gettime_ns();
            }

            obs_source_frame *frame = nullptr;
            circlebuf_pop_front(&source->frame_buf, &frame, sizeof(void *));

            source->timing_mutex.lock();
            source->timing_adjust = source->video_time - frame->timestamp;
            source->timing_mutex.unlock();

            struct video_frame output_frame = {};
            if (video_output_lock_frame(source->video, &output_frame, 1, source->video_time)) {
                video_scaler_scale(
                        source->video_scaler,
                        output_frame.data,
                        output_frame.linesize,
                        frame->data,
                        frame->linesize
                );
                video_output_unlock_frame(source->video);
            }

            source->video_time += frame_time_ns;

            bool paused = obs_source_media_get_state(source->obs_source) == OBS_MEDIA_STATE_PAUSED;
            if (paused && !source->frame_buf.size) {
                frame->timestamp += frame_time_ns;
                circlebuf_push_back(&source->frame_buf, &frame, sizeof(void *));
                break;
            } else {
                obs_source_frame_destroy(frame);
            }
        }

        source->frame_buf_mutex.unlock();
        os_sleepto_ns(source->video_time);
    }
}

void Source::source_media_get_audio_callback(void *param, calldata_t *data) {
    auto source = (Source *) param;
    auto *audio = (obs_source_audio *) calldata_ptr(data, "audio");

    size_t size = audio->frames * sizeof(float);
    size_t channels = audio_output_get_channels(source->audio);
    size_t rate = audio_output_get_sample_rate(source->audio);

    source->timing_mutex.lock();
    uint64_t timing_adjust = source->timing_adjust;
    source->timing_mutex.unlock();

    if (!timing_adjust) {
        return;
    }

    source->audio_buf_mutex.lock();

    uint64_t current_audio_time = audio->timestamp + timing_adjust;
    uint64_t diff = uint64_diff(source->last_audio_time, current_audio_time);

    if (!source->audio_time || current_audio_time < source->audio_time || current_audio_time - source->audio_time > MAX_AUDIO_BUFFER) {
        blog(LOG_INFO, "[%s] reset audio buffer, audio_time: %lld ms, current audio time: %lld ms",
             source->id.c_str(), source->audio_time, current_audio_time);
        for (size_t i = 0; i < channels; i++) {
            circlebuf_pop_front(&source->audio_buf[i], nullptr, source->audio_buf[i].size);
        }
        source->audio_time = current_audio_time;
        source->last_audio_time = current_audio_time;
    } else if (diff > AUDIO_SMOOTH_THRESHOLD) {
        blog(LOG_INFO, "[%s] audio buffer placement: %lld", source->id.c_str(), diff);
        size_t buf_placement = ns_to_audio_frames(rate, current_audio_time - source->audio_time) * sizeof(float);
        for (size_t i = 0; i < channels; i++) {
            circlebuf_place(&source->audio_buf[i], buf_placement, audio->data[i], size);
            circlebuf_pop_back(&source->audio_buf[i], nullptr,
                               source->audio_buf[i].size - (buf_placement + size));
        }
        source->last_audio_time = current_audio_time;
    } else {
        for (size_t i = 0; i < channels; i++) {
            circlebuf_push_back(&source->audio_buf[i], audio->data[i], size);
        }
    }

    source->last_audio_time += audio_frames_to_ns(rate, audio->frames);
    source->audio_buf_mutex.unlock();
}

bool Source::audio_output_callback(
        void *param,
        uint64_t start_ts_in,
        uint64_t end_ts_in,
        uint64_t *out_ts,
        uint32_t mixers,
        struct audio_output_data *mixes) {

    UNUSED_PARAMETER(mixers);

    auto source = (Source *) param;
    auto audio = source->audio;
    size_t channels = audio_output_get_channels(audio);
    size_t rate = audio_output_get_sample_rate(source->audio);

    source->audio_buf_mutex.lock();

    ts_info ts = {start_ts_in, end_ts_in};
    uint64_t min_ts = start_ts_in - MAX_AUDIO_BUFFER;

    circlebuf_push_back(&source->audio_timestamp_buf, &ts, sizeof(ts));
    circlebuf_peek_front(&source->audio_timestamp_buf, &ts, sizeof(ts));

    // if audio go forward
    while (source->audio_timestamp_buf.size > sizeof(ts) && source->audio_time >= ts.end) {
        circlebuf_pop_front(&source->audio_timestamp_buf, nullptr, sizeof(ts));
        circlebuf_peek_front(&source->audio_timestamp_buf, &ts, sizeof(ts));
        // padding with mute
        size_t padding_size = AUDIO_OUTPUT_FRAMES * sizeof(float);
        char *padding = new char[padding_size];
        memset(padding, 0, padding_size);
        for (size_t ch = 0; ch < channels; ch++) {
            circlebuf_push_front(&source->audio_buf[ch], padding, padding_size);
        }
        delete[] padding;
    }

    size_t buf_size = source->audio_buf[0].size;
    bool paused = obs_source_media_get_state(source->obs_source) == OBS_MEDIA_STATE_PAUSED;
    bool result;

    if (!source->audio_time || paused || source->audio_time < min_ts || source->audio_time >= ts.end) {
        // if audio stopped or out of range, send mute
        result = true;
    } else if (buf_size < ns_to_audio_frames(rate, ts.end - source->audio_time) * sizeof(float)) {
        // if audio does not catch up, wait buffer
        result = false;
    } else {
        size_t start_frame;
        if (source->audio_time < ts.start) {
            // trunc buffer
            for (size_t ch = 0; ch < channels; ch++) {
                circlebuf_pop_front(&source->audio_buf[ch], nullptr,
                                    ns_to_audio_frames(rate, ts.start - source->audio_time) * sizeof(float));
            }
            start_frame = 0;
        } else {
            start_frame = ns_to_audio_frames(rate, source->audio_time - ts.start);
        }
        size_t audio_size = (AUDIO_OUTPUT_FRAMES - start_frame) * sizeof(float);
        for (size_t ch = 0; ch < channels; ch++) {
            circlebuf_pop_front(&source->audio_buf[ch], mixes[0].data[ch] + start_frame, audio_size);
        }
        result = true;
        source->audio_time = ts.end;
    }

    *out_ts = ts.start;
    if (result) {
        circlebuf_pop_front(&source->audio_timestamp_buf, nullptr, sizeof(ts));
    }

    source->audio_buf_mutex.unlock();
    return result;
}

void Source::source_activate_callback(void *param, calldata_t *data) {
    UNUSED_PARAMETER(data);
    auto source = (Source *) param;
    if (source->settings->isFile && source->settings->startOnActive) {
        source->play();
    }
}

void Source::source_deactivate_callback(void *param, calldata_t *data) {
    UNUSED_PARAMETER(data);
    auto source = (Source *) param;
    if (source->settings->isFile && source->settings->startOnActive) {
        source->pauseToBeginning();
    }
}

Source::Source(std::string &id, std::string &sceneId, obs_scene_t *obs_scene,
               std::shared_ptr<SourceSettings> &settings) :
        id(id),
        sceneId(sceneId),
        obs_scene(obs_scene),
        settings(settings),
        type(Source::getSourceType(settings->type)),
        url(settings->url),
        output(settings->output ? new Output(settings->output) : nullptr),
        obs_source(nullptr),
        obs_scene_item(nullptr),
        obs_volmeter(nullptr),
        obs_fader(nullptr),
        video(nullptr),
        video_stop(false),
        video_thread(),
        video_scaler(nullptr),
        frame_buf(),
        frame_buf_mutex(),
        video_time(0),
        audio(nullptr),
        audio_buf(),
        audio_buf_mutex(),
        audio_timestamp_buf(),
        audio_time(0),
        last_audio_time(0),
        timing_adjust(0),
        timing_mutex() {
}

void Source::start() {
    obs_data_t *obs_data = obs_data_create();
    if (type == Image) {
        obs_data_set_string(obs_data, "file", url.c_str());
        obs_data_set_bool(obs_data, "unload", false);
        obs_source = obs_source_create("image_source", "obs_image_source", obs_data, nullptr);
    } else if (type == MediaSource) {
        obs_data_set_bool(obs_data, "is_local_file", settings->isFile);
        obs_data_set_string(obs_data, settings->isFile ? "local_file" : "input", url.c_str());
        obs_data_set_bool(obs_data, "looping", settings->isFile);
        obs_data_set_bool(obs_data, "hw_decode", settings->hardwareDecoder);
        obs_data_set_bool(obs_data, "close_when_inactive", false);  // make source always read
        obs_data_set_bool(obs_data, "restart_on_activate", false);  // make source always read
        obs_data_set_bool(obs_data, "clear_on_media_end", false);
        obs_source = obs_source_create("ffmpeg_source", this->id.c_str(), obs_data, nullptr);
    }

    obs_data_release(obs_data);

    if (!obs_source) {
        throw std::runtime_error("Failed to create obs_source");
    }

    // Add the source to the scene
    obs_scene_item = obs_scene_add(obs_scene, obs_source);
    if (!obs_scene_item) {
        throw std::runtime_error("Failed to add scene item.");
    }

    // Scale source to output size by setting bounds
    obs_video_info ovi = {};
    obs_get_video_info(&ovi);
    struct vec2 bounds = {};
    bounds.x = (float) ovi.base_width;
    bounds.y = (float) ovi.base_height;
    uint32_t align = OBS_ALIGN_TOP + OBS_ALIGN_LEFT;
    obs_sceneitem_set_bounds_type(obs_scene_item, OBS_BOUNDS_SCALE_INNER);
    obs_sceneitem_set_bounds(obs_scene_item, &bounds);
    obs_sceneitem_set_bounds_alignment(obs_scene_item, align);

    // Volmeter
    obs_volmeter = obs_volmeter_create(OBS_FADER_IEC);
    if (!obs_volmeter) {
        blog(LOG_ERROR, "Failed to create obs volmeter");
    }
    obs_volmeter_attach_source(obs_volmeter, obs_source);
    obs_volmeter_add_callback(obs_volmeter, volmeter_callback, this);

    // Fader
    obs_fader = obs_fader_create(OBS_FADER_IEC);
    if (!obs_fader) {
        blog(LOG_ERROR, "Failed to create obs fader");
    }
    obs_fader_attach_source(obs_fader, obs_source);

    // source output
    if (output) {
        startOutput();
    }

    // pause to beginning if it's start at active
    if (settings->isFile && settings->startOnActive) {
        pauseToBeginning();
    }

    signal_handler_t *handler = obs_source_get_signal_handler(obs_source);
    signal_handler_connect(handler, "activate", source_activate_callback, this);
    signal_handler_connect(handler, "deactivate", source_deactivate_callback, this);
}

void Source::stop() {
    signal_handler_t *handler = obs_source_get_signal_handler(obs_source);
    signal_handler_disconnect(handler, "activate", source_activate_callback, this);
    signal_handler_disconnect(handler, "deactivate", source_deactivate_callback, this);

    if (output) {
        stopOutput();
    }
    if (obs_volmeter) {
        obs_volmeter_remove_callback(obs_volmeter, volmeter_callback, this);
        obs_volmeter_detach_source(obs_volmeter);
        obs_volmeter_destroy(obs_volmeter);
    }
    if (obs_fader) {
        obs_fader_detach_source(obs_fader);
        obs_fader_destroy(obs_fader);
    }

    // obs_sceneitem_remove will call obs_sceneitem_release internally,
    // so it's no need to call obs_sceneitem_release.
    obs_sceneitem_remove(obs_scene_item);
    obs_source_remove(obs_source);
    obs_source_release(obs_source);
    obs_source = nullptr;
    obs_scene_item = nullptr;
}

void Source::restart() {
    stop();
    start();
}

std::string Source::getId() {
    return id;
}

std::string Source::getSceneId() {
    return sceneId;
}

SourceType Source::getType() {
    return type;
}

void Source::setUrl(const std::string &sourceUrl) {
    url = sourceUrl;
    restart();
}

std::string Source::getUrl() {
    return url;
}

void Source::setVolume(float volume) {
    // Set volume in dB
    obs_fader_set_db(obs_fader, volume);
}

float Source::getVolume() {
    return obs_fader ? obs_fader_get_db(obs_fader) : 0;
}

void Source::setAudioLock(bool audioLock) {
    if (obs_source) {
        obs_source_set_audio_lock(obs_source, audioLock);
    }
}

bool Source::getAudioLock() {
    return obs_source != nullptr && obs_source_get_audio_lock(obs_source);
}

void Source::setAudioMonitor(bool audioMonitor) {
    if (obs_source) {
        if (audioMonitor) {
            obs_source_set_monitoring_type(obs_source, OBS_MONITORING_TYPE_MONITOR_ONLY);
        } else {
            obs_source_set_monitoring_type(obs_source, OBS_MONITORING_TYPE_NONE);
        }
    }
}

bool Source::getAudioMonitor() {
    return obs_source && obs_source_get_monitoring_type(obs_source) == OBS_MONITORING_TYPE_MONITOR_ONLY;
}

void Source::startOutput() {
    int width = settings->output->width;
    int height = settings->output->height;
    int fpsNum = settings->fpsNum;
    int fpsDen = settings->fpsDen;
    int samplerate = settings->samplerate;
    if (width == 0 || height == 0 || fpsNum == 0 || fpsDen == 0 || samplerate == 0) {
        throw std::runtime_error(
                "Source width, height, fpsNum, fpsDen, samplerate should not empty if the source has an output.");
    }

    // video output
    obs_video_info ovi = {};
    obs_get_video_info(&ovi);

    video_output_info voi = {};
    std::string videoOutputName = std::string("source_video_output_") + id;
    voi.name = videoOutputName.c_str();
    voi.format = VIDEO_FORMAT_BGRA;
    voi.width = width;
    voi.height = height;
    voi.fps_num = fpsNum;
    voi.fps_den = fpsDen;
    voi.cache_size = 16;
    video_output_open(&video, &voi);

    video_thread = std::thread(&Source::video_output_callback, this);

    // audio output
    for (auto &buf : audio_buf) {
        circlebuf_init(&buf);
    }

    obs_audio_info oai = {};
    obs_get_audio_info(&oai);

    audio_output_info aoi = {};
    std::string audioOutputName = std::string("source_audio_output_") + id;
    aoi.name = audioOutputName.c_str();
    aoi.samples_per_sec = samplerate;
    aoi.format = AUDIO_FORMAT_FLOAT_PLANAR;
    aoi.speakers = oai.speakers;
    aoi.input_callback = audio_output_callback;
    aoi.input_param = this;

    audio_output_open(&audio, &aoi);

    output->start(video, audio);

    signal_handler_t *handler = obs_source_get_signal_handler(obs_source);
    signal_handler_connect(handler, "media_get_frame", source_media_get_frame_callback, this);
    signal_handler_connect(handler, "media_get_audio", source_media_get_audio_callback, this);
}

void Source::stopOutput() {
    signal_handler_t *handler = obs_source_get_signal_handler(obs_source);
    signal_handler_disconnect(handler, "media_get_frame", source_media_get_frame_callback, this);
    signal_handler_disconnect(handler, "media_get_audio", source_media_get_audio_callback, this);

    output->stop();

    // video stop
    video_stop = true;
    video_thread.join();
    video_stop = false;
    video_output_stop(video);
    video_output_close(video);
    if (video_scaler) {
        video_scaler_destroy(video_scaler);
        video_scaler = nullptr;
    }
    while (frame_buf.size > 0) {
        obs_source_frame *frame = nullptr;
        circlebuf_pop_front(&frame_buf, &frame, sizeof(void *));
        obs_source_frame_destroy(frame);
    }

    // audio stop
    audio_output_close(audio);

    for (auto &buf : audio_buf) {
        circlebuf_free(&buf);
    }

    // reset timing
    video_time = 0;
    audio_time = 0;
    last_audio_time = 0;
    timing_adjust = 0;
}

void Source::play() {
    if (obs_source) {
        obs_source_media_play_pause(obs_source, false);
    }
}

void Source::pauseToBeginning() {
    if (obs_source) {
        obs_source_media_play_pause(obs_source, true);
        obs_source_media_set_time(obs_source, 0);
    }
}

void Source::create_video_scaler(obs_source_frame *frame) {
    const struct video_output_info *voi = video_output_get_info(video);
    struct video_scale_info src = {
            .format = frame->format,
            .width = frame->width,
            .height = frame->height,
            .range = frame->full_range ? VIDEO_RANGE_FULL : VIDEO_RANGE_DEFAULT,
            .colorspace = VIDEO_CS_DEFAULT
    };
    struct video_scale_info dest = {
            .format = voi->format,
            .width = voi->width,
            .height = voi->height,
            .range = VIDEO_RANGE_DEFAULT,
            .colorspace = VIDEO_CS_DEFAULT
    };

    int ret = video_scaler_create(&video_scaler, &dest, &src, VIDEO_SCALE_FAST_BILINEAR);
    if (ret != VIDEO_SCALER_SUCCESS) {
        throw std::runtime_error("Failed to create video scaler.");
    }
}

#include "source.h"
#include "callback.h"
#include <media-io/video-frame.h>
#include <util/platform.h>

/* maximum buffer size */
#define MAX_BUF_SIZE (1000 * AUDIO_OUTPUT_FRAMES * sizeof(float))

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

void Source::video_output_callback(void *param, uint32_t cx, uint32_t cy) {
    UNUSED_PARAMETER(cx);
    UNUSED_PARAMETER(cy);

    auto source = (Source *) param;
    auto obs_source = source->obs_source;
    if (!obs_source) {
        return;
    }

    int source_width = (int) obs_source_get_width(obs_source);
    int source_height = (int) obs_source_get_height(obs_source);
    if (source_width == 0 || source_height == 0) {
        return;
    }

    int output_width = source->settings->output->width;
    int output_height = source->settings->output->height;
    float scaleX = (float) output_width / (float) source_width;
    float scaleY = (float) output_height / (float) source_height;

    auto texrender = source->output_texrender;
    auto stagesurface = source->output_stagesurface;
    auto video = source->output_video;

    gs_texrender_reset(texrender);

    if (gs_texrender_begin(texrender, output_width, output_height)) {
        vec4 background = {};
        vec4_zero(&background);

        gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
        gs_ortho(0.0f, (float) output_width, 0.0f, (float) output_height, -100.0f, 100.0f);

        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

        gs_matrix_scale3f(scaleX, scaleY, 1);
        obs_source_video_render(obs_source);

        gs_blend_state_pop();
        gs_texrender_end(texrender);

        struct video_frame output_frame = {};
        if (video_output_lock_frame(video, &output_frame, 1, os_gettime_ns())) {
            gs_stage_texture(stagesurface, gs_texrender_get_texture(texrender));
            uint8_t *video_data = nullptr;
            uint32_t video_linesize;
            if (gs_stagesurface_map(stagesurface, &video_data, &video_linesize)) {
                uint32_t linesize = output_frame.linesize[0];
                for (uint32_t i = 0; i < output_height; i++) {
                    uint32_t dst_offset = linesize * i;
                    uint32_t src_offset = video_linesize * i;
                    memcpy(output_frame.data[0] +
                           dst_offset,
                           video_data + src_offset,
                           linesize);
                }
                gs_stagesurface_unmap(stagesurface);
            }
            video_output_unlock_frame(video);
        }
    }
}

void
Source::audio_capture_callback(void *param, obs_source_t *obs_source, const struct audio_data *audio_data, bool muted) {
    UNUSED_PARAMETER(obs_source);
    UNUSED_PARAMETER(muted);

    auto source = (Source *) param;
    size_t size = audio_data->frames * sizeof(float);
    size_t channels = audio_output_get_channels(source->output_audio);

    source->output_audio_buf_mutex.lock();

    // keep buffer size not too large
    if ((source->output_audio_buf[0].size + size) > MAX_BUF_SIZE) {
        for (size_t i = 0; i < channels; i++) {
            circlebuf_pop_front(&source->output_audio_buf[i], nullptr, size);
        }
    }

    for (size_t i = 0; i < channels; i++) {
        circlebuf_push_back(&source->output_audio_buf[i], audio_data->data[i], size);
    }

    source->output_audio_buf_mutex.unlock();
}

bool Source::audio_output_callback(
        void *param,
        uint64_t start_ts_in,
        uint64_t end_ts_in,
        uint64_t *out_ts,
        uint32_t mixers,
        struct audio_output_data *mixes) {

    UNUSED_PARAMETER(end_ts_in);
    UNUSED_PARAMETER(mixers);

    auto source = (Source *) param;
    auto audio = source->output_audio;
    size_t channels = audio_output_get_channels(audio);
    size_t audio_size = AUDIO_OUTPUT_FRAMES * sizeof(float);

    source->output_audio_buf_mutex.lock();

    bool parsed = obs_source_media_get_state(source->obs_source) == OBS_MEDIA_STATE_PAUSED;
    if (parsed) {
        // clear output buffer and send mute data
        for (size_t ch = 0; ch < channels; ch++) {
            circlebuf_pop_front(&source->output_audio_buf[ch], nullptr, source->output_audio_buf[ch].size);
            memset(mixes[0].data[ch], 0, audio_size);
        }
    } else if (source->output_audio_buf[0].size < audio_size) {
        source->output_audio_buf_mutex.unlock();
        return false;
    } else {
        for (size_t ch = 0; ch < channels; ch++) {
            circlebuf_pop_front(&source->output_audio_buf[ch], mixes[0].data[ch], audio_size);
        }
    }

    source->output_audio_buf_mutex.unlock();
    *out_ts = start_ts_in;
    return true;
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

Source::Source(std::string &id, std::string &sceneId, obs_scene_t *obs_scene, std::shared_ptr<SourceSettings> &settings)
        :
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
        output_video(nullptr),
        output_audio(nullptr),
        output_texrender(nullptr),
        output_stagesurface(nullptr),
        output_audio_buf(),
        output_audio_buf_mutex() {
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
    // video output
    obs_video_info ovi = {};
    obs_get_video_info(&ovi);

    int width = settings->output->width;
    int height = settings->output->height;

    video_output_info vi = {};
    std::string videoOutputName = std::string("source_video_output_") + id;
    vi.name = videoOutputName.c_str();
    vi.format = VIDEO_FORMAT_BGRA;
    vi.width = width;
    vi.height = height;
    vi.fps_num = ovi.fps_num;
    vi.fps_den = ovi.fps_den;
    vi.cache_size = 16;
    video_output_open(&output_video, &vi);

    obs_enter_graphics();
    output_texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    output_stagesurface = gs_stagesurface_create(width, height, GS_BGRA);
    obs_leave_graphics();

    obs_add_main_render_callback(video_output_callback, this);

    // audio output
    for (auto &buf : output_audio_buf) {
        circlebuf_init(&buf);
    }
    obs_source_add_audio_capture_callback(obs_source, audio_capture_callback, this);

    obs_audio_info oai = {};
    obs_get_audio_info(&oai);

    audio_output_info ai = {};
    std::string audioOutputName = std::string("source_audio_output_") + id;
    ai.name = audioOutputName.c_str();
    ai.samples_per_sec = oai.samples_per_sec;
    ai.format = AUDIO_FORMAT_FLOAT_PLANAR;
    ai.speakers = oai.speakers;
    ai.input_callback = audio_output_callback;
    ai.input_param = this;

    audio_output_open(&output_audio, &ai);

    output->start(output_video, output_audio);
}

void Source::stopOutput() {
    output->stop();

    // output video stop
    video_output_stop(output_video);
    obs_remove_main_render_callback(video_output_callback, this);
    obs_enter_graphics();
    gs_stagesurface_destroy(output_stagesurface);
    gs_texrender_destroy(output_texrender);
    obs_leave_graphics();
    video_output_close(output_video);

    // output audio stop
    audio_output_close(output_audio);

    for (auto &buf : output_audio_buf) {
        circlebuf_free(&buf);
    }

    obs_source_remove_audio_capture_callback(obs_source, audio_capture_callback, this);
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

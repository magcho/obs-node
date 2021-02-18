#include "source_transcoder.h"
#include "source.h"
#include <media-io/video-frame.h>
#include <util/platform.h>

#define AUDIO_RESET_THRESHOLD 2000000000
#define AUDIO_SMOOTH_THRESHOLD 70000000
#define MAX_AUDIO_TIMESTAMP_BUFFER 1000000000

struct ts_info {
    uint64_t start;
    uint64_t end;
};

static inline uint64_t uint64_diff(uint64_t ts1, uint64_t ts2) {
    return (ts1 < ts2) ? (ts2 - ts1) : (ts1 - ts2);
}

SourceTranscoder::SourceTranscoder() :
        source(nullptr),
        output(nullptr),
        video(nullptr),
        video_stagesurface(nullptr),
        video_texrender(nullptr),
        last_video_timestamp(0),
        last_video_lagged_frames(0),
        first_frame_outputed(false),
        audio(nullptr),
        audio_buf(),
        audio_buf_mutex(),
        audio_timestamp_buf(),
        audio_time(0),
        last_audio_time(0),
        timing_adjust(0),
        timing_mutex() {
}

void SourceTranscoder::start(Source *s) {
    source = s;
    output = new Output(source->settings->output);

    // video output
    obs_video_info ovi = {};
    obs_get_video_info(&ovi);

    video_output_info voi = {};
    std::string videoOutputName = std::string("source_video_output_") + source->id;
    voi.name = videoOutputName.c_str();
    voi.format = VIDEO_FORMAT_BGRA;
    voi.width = source->settings->output->width;
    voi.height = source->settings->output->height;
    voi.fps_num = ovi.fps_num;
    voi.fps_den = ovi.fps_den;
    voi.cache_size = 16;
    video_output_open(&video, &voi);

    obs_enter_graphics();
    video_texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    video_stagesurface = gs_stagesurface_create(voi.width, voi.height, GS_BGRA);
    obs_leave_graphics();

    obs_add_main_render_callback(source_video_output_callback, this);

    // audio output
    for (auto &buf : audio_buf) {
        circlebuf_init(&buf);
    }

    obs_audio_info oai = {};
    obs_get_audio_info(&oai);

    audio_output_info aoi = {};
    std::string audioOutputName = std::string("source_audio_output_") + source->id;
    aoi.name = audioOutputName.c_str();
    aoi.samples_per_sec = oai.samples_per_sec;
    aoi.format = AUDIO_FORMAT_FLOAT_PLANAR;
    aoi.speakers = oai.speakers;
    aoi.input_callback = audio_output_callback;
    aoi.input_param = this;

    obs_source_add_audio_capture_callback(source->obs_source, audio_capture_callback, this);

    audio_output_open(&audio, &aoi);

    output->start(video, audio);
}

void SourceTranscoder::stop() {
    output->stop();
    delete output;
    output = nullptr;

    // video stop
    obs_remove_main_render_callback(source_video_output_callback, this);

    obs_enter_graphics();
    gs_stagesurface_destroy(video_stagesurface);
    gs_texrender_destroy(video_texrender);
    obs_leave_graphics();

    video_output_stop(video);
    video_output_close(video);

    last_video_timestamp = 0;
    last_video_lagged_frames = 0;
    first_frame_outputed = false;

    // audio stop
    audio_output_close(audio);

    for (auto &buf : audio_buf) {
        circlebuf_free(&buf);
    }

    audio_time = 0;
    last_audio_time = 0;
    timing_adjust = 0;
}

void SourceTranscoder::source_video_output_callback(void *param, uint32_t cx, uint32_t cy) {
    UNUSED_PARAMETER(cx);
    UNUSED_PARAMETER(cy);

    auto transcoder = (SourceTranscoder *) param;
    auto obs_source = transcoder->source->obs_source;
    if (!obs_source) {
        return;
    }

    int source_width = (int) obs_source_get_width(obs_source);
    int source_height = (int) obs_source_get_height(obs_source);
    if (source_width == 0 || source_height == 0) {
        return;
    }

    int output_width = transcoder->source->settings->output->width;
    int output_height = transcoder->source->settings->output->height;
    float scaleX = (float) output_width / (float) source_width;
    float scaleY = (float) output_height / (float) source_height;

    uint64_t video_time = obs_get_video_frame_time();
    uint64_t frame_timestamp = obs_source_get_frame_timestamp(transcoder->source->obs_source);

    if (frame_timestamp != transcoder->last_video_timestamp) {
        transcoder->timing_mutex.lock();
        transcoder->timing_adjust = video_time - frame_timestamp;
        transcoder->timing_mutex.unlock();
        transcoder->last_video_timestamp = frame_timestamp;
    }

    auto texrender = transcoder->video_texrender;
    auto stagesurface = transcoder->video_stagesurface;
    auto video = transcoder->video;

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

        uint32_t lagged_frames = obs_get_lagged_frames();
        uint32_t frame_count = transcoder->first_frame_outputed ? lagged_frames - transcoder->last_video_lagged_frames + 1 : 1;
        transcoder->last_video_lagged_frames = lagged_frames;

        struct video_frame output_frame = {};
        if (video_output_lock_frame(video, &output_frame, frame_count, video_time)) {
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
            transcoder->first_frame_outputed = true;
            video_output_unlock_frame(video);
        }
    }
}

void SourceTranscoder::audio_capture_callback(void *param, obs_source_t *source, const struct audio_data *audio_data, bool muted) {
    UNUSED_PARAMETER(source);
    UNUSED_PARAMETER(muted);

    auto transcoder = (SourceTranscoder *) param;

    size_t channels = audio_output_get_channels(transcoder->audio);
    size_t rate = audio_output_get_sample_rate(transcoder->audio);

    transcoder->timing_mutex.lock();
    uint64_t timing_adjust = transcoder->timing_adjust;
    transcoder->timing_mutex.unlock();

    if (!timing_adjust) {
        return;
    }

    transcoder->audio_buf_mutex.lock();

    uint64_t current_audio_time = audio_data->timestamp + timing_adjust;
    uint64_t diff = uint64_diff(transcoder->last_audio_time, current_audio_time);
    size_t audio_size = audio_data->frames * sizeof(float);

    if (!transcoder->audio_time || current_audio_time < transcoder->audio_time ||
        current_audio_time - transcoder->audio_time > AUDIO_RESET_THRESHOLD) {
        blog(LOG_INFO, "[%s] reset audio buffer, audio timestamp: %llu, audio time: %llu ms, current audio time: %llu ms",
             transcoder->source->id.c_str(), audio_data->timestamp, transcoder->audio_time, current_audio_time);
        for (size_t i = 0; i < channels; i++) {
            circlebuf_pop_front(&transcoder->audio_buf[i], nullptr, transcoder->audio_buf[i].size);
        }
        transcoder->audio_time = current_audio_time;
        transcoder->last_audio_time = current_audio_time;
    } else if (diff > AUDIO_SMOOTH_THRESHOLD) {
        blog(LOG_INFO, "[%s] audio buffer placement: %llu", transcoder->source->id.c_str(), diff);
        size_t buf_placement = ns_to_audio_frames(rate, current_audio_time - transcoder->audio_time) * sizeof(float);
        for (size_t i = 0; i < channels; i++) {
            circlebuf_place(&transcoder->audio_buf[i], buf_placement, audio_data->data[i], audio_size);
            circlebuf_pop_back(&transcoder->audio_buf[i], nullptr,
                               transcoder->audio_buf[i].size - (buf_placement + audio_size));
        }
        transcoder->last_audio_time = current_audio_time;
    } else {
        for (size_t i = 0; i < channels; i++) {
            circlebuf_push_back(&transcoder->audio_buf[i], audio_data->data[i], audio_size);
        }
    }

    transcoder->last_audio_time += audio_frames_to_ns(rate, audio_data->frames);
    transcoder->audio_buf_mutex.unlock();
}

bool SourceTranscoder::audio_output_callback(
        void *param,
        uint64_t start_ts_in,
        uint64_t end_ts_in,
        uint64_t *out_ts,
        uint32_t mixers,
        struct audio_output_data *mixes) {

    UNUSED_PARAMETER(mixers);

    auto transcoder = (SourceTranscoder *) param;
    size_t channels = audio_output_get_channels(transcoder->audio);
    size_t rate = audio_output_get_sample_rate(transcoder->audio);
    ts_info ts = {start_ts_in, end_ts_in};

    transcoder->audio_buf_mutex.lock();

    circlebuf_push_back(&transcoder->audio_timestamp_buf, &ts, sizeof(ts));
    circlebuf_peek_front(&transcoder->audio_timestamp_buf, &ts, sizeof(ts));

    bool paused = obs_source_media_get_state(transcoder->source->obs_source) == OBS_MEDIA_STATE_PAUSED;
    bool result = false;

    if (!transcoder->audio_time || paused) {
        // audio stopped, reset audio and send mute
        transcoder->reset_audio();
        result = true;
    } else if (transcoder->audio_time >= ts.end) {
        // audio go forward, send mute
        blog(LOG_INFO, "[%s] audio go forward, audio time: %llu, ts.end: %llu",
             transcoder->source->id.c_str(), transcoder->audio_time, ts.end);
        result = true;
    } else {
        size_t buffer_size = transcoder->audio_buf[0].size;
        if (transcoder->audio_time < ts.start) {
            // trunc buffer
            size_t trunc_size = ns_to_audio_frames(rate, ts.start - transcoder->audio_time) * sizeof(float);
            if (buffer_size < trunc_size) {
                for (size_t ch = 0; ch < channels; ch++) {
                    circlebuf_pop_front(&transcoder->audio_buf[ch], nullptr, buffer_size);
                }
                buffer_size = 0;
                transcoder->audio_time += audio_frames_to_ns(rate, buffer_size / sizeof(float));
            } else {
                for (size_t ch = 0; ch < channels; ch++) {
                    circlebuf_pop_front(&transcoder->audio_buf[ch], nullptr, trunc_size);
                }
                buffer_size -= trunc_size;
                transcoder->audio_time = ts.start;
            }
        }
        if (transcoder->audio_time >= ts.start) {
            size_t start_frame = ns_to_audio_frames(rate, transcoder->audio_time - ts.start);
            size_t audio_size = (AUDIO_OUTPUT_FRAMES - start_frame) * sizeof(float);
            if (buffer_size >= audio_size) {
                for (size_t ch = 0; ch < channels; ch++) {
                    circlebuf_pop_front(&transcoder->audio_buf[ch], mixes[0].data[ch] + start_frame, audio_size);
                }
                transcoder->audio_time = ts.end;
                result = true;
            }
        }
        if (!result && (end_ts_in - ts.start >= MAX_AUDIO_TIMESTAMP_BUFFER)) {
            // audio lagged too much, reset audio and send mute
            blog(LOG_INFO, "[%s] audio timestamp buffer exceed limit: %llu, audio time: %llu, ts.end: %llu",
                 transcoder->source->id.c_str(), end_ts_in - ts.start, transcoder->audio_time, ts.end);
            transcoder->reset_audio();
            result = true;
        }
    }

    if (result) {
        circlebuf_pop_front(&transcoder->audio_timestamp_buf, nullptr, sizeof(ts));
    }

    transcoder->audio_buf_mutex.unlock();

    *out_ts = ts.start;
    return result;
}

void SourceTranscoder::reset_audio() {
    size_t channels = audio_output_get_channels(audio);
    for (size_t ch = 0; ch < channels; ch++) {
        circlebuf_pop_front(&audio_buf[ch], nullptr, audio_buf[ch].size);
    }
    audio_time = 0;
    last_audio_time = 0;
}

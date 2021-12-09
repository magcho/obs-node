#include "source_transcoder.h"
#include "source.h"
#include "utils.h"
#include <media-io/video-frame.h>
#include <util/platform.h>

#define VIDEO_BUFFER_SIZE 1000000000 // nanoseconds
#define VIDEO_JUMP_THRESHOLD 2000000000 // nanoseconds
#define AUDIO_BUFFER_SIZE 2000000000 // nanoseconds
#define AUDIO_SMOOTH_THRESHOLD 70000000 // nanoseconds
#define AUDIO_TIMESTAMP_BUFFER_SIZE 500000000 // nanoseconds，a litter smaller than AUDIO_BUFFER_SIZE - VIDEO_BUFFER_SIZE

static inline enum gs_color_format convert_video_format(enum video_format format) {
    switch (format) {
        case VIDEO_FORMAT_RGBA:
            return GS_RGBA;
        case VIDEO_FORMAT_BGRA:
        case VIDEO_FORMAT_I40A:
        case VIDEO_FORMAT_I42A:
        case VIDEO_FORMAT_YUVA:
        case VIDEO_FORMAT_AYUV:
            return GS_BGRA;
        default:
            return GS_BGRX;
    }
}

extern "C" bool init_gpu_conversion(uint32_t texture_width[MAX_AV_PLANES],
                         uint32_t texture_height[MAX_AV_PLANES],
                         enum gs_color_format texture_formats[MAX_AV_PLANES],
                         int *channel_count,
                         const struct obs_source_frame *frame);

extern "C" bool update_frame_texrender(int width,
                                       int height,
                                       const struct obs_source_frame *frame,
                                       gs_texture_t *tex[MAX_AV_PLANES],
                                       gs_texrender_t *texrender);

struct ts_info {
    uint64_t start;
    uint64_t end;
};

static inline uint64_t uint64_diff(uint64_t ts1, uint64_t ts2) {
    return (ts1 < ts2) ? (ts2 - ts1) : (ts1 - ts2);
}

static void draw_frame_texture(const gs_texrender_t *texrender) {
    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_technique_t *tech = gs_effect_get_technique(effect, "Draw");
    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);

    gs_texture_t *frame_texture = gs_texrender_get_texture(texrender);
    gs_eparam_t *param = gs_effect_get_param_by_name(effect, "image");
    gs_effect_set_texture(param, frame_texture);

    gs_draw_sprite(frame_texture, 0, 0, 0);

    gs_technique_end_pass(tech);
    gs_technique_end(tech);
}

SourceTranscoder::SourceTranscoder() :
        source(nullptr),
        output(nullptr),
        video(nullptr),
        frame_buf(),
        frame_buf_mutex(),
        frame_textures(),
        frame_texrender(nullptr),
        video_texrender(nullptr),
        video_stagesurface(nullptr),
        texture_width(0),
        texture_height(0),
        texture_format(),
        last_video_time(0),
        last_frame_ts(0),
        video_stop(false),
        video_thread(),
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
    output = new Output(source->output);

    // video output
    obs_video_info ovi = {};
    obs_get_video_info(&ovi);

    video_output_info voi = {};
    std::string videoOutputName = std::string("source_video_output_") + source->id;
    voi.name = videoOutputName.c_str();
    voi.format = VIDEO_FORMAT_BGRA;
    voi.width = source->output->width;
    voi.height = source->output->height;
    voi.fps_num = ovi.fps_num;
    voi.fps_den = ovi.fps_den;
    voi.cache_size = 16;
    video_output_open(&video, &voi);

    video_thread = std::thread(&SourceTranscoder::video_output_callback, this);

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

    signal_handler_t *handler = obs_source_get_signal_handler(source->obs_source);
    signal_handler_connect(handler, "media_get_frame", source_media_get_frame_callback, this);
}

void SourceTranscoder::stop() {
    signal_handler_t *handler = obs_source_get_signal_handler(source->obs_source);
    signal_handler_disconnect(handler, "media_get_frame", source_media_get_frame_callback, this);

    // output stop
    output->stop();
    delete output;
    output = nullptr;

    // video stop
    video_stop = true;
    video_thread.join();
    video_stop = false;

    video_output_stop(video);
    video_output_close(video);

    obs_enter_graphics();
    if (video_stagesurface) {
        gs_stagesurface_destroy(video_stagesurface);
        gs_texrender_destroy(video_texrender);
        video_stagesurface = nullptr;
        video_texrender = nullptr;
    }

    for (auto &frame_texture : frame_textures) {
        if (frame_texture) {
            gs_texture_destroy(frame_texture);
            frame_texture = nullptr;
        }
    }

    if (frame_texrender) {
        gs_texrender_destroy(frame_texrender);
        frame_texrender = nullptr;
    }

    obs_leave_graphics();

    reset_video();

    last_video_time = 0;
    last_frame_ts = 0;

    // audio stop
    obs_source_remove_audio_capture_callback(source->obs_source, audio_capture_callback, this);

    audio_output_close(audio);

    reset_audio();

    audio_time = 0;
    last_audio_time = 0;
    timing_adjust = 0;
}

void SourceTranscoder::source_media_get_frame_callback(void *param, calldata_t *data) {
    auto transcoder = (SourceTranscoder *) param;
    auto *frame = (obs_source_frame *) calldata_ptr(data, "frame");

    if (frame->format == VIDEO_FORMAT_NONE) {
        return;
    }

    obs_source_frame *new_frame = obs_source_frame_create(frame->format, frame->width, frame->height);
    obs_source_frame_copy(new_frame, frame);

    const struct video_output_info *voi = video_output_get_info(transcoder->video);
    uint64_t max_buffer_frames = util_mul_div64(VIDEO_BUFFER_SIZE, voi->fps_num, voi->fps_den * 1000000000UL);

    transcoder->frame_buf_mutex.lock();

    if (transcoder->frame_buf.size / sizeof(void *) >= max_buffer_frames) {
        blog(LOG_INFO, "[%s] exceed max video buffer: %llu", transcoder->source->id.c_str(), max_buffer_frames);
        transcoder->reset_video();
    }

    circlebuf_push_back(&transcoder->frame_buf, &new_frame, sizeof(void *));
    transcoder->frame_buf_mutex.unlock();
}

void SourceTranscoder::video_output_callback(void *param) {
    auto *transcoder = (SourceTranscoder *) param;
    const struct video_output_info *voi = video_output_get_info(transcoder->video);
    uint64_t interval = util_mul_div64(1000000000UL, voi->fps_den, voi->fps_num);
    transcoder->last_video_time = os_gettime_ns();

    while (!transcoder->video_stop) {
        uint64_t video_time = transcoder->last_video_time + interval;
        uint32_t count;
        if (os_sleepto_ns(video_time)) {
            count = 1;
        } else {
            count = (int) ((os_gettime_ns() - transcoder->last_video_time) / interval);
            video_time = transcoder->last_video_time + interval * count;
            if (count > 1) {
                blog(LOG_INFO, "[%s] video lagged: %d", transcoder->source->id.c_str(), count);
            }
        }

        obs_enter_graphics();

        int output_width = transcoder->source->output->width;
        int output_height = transcoder->source->output->height;

        // initialize textrender
        if (!transcoder->video_texrender) {
            transcoder->video_texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
            transcoder->video_stagesurface = gs_stagesurface_create(output_width, output_height, GS_BGRA);
        }

        // render texture
        gs_texrender_reset(transcoder->video_texrender);
        if (gs_texrender_begin(transcoder->video_texrender, output_width, output_height)) {
            gs_blend_state_push();
            gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

            // render background
            vec4 background = {};
            vec4_zero(&background);
            gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);

            // render frame
            transcoder->frame_buf_mutex.lock();
            auto frame = transcoder->get_closest_frame(video_time);
            if (frame) {
                transcoder->timing_mutex.lock();
                transcoder->timing_adjust = video_time - frame->timestamp;
                transcoder->timing_mutex.unlock();
                transcoder->render_frame(frame, output_width, output_height);
            }
            transcoder->frame_buf_mutex.unlock();

            // copy rendered texture to output frame
            struct video_frame output_frame = {};
            if (video_output_lock_frame(transcoder->video, &output_frame, count, video_time)) {
                gs_stage_texture(transcoder->video_stagesurface, gs_texrender_get_texture(transcoder->video_texrender));
                uint8_t *video_data = nullptr;
                uint32_t video_linesize;
                if (gs_stagesurface_map(transcoder->video_stagesurface, &video_data, &video_linesize)) {
                    uint32_t linesize = output_frame.linesize[0];
                    for (uint32_t i = 0; i < output_height; i++) {
                        uint32_t dst_offset = linesize * i;
                        uint32_t src_offset = video_linesize * i;
                        memcpy(output_frame.data[0] +
                               dst_offset,
                               video_data + src_offset,
                               linesize);
                    }
                    gs_stagesurface_unmap(transcoder->video_stagesurface);
                }
                video_output_unlock_frame(transcoder->video);
            }

            gs_blend_state_pop();
            gs_texrender_end(transcoder->video_texrender);
        }

        obs_leave_graphics();
        transcoder->last_video_time = video_time;
    }
}

void SourceTranscoder::audio_capture_callback(void *param, obs_source_t *source, const struct audio_data *audio_data,
                                              bool muted) {
    UNUSED_PARAMETER(source);
    UNUSED_PARAMETER(muted);

    auto transcoder = (SourceTranscoder *) param;

    size_t channels = audio_output_get_channels(transcoder->audio);
    size_t rate = audio_output_get_sample_rate(transcoder->audio);

    transcoder->timing_mutex.lock();
    if (!transcoder->timing_adjust) {
        transcoder->timing_adjust = os_gettime_ns() - audio_data->timestamp;
    }
    uint64_t timing_adjust = transcoder->timing_adjust;
    transcoder->timing_mutex.unlock();

    transcoder->audio_buf_mutex.lock();

    uint64_t current_audio_time = audio_data->timestamp + timing_adjust;
    size_t audio_size = audio_data->frames * sizeof(float);

    // if audio time output range, reset audio
    if (!transcoder->audio_time || current_audio_time < transcoder->audio_time ||
        current_audio_time - transcoder->audio_time > AUDIO_BUFFER_SIZE) {
        blog(LOG_INFO, "[%s] audio buffer reset, audio time: %llu, current audio time: %llu",
             transcoder->source->id.c_str(), transcoder->audio_time, current_audio_time);
        transcoder->reset_audio();
        transcoder->audio_time = current_audio_time;
        transcoder->last_audio_time = current_audio_time;
    }

    uint64_t diff = uint64_diff(transcoder->last_audio_time, current_audio_time);
    if (diff > AUDIO_SMOOTH_THRESHOLD) {
        blog(LOG_DEBUG, "[%s] audio buffer placement: %llu, audio time: %llu, current audio time: %llu",
             transcoder->source->id.c_str(), diff, transcoder->audio_time, current_audio_time);
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
        blog(LOG_DEBUG, "[%s] audio go forward, audio time: %llu, ts.end: %llu",
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
    }

    if (!result && (end_ts_in - ts.start >= AUDIO_TIMESTAMP_BUFFER_SIZE)) {
        result = true;
    }

    if (result) {
        circlebuf_pop_front(&transcoder->audio_timestamp_buf, nullptr, sizeof(ts));
    }

    transcoder->audio_buf_mutex.unlock();

    *out_ts = ts.start;
    return result;
}

obs_source_frame *SourceTranscoder::get_closest_frame(uint64_t video_time) {
    if (!frame_buf.size) {
        return nullptr;
    }

    obs_source_frame *frame;
    circlebuf_peek_front(&frame_buf, &frame, sizeof(void *));

    if (!last_video_time) {
        last_video_time = video_time;
    }

    if (!last_frame_ts) {
        last_frame_ts = frame->timestamp;
    }

    uint64_t sys_offset = video_time - last_video_time;
    uint64_t frame_ts = sys_offset + last_frame_ts;
    while (frame_ts > frame->timestamp && frame_buf.size > sizeof(void *)) {
        uint64_t ts = frame->timestamp;
        circlebuf_pop_front(&frame_buf, &frame, sizeof(void *));
        obs_source_frame_destroy(frame);
        circlebuf_peek_front(&frame_buf, &frame, sizeof(void *));
        if (uint64_diff(ts, frame->timestamp) > VIDEO_JUMP_THRESHOLD) {
            blog(LOG_DEBUG, "[%s] video jump: %llu -> %llu", source->id.c_str(), ts, frame->timestamp);
            frame_ts = frame->timestamp;
            break;
        }
    }
    last_frame_ts = frame_buf.size == sizeof(void *) ? frame->timestamp : frame_ts;
    return frame;
}

void SourceTranscoder::reset_video() {
    while (frame_buf.size > 0) {
        obs_source_frame *frame = nullptr;
        circlebuf_pop_front(&frame_buf, &frame, sizeof(void *));
        obs_source_frame_destroy(frame);
    }
    last_frame_ts = 0;
}

void SourceTranscoder::reset_audio() {
    for (auto &buf : audio_buf) {
        circlebuf_pop_front(&buf, nullptr, buf.size);
    }
    audio_time = 0;
    last_audio_time = 0;
}

void SourceTranscoder::render_frame(obs_source_frame *frame, int output_width, int output_height) {
    const enum gs_color_format format = convert_video_format(frame->format);
    if (frame_textures[0] && (
            frame->width != texture_width ||
            frame->height != texture_height ||
            format != texture_format)) {
        blog(LOG_INFO, "Recreate frame texture");
        for (auto &frame_texture : frame_textures) {
            if (frame_texture) {
                gs_texture_destroy(frame_texture);
                frame_texture = nullptr;
            }
        }
        if (frame_texrender) {
            gs_texrender_destroy(frame_texrender);
            frame_texrender = nullptr;
        }
    }

    if (!frame_textures[0]) {
        uint32_t texture_widths[MAX_AV_PLANES] = {};
        uint32_t texture_heights[MAX_AV_PLANES] = {};
        enum gs_color_format texture_formats[MAX_AV_PLANES] = {};
        int channel_count = 0;
        if (!init_gpu_conversion(texture_widths, texture_heights, texture_formats, &channel_count, frame)) {
            blog(LOG_ERROR, "Failed to create gpu_conversion");
            return;
        }
        for (int c = 0; c < channel_count; ++c)
            frame_textures[c] = gs_texture_create(
                    texture_widths[c],
                    texture_heights[c],
                    texture_formats[c], 1, nullptr,
                    GS_DYNAMIC);
        frame_texrender = gs_texrender_create(format, GS_ZS_NONE);
        texture_width = frame->width;
        texture_height = frame->height;
        texture_format = format;
    }

    // update frame texture
    update_frame_texrender(frame->width, frame->height, frame, frame_textures, frame_texrender);

    // render frame texture center alignment
    int x, y;
    int newCX, newCY;
    float scale;
    GetScaleAndCenterPos(frame->width, frame->height, output_width, output_height, x, y, scale);
    newCX = int(scale * float(frame->width));
    newCY = int(scale * float(frame->height));
    gs_ortho(0.0f, (float) frame->width, 0.0f, (float) frame->height, -100.0f, 100.0f);
    gs_set_viewport(x, y, newCX, newCY);

    draw_frame_texture(frame_texrender);
}
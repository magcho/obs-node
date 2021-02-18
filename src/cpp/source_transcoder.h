#pragma once

#include "output.h"
#include <memory>
#include <mutex>
#include <thread>
#include <obs.h>
#include <util/circlebuf.h>
#include <media-io/video-scaler.h>
#include <media-io/audio-resampler.h>

class Source;

class SourceTranscoder {

public:
    SourceTranscoder();

    void start(Source *source);
    void stop();

private:
    static void source_video_output_callback(
            void *param,
            uint32_t cx,
            uint32_t cy
    );

    static bool audio_output_callback(
            void *param,
            uint64_t start_ts_in,
            uint64_t end_ts_in,
            uint64_t *out_ts,
            uint32_t mixers,
            struct audio_output_data *mixes
    );

    static void audio_capture_callback(
            void *param,
            obs_source_t *source,
            const struct audio_data *audio_data,
            bool muted
    );

    void reset_audio();

    Source *source;
    Output *output;

    video_t *video;
    gs_texrender_t *video_texrender;
    gs_stagesurf_t *video_stagesurface;
    uint64_t last_video_timestamp;
    uint32_t last_video_lagged_frames;
    bool first_frame_outputed;

    audio_t *audio;
    circlebuf audio_buf[MAX_AUDIO_CHANNELS];
    std::mutex audio_buf_mutex;
    circlebuf audio_timestamp_buf;
    uint64_t audio_time;
    uint64_t last_audio_time;

    int64_t timing_adjust;
    std::mutex timing_mutex;
};
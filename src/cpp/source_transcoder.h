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

    static void source_media_get_audio_callback(void *param, calldata_t *data);

    void create_audio_resampler(obs_source_audio *audio);

    Source *source;
    Output *output;

    video_t *video;
    gs_texrender_t *video_texrender;
    gs_stagesurf_t *video_stagesurface;

    audio_t *audio;
    circlebuf audio_buf[MAX_AUDIO_CHANNELS];
    std::mutex audio_buf_mutex;
    circlebuf audio_timestamp_buf;
    uint64_t audio_time;
    uint64_t last_audio_time;
    audio_resampler_t *audio_resampler;

    int64_t timing_adjust;
    std::mutex timing_mutex;
};
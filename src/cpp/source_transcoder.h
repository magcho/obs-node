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
	static void source_media_get_frame_callback(
			void *param,
			calldata_t *data);

	static void video_output_callback(void *param);

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

	void create_video_scaler(obs_source_frame *frame);

	obs_source_frame *get_closest_frame(uint64_t video_time);

	void reset_video();

	void reset_audio();

	Source *source;
	Output *output;

	video_t *video;
	circlebuf frame_buf;
	std::mutex frame_buf_mutex;
	video_scaler_t *video_scaler;
	uint64_t last_video_time;
	uint64_t last_frame_ts;
	std::thread video_thread;
	volatile bool video_stop;

	audio_t *audio;
	circlebuf audio_buf[MAX_AUDIO_CHANNELS];
	std::mutex audio_buf_mutex;
	circlebuf audio_timestamp_buf;
	uint64_t audio_time;
	uint64_t last_audio_time;

	int64_t timing_adjust;
	std::mutex timing_mutex;
};
#include "output.h"
#include "studio.h"

Output::Output(const std::shared_ptr<OutputSettings> &settings) :
        settings(settings),
        video_encoder(nullptr),
        audio_encoder(nullptr),
        output_service(nullptr),
        output(nullptr) {
}

std::shared_ptr<OutputSettings> Output::getSettings() {
    return settings;
}

void Output::start(video_t *video, audio_t *audio) {
    if (!settings) {
        return;
    }

    // video encoder
    std::string encoder = settings->hardwareEnable ? "ffmpeg_nvenc" : "obs_x264";
    video_encoder = obs_video_encoder_create(encoder.c_str(), "h264 enc", nullptr, nullptr);
    if (!video_encoder) {
        throw std::runtime_error("Failed to create video encoder.");
    }

    obs_data_t *video_encoder_settings = obs_encoder_get_settings(video_encoder);
    if (!video_encoder_settings) {
        throw std::runtime_error("Failed to get video encoder settings.");
    }

    obs_data_set_int(video_encoder_settings, "keyint_sec", settings->keyintSec);
    obs_data_set_string(video_encoder_settings, "rate_control", settings->rateControl.c_str());
    obs_data_set_int(video_encoder_settings, "width", settings->width);
    obs_data_set_int(video_encoder_settings, "height", settings->height);
    obs_data_set_string(video_encoder_settings, "preset", settings->preset.c_str());
    obs_data_set_string(video_encoder_settings, "profile", settings->profile.c_str());
    obs_data_set_string(video_encoder_settings, "tune", settings->tune.c_str());
    obs_data_set_string(video_encoder_settings, "x264opts", settings->x264opts.c_str());
    obs_data_set_int(video_encoder_settings, "bitrate", settings->videoBitrateKbps);

    obs_encoder_update(video_encoder, video_encoder_settings);
    obs_encoder_set_scaled_size(video_encoder, settings->width, settings->height);
    obs_encoder_set_video(video_encoder, video);

    // audio encoder
    audio_encoder = obs_audio_encoder_create("ffmpeg_aac", "aac enc", nullptr, 0, nullptr);
    if (!audio_encoder) {
        throw std::runtime_error("Failed to create audio encoder.");
    }

    obs_data_t *audio_encoder_settings = obs_encoder_get_settings(audio_encoder);
    if (!audio_encoder_settings) {
        throw std::runtime_error("Failed to get audio encoder settings.");
    }

    obs_data_set_int(audio_encoder_settings, "bitrate", settings->audioBitrateKbps);
    obs_encoder_update(audio_encoder, audio_encoder_settings);
    obs_encoder_set_audio(audio_encoder, audio);

    // output service
    std::string server;
    std::string key;
    bool is_rtmp = settings->url.find("rtmp", 0) == 0;
    if (is_rtmp) {
        auto index = settings->url.rfind('/');
        server = settings->url.substr(0, index);
        key = settings->url.substr(index + 1);
    } else {
        server = settings->url;
    }

    obs_data_t *output_service_settings = obs_data_create();
    obs_data_set_string(output_service_settings, "server", server.c_str());
    if (!key.empty()) {
        obs_data_set_string(output_service_settings, "key", key.c_str());
    }

    obs_service_update(output_service, output_service_settings);
    output_service = obs_service_create("rtmp_custom", "custom service", output_service_settings, nullptr);
    if (!output_service) {
        throw std::runtime_error("Failed to create output service.");
    }

    obs_service_apply_encoder_settings(output_service, video_encoder_settings, audio_encoder_settings);

    obs_data_release(video_encoder_settings);
    obs_data_release(audio_encoder_settings);
    obs_data_release(output_service_settings);

    // output
    if (is_rtmp) {
        output = obs_output_create("rtmp_output", "rtmp output", nullptr, nullptr);
    } else {
        output = obs_output_create("ffmpeg_mpegts_muxer", "ffmpeg mpegts muxer output", nullptr, nullptr);
        obs_data_t *output_settings = obs_output_get_settings(output);
#ifdef _WIN32
        obs_data_set_string(output_settings, "exec_path", (Studio::getObsBinPath() + "\\obs-ffmpeg-mux.exe").c_str());
#else
        obs_data_set_string(output_settings, "exec_path", (Studio::getObsBinPath() + "/obs-ffmpeg-mux").c_str());
#endif
    }

    if (!output) {
        throw std::runtime_error("Failed to create output.");
    }

    if (video_encoder) {
        obs_output_set_video_encoder(output, video_encoder);
    }

    if (audio_encoder) {
        obs_output_set_audio_encoder(output, audio_encoder, 0);
    }

    obs_output_set_service(output, output_service);

    //delay
    if (settings->delaySec >= 0) {
        obs_output_set_delay(output, settings->delaySec, 0);
    }

    if (!obs_output_start(output)) {
        throw std::runtime_error("Failed to start output.");
    }
}

void Output::stop() {
    if (output) {
        obs_output_stop(output);
        obs_encoder_release(video_encoder);
        obs_encoder_release(audio_encoder);
        obs_output_release(output);
        obs_service_release(output_service);
    }
}

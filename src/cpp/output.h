#pragma once
#include <obs.h>
#include "settings.h"

class Output {

public:
    explicit Output(const std::shared_ptr<OutputSettings> &settings);

    std::shared_ptr<OutputSettings> getSettings();
    void start(video_t *video, audio_t *audio);
    void stop();

private:
    std::shared_ptr<OutputSettings> settings;
    obs_encoder_t *video_encoder;
    obs_encoder_t *audio_encoder;
    obs_service_t *output_service;
    obs_output_t *output;
};
#pragma once
#include <obs.h>
#include "settings.h"

class Output {

public:
    Output(OutputSettings *settings);

    void start(video_t *video, audio_t *audio);
    void stop();

private:
    OutputSettings *settings;
    obs_encoder_t *video_encoder;
    obs_encoder_t *audio_encoder;
    obs_service_t *output_service;
    obs_output_t *output;
};
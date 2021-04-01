#pragma once

#include <string>
#include <optional>
#include <napi.h>

struct VideoSettings {
    explicit VideoSettings(const Napi::Object& videoSettings);
    int baseWidth;
    int baseHeight;
    int outputWidth;
    int outputHeight;
    int fpsNum;
    int fpsDen;
};

struct AudioSettings {
    explicit AudioSettings(const Napi::Object& audioSettings);
    int sampleRate;
};

class OutputSettings {

public:
    explicit OutputSettings(const Napi::Object& outputSettings);
    bool equals(OutputSettings *settings);

    std::string server;
    std::string key;
    bool hardwareEnable;
    int width;
    int height;
    int keyintSec;
    std::string rateControl;
    std::string preset;
    std::string profile;
    std::string tune;
    std::string x264opts;
    int videoBitrateKbps;
    int audioBitrateKbps;
};

class Settings {

public:
    explicit Settings(const Napi::Object& settings);
    ~Settings();
    VideoSettings *video;
    AudioSettings *audio;
    std::vector<OutputSettings*> outputs;
};
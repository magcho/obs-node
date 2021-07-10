#pragma once

#include <string>
#include <memory>
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
    bool equals(const std::shared_ptr<OutputSettings> &settings);

    std::string url;
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
    uint32_t delaySec;
    int mixers;
};

class Settings {

public:
    explicit Settings(const Napi::Object& settings);
    ~Settings();
    std::string locale;
    std::string fontDirectory;
    bool showTimestamp;
    std::string timestampFontPath;
    uint32_t timestampFontHeight;
    VideoSettings *video;
    AudioSettings *audio;
};
#include "settings.h"
#include "utils.h"
#include <obs.h>

VideoSettings::VideoSettings(const Napi::Object &videoSettings) {
    baseWidth = getNapiInt(videoSettings, "baseWidth");
    baseHeight = getNapiInt(videoSettings, "baseHeight");
    outputWidth = getNapiInt(videoSettings, "outputWidth");
    outputHeight = getNapiInt(videoSettings, "outputHeight");
    fpsNum = getNapiInt(videoSettings, "fpsNum");
    fpsDen = getNapiInt(videoSettings, "fpsDen");
}

AudioSettings::AudioSettings(const Napi::Object &audioSettings) {
    sampleRate = getNapiInt(audioSettings, "sampleRate");
}

OutputSettings::OutputSettings(const Napi::Object &outputSettings) {
    server = getNapiString(outputSettings, "server");
    key = getNapiString(outputSettings, "key");
    hardwareEnable = getNapiBoolean(outputSettings, "hardwareEnable");
    width = getNapiInt(outputSettings, "width");
    height = getNapiInt(outputSettings, "height");
    keyintSec = getNapiInt(outputSettings, "keyintSec");
    rateControl = getNapiString(outputSettings, "rateControl");
    preset = getNapiString(outputSettings, "preset");
    profile = getNapiString(outputSettings, "profile");
    tune = getNapiString(outputSettings, "tune");
    x264opts = getNapiStringOrDefault(outputSettings, "x264opts", "");
    videoBitrateKbps = getNapiInt(outputSettings, "videoBitrateKbps");
    audioBitrateKbps = getNapiInt(outputSettings, "audioBitrateKbps");
}

Settings::Settings(const Napi::Object &settings) :
        video(nullptr),
        audio(nullptr),
        outputs() {

    locale = getNapiStringOrDefault(settings, "locale", "zh-CN");

    // video settings
    auto videoSettings = settings.Get("video").As<Napi::Object>();
    video = new VideoSettings(videoSettings);

    // audio settings
    auto audioObject = settings.Get("audio").As<Napi::Object>();
    audio = new AudioSettings(audioObject);

    // output settings
    if (!settings.Get("outputs").IsUndefined()) {
        auto outputObjects = settings.Get("outputs").As<Napi::Array>();
        for (uint32_t i = 0; i < outputObjects.Length(); ++i) {
            outputs.push_back(new OutputSettings(outputObjects.Get(i).As<Napi::Object>()));
        }
    }

    // Show log
    blog(LOG_INFO, "Video Settings");
    blog(LOG_INFO, "=====================");
    blog(LOG_INFO, "baseWidth = %d", video->baseWidth);
    blog(LOG_INFO, "baseHeight = %d", video->baseHeight);
    blog(LOG_INFO, "outputWidth = %d", video->outputWidth);
    blog(LOG_INFO, "outputHeight = %d", video->outputHeight);
    blog(LOG_INFO, "fpsNum = %d", video->fpsNum);
    blog(LOG_INFO, "fpsDen = %d", video->fpsDen);

    blog(LOG_INFO, "Audio Settings");
    blog(LOG_INFO, "=====================");
    blog(LOG_INFO, "sampleRate = %d", audio->sampleRate);

    for (auto output : outputs) {
        blog(LOG_INFO, "Output Settings");
        blog(LOG_INFO, "=====================");
        blog(LOG_INFO, "server = %s", output->server.c_str());
        blog(LOG_INFO, "key = %s", output->key.c_str());
        blog(LOG_INFO, "Video Encoder Settings");
        blog(LOG_INFO, "=====================");
        blog(LOG_INFO, "hardwareEnable = %s", output->hardwareEnable ? "true" : "false");
        blog(LOG_INFO, "keyintSec = %d", output->keyintSec);
        blog(LOG_INFO, "rateControl = %s", output->rateControl.c_str());
        blog(LOG_INFO, "preset = %s", output->preset.c_str());
        blog(LOG_INFO, "profile = %s", output->profile.c_str());
        blog(LOG_INFO, "tune = %s", output->tune.c_str());
        blog(LOG_INFO, "x264opts = %s", output->x264opts.c_str());
        blog(LOG_INFO, "Video bitrateKbps = %d", output->videoBitrateKbps);
        blog(LOG_INFO, "Audio bitrateKbps = %d", output->audioBitrateKbps);
    }
}

Settings::~Settings() {
    delete video;
    delete audio;
    for (auto output : outputs) {
        delete output;
    }
}

SourceSettings::SourceSettings(const Napi::Object &settings) :
        output(nullptr) {
    type = getNapiString(settings, "type");
    isFile = getNapiBooleanOrDefault(settings, "isFile", false);
    url = getNapiString(settings, "url");
    startOnActive = getNapiBooleanOrDefault(settings, "startOnActive", false);
    hardwareDecoder = getNapiBoolean(settings, "hardwareDecoder");
    enableBuffer = getNapiBooleanOrDefault(settings, "enableBuffer", true);
    bufferSize = getNapiIntOrDefault(settings, "bufferSize", 2);
    reconnectDelaySec = getNapiIntOrDefault(settings, "reconnectDelaySec", 10);
    if (!settings.Get("output").IsUndefined()) {
        auto outputSettings = settings.Get("output").As<Napi::Object>();
        output = new OutputSettings(outputSettings);
    }
}

SourceSettings::~SourceSettings() {
    delete output;
}

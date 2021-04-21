#include "settings.h"
#include "utils.h"
#include <obs.h>

VideoSettings::VideoSettings(const Napi::Object &videoSettings) {
    baseWidth = NapiUtil::getInt(videoSettings, "baseWidth");
    baseHeight = NapiUtil::getInt(videoSettings, "baseHeight");
    outputWidth = NapiUtil::getInt(videoSettings, "outputWidth");
    outputHeight = NapiUtil::getInt(videoSettings, "outputHeight");
    fpsNum = NapiUtil::getInt(videoSettings, "fpsNum");
    fpsDen = NapiUtil::getInt(videoSettings, "fpsDen");
}

AudioSettings::AudioSettings(const Napi::Object &audioSettings) {
    sampleRate = NapiUtil::getInt(audioSettings, "sampleRate");
}

OutputSettings::OutputSettings(const Napi::Object &outputSettings) {
    server = NapiUtil::getString(outputSettings, "server");
    key = NapiUtil::getString(outputSettings, "key");
    hardwareEnable = NapiUtil::getBoolean(outputSettings, "hardwareEnable");
    width = NapiUtil::getInt(outputSettings, "width");
    height = NapiUtil::getInt(outputSettings, "height");
    keyintSec = NapiUtil::getInt(outputSettings, "keyintSec");
    rateControl = NapiUtil::getString(outputSettings, "rateControl");
    preset = NapiUtil::getString(outputSettings, "preset");
    profile = NapiUtil::getString(outputSettings, "profile");
    tune = NapiUtil::getString(outputSettings, "tune");
    x264opts = NapiUtil::getStringOptional(outputSettings, "x264opts").value_or("");
    videoBitrateKbps = NapiUtil::getInt(outputSettings, "videoBitrateKbps");
    audioBitrateKbps = NapiUtil::getInt(outputSettings, "audioBitrateKbps");
}

bool OutputSettings::equals(OutputSettings *settings) {
    return server == settings->server &&
            key == settings->key &&
            hardwareEnable == settings->hardwareEnable &&
            width == settings->width &&
            height == settings->height &&
            keyintSec == settings->keyintSec &&
            rateControl == settings->rateControl &&
            preset == settings->preset &&
            profile == settings->profile &&
            tune == settings->tune &&
            x264opts == settings->x264opts &&
            videoBitrateKbps == settings->videoBitrateKbps &&
            audioBitrateKbps == settings->audioBitrateKbps;
}

Settings::Settings(const Napi::Object &settings) :
        video(nullptr),
        audio(nullptr),
        outputs() {

    locale = NapiUtil::getStringOptional(settings, "locale").value_or("zh-CN");

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
#include "settings.h"
#include "utils.h"

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
    url = NapiUtil::getString(outputSettings, "url");
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

bool OutputSettings::equals(const std::shared_ptr<OutputSettings> &settings) {
    return url == settings->url &&
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
        audio(nullptr) {

    locale = NapiUtil::getStringOptional(settings, "locale").value_or("zh-CN");
    fontDirectory = NapiUtil::getStringOptional(settings, "fontDirectory").value_or("");
    showTimestamp = NapiUtil::getBooleanOptional(settings, "showTimestamp").value_or(false);
    timestampFontPath = NapiUtil::getStringOptional(settings, "timestampFontPath").value_or("");
    timestampFontHeight = NapiUtil::getIntOptional(settings, "timestampFontHeight").value_or(40);

    // video settings
    auto videoSettings = settings.Get("video").As<Napi::Object>();
    video = new VideoSettings(videoSettings);

    // audio settings
    auto audioObject = settings.Get("audio").As<Napi::Object>();
    audio = new AudioSettings(audioObject);
}

Settings::~Settings() {
    delete video;
    delete audio;
}
#pragma once

#include "settings.h"
#include "source_transcoder.h"
#include <obs.h>
#include <string>

enum SourceType {
    SOURCE_TYPE_LIVE = 0,
    SOURCE_TYPE_MEDIA = 1,
};

class Source {

friend class SourceTranscoder;

public:
    static SourceType getSourceType(const std::string &sourceType);
    static std::string getSourceTypeString(SourceType sourceType);

    Source(std::string &id,
           std::string &sceneId,
           obs_scene_t *obs_scene,
           Settings *studioSettings,
           const Napi::Object &settings
    );
    ~Source();

    void update(const Napi::Object &settings);

    void restart();

    void screenshot(std::function<void(uint8_t*, int)> callback);

    Napi::Object toNapiObject(Napi::Env env);

    uint64_t getTimestamp();

    uint64_t getServerTimestamp();

private:
    static void volmeter_callback(
            void *param,
            const float *magnitude,
            const float *peak,
            const float *input_peak);

    static void screenshot_callback(void *param);

    static void source_activate_callback(void *param, calldata_t *data);
    static void source_deactivate_callback(void *param, calldata_t *data);

    void start();
    void stop();
    void startOutput();
    void stopOutput();

    void setVolume(int volume);
    void setAudioLock(bool audioLock);
    void setMonitor(bool monitor);

    void play();
    void stopToBeginning();

    std::string id;
    std::string sceneId;
    std::string name;
    SourceType type;
    std::string url;
    bool playOnActive;
    bool hardwareDecoder;
    bool asyncUnbuffered;
    int bufferingMb;
    int reconnectDelaySec;
    int volume;
    bool audioLock;
    bool monitor;
    bool showTimestamp;
    std::shared_ptr<OutputSettings> output;

    obs_scene_t *obs_scene;
    obs_source_t *obs_source;
    obs_sceneitem_t *obs_scene_item;
    obs_volmeter_t *obs_volmeter;
    obs_fader_t *obs_fader;

    SourceTranscoder *transcoder;
};

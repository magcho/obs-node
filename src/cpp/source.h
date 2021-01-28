#pragma once

#include "settings.h"
#include "source_transcoder.h"
#include <obs.h>
#include <string>

enum SourceType {
    Image = 0,
    MediaSource = 1,
};

class Source {

friend class SourceTranscoder;

public:
    static SourceType getSourceType(const std::string &sourceType);

    static std::string getSourceTypeString(SourceType sourceType);

    Source(std::string &id,
           std::string &sceneId,
           obs_scene_t *obs_scene,
           std::shared_ptr<SourceSettings> &settings
    );

    void start();

    void stop();

    void restart();

    std::string getId();

    std::string getSceneId();

    SourceType getType();

    void setUrl(const std::string &sourceUrl);

    std::string getUrl();

    void setVolume(float volume);

    float getVolume();

    void setAudioLock(bool audioLock);

    bool getAudioLock();

    void setAudioMonitor(bool audioMonitor);

    bool getAudioMonitor();

private:
    static void volmeter_callback(
            void *param,
            const float *magnitude,
            const float *peak,
            const float *input_peak);

    static void source_activate_callback(void *param, calldata_t *data);
    static void source_deactivate_callback(void *param, calldata_t *data);

    void play();
    void pauseToBeginning();

    std::string id;
    std::string sceneId;
    obs_scene_t *obs_scene;
    std::shared_ptr<SourceSettings> settings;
    SourceType type;
    std::string url;
    obs_source_t *obs_source;
    obs_sceneitem_t *obs_scene_item;
    obs_volmeter_t *obs_volmeter;
    obs_fader_t *obs_fader;

    SourceTranscoder *transcoder;
};

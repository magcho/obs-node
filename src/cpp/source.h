#pragma once

#include "settings.h"
#include <string>
#include <obs.h>

enum SourceType {
    Image = 0,
    MediaSource = 1,
};

class Source {

public:
    static SourceType getSourceType(const std::string &sourceType);
    static std::string getSourceTypeString(SourceType sourceType);

    Source(std::string &id,
           SourceType type,
           std::string &url,
           std::string &sceneId,
           int sceneIndex,
           obs_scene_t *obs_scene,
           Settings *settings
    );

    void start();

    void stop();

    void restart();

    std:: string getId();

    std:: string getSceneId();

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
    static void obs_volmeter_callback(
            void *param,
            const float magnitude[MAX_AUDIO_CHANNELS],
            const float peak[MAX_AUDIO_CHANNELS],
            const float input_peak[MAX_AUDIO_CHANNELS]);

    std::string id;
    SourceType type;
    std::string url;
    std::string sceneId;
    int sceneIndex;
    obs_scene_t *obs_scene;
    Settings *settings;
    obs_source_t *obs_source;
    obs_sceneitem_t *obs_scene_item;
    obs_volmeter_t *obs_volmeter;
    obs_fader_t *obs_fader;
    bool started;
};

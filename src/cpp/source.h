#pragma once

#include "settings.h"
#include "output.h"
#include <string>
#include <memory>
#include <obs.h>
#include <util/circlebuf.h>

enum SourceType {
    Image = 0,
    MediaSource = 1,
};

class Source {

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

    static void video_output_callback(
            void *param,
            uint32_t cx,
            uint32_t cy
    );

    static bool audio_output_callback(
            void *param,
            uint64_t start_ts_in,
            uint64_t end_ts_in,
            uint64_t *out_ts,
            uint32_t mixers,
            struct audio_output_data *mixes
    );

    static void audio_capture_callback(
            void *param,
            obs_source_t *source,
            const struct audio_data *audio_data,
            bool muted
    );

    static void source_activate_callback(void *param, calldata_t *data);
    static void source_deactivate_callback(void *param, calldata_t *data);

    void startOutput();
    void stopOutput();
    void play();
    void pauseToBeginning();

    std::string id;
    std::string sceneId;
    obs_scene_t *obs_scene;
    std::shared_ptr<SourceSettings> settings;
    SourceType type;
    std::string url;
    Output *output;
    obs_source_t *obs_source;
    obs_sceneitem_t *obs_scene_item;
    obs_volmeter_t *obs_volmeter;
    obs_fader_t *obs_fader;
    video_t *output_video;
    audio_t *output_audio;
    gs_texrender_t *output_texrender;
    gs_stagesurf_t *output_stagesurface;
    circlebuf output_audio_buf[MAX_AUDIO_CHANNELS];
    pthread_mutex_t output_audio_buf_mutex;
};

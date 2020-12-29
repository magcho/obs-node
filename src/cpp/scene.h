#pragma once

#include "settings.h"
#include "source.h"
#include "dsk.h"
#include <string>
#include <map>
#include <obs.h>

class Scene {
public:
    Scene(std::string &id, int index, Settings *settings);
    ~Scene();

    std::string getId() { return id; }

    const std::map<std::string, Source*> &getSources();

    void addSource(std::string &sourceId, SourceType sourceType, std::string &sourceUrl);

    Source *findSource(std::string &sourceId);

    obs_scene_t *getObsOutputScene(std::map<std::string, Dsk*> &dsks);

private:
    static obs_scene_t *createObsScene(std::string &sceneId);

    std::string id;
    int index;
    Settings *settings;
    obs_scene_t *obs_scene;
    obs_scene_t *obs_output_scene;
    std::map<std::string, Source *> sources;
};

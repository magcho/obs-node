#pragma once

#include "settings.h"
#include "source.h"
#include <string>
#include <map>
#include <obs.h>

class Scene {
public:
    Scene(std::string &id, int index, Settings *settings);
    ~Scene();

    std::string getId() { return id; }

    void addSource(std::string &sourceId, Settings *studioSettings, const Napi::Object &settings);

    Source *findSource(std::string &sourceId);

    obs_scene_t *getScene();

    std::map<std::string, Source *> &getSources();

private:
    static obs_scene_t *createObsScene(std::string &sceneId);

    std::string id;
    int index;
    Settings *settings;
    obs_scene_t *obs_scene;
    std::map<std::string, Source *> sources;
};

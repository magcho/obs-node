#include "scene.h"
#include <map>

Scene::Scene(std::string &id, int index, Settings *settings) :
        id(id),
        index(index),
        settings(settings),
        obs_scene(createObsScene(id)) {
}

Scene::~Scene() {
    for (auto source : sources) {
        delete source.second;
    }
    if (obs_scene) {
        obs_scene_release(obs_scene);
    }
}

void Scene::addSource(std::string &sourceId, const Napi::Object &settings) {
    auto source = new Source(sourceId, id, obs_scene, settings);
    sources[sourceId] = source;
}

obs_scene_t *Scene::createObsScene(std::string &sceneId) {
    obs_scene_t *scene = obs_scene_create(sceneId.c_str());
    if (scene == nullptr) {
        throw std::runtime_error("Failed to create obs scene " + sceneId);
    }
    return scene;
}

Source *Scene::findSource(std::string &sourceId) {
    auto it = sources.find(sourceId);
    if (it == sources.end()) {
        throw std::invalid_argument("Can't find source " + sourceId);
    }
    return it->second;
}

obs_scene_t *Scene::getScene() {
    return obs_scene;
}

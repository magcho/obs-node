#pragma once

#include "settings.h"
#include "scene.h"
#include "display.h"
#include "output.h"
#include "overlay.h"
#include <map>
#include <obs.h>
#include "utils.h"

struct DelaySwitchData;

class Studio {

public:
    static void setObsPath(std::string &obsPath);
    static void setCefQueueTaskCallback(std::function<bool(std::function<void()>)> callback);

    static std::string getObsBinPath();
    static std::string getObsPluginPath();
    static std::string getObsPluginDataPath();

    explicit Studio(Settings *settings);

    void startup();

    void shutdown();

    void addOutput(const std::string &outputId, std::shared_ptr<OutputSettings> settings);

    void updateOutput(const std::string &outputId, std::shared_ptr<OutputSettings> settings);

    void removeOutput(const std::string &outputId);

    void addScene(std::string &sceneId);

    void removeScene(std::string &sceneId);

    void addSource(std::string &sceneId, std::string &sourceId, const Napi::Object &settings);

    Source *findSource(std::string &sceneId, std::string &sourceId);

    void switchToScene(std::string &sceneId, std::string &transitionType, int transitionMs, uint64_t timestamp);

    void createDisplay(std::string &displayName, void *parentHandle, int scaleFactor, const std::vector<std::string> &sourceIds);

    void destroyDisplay(std::string &displayName);

    void moveDisplay(std::string &displayName, int x, int y, int width, int height);

    void updateDisplay(std::string &displayName, const std::vector<std::string> &sourceIds);

    Napi::Object getAudio(Napi::Env env);

    void updateAudio(const Napi::Object &audio);

    void addOverlay(Overlay *overlay);

    void removeOverlay(const std::string &overlayId);

    void upOverlay(const std::string &overlayId);

    void downOverlay(const std::string &overlayId);

    std::map<std::string, Overlay *> &getOverlays();

private:
    static void loadModule(const std::string &binPath, const std::string &dataPath);
    static void delay_switch_callback(void *param);
    Scene *findScene(std::string &sceneId);
    uint64_t getSourceTimestamp(std::string &sceneId);

    static std::string obsPath;
    static std::function<bool(std::function<void()>)> cef_queue_task_callback;
    Settings *settings;
    std::map<std::string, Scene *> scenes;
    std::map<std::string, obs_source_t *> transitions;
    std::map<std::string, Display *> displays;
    std::map<std::string, Overlay *> overlays;
    Scene *currentScene;
    std::map<std::string, Output *> outputs;

    volatile bool stop;
    std::thread delay_switch_thread;
    queue<DelaySwitchData *> delay_switch_queue;
};
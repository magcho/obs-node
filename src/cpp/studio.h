#pragma once

#include "settings.h"
#include "scene.h"
#include "display.h"
#include "output.h"
#include "overlay.h"
#include <map>
#include <obs.h>

class Studio {

public:
    static void setObsPath(std::string &obsPath);
    static void setFontPath(std::string &fontPath);
    static std::string getObsBinPath();
    static std::string getObsPluginPath();
    static std::string getObsPluginDataPath();
    static std::string getFontPath();

    Studio(Settings *settings);
    ~Studio();

    void startup();

    void shutdown();

    void addScene(std::string &sceneId);

    void addSource(std::string &sceneId, std::string &sourceId, std::shared_ptr<SourceSettings> &settings);

    Source *findSource(std::string &sceneId, std::string &sourceId);

    void addDSK(std::string &id, std::string &position, std::string &url, int left, int top, int width, int height);

    void switchToScene(std::string &sceneId, std::string &transitionType, int transitionMs);

    void createDisplay(std::string &displayName, void *parentHandle, int scaleFactor, std::string &sourceId);

    void destroyDisplay(std::string &displayName);

    void moveDisplay(std::string &displayName, int x, int y, int width, int height);

    bool getAudioWithVideo();

    void setAudioWithVideo(bool audioWithVideo);

    void setPgmMonitor(bool pgmMonitor);

    float getMasterVolume();

    void setMasterVolume(float volume);

    void addOverlay(Overlay *overlay);

    void removeOverlay(const std::string &overlayId);

    void upOverlay(const std::string &overlayId);

    void downOverlay(const std::string &overlayId);

    std::map<std::string, Overlay *> &getOverlays();

private:
    static void loadModule(const std::string &binPath, const std::string &dataPath);
    Scene *findScene(std::string &sceneId);

    static std::string obsPath;
    static std::string fontPath;
    Settings *settings;
    std::map<std::string, Scene *> scenes;
    std::map<std::string, obs_source_t *> transitions;
    std::map<std::string, Display *> displays;
    std::map<std::string, Dsk *> dsks;
    std::map<std::string, Overlay *> overlays;
    Scene *currentScene;
    std::vector<Output *> outputs;
};
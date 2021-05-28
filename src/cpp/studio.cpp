#include "studio.h"
#include "utils.h"
#include <filesystem>
#include <mutex>
#include <obs.h>
#include <util/platform.h>

struct DelaySwitchData {
    std::string sceneId;
    std::string transitionType;
    int transitionMs;
    uint64_t timestamp;
};

#define MAX_SWITCH_DELAY 2000000000

std::mutex scenes_mtx;
std::string Studio::obsPath;
std::string Studio::fontPath;
std::function<bool(std::function<void()>)> Studio::cef_queue_task_callback;

Studio::Studio(Settings *settings) :
          settings(settings),
          currentScene(nullptr),
          outputs(),
          delay_switch_thread(),
          delay_switch_queue(),
          stop(false),
          overlays() {
}

void Studio::startup() {
    auto currentWorkDir = std::filesystem::current_path();

    // Change work directory to obs bin path to setup obs properly.
    blog(LOG_INFO, "Set work directory to %s for loading obs data", getObsBinPath().c_str());
    std::filesystem::current_path(getObsBinPath());

    auto restore = [&] {
        std::filesystem::current_path(currentWorkDir);
    };

    try {
        obs_startup(settings->locale.c_str(), nullptr, nullptr);
        if (!obs_initialized()) {
            throw std::runtime_error("Failed to startup obs studio.");
        }

        // reset video
        if (settings->video) {
            obs_video_info ovi = {};
            memset(&ovi, 0, sizeof(ovi));
            ovi.adapter = 0;
#ifdef _WIN32
            ovi.graphics_module = "libobs-opengl.dll";
#else
            ovi.graphics_module = "libobs-opengl.so";
#endif
            ovi.output_format = VIDEO_FORMAT_NV12;
            ovi.fps_num = settings->video->fpsNum;
            ovi.fps_den = settings->video->fpsDen;
            ovi.base_width = settings->video->baseWidth;
            ovi.base_height = settings->video->baseHeight;
            ovi.output_width = settings->video->outputWidth;
            ovi.output_height = settings->video->outputHeight;
            ovi.gpu_conversion = true; // always be true for the OBS issue

            int result = obs_reset_video(&ovi);
            if (result != OBS_VIDEO_SUCCESS) {
                throw std::runtime_error("Failed to reset video");
            }
        }

        // reset audio
        if (settings->audio) {
            obs_audio_info oai = {};
            memset(&oai, 0, sizeof(oai));
            oai.samples_per_sec = settings->audio->sampleRate;
            oai.speakers = SPEAKERS_STEREO;
            if (!obs_reset_audio(&oai)) {
                throw std::runtime_error("Failed to reset audio");
            }
        }

        // setup cef queue task callback
        obs_data_t *settings = obs_data_create();
        obs_data_set_int(settings, "cef_queue_task_callback", reinterpret_cast<uint64_t>(&Studio::cef_queue_task_callback));
        obs_apply_private_data(settings);
        obs_data_release(settings);

        // load modules
#ifdef _WIN32
        loadModule(getObsPluginPath() + "\\image-source.dll", getObsPluginDataPath() + "\\image-source");
        loadModule(getObsPluginPath() + "\\obs-ffmpeg.dll", getObsPluginDataPath() + "\\obs-ffmpeg");
        loadModule(getObsPluginPath() + "\\obs-transitions.dll", getObsPluginDataPath() + "\\obs-transitions");
        loadModule(getObsPluginPath() + "\\rtmp-services.dll", getObsPluginDataPath() + "\\rtmp-services");
        loadModule(getObsPluginPath() + "\\obs-x264.dll", getObsPluginDataPath() + "\\obs-x264");
        loadModule(getObsPluginPath() + "\\obs-outputs.dll", getObsPluginDataPath() + "\\obs-outputs");
        loadModule(getObsPluginPath() + "\\text-freetype2.dll", getObsPluginDataPath() + "\\text-freetype2");
#else
        loadModule(getObsPluginPath() + "/image-source.so", getObsPluginDataPath() + "/image-source");
        loadModule(getObsPluginPath() + "/obs-ffmpeg.so", getObsPluginDataPath() + "/obs-ffmpeg");
        loadModule(getObsPluginPath() + "/obs-transitions.so", getObsPluginDataPath() + "/obs-transitions");
        loadModule(getObsPluginPath() + "/rtmp-services.so", getObsPluginDataPath() + "/rtmp-services");
        loadModule(getObsPluginPath() + "/obs-x264.so", getObsPluginDataPath() + "/obs-x264");
        loadModule(getObsPluginPath() + "/obs-outputs.so", getObsPluginDataPath() + "/obs-outputs");
        loadModule(getObsPluginPath() + "/text-freetype2.so", getObsPluginDataPath() + "/text-freetype2");
#endif

        obs_post_load_modules();

        for (auto output : outputs) {
            output.second->start(obs_get_video(), obs_get_audio());
        }

        restore();

        //start switch thread
        delay_switch_thread = std::thread(&Studio::delay_switch_callback, this);

    } catch (...) {
        restore();
        throw;
    }
}

void Studio::shutdown() {
    stop = true;
    delay_switch_queue.push(nullptr);
    delay_switch_thread.join();
    for (const auto& scene : scenes) {
        delete scene.second;
    }
    for (const auto& transition : transitions) {
        obs_source_release(transition.second);
    }
    for (const auto& display : displays) {
        delete display.second;
    }
    for (const auto& overlay : overlays) {
        delete overlay.second;
    }
    for (auto output : outputs) {
        output.second->stop();
        delete output.second;
    }
    scenes.clear();
    transitions.clear();
    displays.clear();
    overlays.clear();
    outputs.clear();
    obs_shutdown();
    if (obs_initialized()) {
        throw std::runtime_error("Failed to shutdown obs studio.");
    }
}

void Studio::addOutput(const std::string &outputId, std::shared_ptr<OutputSettings> settings) {
    if (outputs.find(outputId) != outputs.end()) {
        throw std::logic_error("Output: " + outputId + " already existed");
    }
    auto output = new Output(settings);
    output->start(obs_get_video(), obs_get_audio());
    this->outputs[outputId] = output;
}

void Studio::updateOutput(const std::string &outputId, std::shared_ptr<OutputSettings> settings) {
    if (outputs.find(outputId) == outputs.end()) {
        throw std::logic_error("Can't find output: " + outputId);
    }
    if (!outputs[outputId]->getSettings()->equals(settings)) {
        removeOutput(outputId);
        addOutput(outputId, settings);
    }
}

void Studio::removeOutput(const std::string &outputId) {
    if (outputs.find(outputId) == outputs.end()) {
        throw std::logic_error("Can't find output: " + outputId);
    }
    auto output = outputs[outputId];
    output->stop();
    delete output;
    outputs.erase(outputId);
}

void Studio::addScene(std::string &sceneId) {
    std::unique_lock<std::mutex> lock(scenes_mtx);
    int index = (int)scenes.size();
    auto scene = new Scene(sceneId, index, settings);
    scenes[sceneId] = scene;
}

void Studio::removeScene(std::string &sceneId) {
    std::unique_lock<std::mutex> lock(scenes_mtx);
    auto scene = scenes[sceneId];
    scenes.erase(sceneId);
    if (currentScene == scene) {
        currentScene = nullptr;
    }
    delete scene;
}

void Studio::addSource(std::string &sceneId, std::string &sourceId, const Napi::Object &settings) {
    std::unique_lock<std::mutex> lock(scenes_mtx);
    findScene(sceneId)->addSource(sourceId, settings);
}

Source *Studio::findSource(std::string &sceneId, std::string &sourceId) {
    std::unique_lock<std::mutex> lock(scenes_mtx);
    return findScene(sceneId)->findSource(sourceId);
}

void Studio::switchToScene(std::string &sceneId, std::string &transitionType, int transitionMs, uint64_t timestamp) {
    if (timestamp > 0) {
        auto data = new DelaySwitchData{
            .sceneId = sceneId,
            .transitionType = transitionType,
            .transitionMs = transitionMs,
            .timestamp = timestamp,
        };
        delay_switch_queue.push(data);
        return;
    }

    Scene *next;
    {
        std::unique_lock<std::mutex> lock(scenes_mtx);
        next = findScene(sceneId);
    }

    if (!next) {
        throw std::runtime_error("Can't find scene: " + sceneId);
    }

    if (next == currentScene) {
        blog(LOG_INFO, "Same with current scene, no need to switch, skip.");
        return;
    }

    blog(LOG_INFO, "Start transition: %s -> %s", (currentScene ? currentScene->getId().c_str() : ""),
         next->getId().c_str());

    // Find or create transition
    auto it = transitions.find(transitionType);
    if (it == transitions.end()) {
        transitions[transitionType] = obs_source_create(transitionType.c_str(), transitionType.c_str(), nullptr,
                                                        nullptr);
    }

    obs_source_t *transition = transitions[transitionType];
    if (currentScene) {
        obs_transition_set(transition, obs_scene_get_source(currentScene->getScene()));
    }

    obs_set_output_source(0, transition);

    bool ret = obs_transition_start(
            transition,
            OBS_TRANSITION_MODE_AUTO,
            transitionMs,
            obs_scene_get_source(next->getScene())
    );

    if (!ret) {
        throw std::runtime_error("Failed to start transition.");
    }

    currentScene = next;
}

void Studio::delay_switch_callback(void *param) {
    auto *studio = (Studio *) param;
    while (true) {
        DelaySwitchData * data = studio->delay_switch_queue.pop();
        if (studio->stop) {
            break;
        }
        auto curTimestamp = studio->getSourceTimestamp(data->sceneId);
        int64_t time_diff = data->timestamp - curTimestamp;
        blog(LOG_INFO, "sync switch: client_ts = %lld server_ts = %lld time_diff = %lld",
             data->timestamp, curTimestamp, time_diff);
        if (curTimestamp && time_diff > 0 && time_diff < MAX_SWITCH_DELAY) {
            os_sleepto_ns(os_gettime_ns() + time_diff);
        }
        studio->switchToScene(data->sceneId, data->transitionType, data->transitionMs, 0);
    }
}

void Studio::loadModule(const std::string &binPath, const std::string &dataPath) {
    obs_module_t *module = nullptr;
    int code = obs_open_module(&module, binPath.c_str(), dataPath.c_str());
    if (code != MODULE_SUCCESS) {
        throw std::runtime_error("Failed to load module '" + binPath + "'");
    }
    if (!obs_init_module(module)) {
        throw std::runtime_error("Failed to load module '" + binPath + "'");
    }
}

void Studio::setObsPath(std::string &obsPath) {
    Studio::obsPath = obsPath;
}

void Studio::setFontPath(std::string &fontPath) {
    Studio::fontPath = fontPath;
}

void Studio::setCefQueueTaskCallback(std::function<bool(std::function<void()>)> callback) {
    Studio::cef_queue_task_callback = callback;
}

void Studio::createDisplay(std::string &displayName, void *parentHandle, int scaleFactor, const std::vector<std::string> &sourceIds) {
    auto found = displays.find(displayName);
    if (found != displays.end()) {
        throw std::logic_error("Display " + displayName + " already existed");
    }
    auto *display = new Display(parentHandle, scaleFactor, sourceIds);
    displays[displayName] = display;
}

void Studio::destroyDisplay(std::string &displayName) {
    auto found = displays.find(displayName);
    if (found == displays.end()) {
        throw std::logic_error("Can't find display: " + displayName);
    }
    Display *display = found->second;
    displays.erase(displayName);
    delete display;
}

void Studio::moveDisplay(std::string &displayName, int x, int y, int width, int height) {
    auto found = displays.find(displayName);
    if (found == displays.end()) {
        throw std::logic_error("Can't find display: " + displayName);
    }
    found->second->move(x, y, width, height);
}

void Studio::updateDisplay(std::string &displayName, const std::vector<std::string> &sourceIds) {
    auto found = displays.find(displayName);
    if (found == displays.end()) {
        throw std::logic_error("Can't find display: " + displayName);
    }
    found->second->update(sourceIds);
}

Napi::Object Studio::getAudio(Napi::Env env) {
    auto result = Napi::Object::New(env);
    result.Set("volume", (int)obs_mul_to_db(obs_get_master_volume()));
    result.Set("mode", obs_get_audio_with_video() ? "follow" : "standalone");
    return result;
}

void Studio::updateAudio(const Napi::Object &audio) {
    if (!NapiUtil::isUndefined(audio, "volume")) {
        obs_set_master_volume(obs_db_to_mul((float)NapiUtil::getInt(audio, "volume")));
    }
    if (!NapiUtil::isUndefined(audio, "mode")) {
        obs_set_audio_with_video(NapiUtil::getString(audio, "mode") == "follow");
    }
}

void Studio::addOverlay(Overlay *overlay) {
    if (overlays.find(overlay->id) != overlays.end()) {
        throw std::logic_error("Overlay: " + overlay->id + " already existed");
    }
    overlays[overlay->id] = overlay;
}

void Studio::removeOverlay(const std::string &overlayId) {
    if (overlays.find(overlayId) == overlays.end()) {
        throw std::logic_error("Can't find overlay: " + overlayId);
    }
    auto overlay = overlays[overlayId];
    if (overlay->index > -1) {
        overlay->down();
    }
    delete overlay;
    overlays.erase(overlayId);
}

void Studio::upOverlay(const std::string &overlayId) {
    if (overlays.find(overlayId) == overlays.end()) {
        throw std::logic_error("Can't find overlay: " + overlayId);
    }
    if (overlays[overlayId]->index > -1) {
        return;
    }
    // find max overlay index
    int index = -1;
    for (const auto& entry : overlays) {
        if (entry.second->index > index) {
            index = entry.second->index;
        }
    }
    overlays[overlayId]->up(index + 1);
}

void Studio::downOverlay(const std::string &overlayId) {
    if (overlays.find(overlayId) == overlays.end()) {
        throw std::logic_error("Can't find overlay: " + overlayId);
    }
    overlays[overlayId]->down();
}

std::map<std::string, Overlay *> &Studio::getOverlays() {
    return overlays;
}

Scene *Studio::findScene(std::string &sceneId) {
    auto it = scenes.find(sceneId);
    if (it == scenes.end()) {
        throw std::invalid_argument("Can't find scene " + sceneId);
    }
    return it->second;
}

std::string Studio::getObsBinPath() {
#ifdef _WIN32
    return obsPath + "\\bin\\64bit";
#elif __linux__
    return obsPath + "/bin/64bit";
#else
    return obsPath + "/bin";
#endif
}

std::string Studio::getObsPluginPath() {
#ifdef _WIN32
    // Obs plugin path is same with bin path, due to SetDllDirectoryW called in obs-studio/libobs/util/platform-windows.c.
    return obsPath + "\\bin\\64bit";
#elif __linux__
    return obsPath + "/obs-plugins/64bit";
#else
    return obsPath + "/obs-plugins";
#endif
}

std::string Studio::getObsPluginDataPath() {
#ifdef _WIN32
    return obsPath + "\\data\\obs-plugins";
#else
    return obsPath + "/data/obs-plugins";
#endif
}

std::string Studio::getFontPath() {
    return fontPath;
}

uint64_t Studio::getSourceTimestamp(std::string &sceneId) {
    std::unique_lock<std::mutex> lock(scenes_mtx);
    Scene *next = findScene(sceneId);
    if (!next || next->getSources().empty()) {
        return 0;
    }
    return next->getSources().begin()->second->getTimestamp();
}
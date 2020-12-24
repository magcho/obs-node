#include "studio.h"
#include "utils.h"
#include "callback.h"
#include <napi.h>

#ifdef __linux__
// Need QT for linux to setup OpenGL properly.
#include <QApplication>
#include <QPushButton>
QApplication *qApplication;
#endif

std::string obsPath;
Studio *studio = nullptr;
Settings *settings = nullptr;
Napi::ThreadSafeFunction js_volmeter_thread;

struct VolmeterData {
    std::string sceneId;
    std::string sourceId;
    int channels;
    std::vector<float> magnitude;
    std::vector<float> peak;
    std::vector<float> input_peak;
};

Napi::Value setObsPath(const Napi::CallbackInfo &info) {
    obsPath = info[0].As<Napi::String>();
    return info.Env().Undefined();
}

Napi::Value startup(const Napi::CallbackInfo &info) {
#ifdef __linux__
    int argc = 0;
    char **argv = nullptr;
    qApplication = new QApplication(argc, argv);
#endif
    settings = new Settings(info[0].As<Napi::Object>());
    studio = new Studio(obsPath, settings);
    TRY_METHOD(studio->startup())
    return info.Env().Undefined();
}

Napi::Value shutdown(const Napi::CallbackInfo &info) {
    TRY_METHOD(studio->shutdown())
#ifdef __linux__
    delete qApplication;
#endif
    delete studio;
    delete settings;
    return info.Env().Undefined();
}

Napi::Value addScene(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    TRY_METHOD(studio->addScene(sceneId))
    return info.Env().Undefined();
}

Napi::Value addSource(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    std::string sourceId = info[1].As<Napi::String>();
    SourceType sourceType = Source::getSourceType(info[2].As<Napi::String>());
    std::string sourceUrl = info[3].As<Napi::String>();
    TRY_METHOD(studio->addSource(sceneId, sourceId, sourceType, sourceUrl))
    return info.Env().Undefined();
}

Napi::Value updateSource(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    std::string sourceId = info[1].As<Napi::String>();
    std::string sourceUrl = info[2].As<Napi::String>();
    TRY_METHOD(studio->updateSource(sceneId, sourceId, sourceUrl))
    return info.Env().Undefined();
}

Napi::Value addDSK(const Napi::CallbackInfo &info) {
    std::string id = info[0].As<Napi::String>();
    std::string position = info[1].As<Napi::String>();
    std::string url = info[2].As<Napi::String>();
    int left = info[3].As<Napi::Number>();
    int top = info[4].As<Napi::Number>();
    int width = info[5].As<Napi::Number>();
    int height = info[6].As<Napi::Number>();
    TRY_METHOD(studio->addDSK(id, position, url, left, top, width, height))
    return info.Env().Undefined();
}

Napi::Value muteSource(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    std::string sourceId = info[1].As<Napi::String>();
    bool mute = info[2].As<Napi::Boolean>();
    TRY_METHOD(studio->muteSource(sceneId, sourceId, mute))
    return info.Env().Undefined();
}

Napi::Value restartSource(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    std::string sourceId = info[1].As<Napi::String>();
    TRY_METHOD(studio->restartSource(sceneId, sourceId))
    return info.Env().Undefined();
}

Napi::Value switchToScene(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    std::string transitionType = info[1].As<Napi::String>();
    int transitionMs = info[2].As<Napi::Number>();
    TRY_METHOD(studio->switchToScene(sceneId, transitionType, transitionMs))
    return info.Env().Undefined();
}

Napi::Array getScenes(const Napi::CallbackInfo &info) {
    auto scenes = studio->getScenes();
    auto result = Napi::Array::New(info.Env(), scenes.size());
    int i = 0;
    for (auto &scene : scenes) {
        result[(uint32_t) i++] = scene.second->getNapiScene(info.Env());
    }
    return result;
}

Napi::Value createDisplay(const Napi::CallbackInfo &info) {
    std::string displayName = info[0].As<Napi::String>();
    void *parentHandle = info[1].As<Napi::Buffer<void *>>().Data();
    int scaleFactor = info[2].As<Napi::Number>();
    std::string sourceId = info[3].As<Napi::String>();
    TRY_METHOD(studio->createDisplay(displayName, parentHandle, scaleFactor, sourceId))
    return info.Env().Undefined();
}

Napi::Value destroyDisplay(const Napi::CallbackInfo &info) {
    std::string displayName = info[0].As<Napi::String>();
    TRY_METHOD(studio->destroyDisplay(displayName))
    return info.Env().Undefined();
}

Napi::Value moveDisplay(const Napi::CallbackInfo &info) {
    std::string displayName = info[0].As<Napi::String>();
    int x = info[1].As<Napi::Number>();
    int y = info[2].As<Napi::Number>();
    int width = info[3].As<Napi::Number>();
    int height = info[4].As<Napi::Number>();
    TRY_METHOD(studio->moveDisplay(displayName, x, y, width, height))
    return info.Env().Undefined();
}

Napi::Value addVolmeterCallback(const Napi::CallbackInfo &info) {
    auto callback = info[0].As<Napi::Function>();
    js_volmeter_thread = Napi::ThreadSafeFunction::New(
            info.Env(),
            callback,
            "VolmeterThread",
            0,
            1
    );

    VolmeterCallback volmeterCallback = [](std::string &sceneId,
                                           std::string &sourceId,
                                           int channels,
                                           std::vector<float> &magnitude,
                                           std::vector<float> &peak,
                                           std::vector<float> &input_peak) {

        auto data = new VolmeterData {
            .sceneId = sceneId,
            .sourceId = sourceId,
            .channels = channels,
            .magnitude = magnitude,
            .peak = peak,
            .input_peak = input_peak,
        };

        auto callback = [](Napi::Env env, Napi::Function jsCallback, VolmeterData* data) {
            Napi::Array magnitude = Napi::Array::New(env);
            Napi::Array peak = Napi::Array::New(env);
            Napi::Array input_peak = Napi::Array::New(env);
            for (size_t i = 0; i < data->channels; i++) {
                magnitude.Set(i, Napi::Number::New(env, data->magnitude[i]));
            }
            for (size_t i = 0; i < data->channels; i++) {
                peak.Set(i, Napi::Number::New(env, data->peak[i]));
            }
            for (size_t i = 0; i < data->channels; i++) {
                input_peak.Set(i, Napi::Number::New(env, data->input_peak[i]));
            }
            jsCallback.Call({
                Napi::String::New(env, data->sceneId),
                Napi::String::New(env, data->sourceId),
                Napi::Number::New(env, data->channels),
                magnitude,
                peak,
                input_peak
            });
            delete data;
        };

        if (js_volmeter_thread) {
            js_volmeter_thread.BlockingCall(data, callback);
        }
    };
    TRY_METHOD(Callback::setVolmeterCallback(volmeterCallback))
    return info.Env().Undefined();
}

Napi::Value setAudioMixer(const Napi::CallbackInfo &info) {
    Napi::Object audioMixer = info[0].As<Napi::Object>();
    bool audioWithVideo = audioMixer.Get("audioWithVideo").As<Napi::Boolean>();
    Napi::Array mixers = audioMixer.Get("mixers").As<Napi::Array>();
    for(int i = 0; i < mixers.Length(); i++)
    {
        Napi::Value mixer = mixers[i];
        Napi::Object mixerObject = mixer.As<Napi::Object>();
        std::string sceneId = mixerObject.Get("sceneId").As<Napi::String>();
        std::string sourceId = mixerObject.Get("sourceId").As<Napi::String>();
        float volume = mixerObject.Get("volume").As<Napi::Number>();
        bool audioLock = mixerObject.Get("audioLock").As<Napi::Boolean>();
        TRY_METHOD(studio->setSourceVolume(sceneId, sourceId, volume))
        TRY_METHOD(studio->setSourceAudioLock(sceneId, sourceId, audioLock))
    }
    TRY_METHOD(studio->setAudioWithVideo(audioWithVideo))
    return info.Env().Undefined();
}

Napi::Object getAudioMixer(const Napi::CallbackInfo &info) {
    bool audioWithVideo = studio->getAudioWithVideo();
    auto mixers = Napi::Array::New(info.Env());
    int i = 0;
    for (auto &scene : studio->getScenes()) {
        for (auto &source : scene.second->getSources()) {
            mixers[(uint32_t) i++] = source.second->getMixer(info.Env());
        }
    }
    Napi::Object audioMixer = Napi::Object::New(info.Env());
    audioMixer.Set("audioWithVideo", audioWithVideo);
    audioMixer.Set("mixers", mixers);
    return audioMixer;
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set(Napi::String::New(env, "setObsPath"), Napi::Function::New(env, setObsPath));
    exports.Set(Napi::String::New(env, "startup"), Napi::Function::New(env, startup));
    exports.Set(Napi::String::New(env, "shutdown"), Napi::Function::New(env, shutdown));
    exports.Set(Napi::String::New(env, "addScene"), Napi::Function::New(env, addScene));
    exports.Set(Napi::String::New(env, "addSource"), Napi::Function::New(env, addSource));
    exports.Set(Napi::String::New(env, "updateSource"), Napi::Function::New(env, updateSource));
    exports.Set(Napi::String::New(env, "muteSource"), Napi::Function::New(env, muteSource));
    exports.Set(Napi::String::New(env, "restartSource"), Napi::Function::New(env, restartSource));
    exports.Set(Napi::String::New(env, "switchToScene"), Napi::Function::New(env, switchToScene));
    exports.Set(Napi::String::New(env, "getScenes"), Napi::Function::New(env, getScenes));
    exports.Set(Napi::String::New(env, "createDisplay"), Napi::Function::New(env, createDisplay));
    exports.Set(Napi::String::New(env, "destroyDisplay"), Napi::Function::New(env, destroyDisplay));
    exports.Set(Napi::String::New(env, "moveDisplay"), Napi::Function::New(env, moveDisplay));
    exports.Set(Napi::String::New(env, "addDSK"), Napi::Function::New(env, addDSK));
    exports.Set(Napi::String::New(env, "addVolmeterCallback"), Napi::Function::New(env, addVolmeterCallback));
    exports.Set(Napi::String::New(env, "getAudioMixer"), Napi::Function::New(env, getAudioMixer));
    exports.Set(Napi::String::New(env, "setAudioMixer"), Napi::Function::New(env, setAudioMixer));
    return exports;
}

NODE_API_MODULE(obs_node, Init)
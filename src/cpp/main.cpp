#include "studio.h"
#include "utils.h"
#include "callback.h"
#include "overlay.h"
#include <memory>
#include <condition_variable>
#include <napi.h>

#ifdef __linux__
// Need QT for linux to setup OpenGL properly.
#include <QApplication>
#include <QPushButton>
QApplication *qApplication;
#endif

Studio *studio = nullptr;
Settings *settings = nullptr;
Napi::ThreadSafeFunction volmeter_thread = nullptr;
Napi::ThreadSafeFunction cef_queue_task_thread = nullptr;

struct VolmeterData {
    std::string sceneId;
    std::string sourceId;
    int channels;
    std::vector<float> magnitude;
    std::vector<float> peak;
    std::vector<float> input_peak;
};

struct CefCallbackData {
    std::function<void()> task;
};

bool cef_queue_task_callback(std::function<void()> task) {
    auto callback = [](Napi::Env env, Napi::Function jsCallback, CefCallbackData* data) {
        data->task();
        delete data;
    };
    if (!cef_queue_task_thread) {
        blog(LOG_ERROR, "cef_queue_task_thread is null");
        return false;
    }
    auto *data = new CefCallbackData {
        .task = task
    };
    cef_queue_task_thread.NonBlockingCall(data, callback);
    return true;
}

Napi::Value setObsPath(const Napi::CallbackInfo &info) {
    std::string obsPath = info[0].As<Napi::String>();
    Studio::setObsPath(obsPath);
    return info.Env().Undefined();
}

Napi::Value setFontPath(const Napi::CallbackInfo &info) {
    std::string fontPath = info[0].As<Napi::String>();
    Studio::setFontPath(fontPath);
    return info.Env().Undefined();
}

Napi::Value startup(const Napi::CallbackInfo &info) {
#ifdef __linux__
    int argc = 0;
    char **argv = nullptr;
    qApplication = new QApplication(argc, argv);
#endif
    settings = new Settings(info[0].As<Napi::Object>());
    studio = new Studio(settings);

    // set cef callback
    cef_queue_task_thread = Napi::ThreadSafeFunction::New(
            info.Env(),
            Napi::Function::New(info.Env(), [](const Napi::CallbackInfo &info) {}),
            "cef_queue_task_thread",
            0,
            1
    );
    Studio::setCefQueueTaskCallback(cef_queue_task_callback);

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
    cef_queue_task_thread.Release();
    return info.Env().Undefined();
}

Napi::Value addScene(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    TRY_METHOD(studio->addScene(sceneId))
    return info.Env().Undefined();
}

Napi::Value removeScene(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    TRY_METHOD(studio->removeScene(sceneId))
    return info.Env().Undefined();
}

Napi::Value addSource(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    std::string sourceId = info[1].As<Napi::String>();
    auto settings = info[2].As<Napi::Object>();
    TRY_METHOD(studio->addSource(sceneId, sourceId, settings))
    return info.Env().Undefined();
}

Napi::Value updateSource(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    std::string sourceId = info[1].As<Napi::String>();
    auto settings = info[2].As<Napi::Object>();

    Source *source;
    TRY_METHOD(source = studio->findSource(sceneId, sourceId))
    TRY_METHOD(source->update(settings))
    return info.Env().Undefined();
}

Napi::Object getSource(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    std::string sourceId = info[1].As<Napi::String>();

    Source *source;
    TRY_METHOD(source = studio->findSource(sceneId, sourceId))
    return source->toNapiObject(info.Env());
}

Napi::Value restartSource(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    std::string sourceId = info[1].As<Napi::String>();
    TRY_METHOD(studio->findSource(sceneId, sourceId)->restart())
    return info.Env().Undefined();
}

Napi::Value switchToScene(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    std::string transitionType = info[1].As<Napi::String>();
    int transitionMs = info[2].As<Napi::Number>();
    uint64_t timestamp = info[3].IsUndefined() ? 0 : std::stoull((std::string)info[3].As<Napi::String>());
    TRY_METHOD(studio->switchToScene(sceneId, transitionType, transitionMs, timestamp))
    return info.Env().Undefined();
}

Napi::Value addOutput(const Napi::CallbackInfo &info) {
    std::string id = info[0].As<Napi::String>();
    auto settings = std::make_shared<OutputSettings>(info[1].As<Napi::Object>());
    TRY_METHOD(studio->addOutput(id, settings))
    return info.Env().Undefined();
}

Napi::Value updateOutput(const Napi::CallbackInfo &info) {
    std::string id = info[0].As<Napi::String>();
    auto settings =  std::make_shared<OutputSettings>(info[1].As<Napi::Object>());
    TRY_METHOD(studio->updateOutput(id, settings))
    return info.Env().Undefined();
}

Napi::Value removeOutput(const Napi::CallbackInfo &info) {
    std::string id = info[0].As<Napi::String>();
    TRY_METHOD(studio->removeOutput(id));
    return info.Env().Undefined();
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
    volmeter_thread = Napi::ThreadSafeFunction::New(
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
                magnitude.Set((int)i, Napi::Number::New(env, data->magnitude[i]));
            }
            for (size_t i = 0; i < data->channels; i++) {
                peak.Set((int)i, Napi::Number::New(env, data->peak[i]));
            }
            for (size_t i = 0; i < data->channels; i++) {
                input_peak.Set((int)i, Napi::Number::New(env, data->input_peak[i]));
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

        if (volmeter_thread) {
            volmeter_thread.BlockingCall(data, callback);
        }
    };
    TRY_METHOD(Callback::setVolmeterCallback(volmeterCallback))
    return info.Env().Undefined();
}

Napi::Object getAudio(const Napi::CallbackInfo &info) {
    Napi::Object result;
    TRY_METHOD(result = studio->getAudio(info.Env()))
    return result;
}

Napi::Value updateAudio(const Napi::CallbackInfo &info) {
    TRY_METHOD(studio->updateAudio(info[0].As<Napi::Object>()))
    return info.Env().Undefined();
}

Napi::Value screenshot(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    std::string sourceId = info[1].As<Napi::String>();

    Source *source;
    TRY_METHOD(source = studio->findSource(sceneId, sourceId))

    auto deferred = Napi::Promise::Deferred::New(info.Env());
    auto tsfn = Napi::ThreadSafeFunction::New(
            info.Env(),
            Napi::Function::New(info.Env(), [](const Napi::CallbackInfo &info) {}),
            "Screenshot threadSafe function",
            0,
            1);

    source->screenshot([deferred, tsfn](const uint8_t *data, size_t size) {
        std::mutex mtx;
        std::unique_lock<std::mutex> lock(mtx);
        std::condition_variable cv;
        tsfn.BlockingCall([deferred, tsfn, &cv, data, size](Napi::Env env, Napi::Function jsCallback) {
            deferred.Resolve(Napi::Buffer<uint8_t>::Copy(env, data, size));
            (const_cast<Napi::ThreadSafeFunction&>(tsfn)).Release();
            cv.notify_one();
        });
        cv.wait(lock);
    });

    return deferred.Promise();
}

Napi::Value addOverlay(const Napi::CallbackInfo &info) {
    auto overlay = Overlay::create(info[0].As<Napi::Object>());
    TRY_METHOD(studio->addOverlay(overlay))
    return info.Env().Undefined();
}

Napi::Value removeOverlay(const Napi::CallbackInfo &info) {
    std::string overlayId = info[0].As<Napi::String>();
    TRY_METHOD(studio->removeOverlay(overlayId))
    return info.Env().Undefined();
}

Napi::Value upOverlay(const Napi::CallbackInfo &info) {
    std::string overlayId = info[0].As<Napi::String>();
    TRY_METHOD(studio->upOverlay(overlayId))
    return info.Env().Undefined();
}

Napi::Value downOverlay(const Napi::CallbackInfo &info) {
    std::string overlayId = info[0].As<Napi::String>();
    TRY_METHOD(studio->downOverlay(overlayId))
    return info.Env().Undefined();
}

Napi::Value getOverlays(const Napi::CallbackInfo &info) {
    auto overlays = studio->getOverlays();
    Napi::Array result = Napi::Array::New(info.Env(), overlays.size());
    int index = 0;
    for (const auto& overlay : overlays) {
        result.Set(index++, overlay.second->toNapiObject(info.Env()));
    }
    return result;
}

Napi::Value getSourceServerTimestamp(const Napi::CallbackInfo &info) {
    std::string sceneId = info[0].As<Napi::String>();
    std::string sourceId = info[1].As<Napi::String>();
    Source *source;
    TRY_METHOD(source = studio->findSource(sceneId, sourceId))
    return Napi::String::New(info.Env(), std::to_string(source->getServerTimestamp()));
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set(Napi::String::New(env, "setObsPath"), Napi::Function::New(env, setObsPath));
    exports.Set(Napi::String::New(env, "setFontPath"), Napi::Function::New(env, setFontPath));
    exports.Set(Napi::String::New(env, "startup"), Napi::Function::New(env, startup));
    exports.Set(Napi::String::New(env, "shutdown"), Napi::Function::New(env, shutdown));
    exports.Set(Napi::String::New(env, "addScene"), Napi::Function::New(env, addScene));
    exports.Set(Napi::String::New(env, "removeScene"), Napi::Function::New(env, removeScene));
    exports.Set(Napi::String::New(env, "addSource"), Napi::Function::New(env, addSource));
    exports.Set(Napi::String::New(env, "getSource"), Napi::Function::New(env, getSource));
    exports.Set(Napi::String::New(env, "getSourceServerTimestamp"), Napi::Function::New(env, getSourceServerTimestamp));
    exports.Set(Napi::String::New(env, "updateSource"), Napi::Function::New(env, updateSource));
    exports.Set(Napi::String::New(env, "restartSource"), Napi::Function::New(env, restartSource));
    exports.Set(Napi::String::New(env, "switchToScene"), Napi::Function::New(env, switchToScene));
    exports.Set(Napi::String::New(env, "addOutput"), Napi::Function::New(env, addOutput));
    exports.Set(Napi::String::New(env, "updateOutput"), Napi::Function::New(env, updateOutput));
    exports.Set(Napi::String::New(env, "removeOutput"), Napi::Function::New(env, removeOutput));
    exports.Set(Napi::String::New(env, "createDisplay"), Napi::Function::New(env, createDisplay));
    exports.Set(Napi::String::New(env, "destroyDisplay"), Napi::Function::New(env, destroyDisplay));
    exports.Set(Napi::String::New(env, "moveDisplay"), Napi::Function::New(env, moveDisplay));
    exports.Set(Napi::String::New(env, "addVolmeterCallback"), Napi::Function::New(env, addVolmeterCallback));
    exports.Set(Napi::String::New(env, "getAudio"), Napi::Function::New(env, getAudio));
    exports.Set(Napi::String::New(env, "updateAudio"), Napi::Function::New(env, updateAudio));
    exports.Set(Napi::String::New(env, "screenshot"), Napi::Function::New(env, screenshot));
    exports.Set(Napi::String::New(env, "addOverlay"), Napi::Function::New(env, addOverlay));
    exports.Set(Napi::String::New(env, "removeOverlay"), Napi::Function::New(env, removeOverlay));
    exports.Set(Napi::String::New(env, "upOverlay"), Napi::Function::New(env, upOverlay));
    exports.Set(Napi::String::New(env, "downOverlay"), Napi::Function::New(env, downOverlay));
    exports.Set(Napi::String::New(env, "getOverlays"), Napi::Function::New(env, getOverlays));
    return exports;
}

NODE_API_MODULE(obs_node, Init)
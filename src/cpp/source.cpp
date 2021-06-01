#include "source.h"
#include <utility>
#include "callback.h"
#include "utils.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
extern "C" {
#include "stb/stb_image_write.h"
}

struct ScreenshotContext {
    Source *source;
    std::function<void(uint8_t *, int)> callback;
};

SourceType Source::getSourceType(const std::string &sourceType) {
    if (sourceType == "live") {
        return SOURCE_TYPE_LIVE;
    } else if (sourceType == "media") {
        return SOURCE_TYPE_MEDIA;
    } else {
        throw std::invalid_argument("Invalid sourceType: " + sourceType);
    }
}

std::string Source::getSourceTypeString(SourceType sourceType) {
    switch (sourceType) {
        case SOURCE_TYPE_LIVE:
            return "live";
        case SOURCE_TYPE_MEDIA:
            return "media";
        default:
            throw std::invalid_argument("Invalid sourceType: " + std::to_string(sourceType));
    }
}

static void write_png_callback(void *context, void *data, int size) {
    auto c = (ScreenshotContext *) context;
    c->callback((uint8_t *) data, (size_t) size);
}

void Source::volmeter_callback(void *param, const float *magnitude, const float *peak, const float *input_peak) {
    auto source = static_cast<Source *>(param);
    auto callback = Callback::getVolmeterCallback();
    if (callback && source->obs_volmeter) {
        int channels = obs_volmeter_get_nr_channels(source->obs_volmeter);
        std::vector<float> vecMagnitude;
        std::vector<float> vecPeak;
        std::vector<float> vecInputPeak;
        for (size_t ch = 0; ch < channels; ch++) {
            vecMagnitude.push_back(magnitude[ch]);
            vecPeak.push_back(peak[ch]);
            vecInputPeak.push_back(input_peak[ch]);
        }
        callback(source->sceneId, source->id, channels, vecMagnitude, vecPeak, vecInputPeak);
    }
}

void Source::screenshot_callback(void *param) {
    auto p = (ScreenshotContext *) param;
    int width = (int) obs_source_get_width(p->source->obs_source);
    int height = (int) obs_source_get_height(p->source->obs_source);
    if (width == 0 || height == 0) {
        return;
    }

    obs_enter_graphics();
    gs_texrender_t *texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_stagesurf_t *stagesurf = gs_stagesurface_create(width, height, GS_RGBA);
    try {
        if (gs_texrender_begin(texrender, width, height)) {
            vec4 background = {};
            vec4_zero(&background);
            gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
            gs_ortho(0.0f, (float) width, 0.0f, (float) height, -100.0f, 100.0f);
            gs_blend_state_push();
            gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
            obs_source_video_render(p->source->obs_source);
            gs_blend_state_pop();
            gs_texrender_end(texrender);
            gs_stage_texture(stagesurf, gs_texrender_get_texture(texrender));
            uint8_t *video_data = nullptr;
            uint32_t video_linesize = 0;
            if (gs_stagesurface_map(stagesurf, &video_data, &video_linesize)) {
                stbi_write_png_to_func(write_png_callback, p, width, height, 4, video_data, video_linesize);
                gs_stagesurface_unmap(stagesurf);
            }
        }
    } catch (...) {
        blog(LOG_ERROR, "Screenshot error");
    }
    gs_texrender_destroy(texrender);
    gs_stagesurface_destroy(stagesurf);
    obs_leave_graphics();
}

void Source::source_activate_callback(void *param, calldata_t *data) {
    UNUSED_PARAMETER(data);
    auto source = (Source *) param;
    if (source->type == SOURCE_TYPE_MEDIA && source->playOnActive) {
        source->play();
    }
}

void Source::source_deactivate_callback(void *param, calldata_t *data) {
    UNUSED_PARAMETER(data);
    auto source = (Source *) param;
    if (source->type == SOURCE_TYPE_MEDIA && source->playOnActive) {
        source->stopToBeginning();
    }
}

Source::Source(std::string &id, std::string &sceneId, obs_scene_t *obs_scene, Settings *studioSettings,
               const Napi::Object &settings) :
        id(id),
        sceneId(sceneId),
        output(nullptr),
        obs_scene(obs_scene),
        obs_source(nullptr),
        obs_scene_item(nullptr),
        obs_volmeter(nullptr),
        obs_fader(nullptr),
        transcoder(nullptr) {
    name = NapiUtil::getString(settings, "name");
    type = Source::getSourceType(NapiUtil::getString(settings, "type"));
    url = NapiUtil::getString(settings, "url");
    hardwareDecoder = NapiUtil::getBooleanOptional(settings, "hardwareDecoder").value_or(false);
    playOnActive = NapiUtil::getBooleanOptional(settings, "playOnActive").value_or(false);
    asyncUnbuffered = NapiUtil::getBooleanOptional(settings, "asyncUnbuffered").value_or(false);
    bufferingMb = NapiUtil::getIntOptional(settings, "bufferingMb").value_or(2);
    reconnectDelaySec = NapiUtil::getIntOptional(settings, "reconnectDelaySec").value_or(10);
    volume = NapiUtil::getIntOptional(settings, "volume").value_or(0);
    audioLock = NapiUtil::getBooleanOptional(settings, "audioLock").value_or(false);
    monitor = NapiUtil::getBooleanOptional(settings, "monitor").value_or(false);
    showTimestamp = studioSettings->showTimestamp;
    if (!settings.Get("output").IsUndefined() && !settings.Get("output").IsNull()) {
        output = std::make_shared<OutputSettings>(settings.Get("output").As<Napi::Object>());
    }
    // start source
    start();
}

Source::~Source() {
    stop();
}

void Source::update(const Napi::Object &settings) {
    bool restart = false;
    bool restartOutput = false;
    if (!NapiUtil::isUndefined(settings, "name")) {
        name = NapiUtil::getString(settings, "name");
    }
    if (!NapiUtil::isUndefined(settings, "type")) {
        auto value = Source::getSourceType(NapiUtil::getString(settings, "type"));
        if (type != value) {
            type = value;
            restart = true;
        }
    }
    if (!NapiUtil::isUndefined(settings, "url")) {
        auto value = NapiUtil::getString(settings, "url");
        if (url != value) {
            url = value;
            restart = true;
        }
    }
    if (!NapiUtil::isUndefined(settings, "hardwareDecoder")) {
        auto value = NapiUtil::getBoolean(settings, "hardwareDecoder");
        if (hardwareDecoder != value) {
            hardwareDecoder = value;
            restart = true;
        }
    }
    if (!NapiUtil::isUndefined(settings, "playOnActive")) {
        auto value = NapiUtil::getBoolean(settings, "playOnActive");
        if (playOnActive != value) {
            playOnActive = value;
            restart = true;
        }
    }
    if (!NapiUtil::isUndefined(settings, "asyncUnbuffered")) {
        auto value = NapiUtil::getBoolean(settings, "asyncUnbuffered");
        if (asyncUnbuffered != value) {
            asyncUnbuffered = value;
            restart = true;
        }
    }
    if (!NapiUtil::isUndefined(settings, "bufferingMb")) {
        auto value = NapiUtil::getInt(settings, "bufferingMb");
        if (bufferingMb != value) {
            bufferingMb = value;
            restart = true;
        }
    }
    if (!NapiUtil::isUndefined(settings, "reconnectDelaySec")) {
        auto value = NapiUtil::getInt(settings, "reconnectDelaySec");
        if (reconnectDelaySec != value) {
            reconnectDelaySec = value;
            restart = true;
        }
    }
    if (!NapiUtil::isUndefined(settings, "volume")) {
        auto value = NapiUtil::getInt(settings, "volume");
        if (volume != value) {
            volume = value;
            setVolume(volume);
        }
    }
    if (!NapiUtil::isUndefined(settings, "audioLock")) {
        auto value = NapiUtil::getBoolean(settings, "audioLock");
        if (audioLock != value) {
            audioLock = value;
            setAudioLock(audioLock);
        }
    }
    if (!NapiUtil::isUndefined(settings, "monitor")) {
        auto value = NapiUtil::getBoolean(settings, "monitor");
        if (monitor != value) {
            monitor = value;
            setMonitor(monitor);
        }
    }
    if (!NapiUtil::isUndefined(settings, "output")) {
        if (settings.Get("output").IsNull()) {
            if (output) {
                stopOutput();
                output = nullptr;
            }
        } else {
            auto value = std::make_shared<OutputSettings>(settings.Get("output").As<Napi::Object>());
            if (!output || !output->equals(value)) {
                output = value;
                restartOutput = true;
            }
        }
    }
    if (restart) {
        stop();
        start();
    } else if (restartOutput) {
        stopOutput();
        startOutput();
    }
}

void Source::screenshot(std::function<void(uint8_t *, int)> callback) {
    auto context = new ScreenshotContext{
            .source = this,
            .callback = std::move(callback),
    };
    obs_queue_task(OBS_TASK_GRAPHICS, screenshot_callback, context, false);
}

Napi::Object Source::toNapiObject(Napi::Env env) {
    auto result = Napi::Object::New(env);
    result.Set("id", id);
    result.Set("sceneId", sceneId);
    result.Set("name", name);
    result.Set("type", Source::getSourceTypeString(type));
    result.Set("url", url);
    result.Set("playOnActive", playOnActive);
    result.Set("hardwareDecoder", hardwareDecoder);
    result.Set("asyncUnbuffered", asyncUnbuffered);
    result.Set("bufferingMb", bufferingMb);
    result.Set("reconnectDelaySec", reconnectDelaySec);
    result.Set("volume", volume);
    result.Set("monitor", monitor);
    result.Set("audioLock", audioLock);
    return result;
}

uint64_t Source::getTimestamp() {
    return obs_source_get_frame_timestamp(obs_source);
}

uint64_t Source::getServerTimestamp() {
    uint64_t server_timestamp = obs_source_get_server_timestamp(obs_source);
    uint64_t external_timestamp = obs_source_get_external_timestamp(obs_source);
    return server_timestamp > 0 ? server_timestamp : external_timestamp;
}

void Source::start() {
    obs_data_t *obs_data = obs_data_create();
    obs_data_set_bool(obs_data, "is_local_file", type == SOURCE_TYPE_MEDIA);
    obs_data_set_string(obs_data, type == SOURCE_TYPE_MEDIA ? "local_file" : "input", url.c_str());
    obs_data_set_bool(obs_data, "looping", type == SOURCE_TYPE_MEDIA);
    obs_data_set_bool(obs_data, "hw_decode", hardwareDecoder);
    obs_data_set_bool(obs_data, "close_when_inactive", false);  // make source always read
    obs_data_set_bool(obs_data, "restart_on_activate", false);  // make source always read
    obs_data_set_bool(obs_data, "clear_on_media_end", false);
    obs_data_set_int(obs_data, "buffering_mb", bufferingMb);
    obs_data_set_int(obs_data, "reconnect_delay_sec", reconnectDelaySec);
    obs_source = obs_source_create("ffmpeg_source", this->id.c_str(), obs_data, nullptr);
    obs_data_release(obs_data);

    if (!obs_source) {
        throw std::runtime_error("Failed to create obs_source");
    }

    obs_source_set_async_unbuffered(obs_source, asyncUnbuffered);

    if (showTimestamp) {
        obs_source_show_timestamp(obs_source, true);
    }

    // Add the source to the scene
    obs_scene_item = obs_scene_add(obs_scene, obs_source);
    if (!obs_scene_item) {
        throw std::runtime_error("Failed to add scene item.");
    }

    // Scale source to output size by setting bounds
    obs_video_info ovi = {};
    obs_get_video_info(&ovi);
    struct vec2 bounds = {};
    bounds.x = (float) ovi.base_width;
    bounds.y = (float) ovi.base_height;
    uint32_t align = OBS_ALIGN_CENTER;
    obs_sceneitem_set_bounds_type(obs_scene_item, OBS_BOUNDS_SCALE_INNER);
    obs_sceneitem_set_bounds(obs_scene_item, &bounds);
    obs_sceneitem_set_bounds_alignment(obs_scene_item, align);

    // Volmeter
    obs_volmeter = obs_volmeter_create(OBS_FADER_IEC);
    if (!obs_volmeter) {
        blog(LOG_ERROR, "Failed to create obs volmeter");
    }
    obs_volmeter_attach_source(obs_volmeter, obs_source);
    obs_volmeter_add_callback(obs_volmeter, volmeter_callback, this);

    // Fader
    obs_fader = obs_fader_create(OBS_FADER_IEC);
    if (!obs_fader) {
        blog(LOG_ERROR, "Failed to create obs fader");
    }
    obs_fader_attach_source(obs_fader, obs_source);

    // pause to beginning if it's start at active
    if (type == SOURCE_TYPE_MEDIA && playOnActive) {
        stopToBeginning();
    }

    // audio
    setVolume(volume);
    setAudioLock(audioLock);
    setMonitor(monitor);

    signal_handler_t *handler = obs_source_get_signal_handler(obs_source);
    signal_handler_connect(handler, "activate", source_activate_callback, this);
    signal_handler_connect(handler, "deactivate", source_deactivate_callback, this);

    // output
    startOutput();
}

void Source::stop() {
    stopOutput();

    signal_handler_t *handler = obs_source_get_signal_handler(obs_source);
    signal_handler_disconnect(handler, "activate", source_activate_callback, this);
    signal_handler_disconnect(handler, "deactivate", source_deactivate_callback, this);

    if (obs_volmeter) {
        obs_volmeter_remove_callback(obs_volmeter, volmeter_callback, this);
        obs_volmeter_detach_source(obs_volmeter);
        obs_volmeter_destroy(obs_volmeter);
    }
    if (obs_fader) {
        obs_fader_detach_source(obs_fader);
        obs_fader_destroy(obs_fader);
    }

    // obs_sceneitem_remove will call obs_sceneitem_release internally,
    // so it's no need to call obs_sceneitem_release.
    obs_sceneitem_remove(obs_scene_item);
    obs_source_remove(obs_source);
    obs_source_release(obs_source);
    obs_source = nullptr;
    obs_scene_item = nullptr;
}

void Source::startOutput() {
    if (output) {
        transcoder = new SourceTranscoder();
        transcoder->start(this);
    }
}

void Source::stopOutput() {
    if (transcoder) {
        transcoder->stop();
        delete transcoder;
        transcoder = nullptr;
    }
}

void Source::restart() {
    stop();
    start();
}

void Source::play() {
    if (obs_source) {
        obs_source_media_play_pause(obs_source, false);
    }
}

void Source::stopToBeginning() {
    if (obs_source) {
        obs_source_media_play_pause(obs_source, true);
        obs_source_media_set_time(obs_source, 0);
    }
}

void Source::setVolume(int volume) {
    // Set volume in dB
    obs_fader_set_db(obs_fader, (float) volume);
}

void Source::setAudioLock(bool audioLock) {
    if (obs_source) {
        obs_source_set_audio_lock(obs_source, audioLock);
    }
}

void Source::setMonitor(bool monitor) {
    if (obs_source) {
        if (monitor) {
            obs_source_set_monitoring_type(obs_source, OBS_MONITORING_TYPE_MONITOR_ONLY);
        } else {
            obs_source_set_monitoring_type(obs_source, OBS_MONITORING_TYPE_NONE);
        }
    }
}
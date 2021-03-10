#include "source.h"

#include <utility>
#include "callback.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
extern "C" {
#include "stb/stb_image_write.h"
}


struct ScreenshotContext {
    Source *source;
    std::function<void(uint8_t*, int)> callback;
};

static void write_png_callback(void *context, void *data, int size) {
    auto c = (ScreenshotContext *) context;
    c->callback((uint8_t*) data, (size_t) size);
}

SourceType Source::getSourceType(const std::string &sourceType) {
    if (sourceType == "Image") {
        return Image;
    } else if (sourceType == "MediaSource") {
        return MediaSource;
    } else {
        throw std::invalid_argument("Invalid sourceType: " + sourceType);
    }
}

std::string Source::getSourceTypeString(SourceType sourceType) {
    switch (sourceType) {
        case Image:
            return "Image";
        case MediaSource:
            return "MediaSource";
        default:
            throw std::invalid_argument("Invalid sourceType: " + std::to_string(sourceType));
    }
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
    auto p = (ScreenshotContext *)param;
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
    } catch(...) {
        blog(LOG_ERROR, "Screenshot error");
    }
    gs_texrender_destroy(texrender);
    gs_stagesurface_destroy(stagesurf);
    obs_leave_graphics();
}

void Source::source_activate_callback(void *param, calldata_t *data) {
    UNUSED_PARAMETER(data);
    auto source = (Source *) param;
    if (source->settings->isFile && source->settings->startOnActive) {
        source->play();
    }
}

void Source::source_deactivate_callback(void *param, calldata_t *data) {
    UNUSED_PARAMETER(data);
    auto source = (Source *) param;
    if (source->settings->isFile && source->settings->startOnActive) {
        source->pauseToBeginning();
    }
}

Source::Source(std::string &id, std::string &sceneId, obs_scene_t *obs_scene,
               std::shared_ptr<SourceSettings> &settings) :
        id(id),
        sceneId(sceneId),
        obs_scene(obs_scene),
        settings(settings),
        type(Source::getSourceType(settings->type)),
        url(settings->url),
        obs_source(nullptr),
        obs_scene_item(nullptr),
        obs_volmeter(nullptr),
        obs_fader(nullptr),
        transcoder(nullptr) {
}

void Source::start() {
    obs_data_t *obs_data = obs_data_create();
    if (type == Image) {
        obs_data_set_string(obs_data, "file", url.c_str());
        obs_data_set_bool(obs_data, "unload", false);
        obs_source = obs_source_create("image_source", "obs_image_source", obs_data, nullptr);
    } else if (type == MediaSource) {
        obs_data_set_bool(obs_data, "is_local_file", settings->isFile);
        obs_data_set_string(obs_data, settings->isFile ? "local_file" : "input", url.c_str());
        obs_data_set_bool(obs_data, "looping", settings->isFile);
        obs_data_set_bool(obs_data, "hw_decode", settings->hardwareDecoder);
        obs_data_set_bool(obs_data, "close_when_inactive", false);  // make source always read
        obs_data_set_bool(obs_data, "restart_on_activate", false);  // make source always read
        obs_data_set_bool(obs_data, "clear_on_media_end", false);
        obs_data_set_int(obs_data, "buffering_mb",settings->bufferSize);
        obs_source = obs_source_create("ffmpeg_source", this->id.c_str(), obs_data, nullptr);
        obs_source_set_async_unbuffered(obs_source, !settings->enableBuffer);
    }

    obs_data_release(obs_data);

    if (!obs_source) {
        throw std::runtime_error("Failed to create obs_source");
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

    // source output
    if (settings->output) {
        transcoder = new SourceTranscoder();
        transcoder->start(this);
    }

    // pause to beginning if it's start at active
    if (settings->isFile && settings->startOnActive) {
        pauseToBeginning();
    }

    signal_handler_t *handler = obs_source_get_signal_handler(obs_source);
    signal_handler_connect(handler, "activate", source_activate_callback, this);
    signal_handler_connect(handler, "deactivate", source_deactivate_callback, this);
}

void Source::stop() {
    signal_handler_t *handler = obs_source_get_signal_handler(obs_source);
    signal_handler_disconnect(handler, "activate", source_activate_callback, this);
    signal_handler_disconnect(handler, "deactivate", source_deactivate_callback, this);

    if (transcoder) {
        transcoder->stop();
        delete transcoder;
        transcoder = nullptr;
    }

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

void Source::restart() {
    stop();
    start();
}

std::string Source::getId() {
    return id;
}

std::string Source::getSceneId() {
    return sceneId;
}

SourceType Source::getType() {
    return type;
}

void Source::setUrl(const std::string &sourceUrl) {
    url = sourceUrl;
    restart();
}

std::string Source::getUrl() {
    return url;
}

void Source::setVolume(float volume) {
    // Set volume in dB
    obs_fader_set_db(obs_fader, volume);
}

float Source::getVolume() {
    return obs_fader ? obs_fader_get_db(obs_fader) : 0;
}

void Source::screenshot(std::function<void(uint8_t*, int)> callback) {
    auto p = new ScreenshotContext {
      .source = this,
      .callback = std::move(callback),
    };
    obs_queue_task(OBS_TASK_GRAPHICS, screenshot_callback, p, false);
}

void Source::setAudioLock(bool audioLock) {
    if (obs_source) {
        obs_source_set_audio_lock(obs_source, audioLock);
    }
}

bool Source::getAudioLock() {
    return obs_source != nullptr && obs_source_get_audio_lock(obs_source);
}

void Source::setAudioMonitor(bool audioMonitor) {
    if (obs_source) {
        if (audioMonitor) {
            obs_source_set_monitoring_type(obs_source, OBS_MONITORING_TYPE_MONITOR_ONLY);
        } else {
            obs_source_set_monitoring_type(obs_source, OBS_MONITORING_TYPE_NONE);
        }
    }
}

bool Source::getAudioMonitor() {
    return obs_source && obs_source_get_monitoring_type(obs_source) == OBS_MONITORING_TYPE_MONITOR_ONLY;
}

void Source::play() {
    if (obs_source) {
        obs_source_media_play_pause(obs_source, false);
    }
}

void Source::pauseToBeginning() {
    if (obs_source) {
        obs_source_media_play_pause(obs_source, true);
        obs_source_media_set_time(obs_source, 0);
    }
}

#include "source.h"
#include "callback.h"

SourceType Source::getSourceType(const std::string &sourceType) {
    if (sourceType == "Image") {
        return Image;
    } else if (sourceType == "MediaSource") {
        return MediaSource;
    } else {
        throw std::invalid_argument("Invalid sourceType: " + sourceType);
    }
}

void Source::obs_volmeter_callback(void *param, const float *magnitude, const float *peak, const float *input_peak) {
    auto source = static_cast<Source *>(param);
    auto callback = Callback::getVolmeterCallback();
    if (callback && source->obs_volmeter) {
        int channels = obs_volmeter_get_nr_channels(source -> obs_volmeter);
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

Source::Source(std::string &id, SourceType type, std::string &url, std::string &sceneId, int sceneIndex, obs_scene_t *obs_scene,
               Settings *settings)
        : id(id),
          type(type),
          url(url),
          sceneId(sceneId),
          sceneIndex(sceneIndex),
          obs_scene(obs_scene),
          settings(settings),
          obs_source(nullptr),
          obs_scene_item(nullptr),
          obs_volmeter(nullptr),
          started(false) {
}

void Source::start() {
    obs_data_t *obs_data = obs_data_create();
    if (type == Image) {
        obs_data_set_string(obs_data, "file", url.c_str());
        obs_data_set_bool(obs_data, "unload", false);
        obs_source = obs_source_create("image_source", "obs_image_source", obs_data, nullptr);
    } else if (type == MediaSource) {
        obs_data_set_string(obs_data, "input", url.c_str());
        obs_data_set_bool(obs_data, "is_local_file", false);
        obs_data_set_bool(obs_data, "looping", false);
        if (settings->videoDecoder) {
            obs_data_set_bool(obs_data, "hw_decode", settings->videoDecoder->hardwareEnable);
        }
        obs_data_set_bool(obs_data, "close_when_inactive", false);  // make source always read
        obs_data_set_bool(obs_data, "restart_on_activate", false);  // make source always read
        obs_source = obs_source_create("ffmpeg_source", this->id.c_str(), obs_data, nullptr);
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
    if (settings->video && settings->video->baseWidth > 0 && settings->video->baseHeight > 0) {
        struct vec2 bounds = {};
        bounds.x = (float) settings->video->baseWidth;
        bounds.y = (float) settings->video->baseHeight;
        uint32_t align = OBS_ALIGN_TOP + OBS_ALIGN_LEFT;
        obs_sceneitem_set_bounds_type(obs_scene_item, OBS_BOUNDS_SCALE_INNER);
        obs_sceneitem_set_bounds(obs_scene_item, &bounds);
        obs_sceneitem_set_bounds_alignment(obs_scene_item, align);
        started = true;
    }

    // Volmeter
    obs_volmeter = obs_volmeter_create(OBS_FADER_IEC);
    if (!obs_volmeter) {
        blog(LOG_ERROR, "Failed to create obs volmeter");
    }
    obs_volmeter_attach_source(obs_volmeter, obs_source);
    obs_volmeter_add_callback(obs_volmeter, obs_volmeter_callback, this);

    // Fader
    obs_fader = obs_fader_create(OBS_FADER_IEC);
    if (!obs_fader) {
        blog(LOG_ERROR, "Failed to create obs fader");
    }
    obs_fader_attach_source(obs_fader, obs_source);
}

void Source::stop() {
    if (obs_volmeter) {
        obs_volmeter_remove_callback(obs_volmeter, obs_volmeter_callback, this);
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
    started = false;
}

void Source::updateUrl(std::string &sourceUrl) {
    stop();
    url = sourceUrl;
    start();
}

void Source::mute(bool mute) {
    if (obs_source) {
        obs_source_set_muted(obs_source, mute);
        if (!mute && obs_source_get_monitoring_type(obs_source) == OBS_MONITORING_TYPE_NONE) {
            obs_source_set_monitoring_type(obs_source, OBS_MONITORING_TYPE_MONITOR_ONLY);
        }
    }
}

float Source::getVolume() {
    return obs_fader ? obs_fader_get_db(obs_fader) : 0;
}

void Source::setVolume(float volume) {
    // Set volume in dB
    obs_fader_set_db(obs_fader, volume);
}

bool Source::getAudioLock() {
    return obs_source ? obs_source_get_audio_lock(obs_source) : false;
}

void Source::setAudioLock(bool audioLock) {
    if (obs_source) {
        obs_source_set_audio_lock(obs_source, audioLock);
    }
}

Napi::Object Source::getSource(const Napi::Env &env) {
    auto source = Napi::Object::New(env);
    source.Set("id", id);
    source.Set("type", getSourceTypeString(type));
    source.Set("url", url);
    return source;
}

Napi::Object Source::getMixer(const Napi::Env &env) {
    auto source = Napi::Object::New(env);
    source.Set("sourceId", id);
    source.Set("sceneId", sceneId);
    source.Set("volume", getVolume());
    source.Set("audioLock", getAudioLock());
    return source;
}
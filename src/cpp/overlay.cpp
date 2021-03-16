#include "overlay.h"
#include "utils.h"
#include "studio.h"

#define OBS_OVERLAY_START_CHANNEL 10

unsigned long hex_to_number(const std::string &hex) {
    char *p;
    unsigned long n = strtoul(hex.c_str(), &p, 16);
    if (*p != 0) {
        blog(LOG_INFO, "can't convert hex string to number: %s", hex.c_str());
        return -1L;
    } else {
        return n;
    }
}

Overlay *Overlay::create(Napi::Object object) {
    std::string t = getNapiString(object, "type");
    if (t == "cg") {
        return new CG(object);
    }
    return nullptr;
}

Overlay::Overlay(Napi::Object object) :
        index(-1) {
    id = getNapiString(object, "id");
    name = getNapiString(object, "name");
    auto t = getNapiString(object, "type");
    if (t == "cg") {
        type = OVERLAY_TYPE_CG;
    } else {
        type = OVERLAY_TYPE_UNKNOWN;
    }
}

Napi::Object Overlay::toNapiObject(Napi::Env env) {
    Napi::Object object = Napi::Object::New(env);
    object.Set("id", id);
    object.Set("name", name);
    object.Set("type", type == OVERLAY_TYPE_CG ? "cg" : "unknown");
    object.Set("status", index > -1 ? "up" : "down");
    return object;
}

CG::CG(Napi::Object object) : Overlay(object) {
    obs_video_info ovi = {};
    obs_get_video_info(&ovi);

    baseWidth = getNapiInt(object, "baseWidth");
    baseHeight = getNapiInt(object, "baseHeight");
    double scaleX = ovi.base_width * 1.0 / baseWidth;
    double scaleY = ovi.base_height * 1.0 / baseHeight;

    if (object.Get("items").IsUndefined()) {
        obs_scene = nullptr;
        return;
    }
    auto is = object.Get("items").As<Napi::Array>();
    for (uint32_t i = 0; i < is.Length(); ++i) {
        auto item = is.Get(i).As<Napi::Object>();
        std::string type = item.Get("type").As<Napi::String>();
        std::string itemId = id + "_item_" + std::to_string(i);
        if (type == "text") {
            items.push_back(new CGText(itemId, item, scaleX));
        } else if (type == "image") {
            items.push_back(new CGImage(itemId, item));
        }
    }

    // create overlay scene
    obs_scene = obs_scene_create_private(("overlay_" + id).c_str());
    for (auto &item : items) {
        // Add the source to the scene
        obs_scene_item *obs_scene_item = obs_scene_add(obs_scene, item->obs_source);
        if (!obs_scene_item) {
            throw std::runtime_error("Failed to add scene item for CG " + id);
        }
        // set position
        struct vec2 pos = {};
        pos.x = (float) (item->x * scaleX);
        pos.y = (float) (item->y * scaleY);
        obs_sceneitem_set_pos(obs_scene_item, &pos);
        // set size
        if (item->type == CG_ITEM_TYPE_IMAGE) {
            struct vec2 bounds = {};
            bounds.x = (float) (item->width * scaleX);
            bounds.y = (float) (item->height * scaleY);
            obs_sceneitem_set_bounds(obs_scene_item, &bounds);
            obs_sceneitem_set_bounds_type(obs_scene_item, OBS_BOUNDS_SCALE_INNER);
        }
        // set top most
        obs_sceneitem_set_order(obs_scene_item, OBS_ORDER_MOVE_TOP);
    }
}

CG::~CG() {
    for (auto item : items) {
        delete item;
    }
    obs_scene_release(obs_scene);
}

void CG::up(int index) {
    obs_set_output_source(OBS_OVERLAY_START_CHANNEL + index, obs_scene_get_source(obs_scene));
    this->index = index;
}

void CG::down() {
    obs_set_output_source(OBS_OVERLAY_START_CHANNEL + index, nullptr);
    this->index = -1;
}

Napi::Object CG::toNapiObject(Napi::Env env) {
    Napi::Object object = Overlay::toNapiObject(env);
    object.Set("baseWidth", baseWidth);
    object.Set("baseHeight", baseHeight);
    Napi::Array is = Napi::Array::New(env, items.size());
    for (uint32_t i = 0; i < items.size(); ++i) {
        is.Set(i, items[i]->toNapiObject(env));
    }
    object.Set("items", is);
    return object;
}

CGItem::CGItem(Napi::Object object) {
    auto t = getNapiString(object, "type");
    if (t == "image") {
        type = CG_ITEM_TYPE_IMAGE;
    } else if (t == "text") {
        type = CG_ITEM_TYPE_TEXT;
    } else {
        type = CG_ITEM_TYPE_UNKNOWN;
    }
    x = getNapiInt(object, "x");
    y = getNapiInt(object, "y");
    width = getNapiInt(object, "width");
    height = getNapiInt(object, "height");
    obs_source = nullptr;
}

CGItem::~CGItem() {
    obs_source_release(obs_source);
}

Napi::Object CGItem::toNapiObject(Napi::Env env) {
    Napi::Object object = Napi::Object::New(env);
    object.Set("type", type == CG_ITEM_TYPE_TEXT ? "text" : type == CG_ITEM_TYPE_IMAGE ? "image" : "unknown");
    object.Set("x", x);
    object.Set("y", y);
    object.Set("width", width);
    object.Set("height", height);
    return object;
}

CGText::CGText(const std::string &itemId, Napi::Object object, double scaleX) : CGItem(object) {
    content = getNapiString(object, "content");
    fontSize = getNapiInt(object, "fontSize");
    fontFamily = getNapiString(object, "fontFamily");
    colorABGR = getNapiString(object, "colorABGR");
    obs_data_t *settings = obs_data_create();
    obs_data_t *font = obs_data_create();
    obs_data_set_string(font, "face", fontFamily.c_str());
    obs_data_set_int(font, "size", (int) (fontSize * scaleX));
    obs_data_set_obj(settings, "font", font);
    obs_data_set_int(settings, "color1", hex_to_number(colorABGR));
    obs_data_set_int(settings, "color2", hex_to_number(colorABGR));
    obs_data_set_string(settings, "text", content.c_str());
    obs_data_set_int(settings, "custom_width", (int) (width * scaleX));
    if (!Studio::getFontPath().empty()) {
        obs_data_set_string(settings, "custom_font_path", Studio::getFontPath().c_str());
    }
    obs_source = obs_source_create("text_ft2_source_v2", itemId.c_str(), settings, nullptr);
    obs_data_release(font);
    obs_data_release(settings);
    if (!obs_source) {
        throw std::runtime_error("Failed to create obs source for CG text.");
    }
}

Napi::Object CGText::toNapiObject(Napi::Env env) {
    Napi::Object object = CGItem::toNapiObject(env);
    object.Set("content", content);
    object.Set("fontSize", fontSize);
    object.Set("fontFamily", fontFamily);
    object.Set("colorABGR", colorABGR);
    return object;
}

CGImage::CGImage(const std::string &itemId, Napi::Object object) : CGItem(object) {
    url = getNapiString(object, "url");
    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "file", url.c_str());
    obs_data_set_bool(settings, "unload", false);
    obs_source = obs_source_create("image_source", itemId.c_str(), settings, nullptr);
    obs_data_release(settings);
    if (!obs_source) {
        throw std::runtime_error("Failed to create obs source for CG image.");
    }
}

Napi::Object CGImage::toNapiObject(Napi::Env env) {
    Napi::Object object = CGItem::toNapiObject(env);
    object.Set("url", url);
    return object;
}

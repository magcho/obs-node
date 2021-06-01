#pragma once
#include "settings.h"
#include <string>
#include <vector>
#include <napi.h>
#include <obs.h>

enum OverlayType {
    OVERLAY_TYPE_UNKNOWN,
    OVERLAY_TYPE_CG,
};

enum CGItemType {
    CG_ITEM_TYPE_UNKNOWN,
    CG_ITEM_TYPE_TEXT,
    CG_ITEM_TYPE_IMAGE,
};

class Overlay {

    friend class Studio;

public:
    static Overlay *create(Napi::Object object, Settings *settings);
    virtual ~Overlay() = default;

    virtual void up(int index) = 0;
    virtual void down() = 0;
    virtual Napi::Object toNapiObject(Napi::Env env);

    std::string id;
    std::string name;
    OverlayType type;
    int index;

protected:
    explicit Overlay(Napi::Object object);
};

class CGItem {

    friend class CG;

public:
    virtual ~CGItem();
    virtual Napi::Object toNapiObject(Napi::Env env);

    CGItemType type;
    int x;
    int y;
    int width;
    int height;

protected:
    explicit CGItem(Napi::Object object);
    obs_source_t *obs_source;
};

class CGImage : public CGItem {

public:
    explicit CGImage(const std::string &itemId, Napi::Object object);
    Napi::Object toNapiObject(Napi::Env env) override;

    std::string url;
};

class CGText : public CGItem {

public:
    explicit CGText(const std::string &itemId, Napi::Object object, double scaleX, Settings *settings);
    Napi::Object toNapiObject(Napi::Env env) override;

    std::string content;
    int fontSize;
    std::string fontFamily;
    std::string colorABGR;
};

class CG : public Overlay {

public:
    explicit CG(Napi::Object object, Settings *settings);
    ~CG() override;

    Napi::Object toNapiObject(Napi::Env env) override;
    void up(int index) override;
    void down() override;

    int baseWidth;
    int baseHeight;
    std::vector<CGItem*> items;

private:
    obs_scene_t *obs_scene;
};

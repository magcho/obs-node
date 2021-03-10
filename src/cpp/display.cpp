// Most of code in this file are copied from
// https://github.com/stream-labs/obs-studio-node/blob/staging/obs-studio-server/source/nodeobs_display.cpp

#include "display.h"
#include "./platform/platform.h"

Display::Display(void *parentHandle, int scaleFactor, std::string &sourceName) {
    this->parentHandle = parentHandle;
    this->scaleFactor = scaleFactor;

    // create window for display
    this->windowHandle = createDisplayWindow(this->parentHandle);

    // create display
    gs_init_data gs_init_data = {};
    gs_init_data.adapter = 0;
    gs_init_data.cx = 1;
    gs_init_data.cy = 1;
    gs_init_data.num_backbuffers = 1;
    gs_init_data.format = GS_RGBA;
    gs_init_data.zsformat = GS_ZS_NONE;
#ifdef _WIN32
    gs_init_data.window.hwnd = this->windowHandle;
#elif __APPLE__
    gs_init_data.window.view = static_cast<objc_object *>(this->windowHandle);
#endif
    obs_display = obs_display_create(&gs_init_data, 0x0);

    if (!obs_display) {
        throw std::runtime_error("Failed to create the display");
    }

    // obs source
    if (sourceName == "output") {
        displayOutput = true;
        obs_source = nullptr;
    } else {
        displayOutput = false;
        obs_source = obs_get_source_by_name(sourceName.c_str());
        obs_source_inc_showing(obs_source);
    }

    // draw callback
    obs_display_add_draw_callback(obs_display, displayCallback, this);
}

Display::~Display() {
    obs_display_remove_draw_callback(obs_display, displayCallback, this);
    if (obs_source) {
        obs_source_dec_showing(obs_source);
        obs_source_release(obs_source);
    }
    if (obs_display) {
        obs_display_destroy(obs_display);
    }
    destroyWindow(windowHandle);
}

void Display::move(int x, int y, int width, int height) {
    moveWindow(windowHandle, x, y, width, height);
    if (this->width != width && this->height != height) {
        obs_display_resize(obs_display, width * scaleFactor, height * scaleFactor);
    }
    this->x = x;
    this->y = y;
    this->width = width;
    this->height = height;
}

void Display::displayCallback(void *displayPtr, uint32_t cx, uint32_t cy) {
    auto *dp = static_cast<Display *>(displayPtr);

    // Get proper source/base size.
    uint32_t width = 0;
    uint32_t height = 0;
    if (dp->displayOutput) {
        obs_video_info ovi = {};
        obs_get_video_info(&ovi);
        width = ovi.base_width;
        height = ovi.base_height;
    } else if (dp->obs_source) {
        width = obs_source_get_width(dp->obs_source);
        height = obs_source_get_height(dp->obs_source);
    }

    if (width == 0)
        width = 1;
    if (height == 0)
        height = 1;

    gs_projection_push();

    gs_ortho(0.0f, (float)width, 0.0f, (float)height, -1, 1);

    // Source Rendering
    if (dp->displayOutput) {
        obs_render_main_view();
    } else if (dp->obs_source) {
        obs_source_video_render(dp->obs_source);
    }

    gs_projection_pop();
}
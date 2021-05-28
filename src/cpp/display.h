#pragma once

#include "settings.h"
#include <string>
#include <obs.h>
#include <graphics/graphics.h>

class Display {

public:
    Display(void *parentHandle, int scaleFactor, const std::vector<std::string> &sourceIds);

    ~Display();

    void move(int x, int y, int width, int height);

    void update(const std::vector<std::string> &sourceIds);

private:
    static void displayCallback(void *displayPtr, uint32_t cx, uint32_t cy);
    void addSources(const std::vector<std::string> &sourceIds);
    void clearSources();

    void *parentHandle; // For MacOS is NSView**, For Windows is HWND*
    int scaleFactor;
    void *windowHandle;
    obs_display_t *obs_display;
    std::vector<obs_source_t *> obs_sources;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::mutex sources_mtx;
};
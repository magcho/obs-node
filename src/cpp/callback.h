#pragma once
#include <functional>

typedef std::function<void(
        std::string &sourceId,
        std::string &sceneId,
        int channels,
        std::vector<float> &magnitude,
        std::vector<float> &peak,
        std::vector<float> &input_peak)> VolmeterCallback;

class Callback {
public:
    static void setVolmeterCallback(VolmeterCallback &callback);
    static VolmeterCallback getVolmeterCallback();

private:
    static VolmeterCallback volmeterCallback;
};

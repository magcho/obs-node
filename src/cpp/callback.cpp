#include "callback.h"

VolmeterCallback Callback::volmeterCallback;

void Callback::setVolmeterCallback(VolmeterCallback &callback) {
    volmeterCallback = callback;
}

VolmeterCallback Callback::getVolmeterCallback() {
    return volmeterCallback;
}
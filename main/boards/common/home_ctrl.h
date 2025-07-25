#pragma once

#include <cstdint>
#include <functional>
#include <driver/gpio.h>
#include <esp_timer.h>

class HomeCtrl {
public:
    HomeCtrl();
    ~HomeCtrl();

    void ctrlLamp(bool state);
    inline bool lampState() const { return lamp_state_; }

protected:
    virtual void CtrlLamp(bool state) = 0;

    bool lamp_state_ = 0;
};

#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include <array>
#include <cstdint>

#include <driver/gpio.h>
#include <driver/ledc.h>

class ServoController {
public:
    static ServoController& GetInstance();

    void Initialize();
    void SetAngle(int servo_id, float angle);
    void SetPulseWidth(int servo_id, uint32_t pulse_us);

private:
    ServoController() = default;

    static constexpr int kServoCount = 4;

    bool initialized_ = false;
    std::array<gpio_num_t, kServoCount> gpio_nums_ {};
    std::array<ledc_channel_t, kServoCount> channels_ {};
};

#endif // SERVO_CONTROLLER_H

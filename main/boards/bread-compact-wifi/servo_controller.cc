#include "servo_controller.h"

#include <algorithm>
#include <cmath>

#include <esp_log.h>

#include "config.h"
#include "mcp_server.h"

#define TAG "ServoController"

#if !defined(SERVO_GPIO_0) && defined(FL_GPIO_NUM)
#define SERVO_GPIO_0 FL_GPIO_NUM
#endif

#if !defined(SERVO_GPIO_1) && defined(FR_GPIO_NUM)
#define SERVO_GPIO_1 FR_GPIO_NUM
#endif

#if !defined(SERVO_GPIO_2) && defined(BL_GPIO_NUM)
#define SERVO_GPIO_2 BL_GPIO_NUM
#endif

#if !defined(SERVO_GPIO_3) && defined(BR_GPIO_NUM)
#define SERVO_GPIO_3 BR_GPIO_NUM
#endif

#ifndef SERVO_GPIO_0
#define SERVO_GPIO_0 GPIO_NUM_NC
#endif

#ifndef SERVO_GPIO_1
#define SERVO_GPIO_1 GPIO_NUM_NC
#endif

#ifndef SERVO_GPIO_2
#define SERVO_GPIO_2 GPIO_NUM_NC
#endif

#ifndef SERVO_GPIO_3
#define SERVO_GPIO_3 GPIO_NUM_NC
#endif

namespace {
constexpr ledc_mode_t kLedcMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kLedcTimer = LEDC_TIMER_2;
constexpr ledc_timer_bit_t kDutyResolution = LEDC_TIMER_13_BIT;
constexpr uint32_t kServoFrequencyHz = 50;
constexpr uint32_t kServoPeriodUs = 1000000 / kServoFrequencyHz;
constexpr uint32_t kMaxDuty = (1 << 13) - 1;
constexpr uint32_t kMinPulseUs = 500;
constexpr uint32_t kMaxPulseUs = 2500;
constexpr float kMinAngle = 0.0f;
constexpr float kMaxAngle = 180.0f;

uint32_t PulseWidthToDuty(uint32_t pulse_us) {
    return pulse_us * kMaxDuty / kServoPeriodUs;
}
} // namespace

ServoController& ServoController::GetInstance() {
    static ServoController instance;
    return instance;
}

void ServoController::Initialize() {
    if (initialized_) {
        return;
    }

    gpio_nums_ = {SERVO_GPIO_0, SERVO_GPIO_1, SERVO_GPIO_2, SERVO_GPIO_3};
    channels_ = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3};

    ledc_timer_config_t timer_config = {
        .speed_mode = kLedcMode,
        .duty_resolution = kDutyResolution,
        .timer_num = kLedcTimer,
        .freq_hz = kServoFrequencyHz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    int attached_count = 0;
    for (int i = 0; i < kServoCount; ++i) {
        if (gpio_nums_[i] == GPIO_NUM_NC) {
            continue;
        }

        ledc_channel_config_t channel_config = {
            .gpio_num = gpio_nums_[i],
            .speed_mode = kLedcMode,
            .channel = channels_[i],
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = kLedcTimer,
            .duty = PulseWidthToDuty(1500),
            .hpoint = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
        ++attached_count;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Initialized %d servo channel(s)", attached_count);

    // Old servo tools are disabled while the servo control flow is being replaced.
    // auto& mcp_server = McpServer::GetInstance();
    // mcp_server.AddTool(
    //     "self.servo.set_angle",
    //     "Set a servo angle. servo_id starts from 0. angle range is 0 to 180.",
    //     PropertyList({
    //         Property("servo_id", kPropertyTypeInteger, 0, kServoCount - 1),
    //         Property("angle", kPropertyTypeInteger, 0, 180),
    //     }),
    //     [this](const PropertyList& properties) -> ReturnValue {
    //         SetAngle(properties["servo_id"].value<int>(), properties["angle"].value<int>());
    //         return true;
    //     });

    // mcp_server.AddTool(
    //     "self.servo.set_pulse_width",
    //     "Set a servo PWM pulse width in microseconds. servo_id starts from 0. pulse_us range is 500 to 2500.",
    //     PropertyList({
    //         Property("servo_id", kPropertyTypeInteger, 0, kServoCount - 1),
    //         Property("pulse_us", kPropertyTypeInteger, static_cast<int>(kMinPulseUs), static_cast<int>(kMaxPulseUs)),
    //     }),
    //     [this](const PropertyList& properties) -> ReturnValue {
    //         SetPulseWidth(properties["servo_id"].value<int>(), properties["pulse_us"].value<int>());
    //         return true;
    //     });
}

void ServoController::SetAngle(int servo_id, float angle) {
    if (std::isnan(angle)) {
        ESP_LOGW(TAG, "Ignore invalid servo angle: NaN");
        return;
    }

    angle = std::clamp(angle, kMinAngle, kMaxAngle);
    uint32_t pulse_us = kMinPulseUs +
        static_cast<uint32_t>(std::round(angle * (kMaxPulseUs - kMinPulseUs) / kMaxAngle));

    SetPulseWidth(servo_id, pulse_us);
}

void ServoController::SetPulseWidth(int servo_id, uint32_t pulse_us) {
    if (!initialized_) {
        Initialize();
    }

    if (servo_id < 0 || servo_id >= kServoCount) {
        ESP_LOGW(TAG, "Invalid servo id: %d", servo_id);
        return;
    }

    if (gpio_nums_[servo_id] == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "Servo %d GPIO is not configured", servo_id);
        return;
    }

    pulse_us = std::clamp(pulse_us, kMinPulseUs, kMaxPulseUs);
    ESP_ERROR_CHECK(ledc_set_duty(kLedcMode, channels_[servo_id], PulseWidthToDuty(pulse_us)));
    ESP_ERROR_CHECK(ledc_update_duty(kLedcMode, channels_[servo_id]));
}

#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "servo_dog_ctrl.h"

#include <esp_log.h>
#include <esp_err.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <iot_servo.h>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"

class CompactWifiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        touch_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        touch_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeServoDog() {
        servo_dog_ctrl_config_t config = {
            .fl_gpio_num = FL_GPIO_NUM,
            .fr_gpio_num = FR_GPIO_NUM,
            .bl_gpio_num = BL_GPIO_NUM,
            .br_gpio_num = BR_GPIO_NUM,
        };

        esp_err_t ret = servo_dog_ctrl_init(&config);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "servo_dog_ctrl initialized successfully");
            vTaskDelay(pdMS_TO_TICKS(1000));

            ret = iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 20);
            ESP_LOGI(TAG, "direct FL servo angle 20 ret: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(500));

            ret = iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 120);
            ESP_LOGI(TAG, "direct FL servo angle 120 ret: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(500));

            ret = servo_dog_ctrl_send(DOG_STATE_INSTALLATION, NULL);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "DOG_STATE_INSTALLATION command sent");
            } else {
                ESP_LOGE(TAG, "Failed to send INSTALLATION command: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGE(TAG, "servo_dog_ctrl init failed: %s", esp_err_to_name(ret));
        }
    }

    void RegisterServoDogTools() {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool("self.dog.basic_control", "机器人的基础动作。机器人可以做以下基础动作：\n"
            "forward: 向前移动\nbackward: 向后移动\nturn_left: 向左转\nturn_right: 向右转\nstop: 立即停止当前动作",
            PropertyList({
                Property("action", kPropertyTypeString),
            }), [](const PropertyList& properties) -> ReturnValue {
                const std::string& action = properties["action"].value<std::string>();
                if (action == "forward") {
                    servo_dog_ctrl_send(DOG_STATE_FORWARD, NULL);
                } else if (action == "backward") {
                    servo_dog_ctrl_send(DOG_STATE_BACKWARD, NULL);
                } else if (action == "turn_left") {
                    servo_dog_ctrl_send(DOG_STATE_TURN_LEFT, NULL);
                } else if (action == "turn_right") {
                    servo_dog_ctrl_send(DOG_STATE_TURN_RIGHT, NULL);
                } else if (action == "stop") {
                    servo_dog_ctrl_send(DOG_STATE_IDLE, NULL);
                } else {
                    return false;
                }
                return true;
            });

        mcp_server.AddTool("self.dog.advanced_control", "机器人的扩展动作。机器人可以做以下扩展动作：\n"
            "sway_back_forth: 前后摇摆\nlay_down: 趴下\nsway: 左右摇摆\nretract_legs: 收回腿部\n"
            "shake_hand: 握手\nshake_back_legs: 伸懒腰\njump_forward: 向前跳跃",
            PropertyList({
                Property("action", kPropertyTypeString),
            }), [](const PropertyList& properties) -> ReturnValue {
                const std::string& action = properties["action"].value<std::string>();
                if (action == "sway_back_forth") {
                    servo_dog_ctrl_send(DOG_STATE_SWAY_BACK_FORTH, NULL);
                } else if (action == "lay_down") {
                    servo_dog_ctrl_send(DOG_STATE_LAY_DOWN, NULL);
                } else if (action == "sway") {
                    dog_action_args_t args = {
                        .repeat_count = 4,
                    };
                    servo_dog_ctrl_send(DOG_STATE_SWAY, &args);
                } else if (action == "retract_legs") {
                    servo_dog_ctrl_send(DOG_STATE_RETRACT_LEGS, NULL);
                } else if (action == "shake_hand") {
                    servo_dog_ctrl_send(DOG_STATE_SHAKE_HAND, NULL);
                } else if (action == "shake_back_legs") {
                    servo_dog_ctrl_send(DOG_STATE_SHAKE_BACK_LEGS, NULL);
                } else if (action == "jump_forward") {
                    servo_dog_ctrl_send(DOG_STATE_JUMP_FORWARD, NULL);
                } else {
                    return false;
                }
                return true;
            });
    }

    // 物联网初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
        InitializeServoDog();
        RegisterServoDogTools();
    }

public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeTools();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_SPK_SLOT_MASK,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN, AUDIO_I2S_MIC_SLOT_MASK);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(CompactWifiBoard);

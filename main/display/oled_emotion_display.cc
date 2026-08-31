#include "oled_emotion_display.h"

#include <array>
#include <cstdint>
#include <esp_random.h>

namespace {
constexpr int kOledWidth = 128;
constexpr int kOledHeight = 64;
constexpr int kOledStride = 16;
constexpr int kOledPaletteBytes = 8;
constexpr int kOledBitmapBytes = kOledPaletteBytes + kOledStride * kOledHeight;
constexpr int kBlinkFrameDurationMs = 60;
constexpr int kWakeBlinkFrameDurationMs = 35;
constexpr int kListeningBreatheFrameDurationMs = 900;
constexpr int kThinkingBreatheFrameDurationMs = 1100;
constexpr int kSleepyBlinkFrameDurationMs = 180;
constexpr int kTransitionFrameDurationMs = 70;
constexpr int kBlinkMinDelayMs = 3000;
constexpr int kBlinkMaxDelayMs = 6000;
constexpr int kWakeBlinkRepeatCount = 3;
constexpr int kListeningBlinkMinSteps = 3;
constexpr int kListeningBlinkMaxSteps = 6;
constexpr int kThinkingBlinkMinSteps = 4;
constexpr int kThinkingBlinkMaxSteps = 7;
constexpr int kSleepyBlinkMinDelayMs = 1600;
constexpr int kSleepyBlinkMaxDelayMs = 3200;

using Bitmap = std::array<uint8_t, kOledBitmapBytes>;

struct RoundedRect {
    int x0;
    int x1;
    int y0;
    int y1;
    int radius;
};

struct Point {
    int x;
    int y;
};

Bitmap CreateBlankBitmap() {
    Bitmap bitmap = {};

    // LVGL I1 image data starts with a two-color ARGB8888 palette.
    bitmap[0] = 0xff;
    bitmap[1] = 0xff;
    bitmap[2] = 0xff;
    bitmap[3] = 0xff;
    bitmap[4] = 0x00;
    bitmap[5] = 0x00;
    bitmap[6] = 0x00;
    bitmap[7] = 0xff;

    return bitmap;
}

void SetPixel(Bitmap& bitmap, int x, int y) {
    if (x < 0 || x >= kOledWidth || y < 0 || y >= kOledHeight) {
        return;
    }

    const int byte_index = kOledPaletteBytes + y * kOledStride + x / 8;
    const int bit_index = 7 - x % 8;
    bitmap[byte_index] = static_cast<uint8_t>(bitmap[byte_index] | (1 << bit_index));
}

bool IsInsideRoundedRect(int x, int y, const RoundedRect& rect) {
    if (x < rect.x0 || x > rect.x1 || y < rect.y0 || y > rect.y1) {
        return false;
    }

    int cx = x;
    int cy = y;
    if (x < rect.x0 + rect.radius) {
        cx = rect.x0 + rect.radius;
    } else if (x > rect.x1 - rect.radius) {
        cx = rect.x1 - rect.radius;
    }
    if (y < rect.y0 + rect.radius) {
        cy = rect.y0 + rect.radius;
    } else if (y > rect.y1 - rect.radius) {
        cy = rect.y1 - rect.radius;
    }

    const int dx = x - cx;
    const int dy = y - cy;
    return dx * dx + dy * dy <= rect.radius * rect.radius;
}

bool IsInsidePolygon(int x, int y, const Point* points, int count) {
    bool inside = false;
    for (int i = 0, j = count - 1; i < count; j = i++) {
        const Point& pi = points[i];
        const Point& pj = points[j];
        const bool crosses = (pi.y > y) != (pj.y > y);
        if (crosses) {
            const int intersect_x = (pj.x - pi.x) * (y - pi.y) / (pj.y - pi.y) + pi.x;
            if (x < intersect_x) {
                inside = !inside;
            }
        }
    }
    return inside;
}

void FillRoundedRect(Bitmap& bitmap, const RoundedRect& rect) {
    for (int y = rect.y0; y <= rect.y1; ++y) {
        for (int x = rect.x0; x <= rect.x1; ++x) {
            if (IsInsideRoundedRect(x, y, rect)) {
                SetPixel(bitmap, x, y);
            }
        }
    }
}

void FillPolygon(Bitmap& bitmap, const Point* points, int count) {
    int min_x = points[0].x;
    int max_x = points[0].x;
    int min_y = points[0].y;
    int max_y = points[0].y;
    for (int i = 1; i < count; ++i) {
        if (points[i].x < min_x) {
            min_x = points[i].x;
        }
        if (points[i].x > max_x) {
            max_x = points[i].x;
        }
        if (points[i].y < min_y) {
            min_y = points[i].y;
        }
        if (points[i].y > max_y) {
            max_y = points[i].y;
        }
    }

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (IsInsidePolygon(x, y, points, count)) {
                SetPixel(bitmap, x, y);
            }
        }
    }
}

void FillCircle(Bitmap& bitmap, int center_x, int center_y, int radius) {
    for (int y = center_y - radius; y <= center_y + radius; ++y) {
        for (int x = center_x - radius; x <= center_x + radius; ++x) {
            const int dx = x - center_x;
            const int dy = y - center_y;
            if (dx * dx + dy * dy <= radius * radius) {
                SetPixel(bitmap, x, y);
            }
        }
    }
}

void DrawQuadraticStroke(Bitmap& bitmap, Point start, Point control, Point end, int radius) {
    constexpr int kSegments = 40;
    for (int i = 0; i <= kSegments; ++i) {
        const int inv = kSegments - i;
        const int x = (inv * inv * start.x + 2 * inv * i * control.x + i * i * end.x) / (kSegments * kSegments);
        const int y = (inv * inv * start.y + 2 * inv * i * control.y + i * i * end.y) / (kSegments * kSegments);
        FillCircle(bitmap, x, y, radius);
    }
}

Bitmap CreateEyesBitmap(const RoundedRect& left_eye, const RoundedRect& right_eye) {
    Bitmap bitmap = CreateBlankBitmap();

    const RoundedRect eyes[] = {left_eye, right_eye};

    for (const auto& eye : eyes) {
        FillRoundedRect(bitmap, eye);
    }

    return bitmap;
}

Bitmap CreateHappyEyesBitmap() {
    Bitmap bitmap = CreateBlankBitmap();
    DrawQuadraticStroke(bitmap, {18, 39}, {32, 23}, {46, 39}, 5);
    DrawQuadraticStroke(bitmap, {82, 39}, {96, 23}, {110, 39}, 5);
    return bitmap;
}

Bitmap CreateAngryEyesBitmap() {
    Bitmap bitmap = CreateBlankBitmap();
    const Point left_eye[] = {{18, 8}, {46, 20}, {46, 46}, {42, 54}, {32, 56}, {22, 52}, {18, 43}};
    const Point right_eye[] = {{82, 20}, {110, 8}, {110, 43}, {106, 52}, {96, 56}, {86, 54}, {82, 46}};
    FillPolygon(bitmap, left_eye, 7);
    FillPolygon(bitmap, right_eye, 7);
    return bitmap;
}

Bitmap CreateSadEyesBitmap() {
    Bitmap bitmap = CreateBlankBitmap();
    const Point left_eye[] = {{18, 20}, {46, 8}, {46, 43}, {42, 52}, {32, 56}, {22, 54}, {18, 46}};
    const Point right_eye[] = {{82, 8}, {110, 20}, {110, 46}, {106, 54}, {96, 56}, {86, 52}, {82, 43}};
    FillPolygon(bitmap, left_eye, 7);
    FillPolygon(bitmap, right_eye, 7);
    return bitmap;
}

struct EyeFrame {
    Bitmap bitmap;
    lv_image_dsc_t image;

    EyeFrame(const RoundedRect& left_eye, const RoundedRect& right_eye)
        : bitmap(CreateEyesBitmap(left_eye, right_eye)),
          image {
              .header = {
                  .magic = LV_IMAGE_HEADER_MAGIC,
                  .cf = LV_COLOR_FORMAT_I1,
                  .flags = 0,
                  .w = kOledWidth,
                  .h = kOledHeight,
                  .stride = kOledStride,
                  .reserved_2 = 0,
              },
              .data_size = static_cast<uint32_t>(bitmap.size()),
              .data = bitmap.data(),
              .reserved = nullptr,
              .reserved_2 = nullptr,
          } {
    }

    explicit EyeFrame(Bitmap source)
        : bitmap(source),
          image {
              .header = {
                  .magic = LV_IMAGE_HEADER_MAGIC,
                  .cf = LV_COLOR_FORMAT_I1,
                  .flags = 0,
                  .w = kOledWidth,
                  .h = kOledHeight,
                  .stride = kOledStride,
                  .reserved_2 = 0,
              },
              .data_size = static_cast<uint32_t>(bitmap.size()),
              .data = bitmap.data(),
              .reserved = nullptr,
              .reserved_2 = nullptr,
          } {
    }
};

const EyeFrame* GetNeutralBlinkFrames() {
    static const EyeFrame frames[] = {
        EyeFrame({18, 46, 8, 55, 7}, {82, 110, 8, 55, 7}),
        EyeFrame({18, 46, 23, 40, 5}, {82, 110, 23, 40, 5}),
        EyeFrame({18, 46, 30, 34, 2}, {82, 110, 30, 34, 2}),
    };
    return frames;
}

const EyeFrame* GetListeningBreatheFrames() {
    static const EyeFrame frames[] = {
        EyeFrame({18, 46, 7, 56, 7}, {82, 110, 7, 56, 7}),
        EyeFrame({18, 46, 8, 55, 7}, {82, 110, 8, 55, 7}),
        EyeFrame({19, 45, 9, 54, 7}, {83, 109, 9, 54, 7}),
    };
    return frames;
}

const EyeFrame* GetThinkingBreatheFrames() {
    static const EyeFrame frames[] = {
        EyeFrame({18, 46, 2, 49, 7}, {82, 110, 2, 49, 7}),
        EyeFrame({18, 46, 3, 48, 7}, {82, 110, 3, 48, 7}),
        EyeFrame({19, 45, 4, 47, 7}, {83, 109, 4, 47, 7}),
    };
    return frames;
}

const EyeFrame* GetThinkingBlinkFrames() {
    static const EyeFrame frames[] = {
        EyeFrame({18, 46, 2, 49, 7}, {82, 110, 2, 49, 7}),
        EyeFrame({18, 46, 17, 32, 5}, {82, 110, 17, 32, 5}),
        EyeFrame({18, 46, 24, 28, 2}, {82, 110, 24, 28, 2}),
    };
    return frames;
}

const EyeFrame* GetSleepyBlinkFrames() {
    static const EyeFrame frames[] = {
        EyeFrame({18, 46, 23, 40, 5}, {82, 110, 23, 40, 5}),
        EyeFrame({18, 46, 26, 37, 4}, {82, 110, 26, 37, 4}),
        EyeFrame({18, 46, 30, 34, 2}, {82, 110, 30, 34, 2}),
    };
    return frames;
}

const EyeFrame& GetHappyFrame() {
    static const EyeFrame frame(CreateHappyEyesBitmap());
    return frame;
}

const EyeFrame& GetAngryFrame() {
    static const EyeFrame frame(CreateAngryEyesBitmap());
    return frame;
}

const EyeFrame& GetSadFrame() {
    static const EyeFrame frame(CreateSadEyesBitmap());
    return frame;
}
}

OledEmotionDisplay::OledEmotionDisplay(lv_obj_t* parent) {
    image_ = lv_image_create(parent);
    ShowFrame(0);
    lv_obj_center(image_);
    lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);

    blink_timer_ = lv_timer_create(BlinkTimerCallback, kBlinkMinDelayMs, this);
    lv_timer_pause(blink_timer_);
}

OledEmotionDisplay::~OledEmotionDisplay() {
    if (blink_timer_ != nullptr) {
        lv_timer_delete(blink_timer_);
    }
    if (image_ != nullptr) {
        lv_obj_del(image_);
    }
}

void OledEmotionDisplay::SetEmotion(PetEmotion emotion) {
    if (image_ == nullptr) {
        return;
    }

    if (visible_ && emotion_ == emotion && blink_mode_ != BlinkMode::Transition) {
        return;
    }
    if (visible_ && blink_mode_ == BlinkMode::Transition && target_emotion_ == emotion) {
        return;
    }

    if (visible_ && emotion != PetEmotion::Wakeup && emotion != PetEmotion::Waking && emotion != PetEmotion::Listening) {
        StartTransition(emotion);
        return;
    }

    ApplyEmotion(emotion);
}

void OledEmotionDisplay::ApplyEmotion(PetEmotion emotion) {
    emotion_ = emotion;
    target_emotion_ = emotion;
    blink_frame_index_ = -1;
    blink_repeat_count_ = 0;
    breathe_frame_index_ = 0;
    steps_until_blink_ = 0;

    if (emotion_ == PetEmotion::Neutral || emotion_ == PetEmotion::Speaking) {
        blink_mode_ = BlinkMode::IdleRandom;
        visible_ = true;
        ShowFrame(0);
        lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
        ScheduleNextBlink();
    } else if (emotion_ == PetEmotion::Wakeup || emotion_ == PetEmotion::Waking) {
        StartWakeBlink();
    } else if (emotion_ == PetEmotion::Listening) {
        blink_mode_ = BlinkMode::Listening;
        visible_ = true;
        ShowListeningFrame(0);
        lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
        ScheduleNextListeningBlink();
    } else if (emotion_ == PetEmotion::Thinking) {
        blink_mode_ = BlinkMode::Thinking;
        visible_ = true;
        ShowThinkingFrame(0);
        lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
        ScheduleNextThinkingBlink();
    } else if (emotion_ == PetEmotion::Happy) {
        visible_ = true;
        ShowHappyFrame();
        lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
        if (blink_timer_ != nullptr) {
            lv_timer_pause(blink_timer_);
        }
    } else if (emotion_ == PetEmotion::Sad) {
        visible_ = true;
        ShowSadFrame();
        lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
        if (blink_timer_ != nullptr) {
            lv_timer_pause(blink_timer_);
        }
    } else if (emotion_ == PetEmotion::Angry) {
        visible_ = true;
        ShowAngryFrame();
        lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
        if (blink_timer_ != nullptr) {
            lv_timer_pause(blink_timer_);
        }
    } else if (emotion_ == PetEmotion::Sleepy) {
        blink_mode_ = BlinkMode::Sleepy;
        visible_ = true;
        ShowSleepyFrame(0);
        lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
        ScheduleNextSleepyBlink();
    } else {
        Hide();
    }
}

void OledEmotionDisplay::ShowIdleEyes() {
    SetEmotion(PetEmotion::Neutral);
}

void OledEmotionDisplay::Hide() {
    visible_ = false;
    blink_frame_index_ = -1;
    blink_repeat_count_ = 0;
    breathe_frame_index_ = 0;
    steps_until_blink_ = 0;
    if (blink_timer_ != nullptr) {
        lv_timer_pause(blink_timer_);
    }
    if (image_ != nullptr) {
        lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
    }
}

void OledEmotionDisplay::ShowFrame(int frame_index) {
    if (image_ == nullptr) {
        return;
    }

    static constexpr int frame_map[] = {0, 1, 2, 1, 0};
    const auto* frames = GetNeutralBlinkFrames();
    lv_image_set_src(image_, &frames[frame_map[frame_index]].image);
}

void OledEmotionDisplay::ShowHappyFrame() {
    if (image_ == nullptr) {
        return;
    }

    lv_image_set_src(image_, &GetHappyFrame().image);
}

void OledEmotionDisplay::ShowSadFrame() {
    if (image_ == nullptr) {
        return;
    }

    lv_image_set_src(image_, &GetSadFrame().image);
}

void OledEmotionDisplay::ShowAngryFrame() {
    if (image_ == nullptr) {
        return;
    }

    lv_image_set_src(image_, &GetAngryFrame().image);
}

void OledEmotionDisplay::ShowListeningFrame(int frame_index) {
    if (image_ == nullptr) {
        return;
    }

    static constexpr int frame_map[] = {0, 1, 2, 1};
    const auto* frames = GetListeningBreatheFrames();
    lv_image_set_src(image_, &frames[frame_map[frame_index]].image);
}

void OledEmotionDisplay::ShowThinkingFrame(int frame_index) {
    if (image_ == nullptr) {
        return;
    }

    static constexpr int frame_map[] = {0, 1, 2, 1};
    const auto* frames = GetThinkingBreatheFrames();
    lv_image_set_src(image_, &frames[frame_map[frame_index]].image);
}

void OledEmotionDisplay::ShowThinkingBlinkFrame(int frame_index) {
    if (image_ == nullptr) {
        return;
    }

    static constexpr int frame_map[] = {0, 1, 2, 1, 0};
    const auto* frames = GetThinkingBlinkFrames();
    lv_image_set_src(image_, &frames[frame_map[frame_index]].image);
}

void OledEmotionDisplay::ShowSleepyFrame(int frame_index) {
    if (image_ == nullptr) {
        return;
    }

    static constexpr int frame_map[] = {0, 1, 2, 1, 0};
    const auto* frames = GetSleepyBlinkFrames();
    lv_image_set_src(image_, &frames[frame_map[frame_index]].image);
}

void OledEmotionDisplay::ScheduleNextBlink() {
    if (blink_timer_ == nullptr) {
        return;
    }

    blink_mode_ = BlinkMode::IdleRandom;
    const uint32_t range = kBlinkMaxDelayMs - kBlinkMinDelayMs + 1;
    lv_timer_set_period(blink_timer_, kBlinkMinDelayMs + esp_random() % range);
    lv_timer_resume(blink_timer_);
}

void OledEmotionDisplay::ScheduleNextListeningBlink() {
    if (blink_timer_ == nullptr) {
        return;
    }

    blink_mode_ = BlinkMode::Listening;
    blink_frame_index_ = -1;
    const uint32_t range = kListeningBlinkMaxSteps - kListeningBlinkMinSteps + 1;
    steps_until_blink_ = kListeningBlinkMinSteps + esp_random() % range;
    lv_timer_set_period(blink_timer_, kListeningBreatheFrameDurationMs);
    lv_timer_resume(blink_timer_);
}

void OledEmotionDisplay::ScheduleNextThinkingBlink() {
    if (blink_timer_ == nullptr) {
        return;
    }

    blink_mode_ = BlinkMode::Thinking;
    blink_frame_index_ = -1;
    const uint32_t range = kThinkingBlinkMaxSteps - kThinkingBlinkMinSteps + 1;
    steps_until_blink_ = kThinkingBlinkMinSteps + esp_random() % range;
    lv_timer_set_period(blink_timer_, kThinkingBreatheFrameDurationMs);
    lv_timer_resume(blink_timer_);
}

void OledEmotionDisplay::ScheduleNextSleepyBlink() {
    if (blink_timer_ == nullptr) {
        return;
    }

    blink_mode_ = BlinkMode::Sleepy;
    blink_frame_index_ = -1;
    const uint32_t range = kSleepyBlinkMaxDelayMs - kSleepyBlinkMinDelayMs + 1;
    lv_timer_set_period(blink_timer_, kSleepyBlinkMinDelayMs + esp_random() % range);
    lv_timer_resume(blink_timer_);
}

void OledEmotionDisplay::StartWakeBlink() {
    visible_ = true;
    blink_mode_ = BlinkMode::WakeTriple;
    blink_repeat_count_ = kWakeBlinkRepeatCount;
    blink_frame_index_ = 1;
    breathe_frame_index_ = 0;
    steps_until_blink_ = 0;
    ShowFrame(0);
    lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);

    if (blink_timer_ != nullptr) {
        lv_timer_set_period(blink_timer_, kWakeBlinkFrameDurationMs);
        lv_timer_resume(blink_timer_);
        lv_timer_ready(blink_timer_);
    }
}

void OledEmotionDisplay::StartTransition(PetEmotion target_emotion) {
    target_emotion_ = target_emotion;
    blink_mode_ = BlinkMode::Transition;
    blink_frame_index_ = 1;
    blink_repeat_count_ = 0;
    breathe_frame_index_ = 0;
    steps_until_blink_ = 0;
    visible_ = true;
    ShowFrame(0);
    lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);

    if (blink_timer_ != nullptr) {
        lv_timer_set_period(blink_timer_, kTransitionFrameDurationMs);
        lv_timer_resume(blink_timer_);
        lv_timer_ready(blink_timer_);
    }
}

void OledEmotionDisplay::BlinkTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<OledEmotionDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr || !self->visible_) {
        lv_timer_pause(timer);
        return;
    }

    if (self->blink_mode_ == BlinkMode::WakeTriple) {
        self->ShowFrame(self->blink_frame_index_);
        if (self->blink_frame_index_ == 4) {
            self->blink_repeat_count_--;
            if (self->blink_repeat_count_ <= 0) {
                if (self->emotion_ == PetEmotion::Waking) {
                    self->blink_repeat_count_ = kWakeBlinkRepeatCount;
                    self->blink_frame_index_ = 1;
                    lv_timer_set_period(timer, kWakeBlinkFrameDurationMs);
                } else {
                    self->blink_frame_index_ = -1;
                    self->SetEmotion(PetEmotion::Listening);
                }
            } else {
                self->blink_frame_index_ = 1;
                lv_timer_set_period(timer, kWakeBlinkFrameDurationMs);
            }
        } else {
            self->blink_frame_index_++;
            lv_timer_set_period(timer, kWakeBlinkFrameDurationMs);
        }
        return;
    }

    if (self->blink_mode_ == BlinkMode::Transition) {
        self->ShowFrame(self->blink_frame_index_);
        if (self->blink_frame_index_ >= 3) {
            self->ApplyEmotion(self->target_emotion_);
        } else {
            self->blink_frame_index_++;
            lv_timer_set_period(timer, kTransitionFrameDurationMs);
        }
        return;
    }

    if (self->blink_mode_ == BlinkMode::Listening) {
        if (self->emotion_ != PetEmotion::Listening) {
            lv_timer_pause(timer);
            return;
        }

        if (self->blink_frame_index_ >= 0) {
            self->ShowFrame(self->blink_frame_index_);
            if (self->blink_frame_index_ >= 4) {
                self->breathe_frame_index_ = 0;
                self->ShowListeningFrame(self->breathe_frame_index_);
                self->ScheduleNextListeningBlink();
            } else {
                self->blink_frame_index_++;
                lv_timer_set_period(timer, kBlinkFrameDurationMs);
            }
            return;
        }

        self->ShowListeningFrame(self->breathe_frame_index_);
        self->breathe_frame_index_ = (self->breathe_frame_index_ + 1) % 4;
        self->steps_until_blink_--;
        if (self->steps_until_blink_ <= 0) {
            self->blink_frame_index_ = 1;
            lv_timer_set_period(timer, kBlinkFrameDurationMs);
        } else {
            lv_timer_set_period(timer, kListeningBreatheFrameDurationMs);
        }
        return;
    }

    if (self->blink_mode_ == BlinkMode::Thinking) {
        if (self->emotion_ != PetEmotion::Thinking) {
            lv_timer_pause(timer);
            return;
        }

        if (self->blink_frame_index_ >= 0) {
            self->ShowThinkingBlinkFrame(self->blink_frame_index_);
            if (self->blink_frame_index_ >= 4) {
                self->breathe_frame_index_ = 0;
                self->ShowThinkingFrame(self->breathe_frame_index_);
                self->ScheduleNextThinkingBlink();
            } else {
                self->blink_frame_index_++;
                lv_timer_set_period(timer, kBlinkFrameDurationMs);
            }
            return;
        }

        self->ShowThinkingFrame(self->breathe_frame_index_);
        self->breathe_frame_index_ = (self->breathe_frame_index_ + 1) % 4;
        self->steps_until_blink_--;
        if (self->steps_until_blink_ <= 0) {
            self->blink_frame_index_ = 1;
            lv_timer_set_period(timer, kBlinkFrameDurationMs);
        } else {
            lv_timer_set_period(timer, kThinkingBreatheFrameDurationMs);
        }
        return;
    }

    if (self->blink_mode_ == BlinkMode::Sleepy) {
        if (self->emotion_ != PetEmotion::Sleepy) {
            lv_timer_pause(timer);
            return;
        }

        if (self->blink_frame_index_ < 0) {
            self->blink_frame_index_ = 1;
        } else {
            self->blink_frame_index_++;
        }

        self->ShowSleepyFrame(self->blink_frame_index_);
        if (self->blink_frame_index_ >= 4) {
            self->blink_frame_index_ = -1;
            self->ShowSleepyFrame(0);
            self->ScheduleNextSleepyBlink();
        } else {
            lv_timer_set_period(timer, kSleepyBlinkFrameDurationMs);
        }
        return;
    }

    if (self->emotion_ != PetEmotion::Neutral) {
        lv_timer_pause(timer);
        return;
    }

    if (self->blink_frame_index_ < 0) {
        self->blink_frame_index_ = 1;
    } else {
        self->blink_frame_index_++;
    }

    if (self->blink_frame_index_ >= 5) {
        self->blink_frame_index_ = -1;
        self->ShowFrame(0);
        self->ScheduleNextBlink();
        return;
    }

    self->ShowFrame(self->blink_frame_index_);
    if (self->blink_frame_index_ == 4) {
        self->blink_frame_index_ = -1;
        self->ScheduleNextBlink();
    } else {
        lv_timer_set_period(timer, kBlinkFrameDurationMs);
    }
}

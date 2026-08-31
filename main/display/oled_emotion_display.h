#ifndef OLED_EMOTION_DISPLAY_H
#define OLED_EMOTION_DISPLAY_H

#include <lvgl.h>

enum class PetEmotion {
    Neutral,
    Happy,
    Sad,
    Angry,
    Thinking,
    Sleepy,
    Waking,
    Wakeup,
    Listening,
    Speaking
};

class OledEmotionDisplay {
public:
    explicit OledEmotionDisplay(lv_obj_t* parent);
    ~OledEmotionDisplay();

    void SetEmotion(PetEmotion emotion);
    void ShowIdleEyes();
    void Hide();

private:
    enum class BlinkMode {
        IdleRandom,
        WakeTriple,
        Transition,
        Listening,
        Thinking,
        Sleepy
    };

    lv_obj_t* image_ = nullptr;
    lv_timer_t* blink_timer_ = nullptr;
    PetEmotion emotion_ = PetEmotion::Neutral;
    PetEmotion target_emotion_ = PetEmotion::Neutral;
    BlinkMode blink_mode_ = BlinkMode::IdleRandom;
    int blink_frame_index_ = -1;
    int blink_repeat_count_ = 0;
    int breathe_frame_index_ = 0;
    int steps_until_blink_ = 0;
    bool visible_ = false;

    void ShowFrame(int frame_index);
    void ShowHappyFrame();
    void ShowSadFrame();
    void ShowAngryFrame();
    void ShowListeningFrame(int frame_index);
    void ShowThinkingFrame(int frame_index);
    void ShowThinkingBlinkFrame(int frame_index);
    void ShowSleepyFrame(int frame_index);
    void ScheduleNextBlink();
    void ScheduleNextListeningBlink();
    void ScheduleNextThinkingBlink();
    void ScheduleNextSleepyBlink();
    void StartWakeBlink();
    void StartTransition(PetEmotion target_emotion);
    void ApplyEmotion(PetEmotion emotion);
    static void BlinkTimerCallback(lv_timer_t* timer);
};

#endif // OLED_EMOTION_DISPLAY_H

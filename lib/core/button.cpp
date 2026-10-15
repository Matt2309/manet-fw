#include "button.h"

ButtonEvent Button::update(bool pressed, uint32_t now_ms) {
    if (!started_) {
        started_ = true;
        raw_ = pressed;
        raw_since_ = now_ms;
        stable_ = false;
        ignore_ = pressed;  // gia' premuto all'avvio: va rilasciato prima
        return ButtonEvent::None;
    }

    if (pressed != raw_) {
        raw_ = pressed;
        raw_since_ = now_ms;
    }

    ButtonEvent ev = ButtonEvent::None;

    // Il livello e' accettato solo se stabile da debounce_ms.
    if (raw_ != stable_ && now_ms - raw_since_ >= debounce_ms_) {
        stable_ = raw_;
        if (stable_) {
            pressed_at_ = raw_since_;
            long_fired_ = false;
        } else {
            uint32_t dur = raw_since_ - pressed_at_;
            if (ignore_) {
                ignore_ = false;
            } else if (!long_fired_ && dur < short_max_ms_) {
                ev = ButtonEvent::Short;
            }
        }
    }

    if (stable_ && !ignore_ && !long_fired_ && ev == ButtonEvent::None &&
        now_ms - pressed_at_ >= long_ms_) {
        long_fired_ = true;
        ev = ButtonEvent::Long;
    }
    return ev;
}

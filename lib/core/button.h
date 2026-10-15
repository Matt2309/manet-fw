// Logica del tasto USER, indipendente dall'hardware.
//
// Si chiama update() a intervalli regolari (circa 10 ms) con il livello letto
// (true = premuto). Restituisce al massimo un evento per chiamata:
//  - Short: rilascio dopo meno di short_max_ms
//  - Long:  scatta appena la pressione raggiunge long_ms, senza attendere il rilascio
// Le pressioni tra short_max_ms e long_ms non producono eventi.
// Un tasto gia' premuto alla prima chiamata (GPIO0 e' un pin di avvio) viene
// ignorato fino al rilascio.
#pragma once

#include <stdint.h>

enum class ButtonEvent : uint8_t { None, Short, Long };

class Button {
public:
    Button(uint32_t debounce_ms, uint32_t short_max_ms, uint32_t long_ms)
        : debounce_ms_(debounce_ms), short_max_ms_(short_max_ms), long_ms_(long_ms) {}

    ButtonEvent update(bool pressed, uint32_t now_ms);

private:
    uint32_t debounce_ms_;
    uint32_t short_max_ms_;
    uint32_t long_ms_;

    bool started_ = false;
    bool raw_ = false;
    uint32_t raw_since_ = 0;
    bool stable_ = false;
    uint32_t pressed_at_ = 0;
    bool long_fired_ = false;
    bool ignore_ = false;
};

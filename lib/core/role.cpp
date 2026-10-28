#include "role.h"

#include "config.h"

bool role_can_toggle(uint32_t fired_ms, bool run_started) {
    uint32_t pressed_ms = fired_ms >= BTN_LONG_MS ? fired_ms - BTN_LONG_MS : 0;
    return !run_started && pressed_ms < ROLE_WINDOW_MS;
}

Role role_toggled(Role r) { return r == Role::Runner ? Role::Leader : Role::Runner; }

Role role_from_byte(uint8_t b) { return b == static_cast<uint8_t>(Role::Leader) ? Role::Leader : Role::Runner; }

const char* role_name(Role r) { return r == Role::Leader ? "CAPO" : "CORR"; }

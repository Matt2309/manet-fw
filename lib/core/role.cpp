#include "role.h"

#include "config.h"

bool role_can_toggle(uint32_t t_ms, bool run_started) {
    return !run_started && t_ms < ROLE_WINDOW_MS;
}

Role role_toggled(Role r) { return r == Role::Runner ? Role::Leader : Role::Runner; }

Role role_from_byte(uint8_t b) { return b == static_cast<uint8_t>(Role::Leader) ? Role::Leader : Role::Runner; }

const char* role_name(Role r) { return r == Role::Leader ? "CAPO" : "CORR"; }

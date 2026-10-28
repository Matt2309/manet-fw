#include "run.h"

#include "config.h"

uint16_t Run::seconds(uint32_t now_ms) const {
    if (state_ == RunState::NotStarted) return packets::T_NOT_STARTED;
    uint32_t s = (now_ms - t0_) / 1000;
    return s > 0xFFFE ? 0xFFFE : static_cast<uint16_t>(s);
}

bool Run::start(uint32_t now_ms) {
    if (state_ == RunState::Running) return false;
    t0_ = now_ms;
    state_ = RunState::Running;
    return true;
}

bool Run::stop() {
    if (state_ != RunState::Running) return false;
    state_ = RunState::Stopped;
    return true;
}

Run::Change Run::adopt(uint32_t implied_t0) {
    int32_t d = static_cast<int32_t>(implied_t0 - t0_);  // positivo: il T0 del compagno e' piu' recente
    switch (state_) {
        case RunState::NotStarted:
            t0_ = implied_t0;
            state_ = RunState::Running;
            return Change::Started;
        case RunState::Running:
            if (d > static_cast<int32_t>(RUN_T0_TOLERANCE_MS)) {  // corsa nuova
                t0_ = implied_t0;
                return Change::Started;
            }
            // Stessa corsa: i T0 ricavati dai secondi interi non sono mai in anticipo, quindi il piu' vecchio e' il migliore.
            if (d < 0 && d >= -static_cast<int32_t>(RUN_T0_TOLERANCE_MS)) t0_ = implied_t0;
            return Change::None;
        case RunState::Stopped:
            if (d > static_cast<int32_t>(RUN_T0_TOLERANCE_MS)) {
                t0_ = implied_t0;
                state_ = RunState::Running;
                return Change::Started;
            }
            return Change::None;
    }
    return Change::None;
}

Run::Change Run::on_command(packets::Cmd cmd, uint16_t elapsed_ds, uint32_t now_ms) {
    if (cmd == packets::Cmd::Start) return adopt(now_ms - static_cast<uint32_t>(elapsed_ds) * 100u);
    return stop() ? Change::Stopped : Change::None;
}

Run::Change Run::on_beacon(const packets::Beacon& b, uint32_t now_ms) {
    if (b.t_s == packets::T_NOT_STARTED) return Change::None;
    uint32_t implied = now_ms - static_cast<uint32_t>(b.t_s) * 1000u;
    if (!b.stopped) return adopt(implied);
    // Il compagno dice che la corsa e' ferma: vale solo se e' la nostra corsa.
    int32_t d = static_cast<int32_t>(implied - t0_);
    if (state_ == RunState::Running && d >= -static_cast<int32_t>(RUN_T0_TOLERANCE_MS) &&
        d <= static_cast<int32_t>(RUN_T0_TOLERANCE_MS)) {
        state_ = RunState::Stopped;
        return Change::Stopped;
    }
    return Change::None;
}

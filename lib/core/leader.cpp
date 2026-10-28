#include "leader.h"

#include <string.h>

namespace {

bool newer(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) > 0; }

}  // namespace

void Leader::set_self(uint8_t id) {
    self_ = id;
    if (Row* r = row(id)) r->known = true;
}

void Leader::reset() {
    memset(rows_, 0, sizeof(rows_));
    set_self(self_);
}

void Leader::clear_alarms() {
    for (Row& r : rows_) {
        memset(r.obs, 0, sizeof(r.obs));
        r.closed_level = 0;
        r.sos_seen = r.sos_latched = r.sos_closed = false;
        r.state = ble::RunnerState::Ok;
    }
}

Leader::Row* Leader::touch(uint8_t id, uint32_t data_ms) {
    Row* r = row(id);
    if (r == nullptr) return nullptr;
    r->known = true;
    if (!r->has_data || newer(data_ms, r->last_data_ms)) r->last_data_ms = data_ms;
    r->has_data = true;
    return r;
}

void Leader::add_r(Row& r, uint32_t now_ms, int8_t value) {
    uint32_t sec = now_ms / 1000;
    Bucket& b = r.r[sec % R_BUCKETS];
    if (b.sec != sec || b.count == 0) b = Bucket{sec, 0, 0};
    b.sum += value;
    b.count++;
}

void Leader::on_self(uint8_t battery_pct, bool sos, uint32_t now_ms) {
    Row* r = touch(self_, now_ms);
    if (r == nullptr) return;
    r->battery_known = true;
    r->battery_pct = battery_pct;
    if (sos) {
        r->sos_ms = now_ms;
        r->sos_seen = true;
    } else {
        r->sos_seen = false;
    }
}

void Leader::on_beacon(const packets::Beacon& b, uint32_t now_ms) {
    Row* r = touch(b.sender, now_ms);
    if (r == nullptr) return;
    r->battery_known = true;
    r->battery_pct = static_cast<uint8_t>(packets::battery_percent(b.battery));
    if (b.sos) {
        r->sos_ms = now_ms;
        r->sos_seen = true;
    } else {
        r->sos_seen = false;  // il corridore l'ha annullato (il capogruppo lo tiene finche' non lo chiude l'app)
    }
}

void Leader::on_record(const packets::TelRecord& rec, uint32_t now_ms, uint32_t age_s) {
    Row* r = touch(rec.id, now_ms - age_s * 1000u);
    if (r == nullptr || age_s > RECORD_FRESH_S) return;  // un record vecchio non descrive lo stato di adesso
    r->battery_known = true;
    r->battery_pct = static_cast<uint8_t>(packets::battery_percent(rec.battery));
    if (rec.sos) r->sos_ms = now_ms;
    r->sos_seen = rec.sos;

    // I flag di allarme di questo osservatore sostituiscono i vecchi: chi non e' nel record non e' piu' riportato.
    for (uint8_t k = 1; k <= MAX_NODES; k++) rows_[k].obs[rec.id] = 0;
    for (uint8_t i = 0; i < rec.n; i++) {
        const packets::TelEntry& e = rec.nb[i];
        Row* target = row(e.id);
        if (target == nullptr || e.id == rec.id) continue;
        target->known = true;
        target->obs[rec.id] = e.alarm;
        add_r(*target, now_ms, e.signal);
    }
}

void Leader::on_alarm(const packets::Alarm& a, uint32_t now_ms) {
    touch(a.sender, now_ms);
    Row* r = row(a.runner);
    if (r == nullptr || a.sender == 0 || a.sender > MAX_NODES) return;
    r->known = true;
    switch (a.type) {
        case packets::AlarmType::PreAlarm:
            r->obs[a.sender] = packets::ALARM_PRE;
            break;
        case packets::AlarmType::Lost:
            r->obs[a.sender] = packets::ALARM_LOST;
            break;
        case packets::AlarmType::End:
            r->obs[a.sender] = 0;
            break;
        case packets::AlarmType::Sos:
            r->sos_ms = now_ms;
            r->sos_seen = true;
            break;
    }
}

bool Leader::observer_valid(uint8_t o, uint32_t now_ms) const {
    if (o == self_) return true;
    const Row& r = rows_[o];
    return r.known && r.has_data && now_ms - r.last_data_ms <= OBSERVER_VALID_MS;
}

ble::RunnerState Leader::compute(uint8_t id, uint32_t now_ms) {
    Row& r = rows_[id];
    int pre_n = 0, lost_n = 0;
    for (uint8_t o = 1; o <= MAX_NODES; o++) {
        if (o == id || r.obs[o] == 0 || !observer_valid(o, now_ms)) continue;
        pre_n++;
        if (r.obs[o] == packets::ALARM_LOST) lost_n++;
    }
    int need_lost = size() <= 2 ? 1 : LOST_MIN_OBSERVERS;
    uint8_t level = lost_n >= need_lost ? 2 : (pre_n >= PRE_MIN_OBSERVERS ? 1 : 0);
    if (level == 0) r.closed_level = 0;           // tornato normale: le chiusure decadono
    if (level <= r.closed_level) level = 0;       // chiuso dall'app

    bool sos_active = r.sos_seen && now_ms - r.sos_ms <= SOS_HOLD_MS;
    if (!sos_active) r.sos_closed = false;        // ha smesso di segnalare: un SOS nuovo si ripropone
    if (sos_active && !r.sos_closed) r.sos_latched = true;

    if (r.sos_latched) return ble::RunnerState::Sos;
    if (level == 2) return ble::RunnerState::Lost;
    if (level == 1) return ble::RunnerState::PreAlarm;
    return ble::RunnerState::Ok;
}

bool Leader::close_alarm(uint8_t id, ble::AlarmKind kind, uint32_t now_ms) {
    Row* r = row(id);
    if (r == nullptr || !r->known) return false;
    switch (kind) {
        case ble::AlarmKind::Sos:
            r->sos_latched = false;
            r->sos_closed = true;
            break;
        case ble::AlarmKind::Lost:
            if (r->closed_level < 2) r->closed_level = 2;
            break;
        case ble::AlarmKind::PreAlarm:
            if (r->closed_level < 1) r->closed_level = 1;
            break;
    }
    r->state = compute(id, now_ms);  // senza evento: e' l'app che ha chiuso
    return true;
}

size_t Leader::tick(uint32_t now_ms, uint32_t run_s, Event* out) {
    size_t n = 0;
    for (uint8_t id = 1; id <= MAX_NODES; id++) {
        Row& r = rows_[id];
        if (!r.known) continue;
        ble::RunnerState prev = r.state;
        ble::RunnerState now = compute(id, now_ms);
        r.state = now;
        if (now == prev) continue;
        using S = ble::RunnerState;
        if (now == S::Sos) {
            out[n++] = Event{run_s, id, ble::EventType::Sos};
        } else if (now == S::Lost && prev != S::Sos) {
            out[n++] = Event{run_s, id, ble::EventType::Lost};
        } else if (now == S::PreAlarm && prev == S::Ok) {
            out[n++] = Event{run_s, id, ble::EventType::PreAlarm};
        } else if (now == S::Ok && (prev == S::PreAlarm || prev == S::Lost)) {
            out[n++] = Event{run_s, id, ble::EventType::Return};
        }
    }
    return n;
}

size_t Leader::size() const {
    size_t n = 0;
    for (uint8_t id = 1; id <= MAX_NODES; id++) n += rows_[id].known ? 1 : 0;
    return n;
}

size_t Leader::snapshot(uint32_t now_ms, ble::Row* out, size_t max) const {
    size_t n = 0;
    uint32_t sec = now_ms / 1000;
    for (uint8_t id = 1; id <= MAX_NODES && n < max; id++) {
        const Row& r = rows_[id];
        if (!r.known) continue;
        int32_t sum = 0;
        int count = 0;
        for (const Bucket& b : r.r) {
            if (b.count > 0 && sec - b.sec < R_BUCKETS) {  // negli ultimi R_REPORT_WINDOW_MS
                sum += b.sum;
                count += b.count;
            }
        }
        int8_t mean = ble::R_ABSENT;
        if (count > 0) {
            int v = static_cast<int>((sum >= 0 ? sum + count / 2 : sum - count / 2) / count);
            mean = static_cast<int8_t>(v < -128 ? -128 : v > 126 ? 126 : v);
        }
        uint32_t age = r.has_data ? (now_ms - r.last_data_ms) / 1000 : 0;
        out[n++] = ble::Row{id, r.state, mean, r.battery_known ? r.battery_pct : static_cast<uint8_t>(0),
                            static_cast<uint16_t>(age > 0xFFFF ? 0xFFFF : age)};
    }
    return n;
}

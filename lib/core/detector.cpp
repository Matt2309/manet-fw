#include "detector.h"

#include <math.h>

namespace {

// Peso minimo delle voci: varianza di un passo di quantizzazione dell'incertezza.
constexpr double MIN_VARIANCE = packets::SIGMA_STEP * packets::SIGMA_STEP / 12.0;
constexpr double SIGMA_A2 = KALMAN_SIGMA_A * KALMAN_SIGMA_A;

void emit(DetEvent* out, size_t& n, uint32_t t_ms, DetKind kind, bool start, uint8_t target, double r) {
    out[n++] = DetEvent{t_ms, kind, start, target, r};
}

}  // namespace

void Detector::on_beacon(const packets::Beacon& b, int8_t rssi, uint32_t now_ms) {
    Peer& p = peer(b.sender);
    p.known = true;
    p.filter.update(now_ms, rssi);

    p.rx[p.rx_next] = now_ms;
    p.rx_next = static_cast<uint16_t>((p.rx_next + 1) % RX_HISTORY_LEN);
    if (p.rx_n < RX_HISTORY_LEN) p.rx_n++;
    p.episode_checked = false;
    p.episode_deaf = false;

    // Si tiene solo l'ultima tabella di ogni mittente.
    for (Entry& e : p.entry) e.valid = false;
    p.table_valid = true;
    p.table_rx_ms = now_ms;
    for (uint8_t i = 0; i < b.n; i++) {
        const packets::BeaconEntry& e = b.nb[i];
        if (e.id == 0 || e.id > MAX_NODES || e.id == self_ || e.id == b.sender) continue;
        p.entry[e.id - 1] = Entry{true, e.signal, e.slope, e.sigma, e.age_ds};
    }
}

bool Detector::fused_level(uint8_t target, uint32_t now_ms, double& level) const {
    const Peer& t = peer(target);
    double sum_w = 0.0, sum_wr = 0.0;

    // Stima propria del collegamento target -> me, predetta a ora.
    if (now_ms - t.filter.last_ms() <= FUSION_MAX_AGE_MS) {
        LinkEstimate x = t.filter.predicted(now_ms);
        double w = 1.0 / fmax(x.p00, MIN_VARIANCE);
        sum_w += w;
        sum_wr += w * x.r;
    }

    // Stime dei vicini sul collegamento target -> k, dall'ultima tabella di k.
    for (uint8_t k = 1; k <= MAX_NODES; k++) {
        if (k == target || k == self_) continue;
        const Peer& pk = peer(k);
        if (!pk.known || !pk.table_valid) continue;
        const Entry& e = pk.entry[target - 1];
        if (!e.valid) continue;
        uint32_t since_rx = now_ms - pk.table_rx_ms;
        if (e.age_q * 100u + since_rx > FUSION_MAX_AGE_MS) continue;
        double age = e.age_q * packets::AGE_STEP + since_rx / 1000.0;
        double value = e.r * packets::SIGNAL_STEP + e.s * packets::SLOPE_STEP * age;
        double std = e.sigma * packets::SIGMA_STEP;
        double var = std * std + SIGMA_A2 * age * age * age / 3.0;
        double w = 1.0 / fmax(var, MIN_VARIANCE);
        sum_w += w;
        sum_wr += w * value;
    }
    if (sum_w <= 0.0) return false;
    level = sum_wr / sum_w;
    return true;
}

int Detector::receptions_in_window(const Peer& p, uint32_t now_ms) const {
    int count = 0;
    for (uint16_t i = 0; i < p.rx_n; i++) {
        if (now_ms - p.rx[i] < RELEASE_TIME_MS) count++;
    }
    return count;
}

// Nessun beacon da altri compagni in (last + periodo, last + silenzio]: e' la scheda a non sentire.
bool Detector::deaf_episode(uint8_t target, uint32_t last_ms) const {
    for (uint8_t k = 1; k <= MAX_NODES; k++) {
        if (k == target || k == self_) continue;
        const Peer& pk = peer(k);
        if (!pk.known) continue;
        for (uint16_t i = 0; i < pk.rx_n; i++) {
            uint32_t d = pk.rx[i] - last_ms;  // un beacon precedente a last_ms dà un valore enorme
            if (d > BEACON_PERIOD_MS && d <= LOST_SILENCE_MS) return false;
        }
    }
    return true;
}

void Detector::tick_pre(uint8_t id, uint32_t now_ms, DetEvent* out, size_t& n) {
    Peer& p = peer(id);
    double level = 0.0;
    bool has = fused_level(id, now_ms, level);

    bool cond = has && level < PRE_ALARM_DBM;
    bool back = has && level > PRE_ALARM_DBM + RELEASE_MARGIN_DB;
    if (cond && !p.cond) p.cond_since = now_ms;
    if (back && !p.back) p.back_since = now_ms;
    p.cond = cond;
    p.back = back;

    bool on = cond && now_ms - p.cond_since >= PRE_ALARM_MIN_MS;
    bool rx_ok = receptions_in_window(p, now_ms) >= RELEASE_MIN_BEACONS;
    bool off = back && now_ms - p.back_since >= RELEASE_TIME_MS && rx_ok;

    if (off && p.pre_on) {
        p.pre_on = false;
        emit(out, n, now_ms, DetKind::PreAlarm, false, id, level);
    } else if (on && !off && !p.pre_on) {
        p.pre_on = true;
        emit(out, n, now_ms, DetKind::PreAlarm, true, id, level);
    }
}

void Detector::tick_lost(uint8_t id, uint32_t now_ms, DetEvent* out, size_t& n) {
    Peer& p = peer(id);
    uint32_t last = p.filter.last_ms();
    bool silent = now_ms - last > LOST_SILENCE_MS;

    if (silent && !p.episode_checked) {
        p.episode_checked = true;
        p.episode_deaf = deaf_episode(id, last);
        if (p.episode_deaf) {
            p.deaf_on = true;
            emit(out, n, now_ms, DetKind::Deaf, true, id, 0.0);
        }
    }
    if (!silent && p.deaf_on) {
        p.deaf_on = false;
        emit(out, n, now_ms, DetKind::Deaf, false, id, 0.0);
    }

    bool on = silent && !p.episode_deaf;
    bool rx_ok = receptions_in_window(p, now_ms) >= RELEASE_MIN_BEACONS;
    if (rx_ok && p.lost_on) {
        p.lost_on = false;
        emit(out, n, now_ms, DetKind::Lost, false, id, 0.0);
    } else if (on && !rx_ok && !p.lost_on) {
        p.lost_on = true;
        emit(out, n, now_ms, DetKind::Lost, true, id, 0.0);
    }
}

size_t Detector::tick(uint32_t now_ms, DetEvent* out) {
    size_t n = 0;
    for (uint8_t id = 1; id <= MAX_NODES; id++) {
        if (id == self_ || !peer(id).known) continue;
        tick_pre(id, now_ms, out, n);
        tick_lost(id, now_ms, out, n);
    }
    return n;
}

uint8_t Detector::fill_entries(uint32_t now_ms, packets::BeaconEntry* out) const {
    uint8_t count = 0;
    for (uint8_t id = 1; id <= MAX_NODES; id++) {
        const Peer& p = peer(id);
        if (id == self_ || !p.known) continue;
        const LinkEstimate& x = p.filter.posterior();
        out[count++] = packets::BeaconEntry{id, packets::quantize_i8(x.r, packets::SIGNAL_STEP),
                                            packets::quantize_slope(x.s),
                                            packets::quantize_u8(sqrt(x.p00), packets::SIGMA_STEP),
                                            packets::age_ds(now_ms - p.filter.last_ms())};
    }
    return count;
}

bool Detector::estimate(uint8_t id, uint32_t now_ms, LinkView& v) const {
    if (id == 0 || id > MAX_NODES || id == self_) return false;
    const Peer& p = peer(id);
    if (!p.known) return false;
    LinkEstimate x = p.filter.predicted(now_ms);
    v = LinkView{x.r, x.s, sqrt(x.p00), now_ms - p.filter.last_ms()};
    return true;
}

uint8_t Detector::pre_count() const {
    uint8_t n = 0;
    for (const Peer& p : peers_) n += p.pre_on ? 1 : 0;
    return n;
}

uint8_t Detector::lost_count() const {
    uint8_t n = 0;
    for (const Peer& p : peers_) n += p.lost_on ? 1 : 0;
    return n;
}

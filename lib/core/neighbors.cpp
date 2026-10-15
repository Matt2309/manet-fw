#include "neighbors.h"

bool NeighborTable::update(uint8_t id, int8_t rssi, uint32_t now_ms) {
    for (size_t i = 0; i < n_; i++) {
        if (s_[i].id == id) {
            s_[i].rssi = rssi;
            s_[i].last_ms = now_ms;
            s_[i].count++;
            return true;
        }
    }
    if (n_ >= beacon::MAX_NEIGHBORS) return false;
    s_[n_] = Neighbor{id, rssi, now_ms, 1};
    n_++;
    return true;
}

const Neighbor* NeighborTable::find(uint8_t id) const {
    for (size_t i = 0; i < n_; i++) {
        if (s_[i].id == id) return &s_[i];
    }
    return nullptr;
}

uint8_t NeighborTable::fill_entries(uint8_t self_id, uint32_t now_ms, beacon::Entry* out) const {
    uint8_t count = 0;
    // Ordine di id crescente: si estrae ogni volta il minimo maggiore dell'ultimo.
    int last = -1;
    for (size_t k = 0; k < n_; k++) {
        const Neighbor* best = nullptr;
        for (size_t i = 0; i < n_; i++) {
            if (s_[i].id == self_id || static_cast<int>(s_[i].id) <= last) continue;
            if (best == nullptr || s_[i].id < best->id) best = &s_[i];
        }
        if (best == nullptr) break;
        out[count++] = beacon::Entry{best->id, best->rssi,
                                     beacon::age_ds(now_ms - best->last_ms),
                                     beacon::VARIANCE_NA};
        last = best->id;
    }
    return count;
}

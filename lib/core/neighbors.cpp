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
    if (n_ >= packets::MAX_NEIGHBORS) return false;
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

uint8_t NeighborTable::heard(uint32_t now_ms, uint32_t window_ms) const {
    uint8_t count = 0;
    for (size_t i = 0; i < n_; i++) {
        if (now_ms - s_[i].last_ms <= window_ms) count++;
    }
    return count;
}

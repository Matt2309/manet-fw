// Tabella dei vicini: ultimo RSSI, istante dell'ultimo beacon e pacchetti ricevuti.
// Non e' thread-safe: appartiene al task della logica.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "packets.h"

struct Neighbor {
    uint8_t id;
    int8_t rssi;
    uint32_t last_ms;
    uint32_t count;
    uint8_t hops;  // salti dal capogruppo dichiarati nell'ultimo beacon
};

class NeighborTable {
public:
    // Registra un beacon ricevuto; false se la tabella e' piena e l'id e' nuovo.
    bool update(uint8_t id, int8_t rssi, uint32_t now_ms, uint8_t hops = packets::HOPS_ISOLATED);

    // Cerca un vicino; nullptr se mai ricevuto.
    const Neighbor* find(uint8_t id) const;

    size_t size() const { return n_; }
    const Neighbor& at(size_t i) const { return s_[i]; }

    // Quanti vicini hanno mandato un beacon negli ultimi window_ms.
    uint8_t heard(uint32_t now_ms, uint32_t window_ms) const;

private:
    Neighbor s_[packets::MAX_NEIGHBORS] = {};
    size_t n_ = 0;
};

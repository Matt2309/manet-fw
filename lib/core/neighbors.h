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
};

class NeighborTable {
public:
    // Registra un beacon ricevuto; false se la tabella e' piena e l'id e' nuovo.
    bool update(uint8_t id, int8_t rssi, uint32_t now_ms);

    // Cerca un vicino; nullptr se mai ricevuto.
    const Neighbor* find(uint8_t id) const;

    size_t size() const { return n_; }

    // Quanti vicini hanno mandato un beacon negli ultimi window_ms.
    uint8_t heard(uint32_t now_ms, uint32_t window_ms) const;

    // Compila le voci di un beacon: tutti i vicini noti tranne self_id, in ordine
    // di id crescente, con l'eta' calcolata a now_ms. Il segnale e' l'ultimo RSSI,
    // pendenza e incertezza non ci sono ancora. Restituisce il numero di voci.
    uint8_t fill_entries(uint8_t self_id, uint32_t now_ms, packets::BeaconEntry* out) const;

private:
    Neighbor s_[packets::MAX_NEIGHBORS] = {};
    size_t n_ = 0;
};

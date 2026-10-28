// Stato della logica di una scheda, senza hardware: vicini, ruolo, beacon da mandare.
//
// Appartiene a un solo task; il tempo entra come parametro (millisecondi dall'accensione).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "detector.h"
#include "neighbors.h"
#include "packets.h"
#include "role.h"

// Copia dello stato per chi lo mostra (display).
struct NodeStatus {
    uint8_t id;
    Role role;
    uint8_t hops;         // salti dal capogruppo, HOPS_ISOLATED se nessuno
    uint8_t heard;        // compagni sentiti di recente
    bool run_started;
    bool two_leaders;     // capogruppo che sente un altro capogruppo prima dello START
    uint8_t pre;          // compagni in pre-allarme
    uint8_t lost;         // compagni dichiarati persi
};

class Node {
public:
    Node(uint8_t id, Role role) : id_(id), role_(role), detector_(id) {}

    // Elabora un pacchetto ricevuto. Scarta intestazioni sbagliate, mittenti non validi
    // e i propri pacchetti. true (e b riempito) solo per un beacon accettato; gli altri
    // tipi arrivano dalle fasi successive.
    bool on_packet(const uint8_t* data, size_t len, int8_t rssi, uint32_t now_ms, packets::Beacon& b);

    // Prepara il prossimo beacon; sent e' la copia decodificata, per il registro.
    // Restituisce i byte scritti in out.
    size_t build_beacon(uint32_t now_ms, int battery_pct, uint8_t* out, size_t cap, packets::Beacon& sent);

    // Controlli a tempo (ogni LOGIC_TICK_MS): scrive in out (Detector::MAX_EVENTS posti) le
    // decisioni del rilevatore cambiate e restituisce quante sono.
    size_t tick(uint32_t now_ms, DetEvent* out) { return detector_.tick(now_ms, out); }

    // Stima del collegamento compagno -> me, per il registro; false se mai sentito.
    bool estimate(uint8_t id, uint32_t now_ms, LinkView& v) const { return detector_.estimate(id, now_ms, v); }

    // Alterna il ruolo se la regola lo permette.
    bool toggle_role(uint32_t now_ms);

    Role role() const { return role_; }
    NodeStatus status(uint32_t now_ms) const;

private:
    uint8_t hops() const;

    uint8_t id_;
    Role role_;
    uint16_t seq_ = 0;
    NeighborTable table_;
    Detector detector_;
    bool leader_seen_ = false;
    uint32_t leader_seen_ms_ = 0;
    bool run_started_ = false;  // lo fissa START, dalla fase 4
};

// Rilevatore di una scheda (stesso di src/detector.py in manet-sim): per ogni compagno
// un Kalman, la fusione con le stime dei vicini, pre-allarme e nodo perso.
//
// Appartiene a un solo task; il tempo entra come parametro (ms dall'accensione).
// Compagni indicizzati dall'ID (1..MAX_NODES); solo quelli sentiti almeno una volta contano.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "kalman.h"
#include "packets.h"

enum class DetKind : uint8_t { PreAlarm, Lost, Deaf };

// Una decisione: inizio o fine di pre-allarme, nodo perso o scheda sorda per un compagno.
struct DetEvent {
    uint32_t t_ms;
    DetKind kind;
    bool start;
    uint8_t target;
    double r;  // segnale fuso, solo per il pre-allarme
};

// Stima del collegamento compagno -> me, predetta a un istante.
struct LinkView {
    double r;       // dBm
    double s;       // dB/s
    double sigma;   // dB
    uint32_t age_ms;  // dall'ultimo beacon
};

class Detector {
public:
    // Eventi per tick: al piu' pre-allarme, perso e sorda per ogni compagno.
    static constexpr size_t MAX_EVENTS = 3 * packets::MAX_NEIGHBORS;

    explicit Detector(uint8_t self_id) : self_(self_id) {}

    // Beacon ricevuto da b.sender (gia' validato): aggiorna il Kalman, lo storico delle
    // ricezioni e la tabella di quel mittente.
    void on_beacon(const packets::Beacon& b, int8_t rssi, uint32_t now_ms);

    // Controlli a tempo (ogni LOGIC_TICK_MS). Scrive in out (MAX_EVENTS posti) le decisioni
    // cambiate e restituisce quante sono.
    size_t tick(uint32_t now_ms, DetEvent* out);

    // Voci del beacon: la stima a posteriori di ogni compagno sentito, in ordine di ID.
    uint8_t fill_entries(uint32_t now_ms, packets::BeaconEntry* out) const;

    // Stima del compagno id predetta a now_ms; false se mai sentito.
    bool estimate(uint8_t id, uint32_t now_ms, LinkView& v) const;

    uint8_t pre_count() const;
    uint8_t lost_count() const;

private:
    // Voce di una tabella ricevuta: stima del collegamento (compagno -> mittente).
    struct Entry {
        bool valid;
        int8_t r;
        int8_t s;
        uint8_t sigma;
        uint8_t age_q;
    };

    struct Peer {
        bool known;
        LinkFilter filter;
        // Istanti delle ultime ricezioni (anello): servono a rx_ok e alla scheda sorda.
        uint32_t rx[RX_HISTORY_LEN];
        uint16_t rx_next;
        uint16_t rx_n;
        // Ultima tabella ricevuta da questo compagno.
        bool table_valid;
        uint32_t table_rx_ms;
        Entry entry[MAX_NODES];
        // Pre-allarme su questo compagno.
        bool cond;
        uint32_t cond_since;
        bool back;
        uint32_t back_since;
        bool pre_on;
        // Nodo perso e scheda sorda. L'episodio di silenzio si valuta una volta sola.
        bool episode_checked;
        bool episode_deaf;
        bool deaf_on;
        bool lost_on;
    };

    Peer& peer(uint8_t id) { return peers_[id - 1]; }
    const Peer& peer(uint8_t id) const { return peers_[id - 1]; }

    bool fused_level(uint8_t target, uint32_t now_ms, double& level) const;
    int receptions_in_window(const Peer& p, uint32_t now_ms) const;
    bool deaf_episode(uint8_t target, uint32_t last_ms) const;
    void tick_pre(uint8_t id, uint32_t now_ms, DetEvent* out, size_t& n);
    void tick_lost(uint8_t id, uint32_t now_ms, DetEvent* out, size_t& n);

    uint8_t self_;
    Peer peers_[MAX_NODES] = {};
};

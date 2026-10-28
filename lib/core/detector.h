// Rilevatore di una scheda (stesso di src/detector.py in manet-sim): per ogni compagno
// un Kalman, la fusione con le stime dei vicini, pre-allarme e nodo perso.
//
// Appartiene a un solo task; il tempo entra come parametro (ms dall'accensione).
// Compagni indicizzati dall'ID (1..MAX_NODES); solo quelli sentiti almeno una volta contano.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "fix.h"
#include "kalman.h"
#include "packets.h"

enum class DetKind : uint8_t { PreAlarm, Lost, Deaf };
// Rilevatore senza GNSS e con il ramo GNSS. Nodo perso e scheda sorda sono gli stessi: si emettono come Base.
enum class DetVersion : uint8_t { Base = 0, Gnss = 1 };

// Una decisione: inizio o fine di pre-allarme, nodo perso o scheda sorda per un compagno.
struct DetEvent {
    uint32_t t_ms;
    DetVersion version;
    DetKind kind;
    bool start;
    uint8_t target;
    double r;          // segnale fuso, solo per il pre-allarme
    double threshold;  // soglia di pre-allarme in vigore, solo per il pre-allarme
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
    // Eventi per tick: al piu' pre-allarme (due versioni), perso e sorda per ogni compagno.
    static constexpr size_t MAX_EVENTS = 4 * packets::MAX_NEIGHBORS;

    explicit Detector(uint8_t self_id) : self_(self_id) {}

    // Beacon ricevuto da b.sender (gia' validato): aggiorna il Kalman, lo storico delle
    // ricezioni e la tabella di quel mittente.
    void on_beacon(const packets::Beacon& b, int8_t rssi, uint32_t now_ms);

    // Ultimo campione GNSS proprio (valido o no).
    void set_own_fix(const GnssSample& s) { own_fix_ = s; }

    // Posizione propria se il fix e' valido e piu' giovane di GNSS_MAX_AGE_MS.
    bool own_position(uint32_t now_ms, packets::Position& out) const;

    // Controlli a tempo (ogni LOGIC_TICK_MS). Scrive in out (MAX_EVENTS posti) le decisioni
    // cambiate e restituisce quante sono.
    size_t tick(uint32_t now_ms, DetEvent* out);

    // Voci del beacon: la stima a posteriori di ogni compagno sentito, in ordine di ID.
    uint8_t fill_entries(uint32_t now_ms, packets::BeaconEntry* out) const;

    // Stima del compagno id predetta a now_ms; false se mai sentito.
    bool estimate(uint8_t id, uint32_t now_ms, LinkView& v) const;

    // Voce di telemetria per il compagno id: segnale fuso (o, se non c'e', la stima propria predetta), incertezza,
    // eta' dall'ultimo beacon e codice di allarme. false se mai sentito.
    bool report(uint8_t id, uint32_t now_ms, packets::TelEntry& e) const;

    // Allarme in corso sul compagno, nella versione scelta da ALARM_USES_GNSS.
    uint8_t alarm_code(uint8_t id) const;

    // Istante dell'ultimo beacon del compagno; false se mai sentito.
    bool last_contact(uint8_t id, uint32_t& last_ms) const;

    // Compagni in pre-allarme: nella versione data, o in quella scelta da ALARM_USES_GNSS.
    uint8_t pre_count(DetVersion v) const;
    uint8_t pre_count() const { return pre_count(ALARM_USES_GNSS ? DetVersion::Gnss : DetVersion::Base); }
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

    // Pre-allarme di una versione: condizione corrente e allarme.
    struct PreState {
        bool cond;
        uint32_t cond_since;
        bool on;
    };

    // Un campione per il ramo GNSS: distanza dal compagno e segnale fuso.
    struct GnssRow {
        uint32_t t_ms;
        float d;
        float r;
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
        // Ultimo segnale fuso calcolato (per la telemetria).
        bool fused_ok;
        double fused_r;
        double fused_sigma;
        // Pre-allarme su questo compagno, per le due versioni. Il ritorno al segnale buono e' lo stesso.
        PreState pre[2];
        bool back;
        uint32_t back_since;
        // Posizione dell'ultimo beacon (assente se il compagno non ha un fix valido) e storia per il ramo GNSS.
        bool has_pos;
        packets::Position pos;
        uint32_t pos_ms;
        GnssRow hist[GNSS_HISTORY_LEN];
        uint8_t hist_n;
        uint8_t hist_next;
        // Nodo perso e scheda sorda. L'episodio di silenzio si valuta una volta sola.
        bool episode_checked;
        bool episode_deaf;
        bool deaf_on;
        bool lost_on;
    };

    Peer& peer(uint8_t id) { return peers_[id - 1]; }
    const Peer& peer(uint8_t id) const { return peers_[id - 1]; }

    bool fused_level(uint8_t target, uint32_t now_ms, double& level, double& sigma) const;
    int receptions_in_window(const Peer& p, uint32_t now_ms) const;
    bool deaf_episode(uint8_t target, uint32_t last_ms) const;
    void gnss_rule(Peer& p, uint32_t now_ms, bool has, double level, double& threshold, uint32_t& min_ms);
    void step_pre(Peer& p, uint8_t id, uint32_t now_ms, DetVersion v, bool has, double level, double threshold,
                  uint32_t min_ms, bool off, DetEvent* out, size_t& n);
    void tick_pre(uint8_t id, uint32_t now_ms, DetEvent* out, size_t& n);
    void tick_lost(uint8_t id, uint32_t now_ms, DetEvent* out, size_t& n);

    uint8_t self_;
    GnssSample own_fix_ = {};
    Peer peers_[MAX_NODES] = {};
};

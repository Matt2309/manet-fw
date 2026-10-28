// Tabella del gruppo sul capogruppo: una riga per corridore, aggiornata da beacon, allarmi e telemetria.
//
// Un corridore sentito almeno una volta resta in tabella fino allo STOP (il totale dei corridori e' il numero
// di righe, compreso il capogruppo). Stato di una riga, per precedenza: SOS, perso, pre-allarme, ok.
//  - pre-allarme: almeno PRE_MIN_OBSERVERS osservatori lo riportano;
//  - perso: almeno LOST_MIN_OBSERVERS lo confermano (uno solo se il gruppo ha 2 schede);
//  - SOS: una volta visto resta finche' non lo si chiude dall'app (anche se il corridore lo annulla).
// La chiusura di un allarme dall'app resta qui: l'allarme (e quelli piu' lievi) non si ripropone finche' il
// corridore non torna normale; un SOS chiuso non si ripropone finche' il corridore non smette di segnalarlo.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ble_codec.h"
#include "config.h"
#include "packets.h"

class Leader {
public:
    // Cambio di stato da comunicare all'app (EVENTO). t_s sono i secondi da T0.
    struct Event {
        uint32_t t_s;
        uint8_t id;
        ble::EventType type;
    };
    static constexpr size_t MAX_EVENTS = 2 * MAX_NODES;

    void set_self(uint8_t id);
    // STOP: azzera la tabella (resta solo la riga del capogruppo).
    void reset();
    // START: toglie allarmi e chiusure, tiene le righe gia' sentite.
    void clear_alarms();

    // Dati propri (batteria, SOS) e beacon dei compagni sentiti direttamente.
    void on_self(uint8_t battery_pct, bool sos, uint32_t now_ms);
    void on_beacon(const packets::Beacon& b, uint32_t now_ms);
    // Record di telemetria; age_s = secondi trascorsi da quando e' stato prodotto (stima del capogruppo).
    void on_record(const packets::TelRecord& r, uint32_t now_ms, uint32_t age_s);
    void on_alarm(const packets::Alarm& a, uint32_t now_ms);

    // CHIUDI_ALLARME dall'app. false se il corridore non e' in tabella. Idempotente, nessun evento.
    bool close_alarm(uint8_t id, ble::AlarmKind kind, uint32_t now_ms);

    // Aggiorna gli stati e restituisce in out (MAX_EVENTS posti) i cambi: PRE_ALLARME, PERSO, SOS, RIENTRO
    // (quest'ultimo solo quando un allarme si spegne da solo, mai per l'SOS).
    size_t tick(uint32_t now_ms, uint32_t run_s, Event* out);

    // Una riga per corridore, anche per quelli non piu' sentiti; restituisce quante.
    size_t snapshot(uint32_t now_ms, ble::Row* out, size_t max) const;
    size_t size() const;

private:
    static constexpr size_t R_BUCKETS = R_REPORT_WINDOW_MS / 1000;

    struct Bucket {
        uint32_t sec;
        int32_t sum;
        uint16_t count;
    };

    struct Row {
        bool known;
        bool has_data;
        uint32_t last_data_ms;
        bool battery_known;
        uint8_t battery_pct;
        Bucket r[R_BUCKETS];
        uint8_t obs[MAX_NODES + 1];  // allarme riportato da ogni osservatore su questo corridore: 0, ALARM_PRE, ALARM_LOST
        uint8_t closed_level;        // allarmi chiusi dall'app: 0 nessuno, 1 pre-allarme, 2 perso (e pre-allarme)
        uint32_t sos_ms;
        bool sos_seen;
        bool sos_latched;
        bool sos_closed;
        ble::RunnerState state;
    };

    Row* row(uint8_t id) { return id >= 1 && id <= MAX_NODES ? &rows_[id] : nullptr; }
    Row* touch(uint8_t id, uint32_t data_ms);
    void add_r(Row& r, uint32_t now_ms, int8_t value);
    bool observer_valid(uint8_t o, uint32_t now_ms) const;
    ble::RunnerState compute(uint8_t id, uint32_t now_ms);

    uint8_t self_ = 0;
    Row rows_[MAX_NODES + 1] = {};
};

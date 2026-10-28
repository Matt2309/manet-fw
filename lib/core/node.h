// Stato della logica di una scheda, senza hardware: vicini, rilevatore, corsa, telemetria, allarmi.
//
// Appartiene a un solo task; il tempo entra come parametro (millisecondi dall'accensione).
// L'adattatore radio fa tre cose: passa i pacchetti ricevuti (on_packet), chiama tick() ogni LOGIC_TICK_MS
// e svuota le uscite (pop_outgoing), riportando l'esito degli invii con conferma (on_send_result).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ble_codec.h"
#include "config.h"
#include "detector.h"
#include "leader.h"
#include "log_types.h"
#include "neighbors.h"
#include "packets.h"
#include "record_queue.h"
#include "role.h"
#include "run.h"
#include "seen.h"

// Pacchetto da mandare.
struct Outgoing {
    bool unicast;   // true: a dest, con conferma; false: a tutti
    uint8_t dest;   // ID del destinatario (unicast)
    uint16_t len;
    uint8_t data[packets::MAX_SIZE];
};

// Messaggio per il task BLE (solo capogruppo). La logica assegna il seq e codifica i byte col codec; il task BLE
// scrive nel file della corsa (se save) e poi li manda.
struct BleMsg {
    enum class Kind : uint8_t {
        Data,     // canale dati: STATO o CONFERMA
        Urgent,   // canale urgenti: EVENTO
        NewRun,   // START: nuovo file della corsa
        EndRun,   // STOP: chiude il file
        Resend,   // RIPRENDI_DA: rimanda i messaggi salvati con from_seq < seq <= to_seq
    };
    Kind kind;
    bool save;
    uint32_t from_seq;
    uint32_t to_seq;
    uint16_t len;
    uint8_t data[ble::MAX_MESSAGE_SIZE];
};

// Riga di registro prodotta dalla logica (l'adattatore la scrive nel file).
struct NodeLog {
    enum class Kind : uint8_t { Tel, Alm, Cmd };
    Kind kind;
    uint32_t t_ms;
    union {
        struct {
            TelAction action;
            uint8_t id;
            uint16_t t_s;
            uint8_t peer;
            uint16_t queued;
            uint8_t tries;
            int32_t ms;  // tempo di conferma, -1 se non c'e'
        } tel;
        struct {
            AlmAction action;
            uint8_t creator;
            uint8_t seq;
            uint8_t runner;
            uint8_t type;
        } alm;
        struct {
            packets::Cmd cmd;
            CmdOrigin origin;
            uint8_t seq;
            uint16_t elapsed_ds;
        } cmd;
    } u;
};

// Copia dello stato per chi lo mostra (display).
struct NodeStatus {
    uint8_t id;
    Role role;
    uint8_t hops;         // salti dal capogruppo, HOPS_ISOLATED se nessuno
    uint8_t heard;        // compagni sentiti di recente
    bool run_started;     // corsa avviata
    RunState run;
    uint32_t run_s;       // secondi da T0 (0 se non avviata)
    bool sos;             // SOS inviato da questa scheda
    bool isolated;        // corsa avviata e nessuna via verso il capogruppo
    bool two_leaders;     // capogruppo che sente un altro capogruppo prima dello START
    uint8_t pre;          // compagni in pre-allarme
    uint8_t lost;         // compagni dichiarati persi
    uint8_t group;        // capogruppo: corridori in tabella
};

class Node {
public:
    enum class CmdResult : uint8_t { Done, Already, NotLeader };

    Node(uint8_t id, Role role)
        : id_(id), role_(role), detector_(id), hops_(role == Role::Leader ? 0 : packets::HOPS_ISOLATED) {
        if (role == Role::Leader) leader_.set_self(id);
    }

    // Modalita' prova F2 (di default quella della build, vedi config.h): invece della telemetria, un unicast al secondo.
    void set_f2(bool on) { f2_ = on; }

    // Seme del generatore casuale dei ritardi di rilancio (l'adattatore passa un numero casuale).
    void seed(uint32_t s) { rng_ = s != 0 ? s : 1; }

    // --- Radio ---------------------------------------------------------------------------------------------
    // Elabora un pacchetto ricevuto. Scarta intestazioni sbagliate, mittenti non validi e i propri pacchetti.
    // true (e b riempito) solo per un beacon accettato; gli altri tipi si elaborano qui dentro.
    bool on_packet(const uint8_t* data, size_t len, int8_t rssi, uint32_t now_ms, packets::Beacon& b);

    // Prepara il prossimo beacon; sent e' la copia decodificata, per il registro.
    // Restituisce i byte scritti in out.
    size_t build_beacon(uint32_t now_ms, int battery_pct, uint8_t* out, size_t cap, packets::Beacon& sent);

    // Controlli a tempo (ogni LOGIC_TICK_MS). Scrive in det (Detector::MAX_EVENTS posti) le decisioni del
    // rilevatore cambiate e restituisce quante sono.
    size_t tick(uint32_t now_ms, DetEvent* det);

    // Prossimo pacchetto da mandare, se e' ora. Chiamare finche' restituisce true.
    bool pop_outgoing(uint32_t now_ms, Outgoing& o);
    // Esito dell'ultimo invio unicast (conferma ESP-NOW).
    void on_send_result(bool ok, uint32_t now_ms);
    // Prossimo messaggio per il BLE (capogruppo).
    bool pop_ble(BleMsg& m);
    // Comando ricevuto dall'app via BLE (capogruppo): lo esegue e prepara la CONFERMA.
    void on_app_command(const ble::Command& c, uint32_t now_ms);
    // Dopo un riavvio a corsa in corso: l'ultimo seq salvato nel file. Il primo T0 ricavato dai compagni
    // continua quella corsa invece di aprirne una nuova.
    void resume_ble_seq(uint32_t last_seq);
    // Prossima riga di registro prodotta dalla logica.
    bool pop_log(NodeLog& item);

    // --- Ingressi dal resto del firmware --------------------------------------------------------------------
    // Campione GNSS proprio (a ogni epoca): posizione nel beacon e ramo GNSS del rilevatore.
    void on_gnss(const GnssSample& s) { detector_.set_own_fix(s); }
    // Comandi dall'app (o dalla seriale) sul capogruppo. Idempotenti.
    CmdResult start_run(uint32_t now_ms);
    CmdResult stop_run(uint32_t now_ms);
    // Pressione lunga: attiva o annulla l'SOS. false se la corsa non e' avviata.
    bool toggle_sos(uint32_t now_ms);
    // Alterna il ruolo se la regola lo permette.
    bool toggle_role(uint32_t now_ms);

    Role role() const { return role_; }
    NodeStatus status(uint32_t now_ms) const;
    // Stima del collegamento compagno -> me, per il registro; false se mai sentito.
    bool estimate(uint8_t id, uint32_t now_ms, LinkView& v) const { return detector_.estimate(id, now_ms, v); }
    size_t queue_size() const { return queue_.size(); }

private:
    static constexpr size_t OUTBOX_LEN = 10;
    static constexpr size_t LOG_LEN = 32;
    static constexpr size_t BLE_QUEUE_LEN = 12;

    struct OutEntry {
        bool used;
        bool unicast;
        uint8_t dest;
        uint32_t due_ms;
        bool refresh_cmd;  // rilancio di un comando: i decimi trascorsi si aggiornano all'invio
        packets::Command cmd;
        uint32_t rx_ms;
        uint16_t len;
        uint8_t data[packets::MAX_SIZE];
    };

    // Invio di telemetria in corso (un solo pacchetto alla volta).
    struct Inflight {
        bool active;
        bool f2;
        uint8_t dest;
        uint8_t cands[2];
        uint8_t n_cands;
        uint8_t cand_idx;
        uint8_t attempt;
        size_t packed;
        uint32_t last_serial;
        uint8_t first_id;
        uint16_t first_t_s;
        uint16_t len;
        uint8_t data[packets::MAX_SIZE];
    };

    bool on_beacon(const uint8_t* data, size_t len, int8_t rssi, uint32_t now_ms, packets::Beacon& b);
    void on_telemetry(const uint8_t* data, size_t len, uint32_t now_ms);
    void on_alarm(const uint8_t* data, size_t len, uint32_t now_ms);
    void on_command(const uint8_t* data, size_t len, uint32_t now_ms);

    void apply_run(Run::Change change, uint32_t now_ms);
    void begin_run(uint32_t now_ms, bool local);
    void end_run();
    void push_ble(BleMsg::Kind kind, bool save, const uint8_t* data, size_t len);
    void push_state(uint32_t now_ms);
    void lead_tick(uint32_t now_ms);

    uint8_t compute_hops(uint32_t now_ms) const;
    size_t candidates(uint32_t now_ms, uint8_t out[2]) const;
    void produce_record(uint32_t now_ms);
    void consume_record(const packets::TelRecord& r, uint32_t now_ms);
    void track_alarms(uint32_t now_ms);
    void create_alarm(uint8_t runner, packets::AlarmType type, uint8_t signal, uint32_t now_ms);
    void pump_uplink(uint32_t now_ms);
    void f2_tick(uint32_t now_ms);
    void send_attempt(uint32_t now_ms);

    int push_out(bool unicast, uint8_t dest, const uint8_t* data, size_t len, uint32_t due_ms);
    void log_tel(uint32_t now_ms, TelAction action, uint8_t id, uint16_t t_s, uint8_t peer, uint8_t tries, int32_t ms);
    void log_alm(uint32_t now_ms, AlmAction action, uint8_t creator, uint8_t seq, uint8_t runner, uint8_t type);
    void log_cmd(uint32_t now_ms, packets::Cmd cmd, CmdOrigin origin, uint8_t seq, uint16_t elapsed_ds);
    NodeLog* new_log(NodeLog::Kind kind, uint32_t now_ms);
    uint32_t rand_below(uint32_t n);

    uint8_t id_;
    Role role_;
    uint16_t seq_ = 0;
    NeighborTable table_;
    Detector detector_;
    Run run_;
    bool leader_seen_ = false;
    uint32_t leader_seen_ms_ = 0;
    int battery_pct_ = 100;
    uint8_t hops_;

    // Telemetria
    RecordQueue queue_;
    SeenRecords seen_records_;
    packets::TelRecord batch_[packets::MAX_TEL_RECORDS];
    uint32_t next_record_ms_ = 0;
    uint32_t last_degraded_ms_ = 0;
    uint32_t next_send_ms_ = 0;
    uint32_t next_queue_log_ms_ = 0;
    Inflight inflight_ = {};
    bool awaiting_ = false;       // un unicast e' partito e manca l'esito
    uint32_t sent_ms_ = 0;
    uint32_t next_f2_ms_ = 0;
    uint8_t last_f2_dest_ = 0;

    // Allarmi, comandi, SOS
    SeenSeq alarm_seen_;
    SeenSeq cmd_seen_;
    uint8_t alarm_seq_ = 0;
    uint8_t cmd_seq_ = 0;
    uint8_t last_alarm_code_[MAX_NODES + 1] = {};
    bool sos_ = false;
    uint32_t next_sos_ms_ = 0;

    OutEntry out_[OUTBOX_LEN] = {};
    NodeLog log_[LOG_LEN] = {};
    size_t log_head_ = 0;
    size_t log_count_ = 0;
    // Capogruppo
    Leader leader_;
    uint32_t ble_seq_ = 0;          // ultimo seq assegnato a STATO/EVENTO
    bool resume_pending_ = false;
    uint32_t next_state_ms_ = 0;
    uint32_t next_self_ms_ = 0;
    BleMsg ble_[BLE_QUEUE_LEN] = {};
    size_t ble_head_ = 0;
    size_t ble_count_ = 0;

    uint32_t rng_ = 2463534242u;
    bool f2_ = F2_TEST;
};

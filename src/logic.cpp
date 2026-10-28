#include "logic.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <esp_system.h>
#include <string.h>

#include "app.h"
#include "ble.h"
#include "ble_codec.h"
#include "config.h"
#include "logger.h"
#include "radio.h"
#include "runlog.h"

volatile uint32_t g_dup_id_ms = 0;

namespace {

Node* g_node_logic = nullptr;
QueueHandle_t g_cmd_queue = nullptr;     // verso la logica: comandi
QueueHandle_t g_gnss_queue = nullptr;    // dal task GNSS: un campione per epoca
QueueHandle_t g_status_queue = nullptr;  // verso la UI: un solo elemento, sovrascritto

bool g_two_leaders = false;
uint32_t g_unexpected_rate = 0;
uint32_t g_last_rate_log = 0;
uint32_t g_last_dup_log = 0;

bool due(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

void publish_status(uint32_t now) {
    NodeStatus s = g_node_logic->status(now);
    xQueueOverwrite(g_status_queue, &s);
    if (s.two_leaders != g_two_leaders) {
        g_two_leaders = s.two_leaders;
        logger::post_sys("%s", g_two_leaders ? "due capogruppo" : "un solo capogruppo");
    }
}

// Righe di registro prodotte dalla logica.
void post_node_log(const NodeLog& l) {
    LogEvent e;
    memset(&e, 0, sizeof(e));
    e.t_ms = l.t_ms;
    switch (l.kind) {
        case NodeLog::Kind::Tel:
            e.type = LogEventType::Tel;
            e.u.tel.action = l.u.tel.action;
            e.u.tel.id = l.u.tel.id;
            e.u.tel.t_s = l.u.tel.t_s;
            e.u.tel.peer = l.u.tel.peer;
            e.u.tel.queued = l.u.tel.queued;
            e.u.tel.tries = l.u.tel.tries;
            e.u.tel.ms = l.u.tel.ms;
            break;
        case NodeLog::Kind::Alm:
            e.type = LogEventType::Alm;
            e.u.alm.action = l.u.alm.action;
            e.u.alm.creator = l.u.alm.creator;
            e.u.alm.seq = l.u.alm.seq;
            e.u.alm.runner = l.u.alm.runner;
            e.u.alm.type = static_cast<packets::AlarmType>(l.u.alm.type);
            break;
        case NodeLog::Kind::Cmd:
            e.type = LogEventType::Cmd;
            e.u.cmd.cmd = l.u.cmd.cmd;
            e.u.cmd.origin = l.u.cmd.origin;
            e.u.cmd.seq = l.u.cmd.seq;
            e.u.cmd.elapsed_ds = l.u.cmd.elapsed_ds;
            break;
    }
    logger::post(e);
}

// Manda quanto la logica ha preparato (allarmi, comandi, telemetria) e scrive le sue righe di registro.
void drain_node(uint32_t now) {
    Outgoing o;
    while (g_node_logic->pop_outgoing(now, o)) {
        if (o.unicast) {
            if (!radio::send_to(o.dest, o.data, o.len)) g_node_logic->on_send_result(false, now);
        } else if (radio::send_broadcast(o.data, o.len) != 0) {
            logger::post_sys("esp_now_send errore (broadcast tipo %d)", o.data[2]);
        }
    }
    NodeLog l;
    while (g_node_logic->pop_log(l)) post_node_log(l);
    // Capogruppo: messaggi per l'app (e per il file della corsa) e comandi arrivati dall'app.
    BleMsg m;
    while (g_node_logic->pop_ble(m)) ble_send(m);
    uint8_t cmd[16];
    size_t n;
    while ((n = ble_take_command(cmd, sizeof(cmd))) > 0) {
        ble::Command c;
        if (ble::decode_command(cmd, n, c)) {
            g_node_logic->on_app_command(c, now);
        } else {
            logger::post_sys("comando BLE non valido (%u byte)", static_cast<unsigned>(n));
        }
    }
}

void handle_rx(const radio::RxPacket& p) {
    if (p.kind == radio::RxPacket::Kind::SendDone) {
        g_node_logic->on_send_result(p.send_ok, p.t_ms);
        return;
    }
    if (!p.rate_ok) {
        g_unexpected_rate++;
        if (p.t_ms - g_last_rate_log >= 10000) {
            g_last_rate_log = p.t_ms;
            logger::post_sys("velocita inattesa src=%d sig_mode=%d rate=%d n=%lu", p.len > 3 ? p.data[3] : -1,
                             p.sig_mode, p.rate, static_cast<unsigned long>(g_unexpected_rate));
        }
    }

    // Un beacon con il mio ID da un altro MAC: due schede con lo stesso ID. (Solo i beacon: allarmi e comandi
    // rilanciati portano l'ID di chi li ha creati ma il MAC di chi li rilancia.)
    if (g_node.id != 0 && p.len > 3 && p.data[2] == static_cast<uint8_t>(packets::Type::Beacon) && p.data[3] == g_node.id &&
        memcmp(p.src, g_node.mac, 6) != 0) {
        g_dup_id_ms = p.t_ms;
        if (p.t_ms - g_last_dup_log >= ID_DUP_LOG_MS || g_last_dup_log == 0) {
            g_last_dup_log = p.t_ms;
            logger::post_sys("ID doppio: %d anche sul MAC %02X:%02X:%02X:%02X:%02X:%02X", g_node.id, p.src[0], p.src[1],
                             p.src[2], p.src[3], p.src[4], p.src[5]);
        }
        return;
    }

    packets::Beacon b;
    if (!g_node_logic->on_packet(p.data, p.len, p.rssi, p.t_ms, b)) return;

    // Il MAC del compagno si impara dal suo beacon: serve per gli invii con conferma.
    int err;
    switch (radio::learn_peer(b.sender, p.src, err)) {
        case radio::PeerResult::Added:
            logger::post_sys("peer %d %02X:%02X:%02X:%02X:%02X:%02X velocita fissata", b.sender, p.src[0], p.src[1], p.src[2],
                             p.src[3], p.src[4], p.src[5]);
            break;
        case radio::PeerResult::MacChanged:
            logger::post_sys("peer %d: MAC cambiato in %02X:%02X:%02X:%02X:%02X:%02X (ID doppio?)", b.sender, p.src[0], p.src[1],
                             p.src[2], p.src[3], p.src[4], p.src[5]);
            break;
        case radio::PeerResult::Failed:
            logger::post_sys("peer %d non registrato err=%d", b.sender, err);
            break;
        case radio::PeerResult::Unchanged:
            break;
    }

    LogEvent e;
    memset(&e, 0, sizeof(e));
    e.type = LogEventType::Rx;
    e.t_ms = p.t_ms;
    e.u.rx.rssi = p.rssi;
    e.u.rx.b = b;
    logger::post(e);
}

void send_beacon(uint32_t now) {
    uint8_t buf[packets::MAX_SIZE];
    packets::Beacon sent;
    size_t n = g_node_logic->build_beacon(now, g_battery_pct, buf, sizeof(buf), sent);
    int err = radio::send_broadcast(buf, n);
    if (err == 0) {
        LogEvent e;
        memset(&e, 0, sizeof(e));
        e.type = LogEventType::Tx;
        e.t_ms = now;
        e.u.tx.seq = sent.seq;
        logger::post(e);
    } else {
        logger::post_sys("esp_now_send errore %d seq=%u", err, sent.seq);
    }
}

// Decisioni del rilevatore e stime del Kalman nel registro.
void log_detector(uint32_t now) {
    static DetEvent events[Detector::MAX_EVENTS];
    size_t n = g_node_logic->tick(now, events);
    for (size_t i = 0; i < n; i++) {
        LogEvent e;
        memset(&e, 0, sizeof(e));
        e.type = LogEventType::Det;
        e.t_ms = events[i].t_ms;
        e.u.det.version = events[i].version;
        e.u.det.kind = events[i].kind;
        e.u.det.start = events[i].start;
        e.u.det.id = events[i].target;
        e.u.det.r = static_cast<float>(events[i].r);
        e.u.det.threshold = static_cast<float>(events[i].threshold);
        logger::post(e);
        // Nodo perso e scheda sorda sono gli stessi nelle due versioni: si scrivono per entrambe.
        if (events[i].kind != DetKind::PreAlarm) {
            e.u.det.version = DetVersion::Gnss;
            logger::post(e);
        }
    }
}

void log_estimates(uint32_t now) {
    for (uint8_t id = 1; id <= MAX_NODES; id++) {
        LinkView v;
        if (!g_node_logic->estimate(id, now, v)) continue;
        LogEvent e;
        memset(&e, 0, sizeof(e));
        e.type = LogEventType::Kal;
        e.t_ms = now;
        e.u.kal.id = id;
        e.u.kal.signal = static_cast<float>(v.r);
        e.u.kal.slope = static_cast<float>(v.s);
        e.u.kal.sigma = static_cast<float>(v.sigma);
        e.u.kal.age_ds = packets::age_ds(v.age_ms);
        logger::post(e);
    }
}

void toggle_role(uint32_t now) {
    if (!g_node_logic->toggle_role(now)) return;
    Role r = g_node_logic->role();
    Preferences prefs;
    if (prefs.begin(ROLE_NVS_NAMESPACE, false)) {
        prefs.putUChar(ROLE_NVS_KEY, static_cast<uint8_t>(r));
        prefs.end();
    } else {
        logger::post_sys("NVS: ruolo non salvato");
    }
    logger::set_role(role_name(r));
    logger::post_sys("ruolo %s", role_name(r));
}

void apply_command(LogicCmd cmd, uint32_t now) {
    switch (cmd) {
        case LogicCmd::ToggleRole:
            toggle_role(now);
            ble_enable(g_node_logic->role() == Role::Leader);  // il BLE serve solo al capogruppo
            break;
        case LogicCmd::ToggleSos:
            if (g_node_logic->toggle_sos(now)) logger::post_sys("sos %s", g_node_logic->status(now).sos ? "attivo" : "annullato");
            break;
        case LogicCmd::Start:
            if (g_node_logic->start_run(now) == Node::CmdResult::Done) logger::post_sys("corsa avviata");
            break;
        case LogicCmd::Stop:
            if (g_node_logic->stop_run(now) == Node::CmdResult::Done) logger::post_sys("corsa ferma");
            break;
    }
    publish_status(now);
}

void logic_task(void*) {
    // Fase iniziale casuale in [0, periodo).
    uint32_t next_beacon = millis() + esp_random() % BEACON_PERIOD_MS;
    uint32_t next_tick = millis() + LOGIC_TICK_MS;
    uint32_t next_kal = millis() + KAL_LOG_PERIOD_MS;
    uint32_t next_heap = millis() + HEAP_LOG_PERIOD_MS;

    for (;;) {
        uint32_t now = millis();
        int32_t wait = static_cast<int32_t>(next_beacon - now);
        int32_t wait_tick = static_cast<int32_t>(next_tick - now);
        if (wait_tick < wait) wait = wait_tick;
        if (wait > static_cast<int32_t>(LOGIC_MAX_WAIT_MS)) wait = LOGIC_MAX_WAIT_MS;
        if (wait < 0) wait = 0;

        radio::RxPacket p;
        if (radio::receive(p, pdMS_TO_TICKS(wait))) {
            handle_rx(p);
            while (radio::receive(p, 0)) handle_rx(p);
        }
        uint8_t cmd;
        while (xQueueReceive(g_cmd_queue, &cmd, 0) == pdTRUE) apply_command(static_cast<LogicCmd>(cmd), millis());
        GnssSample fix;
        while (xQueueReceive(g_gnss_queue, &fix, 0) == pdTRUE) g_node_logic->on_gnss(fix);

        now = millis();
        if (due(now, next_beacon)) {
            send_beacon(now);
            // Periodo piu' jitter uniforme in [-J, +J], come beacon_schedule() del simulatore.
            int32_t jitter = static_cast<int32_t>(esp_random() % (2 * BEACON_JITTER_MS + 1)) -
                             static_cast<int32_t>(BEACON_JITTER_MS);
            next_beacon += static_cast<uint32_t>(static_cast<int32_t>(BEACON_PERIOD_MS) + jitter);
            // Se siamo molto in ritardo (scritture lunghe) si riparte da adesso.
            if (static_cast<int32_t>(millis() - next_beacon) > static_cast<int32_t>(BEACON_PERIOD_MS)) {
                next_beacon = millis();
            }
        }
        if (due(now, next_tick)) {
            next_tick += LOGIC_TICK_MS;
            if (due(now, next_tick)) next_tick = now + LOGIC_TICK_MS;
            log_detector(now);
            publish_status(now);
            if (due(now, next_kal)) {
                next_kal += KAL_LOG_PERIOD_MS;
                if (due(now, next_kal)) next_kal = now + KAL_LOG_PERIOD_MS;
                log_estimates(now);
            }
            if (due(now, next_heap)) {
                next_heap = now + HEAP_LOG_PERIOD_MS;
                logger::post_sys("heap libero=%u minimo=%u coda=%u", static_cast<unsigned>(esp_get_free_heap_size()),
                                 static_cast<unsigned>(esp_get_minimum_free_heap_size()),
                                 static_cast<unsigned>(g_node_logic->queue_size()));
            }
            uint32_t dropped = radio::take_dropped();
            if (dropped > 0) logger::post_sys("coda radio piena persi=%lu", static_cast<unsigned long>(dropped));
        }
        drain_node(millis());
    }
}

}  // namespace

Role logic_init() {
    Role role = Role::Runner;
    Preferences prefs;
    // In scrittura anche per leggere: in sola lettura un namespace nuovo darebbe un errore.
    if (prefs.begin(ROLE_NVS_NAMESPACE, false)) {
        role = role_from_byte(prefs.getUChar(ROLE_NVS_KEY, static_cast<uint8_t>(Role::Runner)));
        prefs.end();
    }
    static Node node(g_node.id, role);
    node.seed(esp_random());
    g_node_logic = &node;
    // Capogruppo riavviato a corsa in corso: si riprende il file della corsa e il seq. Il T0 arriva dai beacon.
    uint32_t last_seq;
    if (role == Role::Leader && runlog::resume(last_seq)) node.resume_ble_seq(last_seq);
    g_cmd_queue = xQueueCreate(8, sizeof(uint8_t));
    g_gnss_queue = xQueueCreate(4, sizeof(GnssSample));
    g_status_queue = xQueueCreate(1, sizeof(NodeStatus));
    return role;
}

void logic_start() {
    ble_enable(g_node_logic->role() == Role::Leader);
    xTaskCreatePinnedToCore(logic_task, "logic", 8192, nullptr, configMAX_PRIORITIES - 2, nullptr, 1);
}

void logic_post_gnss(const GnssSample& s) {
    if (g_gnss_queue != nullptr) xQueueSend(g_gnss_queue, &s, 0);
}

void logic_command(LogicCmd cmd) {
    uint8_t c = static_cast<uint8_t>(cmd);
    if (g_cmd_queue != nullptr) xQueueSend(g_cmd_queue, &c, 0);
}

bool logic_status(NodeStatus& out) {
    return g_status_queue != nullptr && xQueuePeek(g_status_queue, &out, 0) == pdTRUE;
}

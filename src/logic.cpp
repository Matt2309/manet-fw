#include "logic.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <string.h>

#include "app.h"
#include "config.h"
#include "logger.h"
#include "radio.h"

namespace {

Node* g_node_logic = nullptr;
QueueHandle_t g_cmd_queue = nullptr;     // dalla UI: alternare il ruolo
QueueHandle_t g_status_queue = nullptr;  // verso la UI: un solo elemento, sovrascritto

bool g_two_leaders = false;
uint32_t g_unexpected_rate = 0;
uint32_t g_last_rate_log = 0;

bool due(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

void publish_status(uint32_t now) {
    NodeStatus s = g_node_logic->status(now);
    xQueueOverwrite(g_status_queue, &s);
    if (s.two_leaders != g_two_leaders) {
        g_two_leaders = s.two_leaders;
        logger::post_sys("%s", g_two_leaders ? "due capogruppo" : "un solo capogruppo");
    }
}

void handle_rx(const radio::RxPacket& p) {
    if (!p.rate_ok) {
        g_unexpected_rate++;
        if (p.t_ms - g_last_rate_log >= 10000) {
            g_last_rate_log = p.t_ms;
            logger::post_sys("velocita inattesa src=%d sig_mode=%d rate=%d n=%lu", p.len > 3 ? p.data[3] : -1,
                             p.sig_mode, p.rate, static_cast<unsigned long>(g_unexpected_rate));
        }
    }

    packets::Beacon b;
    if (!g_node_logic->on_packet(p.data, p.len, p.rssi, p.t_ms, b)) return;

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

void toggle_role() {
    uint32_t now = millis();
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
    publish_status(now);
}

void logic_task(void*) {
    // Fase iniziale casuale in [0, periodo).
    uint32_t next_beacon = millis() + esp_random() % BEACON_PERIOD_MS;
    uint32_t next_tick = millis() + LOGIC_TICK_MS;

    for (;;) {
        uint32_t now = millis();
        int32_t wait_beacon = static_cast<int32_t>(next_beacon - now);
        int32_t wait_tick = static_cast<int32_t>(next_tick - now);
        int32_t wait = wait_beacon < wait_tick ? wait_beacon : wait_tick;
        if (wait < 0) wait = 0;

        radio::RxPacket p;
        if (radio::receive(p, pdMS_TO_TICKS(wait))) {
            handle_rx(p);
            while (radio::receive(p, 0)) handle_rx(p);
        }
        uint8_t cmd;
        while (xQueueReceive(g_cmd_queue, &cmd, 0) == pdTRUE) toggle_role();

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
            publish_status(now);
            uint32_t dropped = radio::take_dropped();
            if (dropped > 0) logger::post_sys("coda radio piena persi=%lu", static_cast<unsigned long>(dropped));
        }
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
    g_node_logic = &node;
    g_cmd_queue = xQueueCreate(4, sizeof(uint8_t));
    g_status_queue = xQueueCreate(1, sizeof(NodeStatus));
    return role;
}

void logic_start() { xTaskCreatePinnedToCore(logic_task, "logic", 6144, nullptr, configMAX_PRIORITIES - 2, nullptr, 1); }

void logic_toggle_role() {
    uint8_t cmd = 1;
    xQueueSend(g_cmd_queue, &cmd, 0);
}

bool logic_status(NodeStatus& out) {
    return g_status_queue != nullptr && xQueuePeek(g_status_queue, &out, 0) == pdTRUE;
}

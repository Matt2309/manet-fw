#include "radio.h"

#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <string.h>

#include "app.h"
#include "beacon.h"
#include "config.h"
#include "logger.h"

namespace radio {
namespace {

const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

uint8_t g_self_id = 0;
NeighborTable g_table;
portMUX_TYPE g_table_mux = portMUX_INITIALIZER_UNLOCKED;

uint32_t g_unexpected_rate = 0;
uint32_t g_last_rate_log = 0;

void on_recv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    uint32_t now = millis();
    beacon::Beacon b;
    if (len < 0 || !beacon::unpack(data, static_cast<size_t>(len), b)) return;

    const wifi_pkt_rx_ctrl_t* rc = info->rx_ctrl;
    int8_t rssi = rc->rssi;

    // Verifica che il mittente trasmetta davvero a 802.11b 1 Mbit/s (non HT, rate 0).
    if (rc->sig_mode != 0 || rc->rate != WIFI_PHY_RATE_1M_L) {
        g_unexpected_rate++;
        if (now - g_last_rate_log >= 10000) {
            g_last_rate_log = now;
            logger::post_sys("velocita inattesa src=%d sig_mode=%d rate=%d n=%lu", b.id,
                             rc->sig_mode, rc->rate, static_cast<unsigned long>(g_unexpected_rate));
        }
    }

    portENTER_CRITICAL(&g_table_mux);
    g_table.update(b.id, rssi, now);
    portEXIT_CRITICAL(&g_table_mux);

    LogEvent e;
    memset(&e, 0, sizeof(e));
    e.type = LogEventType::Rx;
    e.t_ms = now;
    e.u.rx.src = b.id;
    e.u.rx.seq = b.seq;
    e.u.rx.rssi = rssi;
    e.u.rx.n = b.n;
    memcpy(e.u.rx.tab, b.nb, sizeof(b.nb));
    logger::post(e);
}

void tx_task(void*) {
    uint32_t seq = 0;
    // Fase iniziale casuale in [0, periodo).
    uint32_t next = millis() + esp_random() % BEACON_PERIOD_MS;

    for (;;) {
        int32_t wait = static_cast<int32_t>(next - millis());
        if (wait > 0) vTaskDelay(pdMS_TO_TICKS(wait));
        uint32_t now = millis();

        beacon::Beacon b;
        memset(&b, 0, sizeof(b));
        b.id = g_self_id;
        b.seq = seq;
        portENTER_CRITICAL(&g_table_mux);
        b.n = g_table.fill_entries(g_self_id, now, b.nb);
        portEXIT_CRITICAL(&g_table_mux);

        uint8_t buf[beacon::MAX_SIZE];
        size_t n = beacon::pack(b, buf, sizeof(buf));

        esp_err_t err = esp_now_send(BROADCAST, buf, n);
        if (err == ESP_OK) {
            LogEvent e;
            memset(&e, 0, sizeof(e));
            e.type = LogEventType::Tx;
            e.t_ms = now;
            e.u.tx.seq = seq;
            logger::post(e);
        } else {
            logger::post_sys("esp_now_send errore %d seq=%lu", err, static_cast<unsigned long>(seq));
        }
        seq++;

        // Periodo piu' jitter uniforme in [-J, +J], come beacon_schedule() del simulatore.
        int32_t jitter = static_cast<int32_t>(esp_random() % (2 * BEACON_JITTER_MS + 1)) -
                         static_cast<int32_t>(BEACON_JITTER_MS);
        next += static_cast<uint32_t>(static_cast<int32_t>(BEACON_PERIOD_MS) + jitter);
        // Se siamo molto in ritardo (scritture lunghe) si riparte da adesso.
        if (static_cast<int32_t>(millis() - next) > static_cast<int32_t>(BEACON_PERIOD_MS)) {
            next = millis();
        }
    }
}

}  // namespace

bool init(Info& info) {
    memset(&info, 0, sizeof(info));

    WiFi.mode(WIFI_STA);  // avvia il Wi-Fi, senza connessione
    esp_wifi_set_ps(WIFI_PS_NONE);  // indispensabile per ricevere i broadcast
    esp_err_t err = esp_wifi_set_channel(RADIO_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        dbg("set_channel errore %d", err);
        return false;
    }

    // Potenza: il valore e' in quarti di dBm (40 = 10 dBm). Si rilegge subito.
    esp_wifi_set_max_tx_power(TX_POWER_QDBM);
    int8_t p = 0;
    esp_wifi_get_max_tx_power(&p);
    info.power_qdbm = p;
    info.power_ok = (p == TX_POWER_QDBM);
    dbg("potenza impostata %d riletta %d (%.2f dBm) %s", TX_POWER_QDBM, p, p / 4.0,
        info.power_ok ? "OK" : "DIVERSA");

    err = esp_now_init();
    if (err != ESP_OK) {
        dbg("esp_now_init errore %d", err);
        return false;
    }

    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, BROADCAST, 6);
    peer.channel = RADIO_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        dbg("esp_now_add_peer errore %d", err);
        return false;
    }

    // Velocita' fissa: 802.11b, 1 Mbit/s, preambolo lungo.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
    esp_now_rate_config_t rate;
    memset(&rate, 0, sizeof(rate));
    rate.phymode = WIFI_PHY_MODE_11B;
    rate.rate = WIFI_PHY_RATE_1M_L;
    err = esp_now_set_peer_rate_config(BROADCAST, &rate);
#else
    err = esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_1M_L);
#endif
    info.rate_err = err;
    info.rate_ok = (err == ESP_OK);
    dbg("velocita 802.11b 1 Mbit/s: %s (err %d)", info.rate_ok ? "applicata" : "ERRORE", err);

    // La potenza si riscrive dopo l'avvio di ESP-NOW e si rilegge per sicurezza.
    esp_wifi_set_max_tx_power(TX_POWER_QDBM);
    esp_wifi_get_max_tx_power(&p);
    info.power_qdbm = p;
    info.power_ok = (p == TX_POWER_QDBM);
    return true;
}

void start(uint8_t self_id) {
    g_self_id = self_id;
    esp_now_register_recv_cb(on_recv);
    xTaskCreatePinnedToCore(tx_task, "beacon", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr, 1);
}

void snapshot(NeighborTable& out) {
    portENTER_CRITICAL(&g_table_mux);
    out = g_table;
    portEXIT_CRITICAL(&g_table_mux);
}

}  // namespace radio

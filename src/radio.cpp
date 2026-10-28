#include "radio.h"

#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include <atomic>

#include "app.h"
#include "logger.h"
#include "nodes.h"

namespace radio {
namespace {

const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

QueueHandle_t g_rx_queue = nullptr;
std::atomic<uint32_t> g_dropped{0};

// Il callback copia il pacchetto in coda e torna subito.
void on_recv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len <= 0 || len > ESPNOW_MAX_PAYLOAD) return;
    RxPacket p;
    p.kind = RxPacket::Kind::Receive;
    p.send_ok = false;
    p.t_ms = millis();
    p.rssi = info->rx_ctrl->rssi;
    p.sig_mode = info->rx_ctrl->sig_mode;
    p.rate = info->rx_ctrl->rate;
    // Il mittente deve trasmettere a 802.11b 1 Mbit/s (non HT, rate 0).
    p.rate_ok = (p.sig_mode == 0 && p.rate == WIFI_PHY_RATE_1M_L);
    p.len = static_cast<uint8_t>(len);
    memcpy(p.data, data, static_cast<size_t>(len));
    if (xQueueSend(g_rx_queue, &p, 0) != pdTRUE) g_dropped++;
}

// Esito di un invio unicast: stessa coda dei pacchetti ricevuti.
void on_sent(const esp_now_send_info_t*, esp_now_send_status_t status) {
    RxPacket p;
    memset(&p, 0, sizeof(p));
    p.kind = RxPacket::Kind::SendDone;
    p.send_ok = (status == ESP_NOW_SEND_SUCCESS);
    p.t_ms = millis();
    if (xQueueSend(g_rx_queue, &p, 0) != pdTRUE) g_dropped++;
}

// Velocita' fissa per gli invii a questo indirizzo: 802.11b, 1 Mbit/s, preambolo lungo.
esp_err_t fix_rate(const uint8_t* mac) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
    esp_now_rate_config_t rate;
    memset(&rate, 0, sizeof(rate));
    rate.phymode = WIFI_PHY_MODE_11B;
    rate.rate = WIFI_PHY_RATE_1M_L;
    return esp_now_set_peer_rate_config(mac, &rate);
#else
    (void)mac;
    return esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_1M_L);
#endif
}

esp_err_t add_peer(const uint8_t* mac) {
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = RADIO_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer);
}

}  // namespace

bool init(Info& info) {
    memset(&info, 0, sizeof(info));
    g_rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(RxPacket));
    if (g_rx_queue == nullptr) return false;

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

    err = add_peer(BROADCAST);
    if (err != ESP_OK) {
        dbg("esp_now_add_peer errore %d", err);
        return false;
    }
    err = fix_rate(BROADCAST);
    info.rate_err = err;
    info.rate_ok = (err == ESP_OK);

    // Un peer per ogni altra scheda, per gli invii con conferma (dalla fase 4): stessa velocita'.
    // ESP-NOW non permette di rileggere la velocita' di un peer; la verifica e' il codice di
    // ritorno qui e il controllo su ogni pacchetto ricevuto (rate_ok).
    for (const NodeEntry& n : NODES) {
        if (n.id == 0) break;
        if (memcmp(n.mac, g_node.mac, 6) == 0) continue;
        esp_err_t e = add_peer(n.mac);
        if (e == ESP_OK) e = fix_rate(n.mac);
        if (e == ESP_OK) {
            info.peers++;
        } else {
            info.peers_failed++;
            if (info.rate_err == 0) info.rate_err = e;
            info.rate_ok = false;
        }
    }
    dbg("velocita 802.11b 1 Mbit/s: %s (err %d) peer %d falliti %d", info.rate_ok ? "applicata" : "ERRORE",
        info.rate_err, info.peers, info.peers_failed);

    // La potenza si riscrive dopo l'avvio di ESP-NOW e si rilegge per sicurezza.
    esp_wifi_set_max_tx_power(TX_POWER_QDBM);
    esp_wifi_get_max_tx_power(&p);
    info.power_qdbm = p;
    info.power_ok = (p == TX_POWER_QDBM);
    return true;
}

void start() {
    esp_now_register_recv_cb(on_recv);
    esp_now_register_send_cb(on_sent);
}

bool send_to(uint8_t id, const uint8_t* data, size_t len) {
    const uint8_t* mac = node_mac_from_id(id);
    return mac != nullptr && esp_now_send(mac, data, len) == ESP_OK;
}

bool receive(RxPacket& p, TickType_t wait) { return xQueueReceive(g_rx_queue, &p, wait) == pdTRUE; }

uint32_t take_dropped() { return g_dropped.exchange(0); }

int send_broadcast(const uint8_t* data, size_t len) { return esp_now_send(BROADCAST, data, len); }

}  // namespace radio

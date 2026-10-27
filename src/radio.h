// ESP-NOW: invio, e ricezione in una coda per il task della logica.
#pragma once

#include <freertos/FreeRTOS.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

namespace radio {

struct Info {
    int8_t power_qdbm;   // potenza riletta da esp_wifi_get_max_tx_power()
    bool power_ok;       // riletta uguale a quella impostata
    bool rate_ok;        // velocita' applicata al broadcast e a tutti i peer
    int rate_err;        // primo codice di errore, 0 se ok
    int peers;           // peer registrati oltre al broadcast
    int peers_failed;    // peer non registrati o senza velocita' fissata
};

// Pacchetto ricevuto, copiato dal callback.
struct RxPacket {
    uint32_t t_ms;
    int8_t rssi;
    uint8_t sig_mode;    // 0 = 802.11b/g
    uint8_t rate;
    bool rate_ok;        // 802.11b a 1 Mbit/s, come atteso
    uint8_t len;
    uint8_t data[ESPNOW_MAX_PAYLOAD];
};

// Wi-Fi in STA senza connessione, canale fisso, potenza, peer broadcast e un peer per ogni
// altra scheda di nodes.h, tutti a 802.11b 1 Mbit/s. Crea la coda di ricezione.
bool init(Info& info);

// Registra il callback di ricezione. Da chiamare quando la logica e' pronta.
void start();

// Prossimo pacchetto ricevuto, attendendo al massimo wait.
bool receive(RxPacket& p, TickType_t wait);

// Pacchetti persi perche' la coda era piena, dall'ultima chiamata.
uint32_t take_dropped();

// Invio a tutti. Restituisce il codice di esp_now_send (0 = ok).
int send_broadcast(const uint8_t* data, size_t len);

}  // namespace radio

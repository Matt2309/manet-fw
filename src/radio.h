// ESP-NOW broadcast: beacon periodici e ricezione.
#pragma once

#include <stdint.h>

#include "neighbors.h"

namespace radio {

struct Info {
    int8_t power_qdbm;   // potenza riletta da esp_wifi_get_max_tx_power()
    bool power_ok;       // riletta uguale a quella impostata
    bool rate_ok;        // esp_now_set_peer_rate_config() ha restituito ESP_OK
    int rate_err;        // codice di errore, 0 se ok
};

// Wi-Fi in STA senza connessione, canale fisso, potenza, velocita' 802.11b 1 Mbit/s,
// peer broadcast. Non registra ancora la ricezione.
bool init(Info& info);

// Registra la ricezione e avvia il task dei beacon. Da chiamare con il logger pronto.
void start(uint8_t self_id);

// Copia della tabella dei vicini.
void snapshot(NeighborTable& out);

}  // namespace radio

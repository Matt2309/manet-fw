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
    bool rate_ok;        // velocita' applicata al broadcast
    int rate_err;        // codice di errore, 0 se ok
};

// Pacchetto ricevuto, oppure esito di un invio unicast (kind SendDone): arrivano nella stessa coda, cosi' la
// logica si risveglia subito per entrambi. Copiato dal callback.
struct RxPacket {
    enum class Kind : uint8_t { Receive, SendDone };
    Kind kind;
    bool send_ok;        // SendDone: conferma ricevuta dal destinatario
    uint32_t t_ms;
    int8_t rssi;
    uint8_t sig_mode;    // 0 = 802.11b/g
    uint8_t rate;
    bool rate_ok;        // 802.11b a 1 Mbit/s, come atteso
    uint8_t src[6];      // MAC di chi ha trasmesso
    uint8_t len;
    uint8_t data[ESPNOW_MAX_PAYLOAD];
};

// Wi-Fi in STA senza connessione, canale fisso, potenza, peer broadcast a 802.11b 1 Mbit/s. Crea la coda di
// ricezione. I peer dei compagni non sono in una tabella: si imparano dai beacon (learn_peer).
bool init(Info& info);

// Registra il callback di ricezione. Da chiamare quando la logica e' pronta.
void start();

// Prossimo pacchetto ricevuto, attendendo al massimo wait.
bool receive(RxPacket& p, TickType_t wait);

// Pacchetti persi perche' la coda era piena, dall'ultima chiamata.
uint32_t take_dropped();

enum class PeerResult : uint8_t {
    Unchanged,   // gia' noto, o un nuovo tentativo troppo vicino al precedente
    Added,       // nuovo compagno: registrato come peer a 1 Mbit/s
    MacChanged,  // lo stesso ID da un altro MAC (ID doppio, o scheda sostituita): peer aggiornato
    Failed,      // non registrabile (err): ESP-NOW ammette 20 peer in tutto
};

// Impara (o aggiorna) il MAC del compagno con questo ID, dal suo beacon. Da chiamare dalla logica.
PeerResult learn_peer(uint8_t id, const uint8_t mac[6], int& err);

// Invio con conferma al compagno di ID dato (velocita' gia' fissata per il peer). false se non se ne conosce il MAC
// o l'invio non parte; l'esito arriva poi come RxPacket SendDone.
bool send_to(uint8_t id, const uint8_t* data, size_t len);

// Invio a tutti. Restituisce il codice di esp_now_send (0 = ok).
int send_broadcast(const uint8_t* data, size_t len);

}  // namespace radio

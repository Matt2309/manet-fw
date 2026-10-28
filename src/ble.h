// BLE del capogruppo (libreria BLE del core Arduino, NimBLE): un servizio con tre caratteristiche,
// dati (notify), urgenti (notify) e comandi (write). Il formato dei byte lo conosce solo il codec (ble_codec).
//
// Il task BLE scrive nel file della corsa e manda i messaggi preparati dalla logica; i callback copiano
// il comando in una coda e tornano.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "node.h"

enum class BleState : uint8_t { Off, Advertising, Connected };

// Accende o spegne il BLE (solo il capogruppo lo usa). La prima volta avvia lo stack e il task.
void ble_enable(bool on);

BleState ble_state();

// Dalla logica: un messaggio da mandare (o un'operazione sul file della corsa). false se il BLE e' spento o la coda e' piena.
bool ble_send(const BleMsg& m);

// Dal task BLE alla logica: prossimo comando scritto dall'app; restituisce i byte copiati in out, 0 se non ce ne sono.
size_t ble_take_command(uint8_t* out, size_t cap);

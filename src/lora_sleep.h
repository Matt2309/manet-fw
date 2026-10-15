// La radio LoRa SX1262 non si usa: all'avvio la si mette in sleep.
#pragma once

#include <stddef.h>

// Resetta l'SX1262, verifica che risponda e lo mette in sleep.
// Scrive l'esito in msg; restituisce true se ha risposto ed e' in sleep.
bool lora_sleep(char* msg, size_t cap);

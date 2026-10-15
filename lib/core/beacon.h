// Beacon ESP-NOW: struttura packed, little-endian.
//
//   magic (1) | versione (1) | id (1) | seq (uint32) | n vicini (1)
//   per ogni vicino: id (1) | rssi (int8) | eta' in decimi di s (uint8) | varianza (uint8)
//
// La varianza Kalman non e' ancora disponibile: vale sempre 255.
// Nota: nel Blocco 4 di manet-sim il campo equivalente e' la deviazione standard
// sqrt(P00) in passi da 0,1 dB.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace beacon {

constexpr uint8_t MAGIC = 0x4D;
constexpr uint8_t VERSION = 1;
constexpr size_t MAX_NEIGHBORS = 8;
constexpr size_t HEADER_SIZE = 8;
constexpr size_t ENTRY_SIZE = 4;
constexpr size_t MAX_SIZE = HEADER_SIZE + MAX_NEIGHBORS * ENTRY_SIZE;
constexpr uint8_t VARIANCE_NA = 255;

struct Entry {
    uint8_t id;
    int8_t rssi;
    uint8_t age_ds;  // decimi di secondo, saturata a 255
    uint8_t variance;
};

struct Beacon {
    uint8_t id;
    uint32_t seq;
    uint8_t n;
    Entry nb[MAX_NEIGHBORS];
};

// Eta' in decimi di secondo da millisecondi trascorsi, saturata a 255.
uint8_t age_ds(uint32_t elapsed_ms);

// Scrive il beacon in out; restituisce i byte scritti, 0 se cap e' insufficiente
// o n supera MAX_NEIGHBORS.
size_t pack(const Beacon& b, uint8_t* out, size_t cap);

// Legge un beacon; false se magic, versione o lunghezza non tornano.
bool unpack(const uint8_t* data, size_t len, Beacon& b);

}  // namespace beacon

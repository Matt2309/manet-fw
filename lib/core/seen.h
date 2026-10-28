// Insiemi "gia' visto" per scartare i doppioni, a memoria fissa.
#pragma once

#include <stdint.h>
#include <string.h>

#include "config.h"

// Record di telemetria gia' visti, per mittente: finestra scorrevole sui secondi da T0.
class SeenRecords {
public:
    // true se (origin, t_s) e' nuovo (e lo segna); false se e' un doppione o e' fuori finestra.
    bool add(uint8_t origin, uint16_t t_s);
    void clear() { memset(slot_, 0, sizeof(slot_)); }

private:
    struct Slot {
        bool valid;
        uint16_t newest;
        uint8_t bits[SEEN_RECORD_WINDOW / 8];
    };
    Slot slot_[MAX_NODES + 1] = {};
};

// Numeri progressivi a 8 bit gia' visti, per creatore (allarmi e comandi): finestra di 128 prima e dopo.
class SeenSeq {
public:
    bool add(uint8_t creator, uint8_t seq);
    void clear() { memset(slot_, 0, sizeof(slot_)); }

private:
    struct Slot {
        bool valid;
        uint8_t newest;
        uint8_t bits[32];
    };
    Slot slot_[MAX_NODES + 1] = {};
};

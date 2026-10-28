// Fix GNSS valido e campione di posizione per il resto della logica.
#pragma once

#include <stdint.h>

#include "nmea.h"
#include "packets.h"

// Posizione propria all'istante t_ms (arrivo dell'epoca). valid = rispetta i requisiti di fix_valid.
struct GnssSample {
    uint32_t t_ms;
    bool valid;
    packets::Position pos;
};

// 3D, posizione presente, almeno GNSS_MIN_SAT satelliti, HDOP al massimo GNSS_MAX_HDOP.
// L'eta' dell'epoca si controlla all'uso (GNSS_MAX_AGE_MS).
bool fix_valid(const GpsEpoch& e);

// Campione dell'epoca; la posizione c'e' (HDOP in decimi) solo se il fix e' valido.
GnssSample sample_from_epoch(const GpsEpoch& e);

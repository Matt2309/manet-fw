// Distanze fra posizioni GNSS.
#pragma once

#include "packets.h"

// Distanza in metri fra due posizioni: formula equirettangolare, errore sotto lo 0,1% entro 1 km.
double distance_m(const packets::Position& a, const packets::Position& b);

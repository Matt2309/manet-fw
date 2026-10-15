// Stima della carica di una LiPo a una cella dalla tensione, e media mobile.
#pragma once

#include <stdint.h>

// Carica tipica in % (0..100) da millivolt: 4,20 V = 100%, 3,30 V = 0%,
// interpolazione lineare tra i punti della tabella.
int battery_percent(int mv);

// Media mobile sulle ultime N letture (default 10).
class BatteryAverage {
public:
    static constexpr int N = 10;

    void add(int mv);
    bool ready() const { return count_ > 0; }
    // Media delle letture disponibili; 0 se non ce ne sono.
    int average_mv() const;

private:
    int buf_[N] = {};
    int count_ = 0;
    int next_ = 0;
};

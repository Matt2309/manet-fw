// Filtro di Kalman di un collegamento (stesse equazioni di src/kalman.py in manet-sim).
//
// Stato x = [segnale (dBm), pendenza (dB/s)]; misura z = RSSI del beacon, H = [1, 0];
// F = [[1, dt], [0, 1]] con il dt vero fra due beacon ricevuti;
// Q = sigma_a^2 * [[dt^3/3, dt^2/2], [dt^2/2, dt]] (accelerazione casuale continua).
// Beacon perso: nessun aggiornamento, la stima si predice solo quando serve.
#pragma once

#include <stdint.h>

struct LinkEstimate {
    double r;    // segnale, dBm
    double s;    // pendenza, dB/s
    double p00;  // covarianza
    double p01;
    double p11;
};

class LinkFilter {
public:
    // Aggiorna con un beacon ricevuto a t_ms con RSSI z. Al primo beacon, o dopo un silenzio
    // oltre KALMAN_REINIT_GAP_MS, riparte da r = z, s = 0.
    void update(uint32_t t_ms, double z);

    bool started() const { return started_; }
    uint32_t last_ms() const { return t_last_; }

    // Stima a posteriori dell'ultimo aggiornamento.
    const LinkEstimate& posterior() const { return x_; }

    // Stima predetta a now_ms (solo predizione dall'ultimo aggiornamento).
    LinkEstimate predicted(uint32_t now_ms) const;

private:
    bool started_ = false;
    uint32_t t_last_ = 0;
    LinkEstimate x_ = {};
};

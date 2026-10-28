// Tempo della corsa: T0, START e STOP.
//
// T0 e' l'istante locale (ms dall'accensione) dello START. Chi non ha visto lo START lo ricava dai
// compagni: dai decimi trascorsi nel comando rilanciato, o da un beacon (T0 = ora - secondi da T0).
// Non c'e' un identificativo di corsa: un T0 successivo di oltre RUN_T0_TOLERANCE_MS e' una corsa nuova.
#pragma once

#include <stdint.h>

#include "packets.h"

enum class RunState : uint8_t { NotStarted, Running, Stopped };

class Run {
public:
    // Cosa e' cambiato dopo aver elaborato un comando o un beacon.
    enum class Change : uint8_t { None, Started, Stopped };

    RunState state() const { return state_; }
    bool running() const { return state_ == RunState::Running; }

    // Secondi da T0 per il beacon: T_NOT_STARTED se non avviata, altrimenti saturati a 0xFFFE.
    // Continua a contare dopo lo STOP, cosi' i compagni ricavano lo stesso T0.
    uint16_t seconds(uint32_t now_ms) const;
    uint32_t t0() const { return t0_; }

    // START locale (dall'app, sul capogruppo). false se la corsa e' gia' avviata: il T0 non cambia.
    bool start(uint32_t now_ms);
    // STOP locale. false se la corsa non era avviata.
    bool stop();

    // Comando ricevuto via radio con i decimi di secondo trascorsi dall'invio originale.
    Change on_command(packets::Cmd cmd, uint16_t elapsed_ds, uint32_t now_ms);
    // Beacon di un compagno: porta T0 (secondi) e lo stato della corsa.
    Change on_beacon(const packets::Beacon& b, uint32_t now_ms);

private:
    Change adopt(uint32_t implied_t0);

    RunState state_ = RunState::NotStarted;
    uint32_t t0_ = 0;
};

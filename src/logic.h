// Task della logica: possiede lo stato della scheda (vicini, rilevatore, corsa, coda dei dati) e legge le code.
// Gli altri task ricevono copie.
#pragma once

#include "fix.h"
#include "node.h"
#include "role.h"

// Comandi verso la logica, da altri task (interfaccia, console, BLE).
enum class LogicCmd : uint8_t {
    ToggleRole,  // pressione lunga nella finestra dopo l'accensione
    ToggleSos,   // pressione lunga a corsa avviata
    Start,       // solo capogruppo
    Stop,        // solo capogruppo
};

// Legge il ruolo salvato in NVS (corridore alla prima accensione) e crea Node e code.
Role logic_init();

// Avvia il task: riceve i pacchetti dalla coda, manda i beacon, fa i controlli ogni 100 ms.
void logic_start();

// Accoda un comando; la logica lo applica (e applica le sue regole: finestra del ruolo, solo capogruppo, ecc.).
void logic_command(LogicCmd cmd);

// Dal task GNSS: campione dell'ultima epoca. Non blocca; se la coda e' piena il campione e' perso.
void logic_post_gnss(const GnssSample& s);

// Ultima copia dello stato; false se la logica non ha ancora pubblicato nulla.
bool logic_status(NodeStatus& out);

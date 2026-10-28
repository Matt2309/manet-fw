// Task della logica: possiede lo stato della scheda (vicini, ruolo) e legge le code.
// Gli altri task ricevono copie.
#pragma once

#include "fix.h"
#include "node.h"
#include "role.h"

// Legge il ruolo salvato in NVS (corridore alla prima accensione) e crea Node e code.
Role logic_init();

// Avvia il task: riceve i pacchetti dalla coda, manda i beacon, fa i controlli ogni 100 ms.
void logic_start();

// Chiede di alternare il ruolo; la logica applica la regola della finestra.
void logic_toggle_role();

// Dal task GNSS: campione dell'ultima epoca. Non blocca; se la coda e' piena il campione e' perso.
void logic_post_gnss(const GnssSample& s);

// Ultima copia dello stato; false se la logica non ha ancora pubblicato nulla.
bool logic_status(NodeStatus& out);

// Ruolo della scheda e regola per cambiarlo.
#pragma once

#include <stdint.h>

enum class Role : uint8_t { Runner = 0, Leader = 1 };

// Il ruolo si cambia solo nella finestra dopo l'accensione e prima dello START.
// fired_ms e' l'istante (dall'accensione) in cui la pressione lunga scatta: la finestra conta
// dall'inizio della pressione, cioe' da fired_ms - BTN_LONG_MS.
bool role_can_toggle(uint32_t fired_ms, bool run_started);

Role role_toggled(Role r);

// Valore salvato in NVS -> ruolo; qualsiasi valore sconosciuto vale corridore.
Role role_from_byte(uint8_t b);

const char* role_name(Role r);  // "CORR" o "CAPO"

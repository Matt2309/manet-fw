// Ruolo della scheda e regola per cambiarlo.
#pragma once

#include <stdint.h>

enum class Role : uint8_t { Runner = 0, Leader = 1 };

// Il ruolo si cambia solo nella finestra dopo l'accensione e prima dello START.
// t_ms e' il tempo dall'accensione.
bool role_can_toggle(uint32_t t_ms, bool run_started);

Role role_toggled(Role r);

// Valore salvato in NVS -> ruolo; qualsiasi valore sconosciuto vale corridore.
Role role_from_byte(uint8_t b);

const char* role_name(Role r);  // "CORR" o "CAPO"

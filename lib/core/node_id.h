// ID di una scheda scelto dall'utente (comando seriale "id N").
#pragma once

// Da testo ("5", "0") a ID: 1..MAX_NODES, 0 per "toglie l'ID salvato", -1 se non e' un numero valido
// (vuoto, con segni o lettere, troppo grande).
int parse_node_id(const char* s);

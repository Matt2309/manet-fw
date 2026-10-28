// Nomi dei file di registrazione: /run_NNN.csv, NNN da 001 a 999.
#pragma once

#include <stddef.h>

constexpr int RUN_MAX = 999;

// Numero NNN da un nome ("run_004.csv" o "/run_004.csv"); -1 se non e' un file di run.
int run_number(const char* name);

// Scrive "/run_NNN.csv" in out; false se cap non basta o n e' fuori da 1..999.
bool run_name(int n, char* out, size_t cap);

// Il primo numero libero tra quelli presenti; -1 se sono tutti occupati.
int run_first_free(const int* used, size_t count);

// File dei messaggi BLE di una corsa (capogruppo): /corsa_NNN.bin, NNN da 001 a 999.
// Numero NNN da un nome ("corsa_004.bin" o "/corsa_004.bin"); -1 se non e' un file della corsa.
int corsa_number(const char* name);
// Scrive "/corsa_NNN.bin" in out; false se cap non basta o n e' fuori da 1..999.
bool corsa_name(int n, char* out, size_t cap);

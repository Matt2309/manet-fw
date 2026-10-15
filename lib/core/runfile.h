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

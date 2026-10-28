// File della corsa sul capogruppo: gli STATO e gli EVENTO con seq si scrivono qui, e si sincronizzano su flash,
// prima di essere mandati; servono a RIPRENDI_DA e a riprendere dopo un riavvio.
//
// Formato: per ogni messaggio un byte di lunghezza e i byte del messaggio (gia' codificati dal codec BLE).
// Una bandiera in NVS dice se una corsa e' in corso (messa a START, tolta a STOP).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace runlog {

// START: apre un nuovo /corsa_NNN.bin e mette la bandiera. Chiude l'eventuale file aperto.
bool begin_new();

// Avvio a corsa in corso (bandiera attiva): riapre l'ultimo file in aggiunta; last_seq e' l'ultimo seq salvato.
// false se non c'e' una corsa da riprendere.
bool resume(uint32_t& last_seq);

// STOP: chiude il file e toglie la bandiera.
void end();

// Aggiunge un messaggio e lo sincronizza su flash. false se la scrittura fallisce.
bool append(const uint8_t* msg, size_t len);

// Rilettura in ordine dei messaggi con from_seq < seq <= to_seq.
class Reader {
public:
    ~Reader() { close(); }
    bool open(uint32_t from_seq, uint32_t to_seq);
    // Prossimo messaggio; false alla fine.
    bool next(uint8_t* msg, size_t cap, size_t& len);
    void close();

private:
    void* file_ = nullptr;
    uint32_t from_ = 0, to_ = 0;
};

}  // namespace runlog

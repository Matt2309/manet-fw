// Coda unica dei record di telemetria in attesa: propri e ricevuti dai compagni, in ordine di arrivo.
// Memoria fissa. Piena: il nuovo record sostituisce il piu' vecchio che non sia SOS; i record con SOS non si cancellano mai.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "packets.h"

class RecordQueue {
public:
    static constexpr size_t CAPACITY = TEL_QUEUE_LEN;

    size_t size() const { return count_; }
    bool full() const { return count_ == CAPACITY; }
    bool empty() const { return count_ == 0; }

    // Aggiunge un record. false se e' stato scartato (coda piena di soli SOS e record non SOS).
    bool push(const packets::TelRecord& r);

    // Copia fino a max record, dal piu' vecchio. Il numero di serie del piu' vecchio e' first_serial,
    // quello dell'ultimo copiato last_serial: servono a confermare dopo l'invio anche se nel frattempo la coda e' cambiata.
    size_t peek(packets::TelRecord* out, size_t max, uint32_t& first_serial, uint32_t& last_serial) const;

    // Numero di serie del record in posizione i (0 = il piu' vecchio).
    uint32_t serial_at(size_t i) const { return serial_[at(i)]; }

    // Toglie tutti i record con numero di serie <= serial.
    void drop_through(uint32_t serial);

    void clear();

private:
    size_t at(size_t i) const { return (head_ + i) % CAPACITY; }
    void pop_front();

    packets::TelRecord rec_[CAPACITY];
    uint32_t serial_[CAPACITY];
    size_t head_ = 0;
    size_t count_ = 0;
    uint32_t next_serial_ = 1;
};

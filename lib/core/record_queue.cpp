#include "record_queue.h"

void RecordQueue::pop_front() {
    head_ = (head_ + 1) % CAPACITY;
    count_--;
}

bool RecordQueue::push(const packets::TelRecord& r) {
    if (count_ == CAPACITY) {
        // Il piu' vecchio che non sia SOS. I SOS davanti a lui slittano di un posto per fargli spazio.
        size_t k = 0;
        while (k < count_ && rec_[at(k)].sos) k++;
        if (k == count_) return false;
        for (size_t j = k; j > 0; j--) {
            rec_[at(j)] = rec_[at(j - 1)];
            serial_[at(j)] = serial_[at(j - 1)];
        }
        pop_front();
    }
    size_t pos = at(count_);
    rec_[pos] = r;
    serial_[pos] = next_serial_++;
    count_++;
    return true;
}

size_t RecordQueue::peek(packets::TelRecord* out, size_t max, uint32_t& first_serial, uint32_t& last_serial) const {
    size_t n = count_ < max ? count_ : max;
    for (size_t i = 0; i < n; i++) out[i] = rec_[at(i)];
    first_serial = n > 0 ? serial_[at(0)] : 0;
    last_serial = n > 0 ? serial_[at(n - 1)] : 0;
    return n;
}

void RecordQueue::drop_through(uint32_t serial) {
    while (count_ > 0 && serial_[head_] <= serial) pop_front();
}

void RecordQueue::clear() {
    head_ = 0;
    count_ = 0;
}

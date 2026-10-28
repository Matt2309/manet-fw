#include <unity.h>

#include "record_queue.h"

void setUp() {}
void tearDown() {}

// La coda e' grande (600 record da ~125 byte): una sola istanza statica per tutti i test.
static RecordQueue q;

static packets::TelRecord rec(uint8_t id, uint16_t t_s, bool sos = false) {
    packets::TelRecord r = {};
    r.id = id;
    r.t_s = t_s;
    r.sos = sos;
    return r;
}

void test_fifo() {
    q.clear();
    TEST_ASSERT_TRUE(q.empty());
    for (uint16_t t = 0; t < 5; t++) TEST_ASSERT_TRUE(q.push(rec(1, t)));
    TEST_ASSERT_EQUAL(5, q.size());
    packets::TelRecord out[8];
    uint32_t first, last;
    TEST_ASSERT_EQUAL(3, q.peek(out, 3, first, last));
    TEST_ASSERT_EQUAL(0, out[0].t_s);
    TEST_ASSERT_EQUAL(2, out[2].t_s);
    TEST_ASSERT_EQUAL(5, q.size());  // peek non toglie
    q.drop_through(last);
    TEST_ASSERT_EQUAL(2, q.size());
    TEST_ASSERT_EQUAL(2, q.peek(out, 8, first, last));
    TEST_ASSERT_EQUAL(3, out[0].t_s);
}

void test_conferma_per_numero_di_serie_con_la_coda_cambiata() {
    q.clear();
    for (uint16_t t = 0; t < RecordQueue::CAPACITY; t++) q.push(rec(1, t));
    TEST_ASSERT_TRUE(q.full());
    packets::TelRecord out[4];
    uint32_t first, last;
    q.peek(out, 4, first, last);  // un invio in corso: i primi 4
    // Nel frattempo la coda piena sovrascrive 2 dei 4 record in volo.
    q.push(rec(2, 1000));
    q.push(rec(2, 1001));
    q.drop_through(last);          // la conferma arriva: ne restano solo 2 da togliere, non 4
    TEST_ASSERT_EQUAL(RecordQueue::CAPACITY - 4 + 2, q.size());
    q.peek(out, 1, first, last);
    TEST_ASSERT_EQUAL(4, out[0].t_s);
}

void test_piena_sovrascrive_il_piu_vecchio() {
    q.clear();
    for (uint16_t t = 0; t < RecordQueue::CAPACITY; t++) q.push(rec(1, t));
    q.push(rec(1, 5000));
    TEST_ASSERT_EQUAL(RecordQueue::CAPACITY, q.size());
    packets::TelRecord out[2];
    uint32_t first, last;
    q.peek(out, 2, first, last);
    TEST_ASSERT_EQUAL(1, out[0].t_s);  // lo 0 e' stato sovrascritto
}

void test_i_sos_non_si_cancellano() {
    q.clear();
    q.push(rec(1, 0, true));
    q.push(rec(1, 1, true));
    for (uint16_t t = 2; t < RecordQueue::CAPACITY; t++) q.push(rec(1, t));
    q.push(rec(1, 7000));   // sovrascrive il primo non-SOS (t = 2); i due SOS davanti restano
    packets::TelRecord out[4];
    uint32_t first, last;
    q.peek(out, 4, first, last);
    TEST_ASSERT_EQUAL(0, out[0].t_s);
    TEST_ASSERT_TRUE(out[0].sos);
    TEST_ASSERT_EQUAL(1, out[1].t_s);
    TEST_ASSERT_TRUE(out[1].sos);
    TEST_ASSERT_EQUAL(3, out[2].t_s);
    TEST_ASSERT_EQUAL(4, out[3].t_s);
    TEST_ASSERT_EQUAL(RecordQueue::CAPACITY, q.size());
}

void test_tutta_di_sos_scarta_il_nuovo() {
    q.clear();
    for (uint16_t t = 0; t < RecordQueue::CAPACITY; t++) q.push(rec(1, t, true));
    TEST_ASSERT_FALSE(q.push(rec(1, 9000)));
    TEST_ASSERT_TRUE(q.push(rec(1, 9001, true)) == false);  // nemmeno un altro SOS: non c'e' nulla da sostituire
    TEST_ASSERT_EQUAL(RecordQueue::CAPACITY, q.size());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fifo);
    RUN_TEST(test_conferma_per_numero_di_serie_con_la_coda_cambiata);
    RUN_TEST(test_piena_sovrascrive_il_piu_vecchio);
    RUN_TEST(test_i_sos_non_si_cancellano);
    RUN_TEST(test_tutta_di_sos_scarta_il_nuovo);
    return UNITY_END();
}

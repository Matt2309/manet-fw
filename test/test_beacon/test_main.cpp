#include <string.h>
#include <unity.h>

#include "beacon.h"

void setUp() {}
void tearDown() {}

static beacon::Beacon esempio() {
    beacon::Beacon b = {};
    b.id = 2;
    b.seq = 0x01020304;
    b.n = 2;
    b.nb[0] = {1, -61, 3, 255};
    b.nb[1] = {3, -70, 5, 255};
    return b;
}

void test_layout_little_endian() {
    uint8_t buf[beacon::MAX_SIZE];
    beacon::Beacon b = esempio();
    size_t n = beacon::pack(b, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(16, n);
    const uint8_t atteso[] = {0x4D, 1, 2, 0x04, 0x03, 0x02, 0x01, 2,
                              1, 0xC3, 3, 255,   // -61 = 0xC3
                              3, 0xBA, 5, 255};  // -70 = 0xBA
    TEST_ASSERT_EQUAL_UINT8_ARRAY(atteso, buf, sizeof(atteso));
}

void test_round_trip() {
    uint8_t buf[beacon::MAX_SIZE];
    beacon::Beacon b = esempio(), r = {};
    size_t n = beacon::pack(b, buf, sizeof(buf));
    TEST_ASSERT_TRUE(beacon::unpack(buf, n, r));
    TEST_ASSERT_EQUAL(2, r.id);
    TEST_ASSERT_EQUAL_UINT32(0x01020304u, r.seq);
    TEST_ASSERT_EQUAL(2, r.n);
    TEST_ASSERT_EQUAL(-61, r.nb[0].rssi);
    TEST_ASSERT_EQUAL(3, r.nb[0].age_ds);
    TEST_ASSERT_EQUAL(3, r.nb[1].id);
    TEST_ASSERT_EQUAL(-70, r.nb[1].rssi);
    TEST_ASSERT_EQUAL(255, r.nb[1].variance);
}

void test_zero_e_massimo_vicini() {
    uint8_t buf[beacon::MAX_SIZE];
    beacon::Beacon b = {}, r = {};
    b.id = 1;
    TEST_ASSERT_EQUAL(beacon::HEADER_SIZE, beacon::pack(b, buf, sizeof(buf)));
    TEST_ASSERT_TRUE(beacon::unpack(buf, beacon::HEADER_SIZE, r));
    TEST_ASSERT_EQUAL(0, r.n);

    b.n = beacon::MAX_NEIGHBORS;
    TEST_ASSERT_EQUAL(beacon::MAX_SIZE, beacon::pack(b, buf, sizeof(buf)));
    TEST_ASSERT_TRUE(beacon::unpack(buf, beacon::MAX_SIZE, r));
    b.n = beacon::MAX_NEIGHBORS + 1;
    TEST_ASSERT_EQUAL(0, beacon::pack(b, buf, sizeof(buf)));
}

void test_buffer_troppo_piccolo() {
    uint8_t buf[10];
    beacon::Beacon b = esempio();
    TEST_ASSERT_EQUAL(0, beacon::pack(b, buf, sizeof(buf)));
}

void test_scarta_magic_versione_lunghezza() {
    uint8_t buf[beacon::MAX_SIZE];
    beacon::Beacon b = esempio(), r = {};
    size_t n = beacon::pack(b, buf, sizeof(buf));

    uint8_t x[beacon::MAX_SIZE];
    memcpy(x, buf, n);
    x[0] = 0x4E;
    TEST_ASSERT_FALSE(beacon::unpack(x, n, r));
    memcpy(x, buf, n);
    x[1] = 2;
    TEST_ASSERT_FALSE(beacon::unpack(x, n, r));
    TEST_ASSERT_FALSE(beacon::unpack(buf, n - 1, r));  // troncato
    TEST_ASSERT_FALSE(beacon::unpack(buf, 3, r));
    memcpy(x, buf, n);
    x[7] = 9;  // n vicini oltre il massimo
    TEST_ASSERT_FALSE(beacon::unpack(x, n, r));
}

void test_eta_saturata() {
    TEST_ASSERT_EQUAL(0, beacon::age_ds(99));
    TEST_ASSERT_EQUAL(1, beacon::age_ds(100));
    TEST_ASSERT_EQUAL(254, beacon::age_ds(25400));
    TEST_ASSERT_EQUAL(255, beacon::age_ds(25500));
    TEST_ASSERT_EQUAL(255, beacon::age_ds(4000000000u));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_layout_little_endian);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_zero_e_massimo_vicini);
    RUN_TEST(test_buffer_troppo_piccolo);
    RUN_TEST(test_scarta_magic_versione_lunghezza);
    RUN_TEST(test_eta_saturata);
    return UNITY_END();
}

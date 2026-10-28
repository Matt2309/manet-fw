#include <unity.h>

#include "fix.h"

void setUp() {}
void tearDown() {}

static GpsEpoch buono() {
    GpsEpoch e = {};
    e.t_ms = 5000;
    e.has_pos = true;
    e.lat = 44.4936;
    e.lon = 11.3426;
    e.has_sat = true;
    e.sat = 8;
    e.has_hdop = true;
    e.hdop = 1.2f;
    e.fix = 3;
    return e;
}

void test_fix_buono() { TEST_ASSERT_TRUE(fix_valid(buono())); }

void test_satelliti_minimi() {
    GpsEpoch e = buono();
    e.sat = 6;
    TEST_ASSERT_TRUE(fix_valid(e));
    e.sat = 5;
    TEST_ASSERT_FALSE(fix_valid(e));
    e.has_sat = false;
    e.sat = 9;
    TEST_ASSERT_FALSE(fix_valid(e));
}

void test_hdop_massimo() {
    GpsEpoch e = buono();
    e.hdop = 2.5f;
    TEST_ASSERT_TRUE(fix_valid(e));
    e.hdop = 2.6f;
    TEST_ASSERT_FALSE(fix_valid(e));
    e.has_hdop = false;
    e.hdop = 1.0f;
    TEST_ASSERT_FALSE(fix_valid(e));
}

void test_serve_il_3d_e_la_posizione() {
    GpsEpoch e = buono();
    e.fix = 2;
    TEST_ASSERT_FALSE(fix_valid(e));
    e.fix = 0;
    TEST_ASSERT_FALSE(fix_valid(e));
    e = buono();
    e.has_pos = false;
    TEST_ASSERT_FALSE(fix_valid(e));
}

void test_campione() {
    GpsEpoch e = buono();
    GnssSample s = sample_from_epoch(e);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_UINT32(5000, s.t_ms);
    TEST_ASSERT_EQUAL_INT32(444936000, s.pos.lat_e7);
    TEST_ASSERT_EQUAL_INT32(113426000, s.pos.lon_e7);
    TEST_ASSERT_EQUAL_UINT8(12, s.pos.hdop_dm);

    e.sat = 4;
    s = sample_from_epoch(e);
    TEST_ASSERT_FALSE(s.valid);
    TEST_ASSERT_EQUAL_UINT32(5000, s.t_ms);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fix_buono);
    RUN_TEST(test_satelliti_minimi);
    RUN_TEST(test_hdop_massimo);
    RUN_TEST(test_serve_il_3d_e_la_posizione);
    RUN_TEST(test_campione);
    return UNITY_END();
}

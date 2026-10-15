#include <string.h>
#include <unity.h>

#include "csv.h"

void setUp() {}
void tearDown() {}

void test_header() {
    char b[256];
    size_t n = csv_header(b, sizeof(b), "1.0.0", 2, "AA:BB:CC:DD:EE:FF", 1, 40, 500, 50, "11b_1M");
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "# manet-fw 1.0.0 id=2 mac=AA:BB:CC:DD:EE:FF canale=1 potenza_dbm=10.00 periodo_ms=500 "
        "jitter_ms=50 rate=11b_1M\n",
        b);
}

void test_tx_evt_bat() {
    char b[64];
    csv_tx(b, sizeof(b), 1234, 7);
    TEST_ASSERT_EQUAL_STRING("TX,1234,7\n", b);
    csv_evt(b, sizeof(b), 99, 3);
    TEST_ASSERT_EQUAL_STRING("EVT,99,3\n", b);
    csv_bat(b, sizeof(b), 30000, 3920);
    TEST_ASSERT_EQUAL_STRING("BAT,30000,3920\n", b);
}

void test_rx_con_tabella() {
    char b[128];
    beacon::Entry t[2] = {{2, -61, 3, 255}, {3, -70, 5, 255}};
    size_t n = csv_rx(b, sizeof(b), 5000, 1, 42, -58, t, 2);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("RX,5000,1,42,-58,2:-61:3:255;3:-70:5:255\n", b);
}

void test_rx_tabella_vuota() {
    char b[64];
    csv_rx(b, sizeof(b), 5000, 3, 0, -99, nullptr, 0);
    TEST_ASSERT_EQUAL_STRING("RX,5000,3,0,-99,\n", b);
}

void test_rx_buffer_piccolo() {
    char b[20];
    beacon::Entry t[1] = {{2, -61, 3, 255}};
    TEST_ASSERT_EQUAL(0, csv_rx(b, sizeof(b), 5000, 1, 42, -58, t, 1));
}

void test_gps_completo() {
    GpsEpoch e = {};
    e.t_ms = 8000;
    e.has_time = e.has_date = true;
    e.year = 2026; e.month = 10; e.day = 7;
    e.hour = 12; e.minute = 34; e.second = 56; e.centisecond = 25;
    e.has_pos = true; e.lat = 44.4936; e.lon = 11.3426;
    e.has_alt = true; e.alt = 54.3f;
    e.has_speed = true; e.speed_kmh = 11.5f;
    e.has_course = true; e.course = 270.0f;
    e.has_hdop = true; e.hdop = 0.9f;
    e.has_sat = true; e.sat = 9;
    e.fix = 3;
    char b[200];
    csv_gps(b, sizeof(b), 8000, e);
    TEST_ASSERT_EQUAL_STRING(
        "GPS,8000,2026-10-07T12:34:56.25Z,44.4936000,11.3426000,54.3,11.50,270.0,0.90,9,3\n", b);
}

void test_gps_senza_fix() {
    GpsEpoch e = {};
    e.has_time = e.has_date = true;
    e.year = 2026; e.month = 1; e.day = 2;
    e.hour = 3; e.minute = 4; e.second = 5; e.centisecond = 0;
    e.has_sat = true; e.sat = 0;
    e.fix = 0;
    char b[200];
    csv_gps(b, sizeof(b), 100, e);
    TEST_ASSERT_EQUAL_STRING("GPS,100,2026-01-02T03:04:05.00Z,,,,,,,0,0\n", b);
}

void test_gps_senza_ora() {
    GpsEpoch e = {};
    char b[200];
    csv_gps(b, sizeof(b), 100, e);
    TEST_ASSERT_EQUAL_STRING("GPS,100,,,,,,,,,0\n", b);
}

void test_sys_pulisce_il_messaggio() {
    char b[200];
    csv_sys(b, sizeof(b), 77, "coda piena persi=3");
    TEST_ASSERT_EQUAL_STRING("SYS,77,coda piena persi=3\n", b);
    csv_sys(b, sizeof(b), 77, "a,b\nc");
    TEST_ASSERT_EQUAL_STRING("SYS,77,a b c\n", b);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_header);
    RUN_TEST(test_tx_evt_bat);
    RUN_TEST(test_rx_con_tabella);
    RUN_TEST(test_rx_tabella_vuota);
    RUN_TEST(test_rx_buffer_piccolo);
    RUN_TEST(test_gps_completo);
    RUN_TEST(test_gps_senza_fix);
    RUN_TEST(test_gps_senza_ora);
    RUN_TEST(test_sys_pulisce_il_messaggio);
    return UNITY_END();
}

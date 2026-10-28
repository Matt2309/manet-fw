#include <string.h>
#include <unity.h>

#include "csv.h"

void setUp() {}
void tearDown() {}

void test_header() {
    char b[256];
    size_t n = csv_header(b, sizeof(b), "2.0.0", 2, "CAPO", "AA:BB:CC:DD:EE:FF", 1, 40, 500, 50, "11b_1M", 2);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "# manet-fw 2.0.0 id=2 ruolo=CAPO mac=AA:BB:CC:DD:EE:FF canale=1 potenza_dbm=10.00 periodo_ms=500 "
        "jitter_ms=50 rate=11b_1M proto=2\n",
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

static packets::Beacon beacon_esempio() {
    packets::Beacon x = {};
    x.sender = 1;
    x.seq = 42;
    x.hops = 0;
    x.t_s = packets::T_NOT_STARTED;
    x.battery = 5;
    x.n = 2;
    x.nb[0] = {2, -61, -3, 25, 3};
    x.nb[1] = {3, -70, 4, 255, 5};
    return x;
}

void test_rx_con_tabella() {
    char b[128];
    packets::Beacon x = beacon_esempio();
    size_t n = csv_rx(b, sizeof(b), 5000, x, -58, false);
    TEST_ASSERT_GREATER_THAN(0, n);
    // I primi sei campi come nel formato di prima; la tabella ha id:segnale:eta:incertezza:pendenza.
    TEST_ASSERT_EQUAL_STRING("RX,5000,1,42,-58,2:-61:3:25:-3;3:-70:5:255:4,0,,5\n", b);
}

void test_rx_con_tempo_della_corsa_e_stato() {
    char b[128];
    packets::Beacon x = beacon_esempio();
    x.n = 0;
    x.t_s = 125;
    x.hops = 2;
    x.sos = true;
    csv_rx(b, sizeof(b), 5000, x, -58, false);
    TEST_ASSERT_EQUAL_STRING("RX,5000,1,42,-58,,2,125,37\n", b);  // 5 | (1 << 5)
}

void test_rx_ridotta() {
    char b[128];
    packets::Beacon x = beacon_esempio();
    csv_rx(b, sizeof(b), 5000, x, -58, true);
    TEST_ASSERT_EQUAL_STRING("RX,5000,1,42,-58,\n", b);
}

void test_rx_tabella_vuota_e_massima() {
    char b[512];
    packets::Beacon x = {};
    x.sender = 3;
    csv_rx(b, sizeof(b), 5000, x, -99, true);
    TEST_ASSERT_EQUAL_STRING("RX,5000,3,0,-99,\n", b);

    x.n = packets::MAX_NEIGHBORS;
    for (int i = 0; i < x.n; i++) x.nb[i] = {static_cast<uint8_t>(i + 1), -100, -128, 255, 255};
    TEST_ASSERT_GREATER_THAN(0, csv_rx(b, sizeof(b), 123456789, x, -100, false));
}

void test_rx_buffer_piccolo() {
    char b[20];
    packets::Beacon x = beacon_esempio();
    TEST_ASSERT_EQUAL(0, csv_rx(b, sizeof(b), 5000, x, -58, false));
}

void test_params() {
    char b[1024];
    size_t n = csv_params(b, sizeof(b));
    TEST_ASSERT_GREATER_THAN(0, n);
    // Ogni riga e' un commento "# param ...", cosi' gli script che saltano i '#' non la vedono.
    int righe = 0;
    for (const char* p = b; *p != '\0';) {
        TEST_ASSERT_EQUAL_INT(0, strncmp(p, "# param ", 8));
        const char* nl = strchr(p, '\n');
        TEST_ASSERT_NOT_NULL(nl);
        righe++;
        p = nl + 1;
    }
    TEST_ASSERT_EQUAL(4, righe);
    TEST_ASSERT_NOT_NULL(strstr(b, "proto=2 "));
    TEST_ASSERT_NOT_NULL(strstr(b, "tick_ms=100 "));
    TEST_ASSERT_NOT_NULL(strstr(b, "rx_ridotta=0\n"));
    TEST_ASSERT_NOT_NULL(strstr(b, "tasto_lungo_ms=3000 "));
    TEST_ASSERT_NOT_NULL(strstr(b, "kalman_r=10.84 "));
    TEST_ASSERT_NOT_NULL(strstr(b, "pre_dbm=-93.0 "));
    TEST_ASSERT_NOT_NULL(strstr(b, "rilascio_ricezione=0.80 "));
    TEST_ASSERT_EQUAL(0, csv_params(b, 20));
}

void test_righe_nuove() {
    char b[128];
    csv_kal(b, sizeof(b), 2000, 3, -71.256f, -0.5f, 2.5f, 4);
    TEST_ASSERT_EQUAL_STRING("KAL,2000,3,-71.26,-0.500,2.50,4\n", b);

    csv_det(b, sizeof(b), 9000, DetVersion::Base, DetKind::PreAlarm, true, 3, -93.4);
    TEST_ASSERT_EQUAL_STRING("DET,9000,base,pre,inizio,3,-93.40\n", b);
    csv_det(b, sizeof(b), 9500, DetVersion::Gnss, DetKind::Lost, false, 3, 0.0);
    TEST_ASSERT_EQUAL_STRING("DET,9500,gnss,perso,fine,3,\n", b);
    csv_det(b, sizeof(b), 9600, DetVersion::Base, DetKind::Deaf, true, 3, 0.0);
    TEST_ASSERT_EQUAL_STRING("DET,9600,base,sorda,inizio,3,\n", b);

    csv_tel(b, sizeof(b), 3000, TelAction::Produced, 2, 125, 0, 7, 0);
    TEST_ASSERT_EQUAL_STRING("TEL,3000,prodotto,2,125,0,7,0\n", b);
    csv_tel(b, sizeof(b), 3100, TelAction::Confirmed, 2, 125, 1, 6, 2);
    TEST_ASSERT_EQUAL_STRING("TEL,3100,confermato,2,125,1,6,2\n", b);
    csv_tel(b, sizeof(b), 3200, TelAction::Queued, 2, 125, 0, 8, 0);
    TEST_ASSERT_EQUAL_STRING("TEL,3200,coda,2,125,0,8,0\n", b);

    csv_cmd(b, sizeof(b), 4000, packets::Cmd::Start, CmdOrigin::App, 1, 0);
    TEST_ASSERT_EQUAL_STRING("CMD,4000,start,app,1,0\n", b);
    csv_cmd(b, sizeof(b), 4100, packets::Cmd::Stop, CmdOrigin::Radio, 9, 12);
    TEST_ASSERT_EQUAL_STRING("CMD,4100,stop,radio,9,12\n", b);

    csv_ble(b, sizeof(b), 5000, BleEvent::Connected, 0);
    TEST_ASSERT_EQUAL_STRING("BLE,5000,connesso,0\n", b);
    csv_ble(b, sizeof(b), 5100, BleEvent::Dropped, 8);
    TEST_ASSERT_EQUAL_STRING("BLE,5100,caduto,8\n", b);

    char piccolo[10];
    TEST_ASSERT_EQUAL(0, csv_kal(piccolo, sizeof(piccolo), 2000, 3, -71.0f, 0.0f, 2.5f, 4));
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
    RUN_TEST(test_rx_con_tempo_della_corsa_e_stato);
    RUN_TEST(test_rx_ridotta);
    RUN_TEST(test_rx_tabella_vuota_e_massima);
    RUN_TEST(test_rx_buffer_piccolo);
    RUN_TEST(test_params);
    RUN_TEST(test_righe_nuove);
    RUN_TEST(test_gps_completo);
    RUN_TEST(test_gps_senza_fix);
    RUN_TEST(test_gps_senza_ora);
    RUN_TEST(test_sys_pulisce_il_messaggio);
    return UNITY_END();
}

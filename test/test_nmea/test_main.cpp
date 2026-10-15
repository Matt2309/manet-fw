#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "nmea.h"

void setUp() {}
void tearDown() {}

// Costruisce "$corpo*CS\r\n" con il checksum giusto.
static void frase(char* out, size_t cap, const char* corpo) {
    uint8_t cs = 0;
    for (const char* p = corpo; *p; p++) cs ^= static_cast<uint8_t>(*p);
    snprintf(out, cap, "$%s*%02X\r\n", corpo, cs);
}

// Passa una frase al parser con tempo di arrivo t; restituisce true se completa un'epoca.
static bool manda(NmeaParser& p, const char* corpo, uint32_t t, GpsEpoch& out) {
    char s[200];
    frase(s, sizeof(s), corpo);
    bool fatto = false;
    for (const char* c = s; *c; c++) {
        if (p.feed(*c, t, out)) fatto = true;
    }
    return fatto;
}

void test_checksum() {
    TEST_ASSERT_TRUE(nmea_checksum_ok(
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"));
    TEST_ASSERT_TRUE(nmea_checksum_ok(
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A"));
    TEST_ASSERT_FALSE(nmea_checksum_ok(
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48"));
    TEST_ASSERT_FALSE(nmea_checksum_ok("$GPGGA,123519"));
    TEST_ASSERT_FALSE(nmea_checksum_ok("GPGGA,1*00"));
    TEST_ASSERT_FALSE(nmea_checksum_ok("$GPGGA,1*ZZ"));
}

// Un'epoca tipica del UC6580: GGA, GSA multipli, RMC; poi quella dopo la chiude.
static void epoca_con_fix(NmeaParser& p, const char* ora, uint32_t t, GpsEpoch& out, bool& chiusa) {
    char b[160];
    snprintf(b, sizeof(b), "GNGGA,%s,4429.6160,N,01120.5560,E,1,09,0.95,54.3,M,48.0,M,,", ora);
    chiusa = manda(p, b, t, out) || chiusa;
    chiusa = manda(p, "GNGSA,A,3,03,04,06,09,17,19,,,,,,,1.8,0.95,1.5,1", t + 5, out) || chiusa;
    chiusa = manda(p, "GNGSA,A,3,01,02,,,,,,,,,,,1.8,0.95,1.5,4", t + 8, out) || chiusa;
    snprintf(b, sizeof(b), "GNRMC,%s,A,4429.6160,N,01120.5560,E,6.21,270.5,071026,,,A,V", ora);
    chiusa = manda(p, b, t + 12, out) || chiusa;
}

void test_epoca_completa_con_fix() {
    NmeaParser p;
    GpsEpoch out = {};
    bool chiusa = false;
    epoca_con_fix(p, "123456.00", 1000, out, chiusa);
    TEST_ASSERT_FALSE(chiusa);  // si chiude quando arriva l'epoca successiva
    epoca_con_fix(p, "123457.00", 2000, out, chiusa);
    TEST_ASSERT_TRUE(chiusa);

    TEST_ASSERT_EQUAL(1000, out.t_ms);  // arrivo della prima frase dell'epoca
    TEST_ASSERT_TRUE(out.has_time && out.has_date);
    TEST_ASSERT_EQUAL(2026, out.year);
    TEST_ASSERT_EQUAL(10, out.month);
    TEST_ASSERT_EQUAL(7, out.day);
    TEST_ASSERT_EQUAL(12, out.hour);
    TEST_ASSERT_EQUAL(34, out.minute);
    TEST_ASSERT_EQUAL(56, out.second);
    TEST_ASSERT_EQUAL(0, out.centisecond);
    TEST_ASSERT_TRUE(out.has_pos);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 44.4936, out.lat);       // 44 + 29.6160/60
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 11.342600, out.lon);     // 11 + 20.5560/60
    TEST_ASSERT_FLOAT_WITHIN(0.01, 54.3, out.alt);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 6.21 * 1.852, out.speed_kmh);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 270.5, out.course);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.95, out.hdop);
    TEST_ASSERT_EQUAL(9, out.sat);
    TEST_ASSERT_EQUAL(3, out.fix);
}

void test_talker_diversi_e_centesimi() {
    NmeaParser p;
    GpsEpoch out = {};
    manda(p, "GPGGA,010203.45,3351.000,S,15112.000,W,1,07,1.2,10.0,M,0,M,,", 100, out);
    manda(p, "GPRMC,010203.45,A,3351.000,S,15112.000,W,0.0,0.0,010125,,,A", 110, out);
    bool chiusa = manda(p, "GPGGA,010204.45,3351.000,S,15112.000,W,1,07,1.2,10.0,M,0,M,,", 1100, out);
    TEST_ASSERT_TRUE(chiusa);
    TEST_ASSERT_EQUAL(45, out.centisecond);
    TEST_ASSERT_EQUAL(2025, out.year);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -(33.0 + 51.0 / 60.0), out.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -(151.0 + 12.0 / 60.0), out.lon);
    TEST_ASSERT_EQUAL(2, out.fix);  // senza GSA ma con qualita' > 0
}

void test_senza_fix_campi_vuoti() {
    NmeaParser p;
    GpsEpoch out = {};
    manda(p, "GNGGA,000010.00,,,,,0,00,99.99,,,,,,", 500, out);
    manda(p, "GNGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99,1", 505, out);
    manda(p, "GNRMC,000010.00,V,,,,,,,060180,,,N,V", 510, out);
    bool chiusa = manda(p, "GNGGA,000011.00,,,,,0,00,99.99,,,,,,", 1500, out);
    TEST_ASSERT_TRUE(chiusa);
    TEST_ASSERT_EQUAL(0, out.fix);
    TEST_ASSERT_FALSE(out.has_pos);
    TEST_ASSERT_FALSE(out.has_alt);
    TEST_ASSERT_FALSE(out.has_speed);
    TEST_ASSERT_TRUE(out.has_time);
    TEST_ASSERT_TRUE(out.has_date);
    TEST_ASSERT_EQUAL(0, out.sat);
}

void test_fix_2d_dal_gsa() {
    NmeaParser p;
    GpsEpoch out = {};
    manda(p, "GNGGA,000010.00,4429.6160,N,01120.5560,E,1,05,2.5,54.3,M,48.0,M,,", 500, out);
    manda(p, "GNGSA,A,2,03,04,06,09,17,,,,,,,,3.0,2.5,1.5,1", 505, out);
    bool chiusa = manda(p, "GNGGA,000011.00,4429.6160,N,01120.5560,E,1,05,2.5,54.3,M,48.0,M,,", 1500, out);
    TEST_ASSERT_TRUE(chiusa);
    TEST_ASSERT_EQUAL(2, out.fix);
}

void test_checksum_errato_scartato() {
    NmeaParser p;
    GpsEpoch out = {};
    const char* rotta = "$GNGGA,000010.00,4429.6160,N,01120.5560,E,1,05,2.5,54.3,M,48.0,M,,*00\r\n";
    for (const char* c = rotta; *c; c++) TEST_ASSERT_FALSE(p.feed(*c, 100, out));
    // L'epoca successiva non chiude nulla perche' la prima frase e' stata scartata.
    TEST_ASSERT_FALSE(manda(p, "GNGGA,000011.00,4429.6160,N,01120.5560,E,1,05,2.5,54.3,M,48.0,M,,", 1100, out));
}

void test_frasi_ignorate_e_spazzatura() {
    NmeaParser p;
    GpsEpoch out = {};
    manda(p, "GNGSV,3,1,10,01,50,100,40,02,40,200,38", 100, out);
    manda(p, "GNVTG,270.5,T,,M,6.21,N,11.5,K,A", 110, out);
    manda(p, "PQTMVER,1.0", 120, out);
    for (const char* c = "\x01\xFF rumore senza dollaro\r\n"; *c; c++) TEST_ASSERT_FALSE(p.feed(*c, 130, out));
    TEST_ASSERT_FALSE(manda(p, "GNGGA,000010.00,,,,,0,00,99.99,,,,,,", 200, out));
    TEST_ASSERT_TRUE(manda(p, "GNGGA,000011.00,,,,,0,00,99.99,,,,,,", 1200, out));
    TEST_ASSERT_EQUAL(200, out.t_ms);
}

void test_riga_troppo_lunga() {
    NmeaParser p;
    GpsEpoch out = {};
    char lunga[400];
    memset(lunga, 'A', sizeof(lunga));
    lunga[0] = '$';
    lunga[sizeof(lunga) - 1] = '\0';
    for (const char* c = lunga; *c; c++) TEST_ASSERT_FALSE(p.feed(*c, 100, out));
    // il parser si riprende
    manda(p, "GNGGA,000010.00,,,,,0,00,99.99,,,,,,", 200, out);
    TEST_ASSERT_TRUE(manda(p, "GNGGA,000011.00,,,,,0,00,99.99,,,,,,", 1200, out));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_checksum);
    RUN_TEST(test_epoca_completa_con_fix);
    RUN_TEST(test_talker_diversi_e_centesimi);
    RUN_TEST(test_senza_fix_campi_vuoti);
    RUN_TEST(test_fix_2d_dal_gsa);
    RUN_TEST(test_checksum_errato_scartato);
    RUN_TEST(test_frasi_ignorate_e_spazzatura);
    RUN_TEST(test_riga_troppo_lunga);
    return UNITY_END();
}

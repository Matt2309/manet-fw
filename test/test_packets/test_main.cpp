#include <string.h>
#include <unity.h>

#include "packets.h"

using namespace packets;

void setUp() {}
void tearDown() {}

static Beacon esempio() {
    Beacon b = {};
    b.sender = 2;
    b.seq = 0x0102;
    b.hops = 1;
    b.t_s = 0x0304;
    b.battery = 9;
    b.sos = true;
    b.n = 2;
    b.nb[0] = {1, -61, -3, 25, 3};
    b.nb[1] = {3, -70, 4, 255, 5};
    return b;
}

static Position pos_esempio() { return Position{455000000, 90000000, 12}; }

void test_beacon_layout() {
    uint8_t buf[MAX_SIZE];
    Beacon b = esempio();
    size_t n = pack_beacon(b, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(11 + 5 * 2, n);
    const uint8_t atteso[] = {0x4D, PROTO_VERSION, 1, 2,        // intestazione
                              0x02, 0x01,                       // seq
                              1,                                // salti
                              0x04, 0x03,                       // t_s
                              9 | (1 << 5),                     // batteria 9 + SOS
                              2,                                // vicini
                              1, 0xC3, 0xFD, 25, 3,             // -61, -3
                              3, 0xBA, 4, 255, 5};              // -70
    TEST_ASSERT_EQUAL_UINT8_ARRAY(atteso, buf, sizeof(atteso));
}

void test_beacon_con_posizione() {
    uint8_t buf[MAX_SIZE];
    Beacon b = esempio(), r = {};
    b.has_pos = true;
    b.pos = pos_esempio();
    size_t n = pack_beacon(b, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(11 + 9 + 5 * 2, n);
    TEST_ASSERT_BITS_HIGH(1 << 6, buf[9]);
    TEST_ASSERT_EQUAL_UINT8(0xC0, buf[11]);  // 455000000 = 0x1B17DC0: byte basso per primo
    TEST_ASSERT_TRUE(unpack_beacon(buf, n, r));
    TEST_ASSERT_TRUE(r.has_pos);
    TEST_ASSERT_EQUAL_INT32(455000000, r.pos.lat_e7);
    TEST_ASSERT_EQUAL_INT32(90000000, r.pos.lon_e7);
    TEST_ASSERT_EQUAL_UINT8(12, r.pos.hdop_dm);
    TEST_ASSERT_EQUAL(3, r.nb[1].id);
}

void test_beacon_round_trip() {
    uint8_t buf[MAX_SIZE];
    Beacon b = esempio(), r = {};
    b.t_s = T_NOT_STARTED;
    b.stopped = true;
    size_t n = pack_beacon(b, buf, sizeof(buf));
    TEST_ASSERT_TRUE(unpack_beacon(buf, n, r));
    TEST_ASSERT_EQUAL(2, r.sender);
    TEST_ASSERT_EQUAL_UINT16(0x0102, r.seq);
    TEST_ASSERT_EQUAL(1, r.hops);
    TEST_ASSERT_EQUAL_UINT16(T_NOT_STARTED, r.t_s);
    TEST_ASSERT_EQUAL(9, r.battery);
    TEST_ASSERT_TRUE(r.stopped);
    TEST_ASSERT_TRUE(r.sos);
    TEST_ASSERT_FALSE(r.has_pos);
    TEST_ASSERT_EQUAL(-61, r.nb[0].signal);
    TEST_ASSERT_EQUAL(-3, r.nb[0].slope);
    TEST_ASSERT_EQUAL(255, r.nb[1].sigma);
}

void test_beacon_limiti() {
    uint8_t buf[MAX_SIZE];
    Beacon b = {}, r = {};
    b.sender = 1;
    TEST_ASSERT_EQUAL(11, pack_beacon(b, buf, sizeof(buf)));
    TEST_ASSERT_TRUE(unpack_beacon(buf, 11, r));
    b.n = MAX_NEIGHBORS;
    b.has_pos = true;
    size_t n = pack_beacon(b, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(11 + 9 + 5 * 19, n);
    TEST_ASSERT_TRUE(n <= MAX_SIZE);
    b.n = MAX_NEIGHBORS + 1;
    TEST_ASSERT_EQUAL(0, pack_beacon(b, buf, sizeof(buf)));
    b = esempio();
    TEST_ASSERT_EQUAL(0, pack_beacon(b, buf, 10));
}

void test_scarto_intestazione_e_lunghezza() {
    uint8_t buf[MAX_SIZE], x[MAX_SIZE];
    Beacon b = esempio(), r = {};
    size_t n = pack_beacon(b, buf, sizeof(buf));

    memcpy(x, buf, n);
    x[0] = 0x4E;
    TEST_ASSERT_FALSE(unpack_beacon(x, n, r));
    memcpy(x, buf, n);
    x[1] = PROTO_VERSION + 1;
    TEST_ASSERT_FALSE(unpack_beacon(x, n, r));
    memcpy(x, buf, n);
    x[2] = 9;  // tipo sconosciuto
    TEST_ASSERT_FALSE(unpack_beacon(x, n, r));
    memcpy(x, buf, n);
    x[2] = static_cast<uint8_t>(Type::Alarm);  // tipo diverso da quello atteso
    TEST_ASSERT_FALSE(unpack_beacon(x, n, r));
    TEST_ASSERT_FALSE(unpack_beacon(buf, n - 1, r));
    TEST_ASSERT_FALSE(unpack_beacon(buf, 3, r));
    memcpy(x, buf, n);
    x[10] = 20;  // vicini oltre il massimo
    TEST_ASSERT_FALSE(unpack_beacon(x, n, r));
    memcpy(x, buf, n);
    x[9] |= 1 << 6;  // dichiara una posizione che non c'e'
    TEST_ASSERT_FALSE(unpack_beacon(x, n, r));
}

void test_read_header() {
    uint8_t buf[MAX_SIZE];
    Beacon b = esempio();
    size_t n = pack_beacon(b, buf, sizeof(buf));
    Type t;
    uint8_t s = 0;
    TEST_ASSERT_TRUE(read_header(buf, n, t, s));
    TEST_ASSERT_EQUAL(Type::Beacon, t);
    TEST_ASSERT_EQUAL(2, s);
}

static TelRecord record(uint8_t id, uint8_t n, bool pos) {
    TelRecord r = {};
    r.id = id;
    r.t_s = static_cast<uint16_t>(1000 + id);
    r.battery = 7;
    r.isolated = true;
    r.has_pos = pos;
    if (pos) r.pos = pos_esempio();
    r.n = n;
    for (uint8_t i = 0; i < n; i++) r.nb[i] = TelEntry{static_cast<uint8_t>(i + 1), -80, 20, 4, static_cast<uint8_t>(i % 2)};
    return r;
}

void test_telemetria_layout_e_round_trip() {
    uint8_t buf[MAX_SIZE];
    TelRecord in = record(5, 2, false), out[4] = {};
    size_t packed = 0, count = 0;
    uint8_t sender = 0;
    size_t n = pack_telemetry(5, &in, 1, buf, sizeof(buf), packed);
    TEST_ASSERT_EQUAL(1, packed);
    TEST_ASSERT_EQUAL(4 + 1 + 5 + 5 * 2, n);
    TEST_ASSERT_EQUAL_UINT8(2, buf[2]);   // tipo telemetria
    TEST_ASSERT_EQUAL_UINT8(1, buf[4]);   // numero di record
    TEST_ASSERT_EQUAL_UINT8(5, buf[5]);   // id del record
    TEST_ASSERT_EQUAL_UINT8(7 | (1 << 5), buf[8]);
    TEST_ASSERT_TRUE(unpack_telemetry(buf, n, sender, out, 4, count));
    TEST_ASSERT_EQUAL(5, sender);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL_UINT16(1005, out[0].t_s);
    TEST_ASSERT_TRUE(out[0].isolated);
    TEST_ASSERT_FALSE(out[0].sos);
    TEST_ASSERT_EQUAL(2, out[0].n);
    TEST_ASSERT_EQUAL(1, out[0].nb[1].alarm);
}

void test_telemetria_riempie_fino_a_250() {
    uint8_t buf[MAX_SIZE];
    TelRecord in[10], out[10];
    for (int i = 0; i < 10; i++) in[i] = record(static_cast<uint8_t>(i + 1), 19, true);  // 5 + 95 + 9 = 109 byte
    size_t packed = 0, count = 0;
    uint8_t sender = 0;
    size_t n = pack_telemetry(1, in, 10, buf, sizeof(buf), packed);
    TEST_ASSERT_EQUAL(2, packed);  // 5 + 2 * 109 = 223; il terzo sforerebbe 250
    TEST_ASSERT_EQUAL(223, n);
    TEST_ASSERT_TRUE(unpack_telemetry(buf, n, sender, out, 10, count));
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_EQUAL(2, out[1].id);

    // Record piccoli: ne entrano molti, mai oltre il limite.
    for (int i = 0; i < 10; i++) in[i] = record(static_cast<uint8_t>(i + 1), 0, false);
    n = pack_telemetry(1, in, 10, buf, sizeof(buf), packed);
    TEST_ASSERT_EQUAL(10, packed);
    TEST_ASSERT_EQUAL(5 + 10 * 5, n);

    // Buffer troppo piccolo anche per il primo record.
    TEST_ASSERT_EQUAL(0, pack_telemetry(1, in, 10, buf, 8, packed));
    TEST_ASSERT_EQUAL(0, packed);
}

void test_telemetria_scarta_malformati() {
    uint8_t buf[MAX_SIZE];
    TelRecord in = record(5, 2, false), out[4];
    size_t packed, count;
    uint8_t sender;
    size_t n = pack_telemetry(5, &in, 1, buf, sizeof(buf), packed);
    TEST_ASSERT_FALSE(unpack_telemetry(buf, n - 1, sender, out, 4, count));
    TEST_ASSERT_FALSE(unpack_telemetry(buf, n + 1, sender, out, 4, count));  // byte in coda
    buf[4] = 0;
    TEST_ASSERT_FALSE(unpack_telemetry(buf, n, sender, out, 4, count));
    buf[4] = 5;  // troppi record rispetto a max
    TEST_ASSERT_FALSE(unpack_telemetry(buf, n, sender, out, 4, count));
}

void test_allarme() {
    uint8_t buf[MAX_SIZE];
    Alarm a = {};
    a.sender = 3;
    a.seq = 200;
    a.runner = 4;
    a.type = AlarmType::Lost;
    a.last_t_s = 0x0A0B;
    a.last_signal = -90;
    size_t n = pack_alarm(a, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(10, n);
    const uint8_t atteso[] = {0x4D, PROTO_VERSION, 3, 3, 200, 4, 2, 0x0B, 0x0A, 0xA6};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(atteso, buf, sizeof(atteso));

    Alarm r = {};
    TEST_ASSERT_TRUE(unpack_alarm(buf, n, r));
    TEST_ASSERT_EQUAL(3, r.sender);
    TEST_ASSERT_EQUAL(AlarmType::Lost, r.type);
    TEST_ASSERT_EQUAL(-90, r.last_signal);
    TEST_ASSERT_FALSE(r.has_pos);

    a.has_pos = true;
    a.pos = pos_esempio();
    n = pack_alarm(a, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(19, n);
    TEST_ASSERT_TRUE(unpack_alarm(buf, n, r));
    TEST_ASSERT_TRUE(r.has_pos);
    TEST_ASSERT_EQUAL_INT32(455000000, r.pos.lat_e7);

    TEST_ASSERT_FALSE(unpack_alarm(buf, 12, r));
    buf[6] = 0;  // tipo non valido
    TEST_ASSERT_FALSE(unpack_alarm(buf, n, r));
    buf[6] = 5;
    TEST_ASSERT_FALSE(unpack_alarm(buf, n, r));
}

void test_comando() {
    uint8_t buf[MAX_SIZE];
    Command c = {1, 7, Cmd::Start, 0x0102};
    size_t n = pack_command(c, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(8, n);
    const uint8_t atteso[] = {0x4D, PROTO_VERSION, 4, 1, 7, 1, 0x02, 0x01};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(atteso, buf, sizeof(atteso));
    Command r = {};
    TEST_ASSERT_TRUE(unpack_command(buf, n, r));
    TEST_ASSERT_EQUAL(Cmd::Start, r.cmd);
    TEST_ASSERT_EQUAL_UINT16(0x0102, r.elapsed_ds);
    TEST_ASSERT_FALSE(unpack_command(buf, 7, r));
    buf[5] = 3;
    TEST_ASSERT_FALSE(unpack_command(buf, n, r));
}

void test_quantizzazione() {
    TEST_ASSERT_EQUAL(0, age_ds(99));
    TEST_ASSERT_EQUAL(1, age_ds(100));
    TEST_ASSERT_EQUAL(255, age_ds(25500));
    TEST_ASSERT_EQUAL(255, age_ds(4000000000u));

    TEST_ASSERT_EQUAL(-61, quantize_i8(-60.6f, 1.0f));
    TEST_ASSERT_EQUAL(127, quantize_i8(500.0f, 1.0f));
    TEST_ASSERT_EQUAL(-128, quantize_i8(-500.0f, 1.0f));
    TEST_ASSERT_EQUAL(-12, quantize_i8(-1.2f, SLOPE_STEP));
    TEST_ASSERT_EQUAL(32, quantize_u8(3.2f, SIGMA_STEP));
    TEST_ASSERT_EQUAL(254, quantize_u8(100.0f, SIGMA_STEP));  // 255 resta riservato a "n.d."
    TEST_ASSERT_EQUAL(0, quantize_u8(-1.0f, SIGMA_STEP));

    TEST_ASSERT_EQUAL(0, battery_code(0));
    TEST_ASSERT_EQUAL(15, battery_code(100));
    TEST_ASSERT_EQUAL(8, battery_code(50));
    TEST_ASSERT_EQUAL(100, battery_percent(15));
    TEST_ASSERT_EQUAL(0, battery_percent(0));
    TEST_ASSERT_EQUAL(53, battery_percent(8));

    TEST_ASSERT_EQUAL_INT32(455000000, degrees_to_e7(45.5));
    TEST_ASSERT_EQUAL_INT32(-123456789, degrees_to_e7(-12.3456789));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_beacon_layout);
    RUN_TEST(test_beacon_con_posizione);
    RUN_TEST(test_beacon_round_trip);
    RUN_TEST(test_beacon_limiti);
    RUN_TEST(test_scarto_intestazione_e_lunghezza);
    RUN_TEST(test_read_header);
    RUN_TEST(test_telemetria_layout_e_round_trip);
    RUN_TEST(test_telemetria_riempie_fino_a_250);
    RUN_TEST(test_telemetria_scarta_malformati);
    RUN_TEST(test_allarme);
    RUN_TEST(test_comando);
    RUN_TEST(test_quantizzazione);
    return UNITY_END();
}

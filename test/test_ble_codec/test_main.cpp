// Esempi esadecimali di docs/protocollo_ble.md (manet-mobile).
#include <unity.h>

#include "ble_codec.h"

using namespace ble;

void setUp() {}
void tearDown() {}

static Row runner() { return Row{2, RunnerState::Ok, -80, 90, 3}; }

void test_stato_con_un_corridore() {
    uint8_t buf[MAX_MESSAGE_SIZE];
    Row r = runner();
    size_t n = encode_state(1, 5, &r, 1, buf, sizeof(buf));
    const uint8_t atteso[] = {0x01, 0x01, 0x01, 0, 0, 0, 0x05, 0, 0, 0, 0x01, 0x02, 0x00, 0xB0, 0x5A, 0x03, 0x00};
    TEST_ASSERT_EQUAL(sizeof(atteso), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(atteso, buf, n);
}

void test_stato_prima_dello_start() {
    uint8_t buf[MAX_MESSAGE_SIZE];
    Row r = runner();
    size_t n = encode_state(0, T_NOT_STARTED, &r, 1, buf, sizeof(buf));
    const uint8_t atteso[] = {0x01, 0x01, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x00, 0xB0, 0x5A, 0x03, 0x00};
    TEST_ASSERT_EQUAL(sizeof(atteso), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(atteso, buf, n);
}

void test_stato_regole_su_seq_e_t() {
    uint8_t buf[MAX_MESSAGE_SIZE];
    Row r = runner();
    TEST_ASSERT_EQUAL(0, encode_state(0, 5, &r, 1, buf, sizeof(buf)));            // seq 0 con t valido
    TEST_ASSERT_EQUAL(0, encode_state(1, T_NOT_STARTED, &r, 1, buf, sizeof(buf))); // t riservato con seq
    TEST_ASSERT_EQUAL(11, encode_state(0, T_NOT_STARTED, nullptr, 0, buf, sizeof(buf)));  // nessun corridore
}

void test_stato_a_20_corridori_e_limiti() {
    uint8_t buf[MAX_MESSAGE_SIZE];
    Row rows[21];
    for (int i = 0; i < 21; i++) rows[i] = Row{static_cast<uint8_t>(i + 1), RunnerState::Lost, R_ABSENT, 100, 65535};
    TEST_ASSERT_EQUAL(131, encode_state(7, 100, rows, 20, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(0x7F, buf[11 + 2]);
    TEST_ASSERT_EQUAL(0xFF, buf[11 + 5]);                              // eta' u16 massima
    TEST_ASSERT_EQUAL(0, encode_state(7, 100, rows, 21, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(0, encode_state(7, 100, rows, 20, buf, 130));    // buffer troppo piccolo
    rows[0].battery = 150;                                             // la batteria satura a 100
    encode_state(7, 100, rows, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(100, buf[14]);
}

void test_evento_sos() {
    uint8_t buf[EVENT_SIZE];
    size_t n = encode_event(258, 10, 3, EventType::Sos, buf, sizeof(buf));
    const uint8_t atteso[] = {0x01, 0x02, 0x02, 0x01, 0, 0, 0x0A, 0, 0, 0, 0x03, 0x02};
    TEST_ASSERT_EQUAL(12, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(atteso, buf, n);
    TEST_ASSERT_EQUAL(0, encode_event(0, 10, 3, EventType::Sos, buf, sizeof(buf)));            // seq >= 1
    TEST_ASSERT_EQUAL(0, encode_event(1, T_NOT_STARTED, 3, EventType::Sos, buf, sizeof(buf))); // t mai riservato
}

void test_conferme() {
    uint8_t buf[CONFIRM_RESUME_SIZE];
    TEST_ASSERT_EQUAL(4, encode_confirm(CommandCode::Start, false, buf, sizeof(buf)));
    const uint8_t start_err[] = {0x01, 0x03, 0x10, 0x01};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(start_err, buf, 4);
    TEST_ASSERT_EQUAL(8, encode_confirm_resume(true, 258, buf, sizeof(buf)));
    const uint8_t resume_ok[] = {0x01, 0x03, 0x13, 0x00, 0x02, 0x01, 0, 0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(resume_ok, buf, 8);
}

void test_comandi_validi() {
    Command c;
    const uint8_t start[] = {0x01, 0x10}, stop[] = {0x01, 0x11}, close[] = {0x01, 0x12, 0x04, 0x02},
                  resume[] = {0x01, 0x13, 0x02, 0x01, 0, 0};
    TEST_ASSERT_TRUE(decode_command(start, sizeof(start), c));
    TEST_ASSERT_EQUAL(CommandCode::Start, c.code);
    TEST_ASSERT_TRUE(decode_command(stop, sizeof(stop), c));
    TEST_ASSERT_EQUAL(CommandCode::Stop, c.code);
    TEST_ASSERT_TRUE(decode_command(close, sizeof(close), c));
    TEST_ASSERT_EQUAL(CommandCode::CloseAlarm, c.code);
    TEST_ASSERT_EQUAL(4, c.id);
    TEST_ASSERT_EQUAL(AlarmKind::Sos, c.kind);
    TEST_ASSERT_TRUE(decode_command(resume, sizeof(resume), c));
    TEST_ASSERT_EQUAL(CommandCode::ResumeFrom, c.code);
    TEST_ASSERT_EQUAL_UINT32(258, c.last_seq);
}

void test_comandi_non_validi() {
    Command c;
    const uint8_t versione[] = {0x02, 0x10}, corto[] = {0x01}, lungo[] = {0x01, 0x10, 0x00}, codice[] = {0x01, 0x14},
                  tipo[] = {0x01, 0x12, 0x04, 0x03}, close_corto[] = {0x01, 0x12, 0x04},
                  resume_corto[] = {0x01, 0x13, 0x02, 0x01};
    TEST_ASSERT_FALSE(decode_command(versione, sizeof(versione), c));
    TEST_ASSERT_FALSE(decode_command(corto, sizeof(corto), c));
    TEST_ASSERT_FALSE(decode_command(nullptr, 0, c));
    TEST_ASSERT_FALSE(decode_command(lungo, sizeof(lungo), c));
    TEST_ASSERT_FALSE(decode_command(codice, sizeof(codice), c));
    TEST_ASSERT_FALSE(decode_command(tipo, sizeof(tipo), c));
    TEST_ASSERT_FALSE(decode_command(close_corto, sizeof(close_corto), c));
    TEST_ASSERT_FALSE(decode_command(resume_corto, sizeof(resume_corto), c));
}

void test_seq_dei_messaggi_salvati() {
    uint8_t buf[MAX_MESSAGE_SIZE];
    Row r = runner();
    size_t n = encode_state(42, 100, &r, 1, buf, sizeof(buf));
    uint32_t seq = 0;
    TEST_ASSERT_TRUE(message_seq(buf, n, seq));
    TEST_ASSERT_EQUAL_UINT32(42, seq);
    n = encode_event(258, 10, 3, EventType::Return, buf, sizeof(buf));
    TEST_ASSERT_TRUE(message_seq(buf, n, seq));
    TEST_ASSERT_EQUAL_UINT32(258, seq);
    n = encode_confirm(CommandCode::Stop, true, buf, sizeof(buf));
    TEST_ASSERT_FALSE(message_seq(buf, n, seq));  // la CONFERMA non ha seq
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_stato_con_un_corridore);
    RUN_TEST(test_stato_prima_dello_start);
    RUN_TEST(test_stato_regole_su_seq_e_t);
    RUN_TEST(test_stato_a_20_corridori_e_limiti);
    RUN_TEST(test_evento_sos);
    RUN_TEST(test_conferme);
    RUN_TEST(test_comandi_validi);
    RUN_TEST(test_comandi_non_validi);
    RUN_TEST(test_seq_dei_messaggi_salvati);
    return UNITY_END();
}

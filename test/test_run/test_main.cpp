#include <unity.h>

#include "config.h"
#include "run.h"

void setUp() {}
void tearDown() {}

static packets::Beacon beacon(uint16_t t_s, bool stopped = false) {
    packets::Beacon b = {};
    b.sender = 2;
    b.t_s = t_s;
    b.stopped = stopped;
    return b;
}

void test_prima_dello_start() {
    Run r;
    TEST_ASSERT_EQUAL(RunState::NotStarted, r.state());
    TEST_ASSERT_EQUAL_UINT16(packets::T_NOT_STARTED, r.seconds(5000));
    TEST_ASSERT_FALSE(r.stop());
}

void test_start_locale_e_secondi() {
    Run r;
    TEST_ASSERT_TRUE(r.start(10000));
    TEST_ASSERT_EQUAL(RunState::Running, r.state());
    TEST_ASSERT_EQUAL_UINT16(0, r.seconds(10999));
    TEST_ASSERT_EQUAL_UINT16(5, r.seconds(15000));
    TEST_ASSERT_EQUAL_UINT32(10000, r.t0());
}

void test_start_ripetuto_non_azzera_t0() {
    Run r;
    r.start(10000);
    TEST_ASSERT_FALSE(r.start(20000));
    TEST_ASSERT_EQUAL_UINT32(10000, r.t0());
}

void test_stop_e_conteggio_che_continua() {
    Run r;
    r.start(1000);
    TEST_ASSERT_TRUE(r.stop());
    TEST_ASSERT_EQUAL(RunState::Stopped, r.state());
    TEST_ASSERT_EQUAL_UINT16(9, r.seconds(10000));  // continua a contare da T0
    TEST_ASSERT_FALSE(r.stop());
}

void test_t0_dal_comando_con_decimi_trascorsi() {
    Run r;
    // Il comando e' partito 1,5 s fa (15 decimi): T0 = ora - 1500.
    TEST_ASSERT_EQUAL(Run::Change::Started, r.on_command(packets::Cmd::Start, 15, 20000));
    TEST_ASSERT_EQUAL_UINT32(18500, r.t0());
    // Lo stesso comando rilanciato da un altro, 0,3 s dopo, da' lo stesso T0 e non cambia nulla.
    TEST_ASSERT_EQUAL(Run::Change::None, r.on_command(packets::Cmd::Start, 18, 20300));
    TEST_ASSERT_EQUAL_UINT32(18500, r.t0());
}

void test_stop_dal_comando() {
    Run r;
    r.start(1000);
    TEST_ASSERT_EQUAL(Run::Change::Stopped, r.on_command(packets::Cmd::Stop, 2, 5000));
    TEST_ASSERT_EQUAL(Run::Change::None, r.on_command(packets::Cmd::Stop, 2, 5100));
    Run n;
    TEST_ASSERT_EQUAL(Run::Change::None, n.on_command(packets::Cmd::Stop, 2, 5000));  // mai avviata
}

void test_t0_dal_beacon_di_un_compagno() {
    Run r;
    // Il compagno e' a 12 s dal suo T0: ora 50000 -> T0 = 38000.
    TEST_ASSERT_EQUAL(Run::Change::Started, r.on_beacon(beacon(12), 50000));
    TEST_ASSERT_EQUAL_UINT32(38000, r.t0());
    // Un altro compagno, con i secondi arrotondati per difetto, da' un T0 piu' tardi: non lo sposta.
    TEST_ASSERT_EQUAL(Run::Change::None, r.on_beacon(beacon(12), 50800));
    TEST_ASSERT_EQUAL_UINT32(38000, r.t0());
    // Un T0 un po' piu' vecchio (entro la tolleranza) lo affina.
    TEST_ASSERT_EQUAL(Run::Change::None, r.on_beacon(beacon(13), 50500));
    TEST_ASSERT_EQUAL_UINT32(37500, r.t0());
}

void test_beacon_senza_corsa_e_corsa_ferma_prima_di_partire() {
    Run r;
    TEST_ASSERT_EQUAL(Run::Change::None, r.on_beacon(beacon(packets::T_NOT_STARTED), 5000));
    TEST_ASSERT_EQUAL(Run::Change::None, r.on_beacon(beacon(30, true), 50000));  // fermata: non la si adotta
    TEST_ASSERT_EQUAL(RunState::NotStarted, r.state());
}

void test_stop_dal_beacon_solo_se_e_la_stessa_corsa() {
    Run r;
    r.start(10000);
    // Un compagno di una corsa vecchia (T0 molto prima) ferma: non ci riguarda.
    TEST_ASSERT_EQUAL(Run::Change::None, r.on_beacon(beacon(500, true), 50000));
    TEST_ASSERT_EQUAL(RunState::Running, r.state());
    // Un compagno della nostra corsa (T0 ~ 10000) ferma.
    TEST_ASSERT_EQUAL(Run::Change::Stopped, r.on_beacon(beacon(40, true), 50400));
    TEST_ASSERT_EQUAL(RunState::Stopped, r.state());
}

void test_nuova_corsa_dopo_lo_stop() {
    Run r;
    r.start(10000);
    r.stop();
    // Un beacon della corsa vecchia (che non ha ancora saputo dello STOP) non la riapre.
    TEST_ASSERT_EQUAL(Run::Change::None, r.on_beacon(beacon(41), 51000));
    TEST_ASSERT_EQUAL(RunState::Stopped, r.state());
    // Un beacon con T0 successivo di oltre la tolleranza e' una corsa nuova.
    TEST_ASSERT_EQUAL(Run::Change::Started, r.on_beacon(beacon(5), 60000));
    TEST_ASSERT_EQUAL(RunState::Running, r.state());
    TEST_ASSERT_EQUAL_UINT32(55000, r.t0());
}

void test_start_nuovo_mentre_in_corsa_riparte() {
    Run r;
    r.start(10000);
    TEST_ASSERT_EQUAL(Run::Change::Started, r.on_command(packets::Cmd::Start, 0, 100000));
    TEST_ASSERT_EQUAL_UINT32(100000, r.t0());
}

void test_saturazione_dei_secondi() {
    Run r;
    r.start(0);
    TEST_ASSERT_EQUAL_UINT16(0xFFFE, r.seconds(70000u * 1000u));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_prima_dello_start);
    RUN_TEST(test_start_locale_e_secondi);
    RUN_TEST(test_start_ripetuto_non_azzera_t0);
    RUN_TEST(test_stop_e_conteggio_che_continua);
    RUN_TEST(test_t0_dal_comando_con_decimi_trascorsi);
    RUN_TEST(test_stop_dal_comando);
    RUN_TEST(test_t0_dal_beacon_di_un_compagno);
    RUN_TEST(test_beacon_senza_corsa_e_corsa_ferma_prima_di_partire);
    RUN_TEST(test_stop_dal_beacon_solo_se_e_la_stessa_corsa);
    RUN_TEST(test_nuova_corsa_dopo_lo_stop);
    RUN_TEST(test_start_nuovo_mentre_in_corsa_riparte);
    RUN_TEST(test_saturazione_dei_secondi);
    return UNITY_END();
}

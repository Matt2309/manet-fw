#include <unity.h>

#include <initializer_list>
#include <vector>

#include "detector.h"

void setUp() {}
void tearDown() {}

// Osservatore con ID 1; i compagni sono 2 e 3. Per ogni scenario si passa il tempo a mano:
// prima i beacon con t <= istante, poi il tick, come fa il task della logica.
struct Bench {
    Detector det{1};
    std::vector<DetEvent> events;

    void beacon(uint32_t t, uint8_t src, int8_t rssi, std::initializer_list<packets::BeaconEntry> table = {}) {
        packets::Beacon b = {};
        b.sender = src;
        for (const packets::BeaconEntry& e : table) b.nb[b.n++] = e;
        det.on_beacon(b, rssi, t);
    }

    void tick(uint32_t t) {
        DetEvent out[Detector::MAX_EVENTS];
        size_t n = det.tick(t, out);
        for (size_t i = 0; i < n; i++) events.push_back(out[i]);
    }

    // Eventi di un tipo per un compagno, in ordine.
    std::vector<DetEvent> of(DetKind kind, uint8_t target) const {
        std::vector<DetEvent> v;
        for (const DetEvent& e : events) {
            if (e.kind == kind && e.target == target) v.push_back(e);
        }
        return v;
    }
};

void test_voci_del_beacon() {
    Bench b;
    b.beacon(1000, 2, -61);
    b.beacon(1200, 3, -70);
    packets::BeaconEntry e[packets::MAX_NEIGHBORS];
    TEST_ASSERT_EQUAL(2, b.det.fill_entries(2500, e));
    TEST_ASSERT_EQUAL(2, e[0].id);
    TEST_ASSERT_EQUAL(-61, e[0].signal);
    TEST_ASSERT_EQUAL(0, e[0].slope);
    TEST_ASSERT_EQUAL(33, e[0].sigma);  // sqrt(10,84) = 3,29 dB
    TEST_ASSERT_EQUAL(15, e[0].age_ds);  // 1500 ms
    TEST_ASSERT_EQUAL(3, e[1].id);
    TEST_ASSERT_EQUAL(13, e[1].age_ds);
}

void test_stima_predetta() {
    Bench b;
    LinkView v;
    TEST_ASSERT_FALSE(b.det.estimate(2, 1000, v));  // mai sentito
    b.beacon(1000, 2, -61);
    TEST_ASSERT_TRUE(b.det.estimate(2, 3000, v));
    TEST_ASSERT_EQUAL_UINT32(2000, v.age_ms);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -61.0, v.r);
    TEST_ASSERT_TRUE(v.sigma > 3.29);  // l'incertezza cresce con la predizione
    TEST_ASSERT_FALSE(b.det.estimate(1, 3000, v));  // se stesso
}

void test_pre_allarme_dopo_un_secondo() {
    Bench b;
    for (uint32_t t = 0; t <= 10000; t += 100) {
        if (t >= 500 && t % 500 == 0) b.beacon(t, 2, -100);
        b.tick(t);
    }
    std::vector<DetEvent> pre = b.of(DetKind::PreAlarm, 2);
    TEST_ASSERT_EQUAL(1, pre.size());
    TEST_ASSERT_TRUE(pre[0].start);
    TEST_ASSERT_EQUAL_UINT32(1500, pre[0].t_ms);  // la condizione inizia a 500: un secondo dopo
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -100.0, pre[0].r);
    TEST_ASSERT_EQUAL(1, b.det.pre_count());
}

void test_segnale_buono_nessun_allarme() {
    Bench b;
    for (uint32_t t = 0; t <= 20000; t += 100) {
        if (t >= 500 && t % 500 == 0) b.beacon(t, 2, -80);
        b.tick(t);
    }
    TEST_ASSERT_EQUAL(0, b.events.size());
}

void test_spegnimento_dopo_30_secondi_con_ricezione() {
    Bench b;
    for (uint32_t t = 0; t <= 60000; t += 100) {
        if (t >= 500 && t % 500 == 0) b.beacon(t, 2, t < 10000 ? -100 : -70);
        b.tick(t);
    }
    std::vector<DetEvent> pre = b.of(DetKind::PreAlarm, 2);
    TEST_ASSERT_EQUAL(2, pre.size());
    TEST_ASSERT_TRUE(pre[0].start);
    TEST_ASSERT_FALSE(pre[1].start);
    // Il segnale sale sopra -90 pochi secondi dopo il cambio (10 s); poi 30 s di segnale buono.
    TEST_ASSERT_TRUE(pre[1].t_ms >= 10000 + 30000);
    TEST_ASSERT_TRUE(pre[1].t_ms <= 10000 + 30000 + 5000);
    TEST_ASSERT_EQUAL(0, b.det.pre_count());
}

void test_senza_ricezione_sufficiente_non_si_spegne() {
    Bench b;
    for (uint32_t t = 0; t <= 100000; t += 100) {
        // 2 Hz con segnale pessimo fino a 10 s, poi 1 Hz con segnale buono: 30 beacon su 48 richiesti.
        if (t >= 500 && t < 10000 && t % 500 == 0) b.beacon(t, 2, -100);
        if (t >= 10000 && t % 1000 == 0) b.beacon(t, 2, -70);
        b.tick(t);
    }
    std::vector<DetEvent> pre = b.of(DetKind::PreAlarm, 2);
    TEST_ASSERT_EQUAL(1, pre.size());
    TEST_ASSERT_EQUAL(1, b.det.pre_count());
}

void test_nodo_perso_dopo_10_secondi() {
    Bench b;
    for (uint32_t t = 0; t <= 60000; t += 100) {
        if (t >= 500 && t % 500 == 0) {
            if (t <= 5000 || t >= 30500) b.beacon(t, 2, -60);
            b.beacon(t, 3, -60);  // il compagno 3 sente sempre: non e' la scheda a essere sorda
        }
        b.tick(t);
    }
    std::vector<DetEvent> lost = b.of(DetKind::Lost, 2);
    TEST_ASSERT_EQUAL(2, lost.size());
    TEST_ASSERT_TRUE(lost[0].start);
    TEST_ASSERT_EQUAL_UINT32(15100, lost[0].t_ms);  // ultimo beacon a 5000; "oltre" 10 s
    TEST_ASSERT_FALSE(lost[1].start);
    TEST_ASSERT_EQUAL_UINT32(54000, lost[1].t_ms);  // 48 beacon dal ritorno (30500..54000)
    TEST_ASSERT_EQUAL(0, b.of(DetKind::Deaf, 2).size());
    TEST_ASSERT_EQUAL(0, b.of(DetKind::Lost, 3).size());
}

void test_scheda_sorda_non_dichiara_persi() {
    Bench b;
    for (uint32_t t = 0; t <= 40000; t += 50) {
        // Entrambi i compagni tacciono insieme (ultimi beacon a 5000 e 5250) e tornano a 30500 e 30750.
        if (t % 500 == 0 && (t <= 5000 || t >= 30500)) b.beacon(t, 2, -60);
        if (t % 500 == 250 && (t <= 5250 || t >= 30750)) b.beacon(t, 3, -60);
        if (t % 100 == 0) b.tick(t);
    }
    TEST_ASSERT_EQUAL(0, b.of(DetKind::Lost, 2).size());
    TEST_ASSERT_EQUAL(0, b.of(DetKind::Lost, 3).size());
    std::vector<DetEvent> d2 = b.of(DetKind::Deaf, 2), d3 = b.of(DetKind::Deaf, 3);
    TEST_ASSERT_EQUAL(2, d2.size());
    TEST_ASSERT_EQUAL(2, d3.size());
    TEST_ASSERT_EQUAL_UINT32(15100, d2[0].t_ms);
    TEST_ASSERT_EQUAL_UINT32(15300, d3[0].t_ms);
    TEST_ASSERT_FALSE(d2[1].start);
    TEST_ASSERT_EQUAL_UINT32(30500, d2[1].t_ms);
    TEST_ASSERT_EQUAL_UINT32(30800, d3[1].t_ms);
}

void test_se_il_compagno_sente_altri_il_perso_scatta() {
    Bench b;
    for (uint32_t t = 0; t <= 20000; t += 100) {
        if (t % 500 == 0 && t >= 500 && t <= 5000) b.beacon(t, 2, -60);
        if (t % 500 == 0 && t >= 500 && t <= 7000) b.beacon(t, 3, -60);  // ancora un beacon dopo 5500
        b.tick(t);
    }
    std::vector<DetEvent> lost = b.of(DetKind::Lost, 2);
    TEST_ASSERT_EQUAL(1, lost.size());
    TEST_ASSERT_TRUE(lost[0].start);
    TEST_ASSERT_EQUAL_UINT32(15100, lost[0].t_ms);
    TEST_ASSERT_EQUAL(0, b.of(DetKind::Deaf, 2).size());
    // Il 3 tace da 7000: la sua finestra e' (7500, 17000] e il 2 non ha beacon: e' sorda.
    TEST_ASSERT_EQUAL(1, b.of(DetKind::Deaf, 3).size());
    TEST_ASSERT_EQUAL(0, b.of(DetKind::Lost, 3).size());
}

void test_fusione_con_le_stime_dei_vicini() {
    // Il compagno 3 riferisce che il 2 e' a -100 dBm con incertezza 1 dB: pesa molto piu' della stima propria.
    Bench b;
    packets::BeaconEntry far = {2, -100, 0, 10, 0};
    for (uint32_t t = 0; t <= 40000; t += 100) {
        if (t >= 500 && t % 500 == 0) b.beacon(t, 2, -60);
        if (t == 1000) b.beacon(t, 3, -60, {far});
        b.tick(t);
    }
    std::vector<DetEvent> pre = b.of(DetKind::PreAlarm, 2);
    TEST_ASSERT_EQUAL(2, pre.size());
    TEST_ASSERT_EQUAL_UINT32(2000, pre[0].t_ms);
    // La voce vale fino a 3000 (2 s di eta'); poi resta la stima propria (-60) e inizia il conto dei 30 s.
    TEST_ASSERT_EQUAL_UINT32(33100, pre[1].t_ms);
}

void test_voce_troppo_vecchia_esclusa() {
    // Stessa voce, ma gia' vecchia di 1,5 s alla ricezione: vale solo 0,5 s, meno del secondo richiesto.
    Bench b;
    packets::BeaconEntry old = {2, -100, 0, 10, 15};
    for (uint32_t t = 0; t <= 10000; t += 100) {
        if (t >= 500 && t % 500 == 0) b.beacon(t, 2, -60);
        if (t == 1000) b.beacon(t, 3, -60, {old});
        b.tick(t);
    }
    TEST_ASSERT_EQUAL(0, b.of(DetKind::PreAlarm, 2).size());
}

void test_riavvio_del_filtro_dopo_silenzio_lungo() {
    Bench b;
    b.beacon(1000, 2, -60);
    b.beacon(1500, 2, -62);
    b.beacon(20000, 2, -90);  // oltre 10 s: riparte da -90, senza trascinare la pendenza
    LinkView v;
    TEST_ASSERT_TRUE(b.det.estimate(2, 20000, v));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -90.0, v.r);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, v.s);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_voci_del_beacon);
    RUN_TEST(test_stima_predetta);
    RUN_TEST(test_pre_allarme_dopo_un_secondo);
    RUN_TEST(test_segnale_buono_nessun_allarme);
    RUN_TEST(test_spegnimento_dopo_30_secondi_con_ricezione);
    RUN_TEST(test_senza_ricezione_sufficiente_non_si_spegne);
    RUN_TEST(test_nodo_perso_dopo_10_secondi);
    RUN_TEST(test_scheda_sorda_non_dichiara_persi);
    RUN_TEST(test_se_il_compagno_sente_altri_il_perso_scatta);
    RUN_TEST(test_fusione_con_le_stime_dei_vicini);
    RUN_TEST(test_voce_troppo_vecchia_esclusa);
    RUN_TEST(test_riavvio_del_filtro_dopo_silenzio_lungo);
    return UNITY_END();
}

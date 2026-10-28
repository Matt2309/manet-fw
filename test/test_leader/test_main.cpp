#include <unity.h>

#include <vector>

#include "leader.h"

void setUp() {}
void tearDown() {}

// Capogruppo 1; gruppo di 4 schede (1, 2, 3, 4) salvo diversa indicazione.
struct Bench {
    Leader l;
    uint32_t now = 100000;
    std::vector<Leader::Event> events;

    Bench(int boards = 4) {
        l.set_self(1);
        for (int id = 2; id <= boards; id++) beacon(static_cast<uint8_t>(id));
    }

    void beacon(uint8_t id, bool sos = false) {
        packets::Beacon b = {};
        b.sender = id;
        b.battery = packets::BATTERY_MAX;
        b.sos = sos;
        l.on_beacon(b, now);
    }

    // Record dell'osservatore o: per ogni coppia (corridore, allarme) una voce con segnale -80.
    void record(uint8_t o, std::initializer_list<std::pair<uint8_t, uint8_t>> entries, uint32_t age_s = 0, bool sos = false) {
        packets::TelRecord r = {};
        r.id = o;
        r.battery = 8;
        r.sos = sos;
        for (const auto& e : entries) r.nb[r.n++] = packets::TelEntry{e.first, -80, 20, 3, e.second};
        l.on_record(r, now, age_s);
    }

    void alarm(uint8_t creator, uint8_t runner, packets::AlarmType t) {
        packets::Alarm a = {};
        a.sender = creator;
        a.runner = runner;
        a.type = t;
        l.on_alarm(a, now);
    }

    void run(uint32_t ms, uint32_t step = 1000) {
        for (uint32_t t = 0; t < ms; t += step) {
            now += step;
            Leader::Event ev[Leader::MAX_EVENTS];
            size_t n = l.tick(now, now / 1000, ev);
            for (size_t i = 0; i < n; i++) events.push_back(ev[i]);
        }
    }

    ble::RunnerState state(uint8_t id) {
        ble::Row rows[ble::MAX_RUNNERS];
        size_t n = l.snapshot(now, rows, ble::MAX_RUNNERS);
        for (size_t i = 0; i < n; i++) {
            if (rows[i].id == id) return rows[i].state;
        }
        return ble::RunnerState::Ok;
    }
    ble::Row row(uint8_t id) {
        ble::Row rows[ble::MAX_RUNNERS];
        size_t n = l.snapshot(now, rows, ble::MAX_RUNNERS);
        for (size_t i = 0; i < n; i++) {
            if (rows[i].id == id) return rows[i];
        }
        return ble::Row{};
    }
    int count(ble::EventType t, uint8_t id) const {
        int n = 0;
        for (const Leader::Event& e : events) n += (e.type == t && e.id == id) ? 1 : 0;
        return n;
    }
};

void test_righe_dai_beacon_e_dal_capogruppo() {
    Bench b;
    ble::Row rows[ble::MAX_RUNNERS];
    TEST_ASSERT_EQUAL(4, b.l.size());          // compreso il capogruppo
    TEST_ASSERT_EQUAL(4, b.l.snapshot(b.now, rows, ble::MAX_RUNNERS));
    TEST_ASSERT_EQUAL(1, rows[0].id);
    TEST_ASSERT_EQUAL(100, rows[1].battery);   // BATTERY_MAX = 100%
    TEST_ASSERT_EQUAL(ble::R_ABSENT, rows[1].r);
    b.run(7000);
    TEST_ASSERT_EQUAL(7, b.row(2).age_s);      // secondi dall'ultimo dato
    b.beacon(2);
    TEST_ASSERT_EQUAL(0, b.row(2).age_s);
}

void test_un_corridore_sentito_resta_fino_allo_stop() {
    Bench b;
    b.run(120000);                             // nessun dato per due minuti
    TEST_ASSERT_EQUAL(4, b.l.size());
    TEST_ASSERT_EQUAL(120, b.row(3).age_s);
    b.l.reset();
    TEST_ASSERT_EQUAL(1, b.l.size());          // solo il capogruppo
}

void test_r_media_degli_ultimi_5_secondi() {
    Bench b;
    b.record(3, {{2, 0}});
    packets::TelRecord r = {};
    r.id = 4;
    r.n = 1;
    r.nb[0] = packets::TelEntry{2, -70, 20, 3, 0};
    b.l.on_record(r, b.now, 0);
    TEST_ASSERT_EQUAL(-75, b.row(2).r);        // media di -80 e -70
    b.run(3000);
    TEST_ASSERT_EQUAL(-75, b.row(2).r);
    b.run(3000);                               // 6 s dopo: i dati sono fuori finestra
    TEST_ASSERT_EQUAL(ble::R_ABSENT, b.row(2).r);
}

void test_pre_allarme_e_rientro() {
    Bench b;
    b.record(3, {{2, packets::ALARM_PRE}});
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::PreAlarm, b.state(2));
    TEST_ASSERT_EQUAL(1, b.count(ble::EventType::PreAlarm, 2));
    b.run(5000);
    TEST_ASSERT_EQUAL(1, b.count(ble::EventType::PreAlarm, 2));   // un solo evento
    b.record(3, {{2, 0}});                                         // l'osservatore non lo riporta piu'
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Ok, b.state(2));
    TEST_ASSERT_EQUAL(1, b.count(ble::EventType::Return, 2));
}

void test_con_un_solo_osservatore_basta_una_conferma() {
    Bench b;
    b.record(3, {{2, packets::ALARM_LOST}});   // solo il 3 ha il corridore 2 nella tabella
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Lost, b.state(2));
    TEST_ASSERT_EQUAL(1, b.count(ble::EventType::Lost, 2));
}

void test_due_osservatori_uno_lo_sente_bene_resta_sospetto() {
    Bench b;
    b.record(3, {{2, packets::ALARM_LOST}});
    b.record(4, {{2, 0}});                     // il 4 lo vede, senza allarme
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::PreAlarm, b.state(2));    // non basta il solo sospetto del 3
    TEST_ASSERT_EQUAL(0, b.count(ble::EventType::Lost, 2));
    b.alarm(4, 2, packets::AlarmType::Lost);                      // ora lo conferma anche il 4
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Lost, b.state(2));
    TEST_ASSERT_EQUAL(1, b.count(ble::EventType::Lost, 2));
    b.alarm(4, 2, packets::AlarmType::End);
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::PreAlarm, b.state(2));    // da perso a sospetto: nessun evento nuovo
    TEST_ASSERT_EQUAL(0, b.count(ble::EventType::Return, 2));
}

void test_con_due_schede_basta_una() {
    Bench b(2);
    b.record(2, {{1, packets::ALARM_LOST}});   // il corridore 2 riporta il capogruppo perso
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Lost, b.state(1));
    TEST_ASSERT_EQUAL(1, b.count(ble::EventType::Lost, 1));
}

void test_osservatore_silenzioso_non_conta() {
    Bench b;
    b.record(3, {{2, packets::ALARM_LOST}});
    b.record(4, {{2, 0}});                     // il 4 lo sente bene
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::PreAlarm, b.state(2));
    b.beacon(3);                               // il 3 e' vivo, il 4 tace
    b.run(OBSERVER_VALID_MS - 1000);
    b.beacon(3);
    b.run(2000);                               // il 4 non da' segni da oltre OBSERVER_VALID_MS: resta il 3 da solo
    TEST_ASSERT_EQUAL(ble::RunnerState::Lost, b.state(2));
    TEST_ASSERT_EQUAL(1, b.count(ble::EventType::Lost, 2));
}

void test_chiusura_dall_app_senza_rientro_e_senza_ripresentarsi() {
    Bench b;
    b.record(3, {{2, packets::ALARM_LOST}});
    b.alarm(4, 2, packets::AlarmType::Lost);
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Lost, b.state(2));
    TEST_ASSERT_TRUE(b.l.close_alarm(2, ble::AlarmKind::Lost, b.now));
    TEST_ASSERT_EQUAL(ble::RunnerState::Ok, b.state(2));
    TEST_ASSERT_TRUE(b.l.close_alarm(2, ble::AlarmKind::Lost, b.now));  // idempotente
    b.run(20000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Ok, b.state(2));                // l'allarme continua ma e' chiuso
    TEST_ASSERT_EQUAL(0, b.count(ble::EventType::Return, 2));           // chiusura dall'app: nessun RIENTRO
    TEST_ASSERT_EQUAL(1, b.count(ble::EventType::Lost, 2));
    // Torna normale e poi si stacca di nuovo: si ripropone.
    b.record(3, {{2, 0}});
    b.alarm(4, 2, packets::AlarmType::End);
    b.run(1000);
    b.record(3, {{2, packets::ALARM_LOST}});
    b.alarm(4, 2, packets::AlarmType::Lost);
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Lost, b.state(2));
    TEST_ASSERT_EQUAL(2, b.count(ble::EventType::Lost, 2));
    TEST_ASSERT_FALSE(b.l.close_alarm(9, ble::AlarmKind::Lost, b.now));  // corridore sconosciuto
}

void test_chiudere_il_perso_chiude_anche_il_pre_allarme() {
    Bench b;
    b.record(3, {{2, packets::ALARM_LOST}});
    b.alarm(4, 2, packets::AlarmType::Lost);
    b.run(1000);
    b.l.close_alarm(2, ble::AlarmKind::Lost, b.now);
    b.alarm(4, 2, packets::AlarmType::End);                              // resta solo il sospetto di uno: sarebbe un pre-allarme
    b.run(2000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Ok, b.state(2));
    TEST_ASSERT_EQUAL(0, b.count(ble::EventType::PreAlarm, 2));
}

void test_sos_resta_finche_non_lo_chiude_l_app() {
    Bench b;
    b.alarm(3, 3, packets::AlarmType::Sos);
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Sos, b.state(3));
    TEST_ASSERT_EQUAL(1, b.count(ble::EventType::Sos, 3));
    // Il corridore lo annulla (beacon senza SOS): il capogruppo lo tiene comunque.
    b.beacon(3, false);
    b.run(2 * SOS_HOLD_MS);
    TEST_ASSERT_EQUAL(ble::RunnerState::Sos, b.state(3));
    TEST_ASSERT_EQUAL(0, b.count(ble::EventType::Return, 3));            // l'SOS non fa RIENTRO
    TEST_ASSERT_TRUE(b.l.close_alarm(3, ble::AlarmKind::Sos, b.now));
    TEST_ASSERT_EQUAL(ble::RunnerState::Ok, b.state(3));
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Ok, b.state(3));
    // Un SOS nuovo dopo che e' finito si ripropone.
    b.alarm(3, 3, packets::AlarmType::Sos);
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Sos, b.state(3));
    TEST_ASSERT_EQUAL(2, b.count(ble::EventType::Sos, 3));
}

void test_sos_chiuso_non_si_ripropone_finche_il_corridore_continua() {
    Bench b;
    b.beacon(3, true);
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Sos, b.state(3));
    b.l.close_alarm(3, ble::AlarmKind::Sos, b.now);
    for (int i = 0; i < 40; i++) {             // il corridore continua a segnalare per 40 s
        b.beacon(3, true);
        b.run(1000);
    }
    TEST_ASSERT_EQUAL(ble::RunnerState::Ok, b.state(3));
    TEST_ASSERT_EQUAL(1, b.count(ble::EventType::Sos, 3));
}

void test_sos_dai_record_e_dai_propri_dati() {
    Bench b;
    b.record(3, {}, 0, true);
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Sos, b.state(3));
    b.l.on_self(70, true, b.now);              // SOS del capogruppo
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Sos, b.state(1));
    TEST_ASSERT_EQUAL(70, b.row(1).battery);
}

void test_record_vecchio_aggiorna_solo_l_ultimo_dato() {
    Bench b;
    b.run(20000);
    b.record(3, {{2, packets::ALARM_PRE}}, 12, true);    // prodotto 12 s fa
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Ok, b.state(2));  // non descrive lo stato di adesso
    TEST_ASSERT_EQUAL(ble::RunnerState::Ok, b.state(3));
    TEST_ASSERT_EQUAL(13, b.row(3).age_s);                // ma l'ultimo dato del 3 e' di 13 s fa, non di 21
}

void test_il_record_sostituisce_i_flag_dello_stesso_osservatore() {
    Bench b;
    b.record(3, {{2, packets::ALARM_PRE}, {4, packets::ALARM_PRE}});
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::PreAlarm, b.state(2));
    TEST_ASSERT_EQUAL(ble::RunnerState::PreAlarm, b.state(4));
    b.record(3, {{4, packets::ALARM_PRE}});               // il 2 non c'e' piu': il flag cade
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Ok, b.state(2));
    TEST_ASSERT_EQUAL(ble::RunnerState::PreAlarm, b.state(4));
}

void test_start_toglie_gli_allarmi_e_tiene_le_righe() {
    Bench b;
    b.record(3, {{2, packets::ALARM_PRE}});
    b.run(1000);
    b.l.clear_alarms();
    b.run(1000);
    TEST_ASSERT_EQUAL(ble::RunnerState::Ok, b.state(2));
    TEST_ASSERT_EQUAL(4, b.l.size());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_righe_dai_beacon_e_dal_capogruppo);
    RUN_TEST(test_un_corridore_sentito_resta_fino_allo_stop);
    RUN_TEST(test_r_media_degli_ultimi_5_secondi);
    RUN_TEST(test_pre_allarme_e_rientro);
    RUN_TEST(test_con_un_solo_osservatore_basta_una_conferma);
    RUN_TEST(test_due_osservatori_uno_lo_sente_bene_resta_sospetto);
    RUN_TEST(test_con_due_schede_basta_una);
    RUN_TEST(test_osservatore_silenzioso_non_conta);
    RUN_TEST(test_chiusura_dall_app_senza_rientro_e_senza_ripresentarsi);
    RUN_TEST(test_chiudere_il_perso_chiude_anche_il_pre_allarme);
    RUN_TEST(test_sos_resta_finche_non_lo_chiude_l_app);
    RUN_TEST(test_sos_chiuso_non_si_ripropone_finche_il_corridore_continua);
    RUN_TEST(test_sos_dai_record_e_dai_propri_dati);
    RUN_TEST(test_record_vecchio_aggiorna_solo_l_ultimo_dato);
    RUN_TEST(test_il_record_sostituisce_i_flag_dello_stesso_osservatore);
    RUN_TEST(test_start_toglie_gli_allarmi_e_tiene_le_righe);
    return UNITY_END();
}

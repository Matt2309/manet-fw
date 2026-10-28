// Confronto con il simulatore: N Node del firmware ricevono i beacon di una corsa simulata di
// manet-sim (tools/esporta_sim.py) e i loro allarmi si confrontano con quelli del rilevatore Python.
//
// Ogni beacon e' costruito dal Node mittente (tabella dei vicini compresa) e consegnato ai Node
// che il simulatore indica come riceventi, con lo stesso RSSI. I tick sono a 100 ms, come la
// griglia del simulatore: a ogni tick prima si elaborano i beacon con t <= istante, poi il tick.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <algorithm>
#include <string>
#include <vector>

#include "config.h"
#include "node.h"

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t TICK_MS = 100;
// Tolleranza sugli istanti: un tick. Le differenze residue vengono da float32 nel simulatore e
// dai pareggi di arrotondamento dell'eta' (0,1 s) e della finestra di 2 s.
constexpr int32_t TOLERANCE_MS = TICK_MS;

struct SimBeacon {
    uint32_t t_ms;
    uint8_t src;
    int rssi[MAX_NODES];  // 1000 = non ricevuto
};

struct Episode {
    int observer, target;
    int32_t start, end;  // end = -1: ancora attivo alla fine
};

enum Kind { PRE = 0, LOST = 1, DEAF = 2, N_KINDS };
int32_t g_max_dev_ms = 0;  // scarto massimo fra istanti corrispondenti, per il riepilogo
const char* const KIND_NAME[N_KINDS] = {"pre", "perso", "sorda"};

struct SimRun {
    int n_nodes = 0;
    uint32_t end_ms = 0;
    std::vector<SimBeacon> beacons;
    std::vector<Episode> expected[N_KINDS];
};

std::vector<std::string> split(const char* line) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = line; *p != '\0' && *p != '\n' && *p != '\r'; p++) {
        if (*p == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += *p;
        }
    }
    out.push_back(cur);
    return out;
}

FILE* open_run(const char* name) {
    // Il percorso dipende da dove PlatformIO lancia il programma: si prova accanto a questo file e dalla radice.
    std::string dir = __FILE__;
    dir = dir.substr(0, dir.find_last_of("/\\") + 1);
    const std::string candidates[] = {dir + name, std::string("test/test_sim_compare/") + name,
                                      std::string("../../test/test_sim_compare/") + name};
    for (const std::string& c : candidates) {
        FILE* f = fopen(c.c_str(), "r");
        if (f != nullptr) return f;
    }
    return nullptr;
}

bool load(const char* name, SimRun& run) {
    FILE* f = open_run(name);
    if (f == nullptr) return false;
    char line[256];
    while (fgets(line, sizeof(line), f) != nullptr) {
        if (line[0] == '#') continue;
        std::vector<std::string> v = split(line);
        if (v[0] == "N") {
            run.n_nodes = atoi(v[1].c_str());
            run.end_ms = static_cast<uint32_t>(atol(v[2].c_str()));
        } else if (v[0] == "B") {
            SimBeacon b;
            b.t_ms = static_cast<uint32_t>(atol(v[1].c_str()));
            b.src = static_cast<uint8_t>(atoi(v[2].c_str()));
            for (int j = 0; j < MAX_NODES; j++) {
                bool has = j < run.n_nodes && 3 + static_cast<size_t>(j) < v.size() && !v[3 + j].empty();
                b.rssi[j] = has ? atoi(v[3 + j].c_str()) : 1000;
            }
            run.beacons.push_back(b);
        } else if (v[0] == "A") {
            int kind = v[1] == "pre" ? PRE : v[1] == "perso" ? LOST : DEAF;
            run.expected[kind].push_back(Episode{atoi(v[2].c_str()), atoi(v[3].c_str()),
                                                 static_cast<int32_t>(atol(v[4].c_str())),
                                                 static_cast<int32_t>(atol(v[5].c_str()))});
        }
    }
    fclose(f);
    return true;
}

// Fa girare i Node sulla corsa e restituisce gli episodi dichiarati dal firmware.
// Nel pre-allarme esiste anche la versione GNSS: senza posizioni nei beacon deve coincidere con la base.
void replay(const SimRun& run, std::vector<Episode> (&got)[N_KINDS], std::vector<Episode>& got_gnss) {
    std::vector<Node> nodes;
    for (int i = 1; i <= run.n_nodes; i++) nodes.emplace_back(static_cast<uint8_t>(i), Role::Runner);
    std::vector<Episode> open[N_KINDS];
    std::vector<Episode> open_gnss;

    size_t next = 0;
    uint8_t buf[packets::MAX_SIZE];
    for (uint32_t t = 0; t <= run.end_ms; t += TICK_MS) {
        for (; next < run.beacons.size() && run.beacons[next].t_ms <= t; next++) {
            const SimBeacon& sb = run.beacons[next];
            packets::Beacon sent, rx;
            size_t len = nodes[sb.src - 1].build_beacon(sb.t_ms, 100, buf, sizeof(buf), sent);
            for (int m = 0; m < run.n_nodes; m++) {
                if (sb.rssi[m] == 1000) continue;
                nodes[m].on_packet(buf, len, static_cast<int8_t>(sb.rssi[m]), sb.t_ms, rx);
            }
        }
        for (int m = 0; m < run.n_nodes; m++) {
            DetEvent ev[Detector::MAX_EVENTS];
            size_t n = nodes[m].tick(t, ev);
            for (size_t k = 0; k < n; k++) {
                int kind = ev[k].kind == DetKind::PreAlarm ? PRE : ev[k].kind == DetKind::Lost ? LOST : DEAF;
                bool gnss = ev[k].version == DetVersion::Gnss;
                std::vector<Episode>& list = gnss ? open_gnss : open[kind];
                std::vector<Episode>& done = gnss ? got_gnss : got[kind];
                if (ev[k].start) {
                    list.push_back(Episode{m + 1, ev[k].target, static_cast<int32_t>(ev[k].t_ms), -1});
                } else {
                    for (Episode& e : list) {
                        if (e.observer == m + 1 && e.target == ev[k].target && e.end == -1) {
                            e.end = static_cast<int32_t>(ev[k].t_ms);
                            done.push_back(e);
                            e.end = -2;  // chiuso e gia' riportato
                            break;
                        }
                    }
                }
            }
        }
    }
    for (int kind = 0; kind < N_KINDS; kind++) {
        for (const Episode& e : open[kind]) {
            if (e.end == -1) got[kind].push_back(e);
        }
    }
    for (const Episode& e : open_gnss) {
        if (e.end == -1) got_gnss.push_back(e);
    }
}

bool near(int32_t a, int32_t b) {
    if (a < 0 || b < 0) return a == b;  // -1 = ancora attivo
    if (std::abs(a - b) <= TOLERANCE_MS) {
        g_max_dev_ms = std::max(g_max_dev_ms, std::abs(a - b));
        return true;
    }
    return false;
}

// Confronta gli episodi di un tipo, per coppia (osservatore, compagno) e per inizio. Stampa le differenze.
int compare(const char* what, std::vector<Episode> want, std::vector<Episode> have) {
    auto order = [](const Episode& a, const Episode& b) {
        if (a.observer != b.observer) return a.observer < b.observer;
        if (a.target != b.target) return a.target < b.target;
        return a.start < b.start;
    };
    std::sort(want.begin(), want.end(), order);
    std::sort(have.begin(), have.end(), order);
    int diff = 0;
    size_t i = 0, j = 0;
    while (i < want.size() || j < have.size()) {
        bool same_pair = i < want.size() && j < have.size() && want[i].observer == have[j].observer &&
                         want[i].target == have[j].target;
        if (same_pair && near(want[i].start, have[j].start) && near(want[i].end, have[j].end)) {
            if (want[i].start != have[j].start || want[i].end != have[j].end) {
                printf("  %s %d->%d: sfasato di un tick: simulatore [%d, %d] firmware [%d, %d]\n", what,
                       want[i].observer, want[i].target, want[i].start, want[i].end, have[j].start, have[j].end);
            }
            i++;
            j++;
            continue;
        }
        diff++;
        if (same_pair) {
            printf("  %s %d->%d: simulatore [%d, %d] firmware [%d, %d]\n", what, want[i].observer, want[i].target,
                   want[i].start, want[i].end, have[j].start, have[j].end);
            i++;
            j++;
        } else if (j >= have.size() || (i < want.size() && order(want[i], have[j]))) {
            printf("  %s %d->%d: solo nel simulatore [%d, %d]\n", what, want[i].observer, want[i].target, want[i].start,
                   want[i].end);
            i++;
        } else {
            printf("  %s %d->%d: solo nel firmware [%d, %d]\n", what, have[j].observer, have[j].target, have[j].start,
                   have[j].end);
            j++;
        }
    }
    return diff;
}

void check(const char* file, int min_expected_per_kind[N_KINDS]) {
    SimRun run;
    TEST_ASSERT_TRUE_MESSAGE(load(file, run), "file della corsa non trovato (tools/esporta_sim.py)");
    TEST_ASSERT_TRUE(run.n_nodes > 1 && run.n_nodes <= MAX_NODES);
    std::vector<Episode> got[N_KINDS], got_gnss;
    replay(run, got, got_gnss);
    int diff = 0;
    for (int k = 0; k < N_KINDS; k++) {
        TEST_ASSERT_TRUE_MESSAGE(static_cast<int>(run.expected[k].size()) >= min_expected_per_kind[k],
                                 "la corsa esportata non contiene abbastanza episodi di questo tipo");
        printf("%s: %s simulatore %u episodi, firmware %u\n", file, KIND_NAME[k],
               static_cast<unsigned>(run.expected[k].size()), static_cast<unsigned>(got[k].size()));
        diff += compare(KIND_NAME[k], run.expected[k], got[k]);
    }
    // Senza posizioni il ramo GNSS non cambia nulla: stessi episodi della versione base, istante per istante.
    TEST_ASSERT_EQUAL_MESSAGE(got[PRE].size(), got_gnss.size(), "senza GNSS le due versioni devono avere gli stessi pre-allarmi");
    for (size_t i = 0; i < got_gnss.size(); i++) {
        bool found = false;
        for (const Episode& b : got[PRE]) {
            found |= b.observer == got_gnss[i].observer && b.target == got_gnss[i].target &&
                     b.start == got_gnss[i].start && b.end == got_gnss[i].end;
        }
        TEST_ASSERT_TRUE_MESSAGE(found, "pre-allarme GNSS senza corrispondente nella versione base");
    }
    printf("%s: scarto massimo fra gli istanti %d ms (tolleranza %d ms)\n", file, static_cast<int>(g_max_dev_ms),
           static_cast<int>(TOLERANCE_MS));
    fflush(stdout);
    TEST_ASSERT_EQUAL_MESSAGE(0, diff, "gli allarmi del firmware non coincidono con quelli del simulatore");
}

}  // namespace

void test_stacco_di_un_compagno() {
    int minimi[N_KINDS] = {5, 5, 1};
    check("sim_sep.csv", minimi);
}

void test_schede_sorde() {
    int minimi[N_KINDS] = {0, 0, 20};
    check("sim_deaf.csv", minimi);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_stacco_di_un_compagno);
    RUN_TEST(test_schede_sorde);
    return UNITY_END();
}

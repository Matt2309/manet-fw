#include "logger.h"

#include <LittleFS.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>

#include "app.h"
#include "config.h"
#include "csv.h"
#include "runfile.h"

namespace logger {
namespace {

constexpr const char* MOUNT = "/littlefs";

QueueHandle_t g_queue = nullptr;
SemaphoreHandle_t g_mutex = nullptr;
std::atomic<uint32_t> g_dropped{0};

State g_state = State::Off;
FILE* g_file = nullptr;
char g_path[24] = "";   // "/run_004.csv"
char g_name[12] = "";   // "run_004"

char g_buf[LOG_BUFFER_BYTES];
size_t g_len = 0;

uint32_t g_last_flush = 0;
uint32_t g_last_space_check = 0;

struct HeaderInfo {
    int id = 0;
    char mac[18] = "";
    int power_qdbm = 0;
    char rate[16] = "";
} g_hdr;

void full_path(const char* path, char* out, size_t cap) { snprintf(out, cap, "%s%s", MOUNT, path); }

// Scrive il buffer su flash e sincronizza. Con il mutex preso.
void flush_locked() {
    if (g_file == nullptr || g_len == 0) return;
    size_t w = fwrite(g_buf, 1, g_len, g_file);
    fflush(g_file);
    fsync(fileno(g_file));
    if (w != g_len) {
        dbg("scrittura incompleta %u/%u", static_cast<unsigned>(w), static_cast<unsigned>(g_len));
    }
    g_len = 0;
    g_last_flush = millis();
}

void append_locked(const char* s, size_t n) {
    if (n == 0) return;
    if (g_len + n > sizeof(g_buf)) flush_locked();
    memcpy(g_buf + g_len, s, n);
    g_len += n;
}

void append_sys_locked(uint32_t t_ms, const char* msg) {
    char line[160];
    append_locked(line, csv_sys(line, sizeof(line), t_ms, msg));
}

void process_event_locked(const LogEvent& e) {
    char line[256];
    size_t n = 0;
    switch (e.type) {
        case LogEventType::Tx:
            n = csv_tx(line, sizeof(line), e.t_ms, e.u.tx.seq);
            break;
        case LogEventType::Rx:
            n = csv_rx(line, sizeof(line), e.t_ms, e.u.rx.src, e.u.rx.seq, e.u.rx.rssi, e.u.rx.tab,
                       e.u.rx.n);
            break;
        case LogEventType::Gps:
            n = csv_gps(line, sizeof(line), e.t_ms, e.u.gps);
            break;
        case LogEventType::Evt:
            n = csv_evt(line, sizeof(line), e.t_ms, e.u.evt.n);
            break;
        case LogEventType::Bat:
            n = csv_bat(line, sizeof(line), e.t_ms, e.u.bat.mv);
            break;
        case LogEventType::Sys:
            n = csv_sys(line, sizeof(line), e.t_ms, e.u.sys);
            break;
    }
    append_locked(line, n);
}

void drain_locked() {
    LogEvent e;
    while (xQueueReceive(g_queue, &e, 0) == pdTRUE) {
        if (g_state == State::Recording) process_event_locked(e);
    }
}

void report_dropped_locked() {
    uint32_t d = g_dropped.exchange(0);
    if (d == 0) return;
    char msg[48];
    snprintf(msg, sizeof(msg), "coda piena persi=%lu", static_cast<unsigned long>(d));
    append_sys_locked(millis(), msg);
}

void close_locked(const char* reason, State next) {
    if (g_file == nullptr) return;
    report_dropped_locked();
    char msg[64];
    snprintf(msg, sizeof(msg), "file chiuso: %s", reason);
    append_sys_locked(millis(), msg);
    flush_locked();
    fclose(g_file);
    g_file = nullptr;
    g_state = next;
}

void writer_task(void*) {
    for (;;) {
        LogEvent e;
        if (xQueueReceive(g_queue, &e, pdMS_TO_TICKS(100)) == pdTRUE) {
            xSemaphoreTake(g_mutex, portMAX_DELAY);
            if (g_state == State::Recording) process_event_locked(e);
            xSemaphoreGive(g_mutex);
        }

        xSemaphoreTake(g_mutex, portMAX_DELAY);
        if (g_state == State::Recording) {
            uint32_t now = millis();
            if (now - g_last_flush >= FLUSH_INTERVAL_MS) {
                report_dropped_locked();
                flush_locked();
            }
            if (now - g_last_space_check >= SPACE_CHECK_INTERVAL_MS) {
                g_last_space_check = now;
                size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
                if (total > 0 && (total - used) * 100 < total * FREE_SPACE_MIN_PCT) {
                    drain_locked();
                    append_sys_locked(now, "memoria piena");
                    close_locked("memoria piena", State::Full);
                }
            }
        }
        xSemaphoreGive(g_mutex);
    }
}

}  // namespace

bool init() {
    g_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(LogEvent));
    g_mutex = xSemaphoreCreateMutex();
    if (g_queue == nullptr || g_mutex == nullptr) return false;
    if (!LittleFS.begin(true, MOUNT, 10, "spiffs")) {
        g_state = State::Error;
        return false;
    }
    return true;
}

void set_header_info(int id, const char* mac, int power_qdbm, const char* rate) {
    g_hdr.id = id;
    snprintf(g_hdr.mac, sizeof(g_hdr.mac), "%s", mac);
    g_hdr.power_qdbm = power_qdbm;
    snprintf(g_hdr.rate, sizeof(g_hdr.rate), "%s", rate);
}

bool open_new() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    bool ok = false;

    if (g_file != nullptr) close_locked("nuovo file", State::Saved);

    // Numeri gia' usati.
    static int used[RUN_MAX];
    size_t count = 0;
    DIR* dir = opendir(MOUNT);
    if (dir != nullptr) {
        while (dirent* de = readdir(dir)) {
            int n = run_number(de->d_name);
            if (n > 0 && count < RUN_MAX) used[count++] = n;
        }
        closedir(dir);
    }
    int n = run_first_free(used, count);

    if (n < 0 || !run_name(n, g_path, sizeof(g_path))) {
        g_state = State::Error;
    } else {
        char full[48];
        full_path(g_path, full, sizeof(full));
        g_file = fopen(full, "w");
        if (g_file == nullptr) {
            g_state = State::Error;
        } else {
            snprintf(g_name, sizeof(g_name), "run_%03d", n);
            g_len = 0;
            char hdr[256];
            size_t h = csv_header(hdr, sizeof(hdr), FW_VERSION, g_hdr.id, g_hdr.mac, RADIO_CHANNEL,
                                  g_hdr.power_qdbm, BEACON_PERIOD_MS, BEACON_JITTER_MS, g_hdr.rate);
            append_locked(hdr, h);
            flush_locked();
            g_last_flush = g_last_space_check = millis();
            g_state = State::Recording;
            ok = true;
            // Eventi accodati mentre non c'era un file: si scartano.
            LogEvent e;
            while (xQueueReceive(g_queue, &e, 0) == pdTRUE) {}
        }
    }
    if (!ok) {
        g_name[0] = '\0';
        g_path[0] = '\0';
    }
    xSemaphoreGive(g_mutex);
    return ok;
}

void start_task() { xTaskCreatePinnedToCore(writer_task, "logger", 6144, nullptr, 2, nullptr, 1); }

void post(const LogEvent& e) {
    if (g_queue == nullptr || xQueueSend(g_queue, &e, 0) != pdTRUE) g_dropped++;
}

void post_sys(const char* fmt, ...) {
    LogEvent e;
    memset(&e, 0, sizeof(e));
    e.type = LogEventType::Sys;
    e.t_ms = millis();
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e.u.sys, sizeof(e.u.sys), fmt, ap);
    va_end(ap);
    post(e);
}

void save_and_close(const char* reason) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (g_state == State::Recording) {
        drain_locked();
        close_locked(reason, State::Saved);
    }
    xSemaphoreGive(g_mutex);
}

State state() { return g_state; }
const char* current_name() { return g_name; }
const char* current_path() { return g_path; }

size_t fs_total() { return LittleFS.totalBytes(); }
size_t fs_used() { return LittleFS.usedBytes(); }

void lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
void unlock() { xSemaphoreGive(g_mutex); }

bool format_locked() {
    g_name[0] = '\0';
    g_path[0] = '\0';
    g_len = 0;
    bool ok = LittleFS.format();
    g_state = State::Off;
    return ok;
}

}  // namespace logger

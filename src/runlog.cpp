#include "runlog.h"

#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "ble_codec.h"
#include "config.h"
#include "runfile.h"

namespace runlog {
namespace {

constexpr const char* MOUNT = "/littlefs";

FILE* g_file = nullptr;
char g_path[40] = "";  // "/littlefs/corsa_004.bin"

void make_path(int n) {
    char name[24];
    corsa_name(n, name, sizeof(name));
    snprintf(g_path, sizeof(g_path), "%s%s", MOUNT, name);
}

void close_file() {
    if (g_file != nullptr) {
        fclose(g_file);
        g_file = nullptr;
    }
}

void set_flag(bool on) {
    Preferences prefs;
    if (prefs.begin(ROLE_NVS_NAMESPACE, false)) {
        prefs.putBool(RUN_NVS_ACTIVE_KEY, on);
        prefs.end();
    }
}

}  // namespace

bool begin_new() {
    close_file();
    Preferences prefs;
    if (!prefs.begin(ROLE_NVS_NAMESPACE, false)) return false;
    int n = prefs.getUShort(RUN_NVS_COUNT_KEY, 0) + 1;
    if (n > RUN_MAX) n = 1;  // dopo 999 corse si riparte (il file piu' vecchio viene sovrascritto)
    prefs.putUShort(RUN_NVS_COUNT_KEY, static_cast<uint16_t>(n));
    prefs.putBool(RUN_NVS_ACTIVE_KEY, true);
    prefs.end();
    make_path(n);
    g_file = fopen(g_path, "wb");
    if (g_file == nullptr) {
        dbg("file della corsa: apertura di %s fallita", g_path);
        return false;
    }
    dbg("file della corsa %s", g_path);
    return true;
}

bool resume(uint32_t& last_seq) {
    last_seq = 0;
    Preferences prefs;
    if (!prefs.begin(ROLE_NVS_NAMESPACE, false)) return false;
    bool active = prefs.getBool(RUN_NVS_ACTIVE_KEY, false);
    int n = prefs.getUShort(RUN_NVS_COUNT_KEY, 0);
    prefs.end();
    if (!active || n < 1) return false;

    make_path(n);
    FILE* f = fopen(g_path, "rb");
    if (f == nullptr) {
        set_flag(false);
        return false;
    }
    uint8_t msg[ble::MAX_MESSAGE_SIZE];
    for (;;) {
        int len = fgetc(f);
        if (len == EOF || len <= 0 || static_cast<size_t>(len) > sizeof(msg)) break;
        if (fread(msg, 1, static_cast<size_t>(len), f) != static_cast<size_t>(len)) break;  // ultimo messaggio troncato
        uint32_t seq;
        if (ble::message_seq(msg, static_cast<size_t>(len), seq)) last_seq = seq;
    }
    fclose(f);
    g_file = fopen(g_path, "ab");
    if (g_file == nullptr) return false;
    dbg("corsa ripresa da %s, ultimo seq %lu", g_path, static_cast<unsigned long>(last_seq));
    return true;
}

void end() {
    close_file();
    set_flag(false);
}

bool append(const uint8_t* msg, size_t len) {
    if (g_file == nullptr || len == 0 || len > ble::MAX_MESSAGE_SIZE) return false;
    uint8_t buf[1 + ble::MAX_MESSAGE_SIZE];
    buf[0] = static_cast<uint8_t>(len);
    memcpy(buf + 1, msg, len);
    bool ok = fwrite(buf, 1, len + 1, g_file) == len + 1;
    ok = (fflush(g_file) == 0) && ok;
    fsync(fileno(g_file));
    return ok;
}

bool Reader::open(uint32_t from_seq, uint32_t to_seq) {
    close();
    if (g_path[0] == '\0') return false;
    file_ = fopen(g_path, "rb");
    from_ = from_seq;
    to_ = to_seq;
    return file_ != nullptr;
}

bool Reader::next(uint8_t* msg, size_t cap, size_t& len) {
    FILE* f = static_cast<FILE*>(file_);
    if (f == nullptr) return false;
    for (;;) {
        int n = fgetc(f);
        if (n == EOF || n <= 0 || static_cast<size_t>(n) > cap) return false;
        if (fread(msg, 1, static_cast<size_t>(n), f) != static_cast<size_t>(n)) return false;
        uint32_t seq;
        if (!ble::message_seq(msg, static_cast<size_t>(n), seq)) continue;
        if (seq <= from_) continue;
        if (seq > to_) return false;
        len = static_cast<size_t>(n);
        return true;
    }
}

void Reader::close() {
    if (file_ != nullptr) {
        fclose(static_cast<FILE*>(file_));
        file_ = nullptr;
    }
}

}  // namespace runlog

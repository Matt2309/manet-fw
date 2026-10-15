#include "console.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app.h"
#include "config.h"
#include "crc32.h"
#include "logger.h"
#include "runfile.h"

namespace {

constexpr const char* MOUNT = "/littlefs";

char g_line[96];
size_t g_len = 0;

void reply(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void reply(const char* fmt, ...) {
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    xSemaphoreTake(g_serial_mutex, portMAX_DELAY);
    Serial.println(buf);
    xSemaphoreGive(g_serial_mutex);
}

// Scrive tutti i byte, ritentando se il buffer USB e' pieno.
bool write_all(const uint8_t* p, size_t n) {
    size_t off = 0;
    int vuoti = 0;
    while (off < n) {
        size_t w = Serial.write(p + off, n - off);
        if (w == 0) {
            if (++vuoti > 400) return false;  // circa 2 s senza progressi
            delay(5);
        } else {
            off += w;
            vuoti = 0;
        }
    }
    return true;
}

// Nome file accettato solo se e' un run_NNN.csv: niente percorsi arbitrari.
bool arg_path(const char* arg, char* full, size_t cap, char* name, size_t name_cap) {
    if (run_number(arg) < 0) return false;
    if (arg[0] == '/') arg++;
    snprintf(name, name_cap, "%s", arg);
    snprintf(full, cap, "%s/%s", MOUNT, arg);
    return true;
}

// Dimensione e CRC32 di un file. Con il lock del logger preso.
bool file_crc(const char* full, uint32_t& size, uint32_t& crc) {
    FILE* f = fopen(full, "rb");
    if (f == nullptr) return false;
    uint8_t buf[512];
    size = 0;
    crc = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        crc = crc32_update(crc, buf, n);
        size += static_cast<uint32_t>(n);
    }
    fclose(f);
    return true;
}

const char* state_name(logger::State s) {
    switch (s) {
        case logger::State::Recording: return "recording";
        case logger::State::Saved: return "saved";
        case logger::State::Full: return "full";
        case logger::State::Error: return "error";
        default: return "off";
    }
}

void cmd_info() {
    reply("INFO id=%d mac=%s fw=%s file=%s state=%s", g_node.id, g_node.mac_str, FW_VERSION,
          logger::current_path(), state_name(logger::state()));
}

void cmd_ls() {
    logger::lock();
    DIR* dir = opendir(MOUNT);
    if (dir != nullptr) {
        while (dirent* de = readdir(dir)) {
            char full[96];
            snprintf(full, sizeof(full), "%s/%s", MOUNT, de->d_name);
            uint32_t size, crc;
            if (!file_crc(full, size, crc)) continue;
            reply("FILE %s %lu %08lX", de->d_name, static_cast<unsigned long>(size),
                  static_cast<unsigned long>(crc));
        }
        closedir(dir);
    }
    logger::unlock();
    reply("END");
}

void cmd_free() {
    size_t total = logger::fs_total(), used = logger::fs_used();
    reply("FREE total=%lu used=%lu free=%lu", static_cast<unsigned long>(total),
          static_cast<unsigned long>(used), static_cast<unsigned long>(total - used));
}

void cmd_get(const char* arg) {
    char full[96], name[32];
    if (!arg_path(arg, full, sizeof(full), name, sizeof(name))) {
        reply("ERR nome file non valido");
        return;
    }
    // Se e' il file aperto, lo chiude prima: la registrazione resta ferma.
    if (logger::state() == logger::State::Recording && strcmp(logger::current_path() + 1, name) == 0) {
        logger::save_and_close("get");
    }

    logger::lock();
    uint32_t size, crc;
    if (!file_crc(full, size, crc)) {
        logger::unlock();
        reply("ERR file inesistente");
        return;
    }
    FILE* f = fopen(full, "rb");
    if (f == nullptr) {
        logger::unlock();
        reply("ERR apertura fallita");
        return;
    }

    // L'uscita seriale e' riservata al trasferimento finche' non e' finito.
    xSemaphoreTake(g_serial_mutex, portMAX_DELAY);
    Serial.setTxTimeoutMs(1000);
    char head[96];
    int h = snprintf(head, sizeof(head), "BEGIN %s %lu %08lX\n", name, static_cast<unsigned long>(size),
                     static_cast<unsigned long>(crc));
    bool ok = write_all(reinterpret_cast<const uint8_t*>(head), static_cast<size_t>(h));
    uint8_t buf[512];
    uint32_t inviati = 0;
    while (ok && inviati < size) {
        size_t want = size - inviati < sizeof(buf) ? size - inviati : sizeof(buf);
        size_t n = fread(buf, 1, want, f);
        if (n == 0) break;
        ok = write_all(buf, n);
        inviati += static_cast<uint32_t>(n);
    }
    if (ok) ok = write_all(reinterpret_cast<const uint8_t*>("END\n"), 4);
    Serial.flush();
    Serial.setTxTimeoutMs(100);
    xSemaphoreGive(g_serial_mutex);

    fclose(f);
    logger::unlock();
}

void cmd_rm(const char* arg) {
    char full[96], name[32];
    if (!arg_path(arg, full, sizeof(full), name, sizeof(name))) {
        reply("ERR nome file non valido");
        return;
    }
    if (logger::state() == logger::State::Recording && strcmp(logger::current_path() + 1, name) == 0) {
        reply("ERR il file e' aperto: chiuderlo con una pressione lunga o con get");
        return;
    }
    logger::lock();
    int r = unlink(full);
    logger::unlock();
    reply(r == 0 ? "OK" : "ERR cancellazione fallita");
}

void cmd_format(const char* arg) {
    if (strcmp(arg, "SI") != 0) {
        reply("ERR serve il comando 'format SI'");
        return;
    }
    logger::save_and_close("format");
    logger::lock();
    bool ok = logger::format_locked();
    logger::unlock();
    reply(ok ? "OK registrazione ferma" : "ERR formattazione fallita");
}

void run_command(char* line) {
    char* arg = strchr(line, ' ');
    if (arg != nullptr) {
        *arg++ = '\0';
        while (*arg == ' ') arg++;
    } else {
        arg = line + strlen(line);
    }

    if (strcmp(line, "info") == 0) {
        cmd_info();
    } else if (strcmp(line, "ls") == 0) {
        cmd_ls();
    } else if (strcmp(line, "free") == 0) {
        cmd_free();
    } else if (strcmp(line, "get") == 0) {
        cmd_get(arg);
    } else if (strcmp(line, "rm") == 0) {
        cmd_rm(arg);
    } else if (strcmp(line, "format") == 0) {
        cmd_format(arg);
    } else if (line[0] != '\0') {
        reply("ERR comando sconosciuto (info, ls, free, get, rm, format)");
    }
}

}  // namespace

void console_poll() {
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            g_line[g_len] = '\0';
            if (g_len > 0) run_command(g_line);
            g_len = 0;
        } else if (g_len < sizeof(g_line) - 1) {
            g_line[g_len++] = c;
        }
    }
}

#include "ble.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <string.h>

#include "app.h"
#include "ble_codec.h"
#include "config.h"
#include "logger.h"
#include "runlog.h"

namespace {

constexpr size_t COMMAND_MAX = 8;
constexpr int OUT_QUEUE_LEN = 12;
constexpr int COMMAND_QUEUE_LEN = 8;

struct Command {
    uint8_t len;
    uint8_t data[COMMAND_MAX];
};

volatile BleState g_state = BleState::Off;
bool g_enabled = false;
bool g_started = false;

BLEServer* g_server = nullptr;
BLECharacteristic* g_data = nullptr;
BLECharacteristic* g_urgent = nullptr;
BLECharacteristic* g_command = nullptr;
QueueHandle_t g_out_queue = nullptr;
QueueHandle_t g_command_queue = nullptr;

void log_ble(BleEvent event, int detail) {
    LogEvent e;
    memset(&e, 0, sizeof(e));
    e.type = LogEventType::Ble;
    e.t_ms = millis();
    e.u.ble.event = event;
    e.u.ble.detail = detail;
    logger::post(e);
}

// Callback brevi: aggiornano lo stato, registrano e tornano.
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server, ble_gap_conn_desc* desc) override {
        g_state = BleState::Connected;
        log_ble(BleEvent::Connected, 0);
        // Intervallo di connessione lungo: basta uno STATO ogni 5 s.
        if (!server->requestConnParams(desc->conn_handle, BLE_CONN_INTERVAL_MIN, BLE_CONN_INTERVAL_MAX, BLE_CONN_LATENCY,
                                      BLE_CONN_TIMEOUT)) {
            logger::post_sys("ble: parametri di connessione non accettati");
        }
    }
    void onDisconnect(BLEServer*, ble_gap_conn_desc*) override {
        g_state = g_enabled ? BleState::Advertising : BleState::Off;
        log_ble(BleEvent::Dropped, 0);
        if (g_enabled) BLEDevice::startAdvertising();
    }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        String v = c->getValue();
        Command cmd;
        if (v.length() == 0 || v.length() > COMMAND_MAX) {
            logger::post_sys("comando BLE scartato (lunghezza %u)", static_cast<unsigned>(v.length()));
            return;
        }
        cmd.len = static_cast<uint8_t>(v.length());
        memcpy(cmd.data, v.c_str(), cmd.len);
        xQueueSend(g_command_queue, &cmd, 0);
    }
};

ServerCallbacks g_server_callbacks;
CommandCallbacks g_command_callbacks;

void notify(BLECharacteristic* ch, const uint8_t* data, size_t len) {
    if (g_state != BleState::Connected || ch == nullptr) return;
    ch->setValue(const_cast<uint8_t*>(data), len);
    ch->notify();
}

// Rinvio dei messaggi salvati dopo RIPRENDI_DA: uno alla volta, fra i messaggi nuovi.
runlog::Reader g_reader;
bool g_resending = false;

void resend_next() {
    uint8_t msg[ble::MAX_MESSAGE_SIZE];
    size_t len;
    if (!g_reader.next(msg, sizeof(msg), len)) {
        g_reader.close();
        g_resending = false;
        return;
    }
    // Lo STATO va sul canale dati, l'EVENTO su quello urgenti.
    notify(msg[1] == static_cast<uint8_t>(ble::MessageType::Event) ? g_urgent : g_data, msg, len);
}

void handle(const BleMsg& m) {
    switch (m.kind) {
        case BleMsg::Kind::Data:
        case BleMsg::Kind::Urgent:
            // Prima su flash, poi sul BLE.
            if (m.save && !runlog::append(m.data, m.len)) logger::post_sys("file della corsa: scrittura fallita");
            notify(m.kind == BleMsg::Kind::Data ? g_data : g_urgent, m.data, m.len);
            break;
        case BleMsg::Kind::NewRun:
            g_reader.close();
            g_resending = false;
            if (!runlog::begin_new()) logger::post_sys("file della corsa: apertura fallita");
            break;
        case BleMsg::Kind::EndRun:
            g_reader.close();
            g_resending = false;
            runlog::end();
            break;
        case BleMsg::Kind::Resend:
            g_resending = g_reader.open(m.from_seq, m.to_seq);
            break;
    }
}

void ble_task(void*) {
    for (;;) {
        BleMsg m;
        if (xQueueReceive(g_out_queue, &m, pdMS_TO_TICKS(g_resending ? BLE_NOTIFY_GAP_MS : 100)) == pdTRUE) {
            handle(m);
            vTaskDelay(pdMS_TO_TICKS(BLE_NOTIFY_GAP_MS));
        } else if (g_resending) {
            resend_next();
        }
    }
}

void start_stack() {
    char name[16];
    snprintf(name, sizeof(name), "Manet-%d", g_node.id);
    BLEDevice::init(name);
    BLEDevice::setMTU(BLE_MTU);

    g_server = BLEDevice::createServer();
    g_server->setCallbacks(&g_server_callbacks);
    BLEService* service = g_server->createService(BLE_SERVICE_UUID);
    g_data = service->createCharacteristic(BLE_DATA_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    g_urgent = service->createCharacteristic(BLE_URGENT_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    g_command = service->createCharacteristic(BLE_COMMAND_UUID, BLECharacteristic::PROPERTY_WRITE);
    g_command->setCallbacks(&g_command_callbacks);
    service->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->setScanResponse(true);

    g_out_queue = xQueueCreate(OUT_QUEUE_LEN, sizeof(BleMsg));
    g_command_queue = xQueueCreate(COMMAND_QUEUE_LEN, sizeof(Command));
    xTaskCreatePinnedToCore(ble_task, "ble", 6144, nullptr, 2, nullptr, 1);
    g_started = true;
}

}  // namespace

void ble_enable(bool on) {
    if (on == g_enabled) return;
    g_enabled = on;
    if (on) {
        if (!g_started) start_stack();
        BLEDevice::startAdvertising();
        g_state = BleState::Advertising;
        logger::post_sys("ble acceso");
    } else if (g_started) {
        BLEDevice::stopAdvertising();
        if (g_server != nullptr && g_server->getConnectedCount() > 0) g_server->disconnect(g_server->getConnId());
        g_state = BleState::Off;
        logger::post_sys("ble spento");
    }
}

BleState ble_state() { return g_state; }

bool ble_send(const BleMsg& m) {
    if (!g_enabled || !g_started) return false;
    return xQueueSend(g_out_queue, &m, 0) == pdTRUE;
}

size_t ble_take_command(uint8_t* out, size_t cap) {
    if (!g_started) return 0;
    Command cmd;
    if (xQueueReceive(g_command_queue, &cmd, 0) != pdTRUE || cmd.len > cap) return 0;
    memcpy(out, cmd.data, cmd.len);
    return cmd.len;
}

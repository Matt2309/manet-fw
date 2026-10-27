// Formattazione delle righe del file di registrazione.
//
// Ogni funzione scrive in buf (con '\n' finale e terminatore) e restituisce i
// caratteri scritti, escluso il terminatore; 0 se cap non basta.
//
//   # intestazione
//   TX,t_ms,seq
//   RX,t_ms,src,seq,rssi,tab,salti,t_s,stato
//                                     tab = id:segnale:eta:incertezza:pendenza;...
//                                     t_s vuoto = corsa non avviata; stato = byte stato del beacon
//                                     in forma ridotta: solo fino a rssi e tab vuoto
//   GPS,t_ms,utc,lat,lon,alt,vel_kmh,rotta,hdop,sat,fix
//   KAL,t_ms,id,segnale,pendenza,incertezza,eta       stima del Kalman per un compagno (1 Hz)
//   DET,t_ms,versione,tipo,inizio|fine,id,R           decisione del rilevatore (base|gnss, pre|perso|sorda)
//   TEL,t_ms,azione,id,t_s,compagno,n_coda,tentativi  telemetria (prodotto|inviato|confermato|ricevuto|coda)
//   CMD,t_ms,comando,origine,seq,decimi               START o STOP (app|radio), solo capogruppo per "app"
//   BLE,t_ms,evento,dettaglio                         connesso|caduto, solo capogruppo
//   EVT,t_ms,n
//   BAT,t_ms,mV
//   SYS,t_ms,messaggio
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "packets.h"
#include "nmea.h"

size_t csv_header(char* buf, size_t cap, const char* fw, int id, const char* role,
                  const char* mac, int channel, int power_qdbm, int period_ms, int jitter_ms,
                  const char* rate, int proto);
// Parametri di config.h: righe "# param chiave=valore ..." con '\n' finale.
size_t csv_params(char* buf, size_t cap);

size_t csv_tx(char* buf, size_t cap, uint32_t t_ms, uint32_t seq);
size_t csv_rx(char* buf, size_t cap, uint32_t t_ms, const packets::Beacon& b, int rssi, bool reduced);
size_t csv_gps(char* buf, size_t cap, uint32_t t_ms, const GpsEpoch& e);
size_t csv_evt(char* buf, size_t cap, uint32_t t_ms, int n);
size_t csv_bat(char* buf, size_t cap, uint32_t t_ms, int mv);
size_t csv_sys(char* buf, size_t cap, uint32_t t_ms, const char* msg);

enum class DetVersion : uint8_t { Base, Gnss };       // rilevatore senza o con GNSS
enum class DetKind : uint8_t { PreAlarm, Lost, Deaf };
enum class TelAction : uint8_t { Produced, Sent, Confirmed, Received, Queued };
enum class CmdOrigin : uint8_t { App, Radio };
enum class BleEvent : uint8_t { Connected, Dropped };

// age_ds in decimi di secondo; sigma e' la deviazione standard in dB.
size_t csv_kal(char* buf, size_t cap, uint32_t t_ms, int id, float signal, float slope, float sigma,
               int age_ds);
// r e' il segnale fuso del compagno; vuoto per la scheda sorda (id = 0).
size_t csv_det(char* buf, size_t cap, uint32_t t_ms, DetVersion v, DetKind kind, bool start, int id,
               float r);
// peer = compagno scelto (0 se nessuno), queued = record in coda, tries = tentativi dell'invio.
size_t csv_tel(char* buf, size_t cap, uint32_t t_ms, TelAction action, int id, int t_s, int peer,
               int queued, int tries);
size_t csv_cmd(char* buf, size_t cap, uint32_t t_ms, packets::Cmd cmd, CmdOrigin origin, int seq,
               int elapsed_ds);
size_t csv_ble(char* buf, size_t cap, uint32_t t_ms, BleEvent event, int detail);

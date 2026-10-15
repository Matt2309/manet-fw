// Formattazione delle righe del file di registrazione.
//
// Ogni funzione scrive in buf (con '\n' finale e terminatore) e restituisce i
// caratteri scritti, escluso il terminatore; 0 se cap non basta.
//
//   # intestazione
//   TX,t_ms,seq
//   RX,t_ms,src,seq,rssi,tab          tab = id:rssi:eta:varianza;...
//   GPS,t_ms,utc,lat,lon,alt,vel_kmh,rotta,hdop,sat,fix
//   EVT,t_ms,n
//   BAT,t_ms,mV
//   SYS,t_ms,messaggio
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "beacon.h"
#include "nmea.h"

size_t csv_header(char* buf, size_t cap, const char* fw, int id, const char* mac,
                  int channel, int power_qdbm, int period_ms, int jitter_ms,
                  const char* rate);
size_t csv_tx(char* buf, size_t cap, uint32_t t_ms, uint32_t seq);
size_t csv_rx(char* buf, size_t cap, uint32_t t_ms, int src, uint32_t seq, int rssi,
              const beacon::Entry* tab, int n);
size_t csv_gps(char* buf, size_t cap, uint32_t t_ms, const GpsEpoch& e);
size_t csv_evt(char* buf, size_t cap, uint32_t t_ms, int n);
size_t csv_bat(char* buf, size_t cap, uint32_t t_ms, int mv);
size_t csv_sys(char* buf, size_t cap, uint32_t t_ms, const char* msg);

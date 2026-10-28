// Tabella MAC -> ID del nodo (1..MAX_NODES).
//
// Il MAC e' quello della interfaccia Wi-Fi STA, stampato all'avvio sulla seriale
// e mostrato sul display quando l'ID e' 0 (scheda non in tabella).
// Aggiungere una riga per scheda, prima del terminatore.
#pragma once

#include <stdint.h>

#include "config.h"

struct NodeEntry {
    uint8_t mac[6];
    uint8_t id;
};

static const NodeEntry NODES[] = {
    {{0xAC, 0xA7, 0x04, 0x3C, 0x37, 0xF0}, 1},
    {{0xAC, 0xA7, 0x04, 0x3C, 0x39, 0xF0}, 2},
    {{0xAC, 0xA7, 0x04, 0x3C, 0x3D, 0x90}, 3},
    {{0, 0, 0, 0, 0, 0}, 0},  // terminatore: non togliere
};

// Il ruolo (corridore o capogruppo) non sta qui: si sceglie con il tasto USER e si salva in NVS.
// Con 20 righe piu' il terminatore la tabella e' al massimo.
static_assert(sizeof(NODES) / sizeof(NODES[0]) <= MAX_NODES + 1, "troppe righe in NODES");

// ID del nodo dal MAC; 0 se il MAC non e' in tabella.
inline uint8_t node_id_from_mac(const uint8_t mac[6]) {
    for (const NodeEntry& n : NODES) {
        if (n.id == 0) break;
        bool uguale = true;
        for (int i = 0; i < 6; i++) {
            if (n.mac[i] != mac[i]) uguale = false;
        }
        if (uguale) return n.id;
    }
    return 0;
}

// MAC di un nodo dall'ID; nullptr se l'ID non e' in tabella.
inline const uint8_t* node_mac_from_id(uint8_t id) {
    for (const NodeEntry& n : NODES) {
        if (n.id == 0) break;
        if (n.id == id) return n.mac;
    }
    return nullptr;
}

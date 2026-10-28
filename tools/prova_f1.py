#!/usr/bin/env python3
"""Prova F1: i beacon ricevuti dal capogruppo cambiano quando il BLE e' collegato?

Si usa il file di registro del capogruppo (righe RX e BLE). Il capogruppo manda uno STATO ogni 5 s sul BLE
anche prima dello START, quindi basta lasciarlo acceso con le altre schede vicine e collegare/scollegare
l'app (o nRF Connect) a intervalli di qualche minuto.

    python tools/prova_f1.py dati/2026-11-03/scheda_1/run_004.csv

Per ogni intervallo con BLE collegato o non collegato (almeno 20 s) e per ogni scheda si stampano i beacon ricevuti,
quelli attesi dal periodo del beacon e la percentuale persa; in fondo il totale per i due stati.
"""

from __future__ import annotations

import argparse
import sys
from collections import defaultdict
from pathlib import Path

MIN_INTERVALLO_S = 20.0


def leggi(percorso: Path):
    """Restituisce (periodo_ms, rx[(t_ms, src, seq, rssi)], ble[(t_ms, connesso)])."""
    periodo = 500.0
    rx, ble = [], []
    for r in percorso.read_text(encoding="utf-8", errors="replace").splitlines():
        if r.startswith("#"):
            for tok in r[1:].split():
                if tok.startswith("periodo_ms="):
                    periodo = float(tok.split("=", 1)[1])
            continue
        f = r.split(",")
        try:
            if f[0] == "RX":
                rx.append((int(f[1]), int(f[2]), int(f[3]), int(f[4])))
            elif f[0] == "BLE" and f[2] in ("connesso", "caduto"):
                ble.append((int(f[1]), f[2] == "connesso"))
        except (ValueError, IndexError):
            continue  # riga troncata
    return periodo, rx, ble


def intervalli(rx, ble):
    """Intervalli [(inizio_ms, fine_ms, collegato)] dal primo all'ultimo istante registrato."""
    if not rx:
        return []
    inizio, fine = rx[0][0], rx[-1][0]
    out, t, collegato = [], inizio, False
    for tb, conn in sorted(ble):
        if tb <= t:
            collegato = conn
            continue
        if tb >= fine:
            break
        out.append((t, tb, collegato))
        t, collegato = tb, conn
    out.append((t, fine, collegato))
    return out


def analizza(percorso: Path) -> list[dict]:
    """Una riga per intervallo e scheda: stato, durata, ricevuti, attesi, persi%, RSSI medio."""
    periodo, rx, ble = leggi(percorso)
    righe = []
    for a, b, collegato in intervalli(rx, ble):
        durata = (b - a) / 1000.0
        if durata < MIN_INTERVALLO_S:
            continue
        per_src: dict[int, dict[int, int]] = defaultdict(dict)
        for t, src, seq, rssi in rx:
            if a <= t < b:
                per_src[src].setdefault(seq, rssi)
        for src, visti in sorted(per_src.items()):
            attesi = durata * 1000.0 / periodo
            ricevuti = len(visti)
            righe.append({
                "collegato": collegato, "inizio_s": a / 1000.0, "durata_s": durata, "src": src,
                "ricevuti": ricevuti, "attesi": attesi,
                "persi_pct": max(0.0, (1.0 - ricevuti / attesi) * 100.0),
                "rssi": sum(visti.values()) / ricevuti,
            })
    return righe


def totali(righe: list[dict]) -> dict[bool, dict]:
    out = {}
    for stato in (False, True):
        sel = [r for r in righe if r["collegato"] == stato]
        att = sum(r["attesi"] for r in sel)
        ric = sum(r["ricevuti"] for r in sel)
        if sel and att > 0:
            out[stato] = {"durata_s": sum(r["durata_s"] for r in sel) / len({r["src"] for r in sel}),
                          "ricevuti": ric, "attesi": att, "persi_pct": max(0.0, (1.0 - ric / att) * 100.0)}
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", type=Path, help="file di registro del capogruppo")
    args = ap.parse_args()
    righe = analizza(args.file)
    if not righe:
        print("Nessun intervallo utile (servono righe RX e almeno 20 s per stato).", file=sys.stderr)
        return 1
    print(f"{'BLE':<10}{'da s':>8}{'durata s':>10}{'da':>4}{'ricevuti':>10}{'attesi':>9}{'persi%':>8}{'RSSI':>7}")
    for r in righe:
        print(f"{'collegato' if r['collegato'] else 'libero':<10}{r['inizio_s']:>8.0f}{r['durata_s']:>10.0f}{r['src']:>4}"
              f"{r['ricevuti']:>10}{r['attesi']:>9.0f}{r['persi_pct']:>8.1f}{r['rssi']:>7.1f}")
    print()
    for stato, t in totali(righe).items():
        print(f"{'BLE collegato' if stato else 'BLE libero':<14} {t['durata_s']:>6.0f} s  persi {t['persi_pct']:.1f}%  "
              f"({t['ricevuti']} su {t['attesi']:.0f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

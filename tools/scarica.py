#!/usr/bin/env python3
"""Scarica i file di registrazione dalle schede collegate via USB.

Per ogni scheda trovata (USB nativo Espressif, VID 0x303A) chiede `info` e `ls`,
poi scarica ogni file con `get` in dati/<data>/scheda_<id>/ controllando
dimensione e CRC32. Non cancella nulla dalla scheda.

Attenzione: `get` sul file aperto lo chiude, quindi la registrazione di quella
scheda si ferma (sul display compare SALVATO). Una pressione lunga la riavvia
su un nuovo file.

Uso:
    python tools/scarica.py                      # tutte le schede trovate
    python tools/scarica.py --porta /dev/cu.usbmodem1101
    python tools/scarica.py --data 2026-10-07 --dest dati
"""
from __future__ import annotations

import argparse
import re
import sys
import time
import zlib
from datetime import date
from pathlib import Path

import serial
from serial.tools import list_ports

VID_ESPRESSIF = 0x303A
BAUD = 115200


class ErroreScheda(Exception):
    pass


# ----------------------------------------------------------------------------
# Protocollo
# ----------------------------------------------------------------------------

def leggi_riga(ser, timeout: float) -> str | None:
    """Legge una riga di testo; None se scade il tempo. Salta le righe di debug '#'."""
    fine = time.monotonic() + timeout
    while time.monotonic() < fine:
        raw = ser.readline()
        if not raw:
            continue
        riga = raw.decode("utf-8", errors="replace").strip()
        if riga and not riga.startswith("#"):
            return riga
    return None


def svuota(ser) -> None:
    """Scarta quello che c'e' gia' sulla seriale."""
    time.sleep(0.1)
    ser.reset_input_buffer()


def comando(ser, testo: str) -> None:
    ser.write((testo + "\n").encode())
    ser.flush()


def chiedi_info(ser) -> dict[str, str]:
    svuota(ser)
    comando(ser, "info")
    riga = leggi_riga(ser, 3.0)
    while riga is not None and not riga.startswith("INFO "):
        riga = leggi_riga(ser, 3.0)
    if riga is None:
        raise ErroreScheda("nessuna risposta a 'info'")
    return dict(kv.split("=", 1) for kv in riga[5:].split() if "=" in kv)


def elenca_file(ser) -> list[tuple[str, int, int]]:
    """Restituisce [(nome, dimensione, crc32)]."""
    svuota(ser)
    comando(ser, "ls")
    file = []
    while True:
        riga = leggi_riga(ser, 60.0)  # il CRC di tutti i file puo' richiedere qualche secondo
        if riga is None:
            raise ErroreScheda("'ls' senza END")
        if riga == "END":
            return file
        m = re.fullmatch(r"FILE (\S+) (\d+) ([0-9A-Fa-f]{8})", riga)
        if m:
            file.append((m.group(1), int(m.group(2)), int(m.group(3), 16)))


def scarica_file(ser, nome: str) -> tuple[bytes, int, int]:
    """Scarica un file; restituisce (dati, dimensione dichiarata, crc dichiarato)."""
    svuota(ser)
    comando(ser, f"get {nome}")
    while True:
        riga = leggi_riga(ser, 30.0)
        if riga is None:
            raise ErroreScheda(f"nessuna risposta a 'get {nome}'")
        if riga.startswith("ERR"):
            raise ErroreScheda(riga)
        m = re.fullmatch(r"BEGIN (\S+) (\d+) ([0-9A-Fa-f]{8})", riga)
        if m:
            break
    dimensione, crc = int(m.group(2)), int(m.group(3), 16)

    dati = bytearray()
    ultimo = time.monotonic()
    while len(dati) < dimensione:
        blocco = ser.read(min(4096, dimensione - len(dati)))
        if blocco:
            dati += blocco
            ultimo = time.monotonic()
        elif time.monotonic() - ultimo > 10.0:
            raise ErroreScheda(f"{nome}: trasferimento fermo a {len(dati)}/{dimensione} byte")
    fine = leggi_riga(ser, 5.0)
    if fine != "END":
        raise ErroreScheda(f"{nome}: manca END dopo i dati (ricevuto {fine!r})")
    return bytes(dati), dimensione, crc


def crc32(dati: bytes) -> int:
    return zlib.crc32(dati) & 0xFFFFFFFF


# ----------------------------------------------------------------------------
# Salvataggio
# ----------------------------------------------------------------------------

def cartella_scheda(radice: Path, info: dict[str, str]) -> Path:
    """Cartella della scheda; se l'ID e' 0 (MAC non in tabella) si usa la coda del MAC."""
    id_ = info.get("id", "0")
    if id_ == "0":
        coda = info.get("mac", "").replace(":", "")[-6:].lower()
        return radice / f"scheda_0_{coda}"
    return radice / f"scheda_{id_}"


def percorso_libero(cartella: Path, nome: str, crc: int) -> Path | None:
    """Percorso dove salvare; None se un file identico (stesso CRC) c'e' gia'."""
    p = cartella / nome
    k = 1
    while p.exists():
        if crc32(p.read_bytes()) == crc:
            return None
        k += 1
        p = cartella / f"{Path(nome).stem}_{k}{Path(nome).suffix}"
    return p


def scarica_scheda(ser, radice: Path) -> int:
    """Scarica tutti i file di una scheda; restituisce il numero di errori."""
    info = chiedi_info(ser)
    cartella = cartella_scheda(radice, info)
    print(f"Scheda id={info.get('id')} mac={info.get('mac')} fw={info.get('fw')} "
          f"stato={info.get('state')} -> {cartella}")
    errori = 0
    cartella.mkdir(parents=True, exist_ok=True)
    for nome, dim, crc in elenca_file(ser):
        try:
            dati, dim_d, crc_d = scarica_file(ser, nome)
        except ErroreScheda as e:
            print(f"  {nome}: ERRORE {e}")
            errori += 1
            continue
        if len(dati) != dim_d or crc32(dati) != crc_d:
            print(f"  {nome}: ERRORE dimensione o CRC32 non coincidono "
                  f"({len(dati)}/{dim_d} byte, crc {crc32(dati):08X}/{crc_d:08X})")
            errori += 1
            continue
        if dim_d != dim or crc_d != crc:
            print(f"  {nome}: nota, il file e' cambiato dopo 'ls' (era aperto e chiuso da 'get')")
        dest = percorso_libero(cartella, nome, crc_d)
        if dest is None:
            print(f"  {nome}: gia' presente, identico ({dim_d} byte)")
            continue
        dest.write_bytes(dati)
        print(f"  {nome}: OK {dim_d} byte, crc32 {crc_d:08X} -> {dest.name}")
    return errori


def apri(porta: str):
    """Apre la porta senza toccare DTR/RTS, che su ESP32-S3 possono resettare la scheda."""
    ser = serial.Serial()
    ser.port = porta
    ser.baudrate = BAUD
    ser.timeout = 1.0
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


def trova_porte() -> list[str]:
    return sorted(p.device for p in list_ports.comports() if p.vid == VID_ESPRESSIF)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--porta", action="append", help="porta seriale (ripetibile); default: tutte le Espressif")
    ap.add_argument("--dest", default="dati", help="cartella radice (default: dati)")
    ap.add_argument("--data", default=date.today().isoformat(), help="sottocartella (default: oggi)")
    args = ap.parse_args()

    porte = args.porta or trova_porte()
    if not porte:
        print("Nessuna scheda trovata (USB Espressif, VID 0x303A). Usare --porta.")
        return 2
    radice = Path(args.dest) / args.data

    errori = 0
    for porta in porte:
        print(f"Porta {porta}")
        try:
            with apri(porta) as ser:
                errori += scarica_scheda(ser, radice)
        except (serial.SerialException, ErroreScheda) as e:
            print(f"  ERRORE {e}")
            errori += 1
    print("Fatto." if errori == 0 else f"Finito con {errori} errori.")
    return 0 if errori == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

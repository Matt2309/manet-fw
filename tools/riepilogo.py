#!/usr/bin/env python3
"""Riepilogo rapido di una corsa registrata da manet-fw.

Legge tutti i run_*.csv nella cartella (anche nelle sottocartelle scheda_N) e produce:
  - per ogni coppia ordinata i->j: beacon inviati, ricevuti, % persi (dai seq),
    RSSI mediano, p5 e p95;
  - per ogni scheda: durata, % di tempo con fix GPS, batteria iniziale e finale,
    eventi, righe SYS;
  - allineamento delle schede sull'UTC del GPS (retta t_ms -> UTC per scheda) e
    scarto residuo, piu' un controllo incrociato sui beacon (UTC rx - UTC tx);
  - percorso.gpx: media delle schede con fix, a 1 Hz, da usare in manet-sim;
  - rssi_coppie.png: RSSI nel tempo per ogni coppia, con gli eventi come linee verticali.

L'UTC di ogni riga GPS e' quello della frase NMEA, mentre t_ms e' l'istante di arrivo
della frase: la retta contiene quindi la latenza di uscita del modulo GNSS, uguale su
tutte le schede dello stesso modello, e l'allineamento tra schede ne e' immune.

Uso:
    python tools/riepilogo.py dati/2026-10-07
    python tools/riepilogo.py dati/2026-10-07 --out risultati --no-grafico
"""
from __future__ import annotations

import argparse
import math
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

MIN_PUNTI_RETTA = 10        # righe GPS con fix per poter allineare un segmento
MAX_BUCO_GPX_S = 5          # buchi piu' corti si interpolano nel GPX
COLORI_SCHEDA = {1: "#2a78d6", 2: "#eb6834", 3: "#1baf7a"}   # slot 1-3 della palette di riferimento
COLORE_ALTRO = "#52514e"
STILI_EVT = {1: "--", 2: ":", 3: "-."}


# ----------------------------------------------------------------------------
# Lettura
# ----------------------------------------------------------------------------

@dataclass
class RigaGps:
    t: int
    utc: float | None
    lat: float | None
    lon: float | None
    alt: float | None
    sat: int | None
    fix: int


@dataclass
class Segmento:
    percorso: Path
    scheda: int
    intestazione: dict[str, str]
    tx: list[tuple[int, int]] = field(default_factory=list)                     # (t, seq)
    rx: list[tuple[int, int, int, int, str]] = field(default_factory=list)      # (t, src, seq, rssi, tab)
    gps: list[RigaGps] = field(default_factory=list)
    evt: list[tuple[int, int]] = field(default_factory=list)                    # (t, n)
    bat: list[tuple[int, int]] = field(default_factory=list)                    # (t, mV)
    sys: list[tuple[int, str]] = field(default_factory=list)
    scartate: int = 0
    # Retta t_ms -> UTC: utc = y0 + c + b * (t/1000 - x0)
    retta: tuple[float, float, float, float] | None = None
    deriva_ppm: float = 0.0
    residuo_rms_ms: float = 0.0
    residuo_max_ms: float = 0.0

    def tempi(self) -> list[int]:
        t = [x[0] for x in self.tx] + [x[0] for x in self.rx] + [g.t for g in self.gps]
        t += [x[0] for x in self.evt] + [x[0] for x in self.bat] + [x[0] for x in self.sys]
        return t

    def intervallo_ms(self) -> tuple[int, int]:
        t = self.tempi()
        return (min(t), max(t)) if t else (0, 0)

    def utc(self, t_ms: float) -> float | None:
        """UTC (secondi dall'epoca) di un istante t_ms, None se il segmento non e' allineato."""
        if self.retta is None:
            return None
        x0, y0, c, b = self.retta
        return y0 + c + b * (t_ms / 1000.0 - x0)


def _utc_da_iso(s: str) -> float | None:
    if not s:
        return None
    try:
        return datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()
    except ValueError:
        return None


def _num(s: str, tipo=float):
    return tipo(s) if s != "" else None


# Righe del firmware 2.x (rilevatore, telemetria, comandi, BLE): non servono alle statistiche
# sui beacon, ma non sono righe malformate.
TIPI_IGNORATI = {"KAL", "DET", "TEL", "CMD", "BLE"}


def leggi_segmento(percorso: Path) -> Segmento | None:
    """Legge un file di registrazione; None se manca l'intestazione."""
    testo = percorso.read_text(encoding="utf-8", errors="replace")
    righe = testo.splitlines()
    troncata = bool(testo) and not testo.endswith("\n")   # distacco durante la scrittura
    if troncata:
        righe = righe[:-1]
    intestazione: dict[str, str] = {}
    for r in righe:
        if r.startswith("#"):
            for tok in r[1:].split():
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    intestazione[k] = v
            break
    if "id" not in intestazione:
        print(f"Attenzione: {percorso} senza intestazione, ignorato", file=sys.stderr)
        return None
    seg = Segmento(percorso, int(intestazione["id"]), intestazione)
    if troncata:
        seg.scartate += 1

    for r in righe:
        if not r or r.startswith("#"):
            continue
        f = r.split(",")
        try:
            tipo = f[0]
            if tipo == "TX":
                seg.tx.append((int(f[1]), int(f[2])))
            elif tipo == "RX":
                seg.rx.append((int(f[1]), int(f[2]), int(f[3]), int(f[4]), f[5]))
            elif tipo == "GPS":
                seg.gps.append(RigaGps(int(f[1]), _utc_da_iso(f[2]), _num(f[3]), _num(f[4]),
                                       _num(f[5]), _num(f[9], int), int(f[10])))
            elif tipo == "EVT":
                seg.evt.append((int(f[1]), int(f[2])))
            elif tipo == "BAT":
                seg.bat.append((int(f[1]), int(f[2])))
            elif tipo == "SYS":
                seg.sys.append((int(f[1]), ",".join(f[2:])))
            elif tipo in TIPI_IGNORATI:
                pass
            else:
                seg.scartate += 1
        except (ValueError, IndexError):
            seg.scartate += 1   # tipicamente l'ultima riga troncata da un distacco
    return seg


def leggi_cartella(cartella: Path) -> list[Segmento]:
    segmenti = []
    for p in sorted(cartella.rglob("run_*.csv")):
        s = leggi_segmento(p)
        if s is not None:
            segmenti.append(s)
    return segmenti


# ----------------------------------------------------------------------------
# Allineamento sull'UTC del GPS
# ----------------------------------------------------------------------------

def allinea(seg: Segmento) -> None:
    """Retta ai minimi quadrati t_ms -> UTC sulle righe GPS con fix; riempie seg.retta."""
    punti = [(g.t / 1000.0, g.utc) for g in seg.gps if g.fix >= 2 and g.utc is not None]
    if len(punti) < MIN_PUNTI_RETTA:
        seg.retta = None
        return
    x = np.array([p[0] for p in punti])
    y = np.array([p[1] for p in punti])
    x0, y0 = x[0], y[0]
    b, c = np.polyfit(x - x0, y - y0, 1)
    res = (y - y0) - (c + b * (x - x0))
    seg.retta = (float(x0), float(y0), float(c), float(b))
    seg.deriva_ppm = float((b - 1.0) * 1e6)
    seg.residuo_rms_ms = float(np.sqrt(np.mean(res ** 2)) * 1000.0)
    seg.residuo_max_ms = float(np.max(np.abs(res)) * 1000.0)


# ----------------------------------------------------------------------------
# Statistiche per coppia
# ----------------------------------------------------------------------------

@dataclass
class StatCoppia:
    src: int
    dst: int
    inviati: int
    ricevuti: int
    attesi: int
    persi_pct: float | None
    rssi: np.ndarray
    scarti_ms: np.ndarray    # UTC rx - UTC tx per ogni beacon, dove i due segmenti sono allineati


def _finestra_utc(a: Segmento, b: Segmento) -> tuple[float, float] | None:
    """Intervallo UTC di registrazione comune a due segmenti allineati."""
    ta0, ta1 = a.intervallo_ms()
    tb0, tb1 = b.intervallo_ms()
    ua0, ua1, ub0, ub1 = a.utc(ta0), a.utc(ta1), b.utc(tb0), b.utc(tb1)
    if None in (ua0, ua1, ub0, ub1):
        return None
    lo, hi = max(ua0, ub0), min(ua1, ub1)
    return (lo, hi) if hi > lo else None


def statistiche_coppia(si: Segmento, sj: Segmento) -> StatCoppia | None:
    """Statistiche dei beacon inviati da si e ricevuti da sj."""
    finestra = _finestra_utc(si, sj)
    allineati = si.retta is not None and sj.retta is not None
    if allineati and finestra is None:
        return None   # registrazioni non sovrapposte

    tx = si.tx
    rx = [r for r in sj.rx if r[1] == si.scheda]
    scarti: list[float] = []

    if allineati:
        lo, hi = finestra
        tx = [x for x in si.tx if lo <= si.utc(x[0]) <= hi]
        rx = [r for r in rx if lo <= sj.utc(r[0]) <= hi + 1.0]
        attesi_seq = {x[1] for x in tx}
        uniche: dict[int, tuple[int, int]] = {}
        for t, _, seq, rssi, _ in rx:
            if seq in attesi_seq and seq not in uniche:
                uniche[seq] = (t, rssi)
        tx_t = {x[1]: x[0] for x in tx}
        for seq, (t, _) in uniche.items():
            scarti.append((sj.utc(t) - si.utc(tx_t[seq])) * 1000.0)
        ricevuti = len(uniche)
        attesi = len(attesi_seq)
        rssi = np.array([v[1] for v in uniche.values()], dtype=float)
    else:
        # Senza GPS non c'e' un intervallo comune: ci si limita ai seq ricevuti.
        uniche_rssi: dict[int, int] = {}
        for _, _, seq, rssi_, _ in rx:
            uniche_rssi.setdefault(seq, rssi_)
        ricevuti = len(uniche_rssi)
        attesi = (max(uniche_rssi) - min(uniche_rssi) + 1) if uniche_rssi else 0
        rssi = np.array(list(uniche_rssi.values()), dtype=float)

    persi = (1.0 - ricevuti / attesi) * 100.0 if attesi > 0 else None
    return StatCoppia(si.scheda, sj.scheda, len(tx), ricevuti, attesi, persi, rssi, np.array(scarti))


def tutte_le_coppie(segmenti: list[Segmento]) -> list[StatCoppia]:
    """Una statistica per ogni coppia ordinata di segmenti di schede diverse."""
    coppie = []
    for si in segmenti:
        for sj in segmenti:
            if si.scheda == sj.scheda:
                continue
            st = statistiche_coppia(si, sj)
            if st is not None:
                coppie.append(st)
    return coppie


def aggrega_coppie(coppie: list[StatCoppia]) -> dict[tuple[int, int], StatCoppia]:
    """Unisce le statistiche della stessa coppia (src, dst) quando ci sono piu' segmenti."""
    out: dict[tuple[int, int], StatCoppia] = {}
    for c in coppie:
        k = (c.src, c.dst)
        if k not in out:
            out[k] = c
            continue
        o = out[k]
        attesi = o.attesi + c.attesi
        ricevuti = o.ricevuti + c.ricevuti
        out[k] = StatCoppia(c.src, c.dst, o.inviati + c.inviati, ricevuti, attesi,
                            (1.0 - ricevuti / attesi) * 100.0 if attesi else None,
                            np.concatenate([o.rssi, c.rssi]), np.concatenate([o.scarti_ms, c.scarti_ms]))
    return out


# ----------------------------------------------------------------------------
# Statistiche per scheda
# ----------------------------------------------------------------------------

@dataclass
class StatScheda:
    scheda: int
    durata_s: float
    fix_pct: float | None
    bat_iniziale: int | None
    bat_finale: int | None
    eventi: int
    sys_righe: list[str]


def statistiche_scheda(scheda: int, segmenti: list[Segmento]) -> StatScheda:
    mie = [s for s in segmenti if s.scheda == scheda]
    durata = sum((hi - lo) / 1000.0 for lo, hi in (s.intervallo_ms() for s in mie))
    gps = [g for s in mie for g in s.gps]
    fix_pct = 100.0 * sum(1 for g in gps if g.fix >= 2) / len(gps) if gps else None
    bat = [b for s in mie for b in s.bat]
    sys_righe = [m for s in mie for _, m in s.sys]
    return StatScheda(scheda, durata, fix_pct,
                      bat[0][1] if bat else None, bat[-1][1] if bat else None,
                      sum(len(s.evt) for s in mie), sys_righe)


# ----------------------------------------------------------------------------
# GPX
# ----------------------------------------------------------------------------

def percorso_medio(segmenti: list[Segmento]) -> tuple[list[tuple[int, float, float, float]], int]:
    """Punti a 1 Hz sulla griglia UTC: media delle schede con fix in quel secondo.

    Restituisce ([(secondo_utc, lat, lon, alt)], buchi_non_riempiti). I buchi fino a
    MAX_BUCO_GPX_S secondi si interpolano linearmente.
    """
    per_secondo: dict[int, list[tuple[float, float, float]]] = {}
    for s in segmenti:
        for g in s.gps:
            if g.fix < 2 or g.lat is None or g.lon is None or g.utc is None:
                continue
            sec = int(math.floor(g.utc + 0.5))
            per_secondo.setdefault(sec, []).append((g.lat, g.lon, g.alt if g.alt is not None else 0.0))
    if not per_secondo:
        return [], 0

    medie = {sec: tuple(float(np.mean([p[k] for p in v])) for k in range(3))
             for sec, v in per_secondo.items()}
    secondi = sorted(medie)
    punti: list[tuple[int, float, float, float]] = []
    buchi = 0
    for prec, succ in zip(secondi, secondi[1:] + [None]):
        punti.append((prec, *medie[prec]))
        if succ is None:
            break
        mancanti = succ - prec - 1
        if mancanti <= 0:
            continue
        if mancanti <= MAX_BUCO_GPX_S:
            for k in range(1, mancanti + 1):
                f = k / (succ - prec)
                punti.append((prec + k, *[medie[prec][m] + f * (medie[succ][m] - medie[prec][m])
                                          for m in range(3)]))
        else:
            buchi += 1
    return punti, buchi


def scrivi_gpx(punti: list[tuple[int, float, float, float]], percorso: Path, nome: str) -> None:
    righe = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<gpx version="1.1" creator="manet-fw riepilogo.py" xmlns="http://www.topografix.com/GPX/1/1">',
             f"  <trk><name>{nome}</name><trkseg>"]
    for sec, lat, lon, alt in punti:
        t = datetime.fromtimestamp(sec, tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        righe.append(f'    <trkpt lat="{lat:.7f}" lon="{lon:.7f}"><ele>{alt:.1f}</ele><time>{t}</time></trkpt>')
    righe += ["  </trkseg></trk>", "</gpx>", ""]
    percorso.write_text("\n".join(righe), encoding="utf-8")


# ----------------------------------------------------------------------------
# Grafico
# ----------------------------------------------------------------------------

def colore(scheda: int) -> str:
    return COLORI_SCHEDA.get(scheda, COLORE_ALTRO)


def grafico_rssi(segmenti: list[Segmento], percorso: Path) -> bool:
    """RSSI nel tempo per ogni coppia ordinata, con gli eventi come linee verticali."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D
    from matplotlib.ticker import MaxNLocator

    schede = sorted({s.scheda for s in segmenti})
    coppie = [(i, j) for i in schede for j in schede if i != j]
    if not coppie:
        return False

    allineati = [s for s in segmenti if s.retta is not None]
    utc_rif = min(s.utc(s.intervallo_ms()[0]) for s in allineati) if allineati else None

    def asse_x(seg: Segmento, t_ms: int) -> float:
        u = seg.utc(t_ms)
        if u is not None and utc_rif is not None:
            return u - utc_rif
        return (t_ms - seg.intervallo_ms()[0]) / 1000.0   # senza GPS: dall'inizio del segmento

    n = len(coppie)
    colonne = 2 if n > 1 else 1
    righe = math.ceil(n / colonne)
    fig, assi = plt.subplots(righe, colonne, figsize=(6.2 * colonne, 2.6 * righe + 0.8),
                             sharex=True, sharey=True, squeeze=False, facecolor="#fcfcfb")
    for ax in assi.flat:
        ax.set_facecolor("#fcfcfb")

    for k, (i, j) in enumerate(coppie):
        ax = assi.flat[k]
        for sj in (s for s in segmenti if s.scheda == j):
            x = [asse_x(sj, r[0]) for r in sj.rx if r[1] == i]
            y = [r[3] for r in sj.rx if r[1] == i]
            ax.plot(x, y, ".", ms=2.5, color=colore(i), alpha=0.8, linestyle="none")
        for s in segmenti:
            if s.retta is None:
                continue
            for t, _ in s.evt:
                ax.axvline(asse_x(s, t), color=colore(s.scheda), linestyle=STILI_EVT.get(s.scheda, "--"),
                           linewidth=1.0, alpha=0.9)
        ax.set_title(f"scheda {i} → scheda {j}", fontsize=10, color="#0b0b0b", loc="left")
        ax.grid(True, color="#e3e2de", linewidth=0.6)
        for lato in ("top", "right"):
            ax.spines[lato].set_visible(False)
        for lato in ("left", "bottom"):
            ax.spines[lato].set_color("#9a998f")
        ax.tick_params(colors="#52514e", labelsize=8)
        ax.yaxis.set_major_locator(MaxNLocator(integer=True))   # l'RSSI e' un intero in dBm
    for k in range(n, righe * colonne):
        assi.flat[k].set_visible(False)
    for ax in assi[:, 0]:
        ax.set_ylabel("RSSI (dBm)", fontsize=9, color="#52514e")
    for ax in assi[-1, :]:
        ax.set_xlabel("tempo (s)" if utc_rif is not None else "tempo dall'inizio (s)", fontsize=9, color="#52514e")

    voci = [Line2D([0], [0], color=colore(s), linestyle=STILI_EVT.get(s, "--"), label=f"evento scheda {s}")
            for s in schede]
    fig.legend(handles=voci, loc="lower center", ncol=len(voci), frameon=False, fontsize=9)
    fig.tight_layout(rect=(0, 0.05, 1, 1))
    fig.savefig(percorso, dpi=150)
    plt.close(fig)
    return True


# ----------------------------------------------------------------------------
# Stampa
# ----------------------------------------------------------------------------

def _f(x, fmt="{:.1f}", vuoto="-"):
    return vuoto if x is None else fmt.format(x)


def stampa(segmenti: list[Segmento], coppie: dict[tuple[int, int], StatCoppia]) -> None:
    schede = sorted({s.scheda for s in segmenti})

    print("\n== Coppie (i -> j) ==")
    print(f"{'coppia':8} {'inviati':>8} {'ricevuti':>9} {'attesi':>7} {'persi%':>7} "
          f"{'RSSI med':>9} {'p5':>6} {'p95':>6}")
    for (i, j), c in sorted(coppie.items()):
        if len(c.rssi):
            med, p5, p95 = np.median(c.rssi), np.percentile(c.rssi, 5), np.percentile(c.rssi, 95)
        else:
            med = p5 = p95 = None
        print(f"{i} -> {j:<3} {c.inviati:8d} {c.ricevuti:9d} {c.attesi:7d} {_f(c.persi_pct):>7} "
              f"{_f(med):>9} {_f(p5, '{:.0f}'):>6} {_f(p95, '{:.0f}'):>6}")

    print("\n== Schede ==")
    print(f"{'scheda':7} {'durata':>9} {'fix%':>6} {'bat ini':>8} {'bat fin':>8} {'EVT':>4}  SYS")
    for sch in schede:
        st = statistiche_scheda(sch, segmenti)
        mm, ss = divmod(int(st.durata_s), 60)
        interessanti = [m for m in st.sys_righe if not m.startswith(("file chiuso", "lora sleep ok"))]
        sys_txt = f"{len(interessanti)} da guardare" if interessanti else "nessuna"
        print(f"{sch:<7} {mm:4d}:{ss:02d} {_f(st.fix_pct):>6} {_f(st.bat_iniziale, '{}'):>8} "
              f"{_f(st.bat_finale, '{}'):>8} {st.eventi:4d}  {sys_txt}")
        for m in interessanti[:5]:
            print(f"          SYS: {m}")
        if len(interessanti) > 5:
            print(f"          ... altre {len(interessanti) - 5}")

    print("\n== Allineamento sull'UTC del GPS ==")
    for s in segmenti:
        if s.retta is None:
            print(f"scheda {s.scheda} ({s.percorso.name}): non allineabile "
                  f"(meno di {MIN_PUNTI_RETTA} righe GPS con fix)")
        else:
            print(f"scheda {s.scheda} ({s.percorso.name}): deriva {s.deriva_ppm:+.1f} ppm, "
                  f"residuo rms {s.residuo_rms_ms:.1f} ms, max {s.residuo_max_ms:.1f} ms")

    print("\n== Controllo incrociato sui beacon (UTC rx - UTC tx, ms) ==")
    qualcuno = False
    for (i, j), c in sorted(coppie.items()):
        if len(c.scarti_ms):
            qualcuno = True
            print(f"{i} -> {j}: mediana {np.median(c.scarti_ms):+.1f}, "
                  f"p5 {np.percentile(c.scarti_ms, 5):+.1f}, p95 {np.percentile(c.scarti_ms, 95):+.1f}")
    if not qualcuno:
        print("(nessuna coppia con entrambe le schede allineate)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cartella", type=Path, help="cartella con i run_*.csv delle schede")
    ap.add_argument("--out", type=Path, help="cartella di uscita (default: la cartella dei dati)")
    ap.add_argument("--no-grafico", action="store_true", help="salta il grafico PNG")
    args = ap.parse_args()

    segmenti = leggi_cartella(args.cartella)
    if not segmenti:
        print(f"Nessun run_*.csv in {args.cartella}")
        return 2
    out = args.out or args.cartella
    out.mkdir(parents=True, exist_ok=True)

    for s in segmenti:
        allinea(s)
        if s.scartate:
            print(f"Nota: {s.percorso.name}: {s.scartate} righe scartate (malformate o troncate)")

    coppie = aggrega_coppie(tutte_le_coppie(segmenti))
    stampa(segmenti, coppie)

    punti, buchi = percorso_medio(segmenti)
    if punti:
        scrivi_gpx(punti, out / "percorso.gpx", args.cartella.name)
        print(f"\nGPX: {len(punti)} punti a 1 Hz -> {out / 'percorso.gpx'}")
        if buchi:
            print(f"Attenzione: {buchi} buchi GPS piu' lunghi di {MAX_BUCO_GPX_S} s non riempiti: "
                  f"il simulatore assume campionamento uniforme a 1 Hz")
    else:
        print("\nGPX: nessun punto con fix, file non scritto")

    if not args.no_grafico:
        if grafico_rssi(segmenti, out / "rssi_coppie.png"):
            print(f"Grafico: {out / 'rssi_coppie.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

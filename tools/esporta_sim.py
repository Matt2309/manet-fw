#!/usr/bin/env python3
"""Esporta da manet-sim una corsa simulata per il test di confronto del firmware.

Si lancia con l'ambiente del simulatore, che non viene modificato (ne' la sua cache):

    PYTHONDONTWRITEBYTECODE=1 ~/manet-sim/venv/bin/python tools/esporta_sim.py sep \\
        --out test/test_sim_compare/sim_sep.csv

Scenari:
  sep   un compagno si stacca a meta' corsa (pre-allarme e nodo perso)
  deaf  nessuno si stacca, ma le raffiche lunghe di perdite rendono "sorde" alcune schede

Il file contiene i beacon (tempo in ms, mittente, RSSI per ogni ricevente) e gli allarmi del
rilevatore Python (soglia di pre-allarme e parametri come in include/config.h). Le schede sono
numerate da 1. I tempi dei beacon sono arrotondati al ms e resi tutti diversi (+1 ms sui pareggi):
il simulatore tratta i pareggi fra tabella e ricezione in modo asimmetrico.

    B,t_ms,mittente,rssi_1,...,rssi_N       rssi vuoto = non ricevuto
    A,tipo,osservatore,compagno,inizio_ms,fine_ms   tipo = pre | perso | sorda; fine -1 = ancora attivo
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Parametri del firmware (include/config.h) che il simulatore ha come configurazione.
SIGMA_A = 0.25
R = 10.84
PRE_ALARM_DBM = -93.0
LOST_RULE = "not_deaf_episode"

DURATION = 300.0
N_NODES = 5

SCENARI = {
    "sep": {
        "separation.enabled": True,
        "separation.node_id": 3,
        "separation.start_time": 100.0,
    },
    "deaf": {
        "separation.enabled": False,
        "packets.reception.background_model": "bursty",
        "packets.reception.bursty.mean_loss": 0.15,
        "packets.reception.bursty.mean_burst": 15.0,
        "packets.reception.bursty.burst_scope": "receiver",
    },
}


def tempi_unici_ms(beacon_times):
    """Tempi dei beacon in ms interi, tutti diversi fra loro (anche fra schede diverse)."""
    import numpy as np

    ms = np.full(beacon_times.shape, -1, dtype=np.int64)
    valid = ~np.isnan(beacon_times)
    flat = [(int(round(beacon_times[n, k] * 1000)), n, k) for n, k in zip(*np.nonzero(valid))]
    flat.sort()
    prev = -1
    for t, n, k in flat:
        if t <= prev:
            t = prev + 1
        ms[n, k] = t
        prev = t
    return ms


def intervalli(stato, t_ms):
    """Da un array (T, M, I) di stati a una lista (osservatore, compagno, inizio, fine)."""
    import numpy as np

    out = []
    n_steps, n_obs, n_tgt = stato.shape
    for m in range(n_obs):
        for i in range(n_tgt):
            if m == i:
                continue
            s = stato[:, m, i].astype(bool)
            prev = False
            start = -1
            for k in range(n_steps):
                if s[k] and not prev:
                    start = int(t_ms[k])
                elif prev and not s[k]:
                    out.append((m + 1, i + 1, start, int(t_ms[k])))
                prev = bool(s[k])
            if prev:
                out.append((m + 1, i + 1, start, -1))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario", choices=sorted(SCENARI))
    ap.add_argument("--sim", default=str(Path.home() / "manet-sim"), help="cartella di manet-sim")
    ap.add_argument("--out", required=True, help="file di uscita")
    ap.add_argument("--duration", type=float, default=DURATION)
    args = ap.parse_args()

    sys.dont_write_bytecode = True
    sys.path.insert(0, args.sim)
    import numpy as np
    from src.common.config import apply_overrides, load_config
    from src.detector import build_inputs, lost_alarm, pre_alarm
    from src.simulator import simulate_run

    cfg = load_config(Path(args.sim) / "config" / "default.yaml")
    cfg = apply_overrides(
        cfg,
        {
            "simulation.duration": args.duration,
            "group.n_nodes": N_NODES,
            "kalman.sigma_a": SIGMA_A,
            "detector.level_threshold": PRE_ALARM_DBM,
            "detector.lost_rule": LOST_RULE,
            **SCENARI[args.scenario],
        },
    )

    # simulate_run non usa la cache su disco del simulatore.
    run = simulate_run(cfg)
    ms = tempi_unici_ms(run["beacon_times"])
    run["beacon_times"] = np.where(ms >= 0, ms / 1000.0, np.nan)

    inputs, _ = build_inputs(run, cfg, SIGMA_A, R)
    t = run["t"]
    t_ms = np.rint(t * 1000).astype(np.int64)
    dcfg = cfg["detector"]
    pre = pre_alarm(inputs.level["fused"], inputs.slope["fused"], inputs.rx_ok, t, dcfg, "level", valid=inputs.valid)
    lost = lost_alarm(inputs.silence, inputs.rx_ok, t, dcfg, inputs.valid, **inputs.lost_kwargs())
    deaf = (inputs.silence > dcfg["lost_silence"]) & inputs.deaf_episode & inputs.valid

    received, rssi = run["received"], run["rssi"]
    n_nodes = received.shape[0]
    beacons = []
    for n in range(n_nodes):
        for k in range(received.shape[1]):
            if ms[n, k] < 0:
                continue
            fields = ["" if not received[n, k, j] else str(int(rssi[n, k, j])) for j in range(n_nodes)]
            beacons.append((int(ms[n, k]), n + 1, fields))
    beacons.sort()

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w") as f:
        f.write(f"# esportato da manet-sim con tools/esporta_sim.py: scenario={args.scenario} "
                f"nodi={n_nodes} durata_s={args.duration:g} soglia_pre_dbm={PRE_ALARM_DBM:g} "
                f"sigma_a={SIGMA_A:g} R={R:g} lost_rule={LOST_RULE}\n")
        f.write(f"N,{n_nodes},{int(t_ms[-1])}\n")
        for tb, src, fields in beacons:
            f.write(f"B,{tb},{src}," + ",".join(fields) + "\n")
        for tipo, stato in (("pre", pre), ("perso", lost), ("sorda", deaf)):
            for m, i, a, b in intervalli(stato, t_ms):
                f.write(f"A,{tipo},{m},{i},{a},{b}\n")
    n_a = {tipo: len(intervalli(stato, t_ms)) for tipo, stato in (("pre", pre), ("perso", lost), ("sorda", deaf))}
    print(f"{out}: {len(beacons)} beacon, allarmi {n_a}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

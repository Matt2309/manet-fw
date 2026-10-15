"""Test di scarica.py e riepilogo.py su dati sintetici."""
import sys
import zlib
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).parent))
import riepilogo as rp  # noqa: E402
import scarica as sc  # noqa: E402

T0 = datetime(2026, 10, 7, 12, 0, 0, tzinfo=timezone.utc).timestamp()


def iso(u: float) -> str:
    d = datetime.fromtimestamp(u, tz=timezone.utc)
    return d.strftime("%Y-%m-%dT%H:%M:%S.") + f"{int(round((u % 1) * 100)):02d}Z"


def scrivi_run(cartella: Path, sid: int, durata=120, deriva_ppm=0.0, offset_ms=0, persi_da: dict | None = None,
               fix=True, evt_a=()):
    """Genera il run della scheda sid. persi_da: {src: funzione seq -> bool (True = perso)}.

    Le schede trasmettono a 2 Hz agli istanti veri k*0.5 + sid*0.1; l'orologio della
    scheda vale t_ms = vero*(1+deriva)*1000 + offset.
    """
    persi_da = persi_da or {}

    def ms(t_vero):
        return int(round(t_vero * (1 + deriva_ppm * 1e-6) * 1000 + offset_ms))

    righe = [f"# manet-fw 1.0.0 id={sid} mac=AA:BB:CC:DD:EE:0{sid} canale=1 potenza_dbm=10.00 "
             f"periodo_ms=500 jitter_ms=50 rate=11b_1M"]
    ev = []
    for k in range(int(durata / 0.5)):
        t = k * 0.5 + sid * 0.1
        ev.append((ms(t), f"TX,{ms(t)},{k}"))
        for src in (1, 2, 3):
            if src == sid:
                continue
            ts = k * 0.5 + src * 0.1 + 0.001
            if persi_da.get(src, lambda s: False)(k):
                continue
            rssi = -60 - src - sid
            ev.append((ms(ts), f"RX,{ms(ts)},{src},{k},{rssi},"))
    for s in range(durata):
        t = s + 0.37
        if fix:
            lat = 44.0 + 1e-5 * s
            ev.append((ms(t), f"GPS,{ms(t)},{iso(T0 + s)},{lat:.7f},11.0000000,50.0,10.00,90.0,0.90,9,3"))
        else:
            ev.append((ms(t), f"GPS,{ms(t)},,,,,,,,0,0"))
        if s % 30 == 0:
            ev.append((ms(t), f"BAT,{ms(t)},{4000 - s}"))
    for n, te in enumerate(evt_a, 1):
        ev.append((ms(te), f"EVT,{ms(te)},{n}"))
    ev.sort(key=lambda e: e[0])
    righe += [r for _, r in ev]
    cartella.mkdir(parents=True, exist_ok=True)
    (cartella / "run_001.csv").write_text("\n".join(righe) + "\n")


@pytest.fixture
def tre_schede(tmp_path):
    scrivi_run(tmp_path / "scheda_1", 1, deriva_ppm=20, offset_ms=5000, evt_a=(30.0,))
    scrivi_run(tmp_path / "scheda_2", 2, deriva_ppm=-35, offset_ms=900000,
               persi_da={1: lambda s: s % 10 == 0})
    scrivi_run(tmp_path / "scheda_3", 3, deriva_ppm=5, offset_ms=100, fix=True)
    return tmp_path


def test_lettura_e_intestazione(tre_schede):
    segs = rp.leggi_cartella(tre_schede)
    assert sorted(s.scheda for s in segs) == [1, 2, 3]
    s1 = next(s for s in segs if s.scheda == 1)
    assert s1.intestazione["canale"] == "1"
    assert len(s1.tx) == 240
    assert len(s1.evt) == 1
    assert s1.gps[0].fix == 3 and s1.gps[0].sat == 9


def test_riga_troncata_scartata(tmp_path):
    scrivi_run(tmp_path, 1)
    p = tmp_path / "run_001.csv"
    p.write_text(p.read_text() + "RX,99999,2,5,-6")   # troncata e senza a capo
    s = rp.leggi_segmento(p)
    assert s.scartate == 1


def test_file_senza_intestazione_ignorato(tmp_path):
    (tmp_path / "run_001.csv").write_text("TX,1,0\n")
    assert rp.leggi_segmento(tmp_path / "run_001.csv") is None


def test_allineamento_recupera_deriva_e_offset(tre_schede):
    segs = rp.leggi_cartella(tre_schede)
    for s in segs:
        rp.allinea(s)
    deriva = {s.scheda: s.deriva_ppm for s in segs}
    # utc = vero; t_ms = vero*(1+d); quindi dUTC/dt_ms = 1/(1+d): deriva misurata = -d circa
    assert deriva[1] == pytest.approx(-20, abs=2)
    assert deriva[2] == pytest.approx(35, abs=2)
    for s in segs:
        assert s.residuo_max_ms < 5.0   # solo l'arrotondamento al ms
    # t_ms -> UTC: un istante vero noto
    s1 = next(s for s in segs if s.scheda == 1)
    t_ms = int(round(60.37 * (1 + 20e-6) * 1000 + 5000))
    # la riga GPS arriva 0,37 s dopo il secondo UTC che porta: la retta assorbe questa latenza
    assert s1.utc(t_ms) == pytest.approx(T0 + 60.0, abs=0.01)


def test_non_allineabile_senza_fix(tmp_path):
    scrivi_run(tmp_path / "a", 1, fix=False)
    s = rp.leggi_segmento(tmp_path / "a" / "run_001.csv")
    rp.allinea(s)
    assert s.retta is None
    assert s.utc(1000) is None


def test_percentuale_persi_per_coppia(tre_schede):
    segs = rp.leggi_cartella(tre_schede)
    for s in segs:
        rp.allinea(s)
    c = rp.aggrega_coppie(rp.tutte_le_coppie(segs))
    assert set(c) == {(i, j) for i in (1, 2, 3) for j in (1, 2, 3) if i != j}
    assert c[(1, 2)].persi_pct == pytest.approx(10.0, abs=1.5)   # un beacon su 10
    assert c[(1, 3)].persi_pct == pytest.approx(0.0, abs=1.5)
    assert c[(2, 1)].persi_pct == pytest.approx(0.0, abs=1.5)
    assert c[(3, 2)].persi_pct == pytest.approx(0.0, abs=1.5)
    # RSSI sintetico: -60 - src - dst
    assert np.median(c[(1, 2)].rssi) == -63
    assert np.median(c[(3, 1)].rssi) == -64
    assert c[(1, 2)].inviati > 200


def test_controllo_incrociato_utc(tre_schede):
    segs = rp.leggi_cartella(tre_schede)
    for s in segs:
        rp.allinea(s)
    c = rp.aggrega_coppie(rp.tutte_le_coppie(segs))
    # rx avviene 1 ms dopo tx nell'istante vero (e i tx hanno sid*0.1 di sfasamento gia' incluso)
    for k, v in c.items():
        assert len(v.scarti_ms) > 100
        assert abs(np.median(v.scarti_ms) - (1.0 + (k[0] - k[1]) * 0.0)) < 15.0


def test_senza_gps_si_usa_il_range_dei_seq(tmp_path):
    scrivi_run(tmp_path / "s1", 1, fix=False)
    scrivi_run(tmp_path / "s2", 2, fix=False, persi_da={1: lambda s: s % 10 == 5})
    segs = rp.leggi_cartella(tmp_path)
    for s in segs:
        rp.allinea(s)
    c = rp.aggrega_coppie(rp.tutte_le_coppie(segs))
    assert c[(1, 2)].persi_pct == pytest.approx(10.0, abs=1.0)
    assert len(c[(1, 2)].scarti_ms) == 0


def test_statistiche_scheda(tre_schede):
    segs = rp.leggi_cartella(tre_schede)
    st = rp.statistiche_scheda(1, segs)
    assert st.durata_s == pytest.approx(120, abs=1.5)
    assert st.fix_pct == 100.0
    assert st.bat_iniziale == 4000 and st.bat_finale == 3910
    assert st.eventi == 1


def test_gpx_media_e_interpolazione(tre_schede, tmp_path):
    segs = rp.leggi_cartella(tre_schede)
    for s in segs:
        rp.allinea(s)
    punti, buchi = rp.percorso_medio(segs)
    assert buchi == 0
    assert len(punti) == 120
    assert [p[0] for p in punti] == list(range(int(T0), int(T0) + 120))
    assert punti[10][1] == pytest.approx(44.0 + 1e-4, abs=1e-9)
    out = tmp_path / "p.gpx"
    rp.scrivi_gpx(punti, out, "prova")
    testo = out.read_text()
    assert 'xmlns="http://www.topografix.com/GPX/1/1"' in testo
    assert testo.count("<trkpt") == 120
    assert "<ele>50.0</ele>" in testo   # richiesto da manet-sim/src/mobility.py


def test_gpx_buchi_corto_interpolato_e_lungo_segnalato():
    def seg(secondi):
        s = rp.Segmento(Path("x"), 1, {"id": "1"})
        s.gps = [rp.RigaGps(i * 1000, T0 + i, 44.0 + i * 1e-5, 11.0, 10.0, 8, 3) for i in secondi]
        return s
    corto, b1 = rp.percorso_medio([seg(list(range(0, 5)) + list(range(8, 12)))])   # buco di 3 s
    assert b1 == 0 and len(corto) == 12
    lungo, b2 = rp.percorso_medio([seg(list(range(0, 5)) + list(range(20, 25)))])  # buco di 15 s
    assert b2 == 1 and len(lungo) == 10


def test_grafico_si_scrive(tre_schede, tmp_path):
    segs = rp.leggi_cartella(tre_schede)
    for s in segs:
        rp.allinea(s)
    out = tmp_path / "g.png"
    assert rp.grafico_rssi(segs, out)
    assert out.stat().st_size > 5000


# --- scarica.py -------------------------------------------------------------

class SerialeFinta:
    """Simula la scheda: risponde a info, ls, get come il firmware."""

    def __init__(self, file: dict[str, bytes], corrompi: bool = False):
        self.file = file
        self.corrompi = corrompi
        self.out = bytearray()

    def write(self, dati: bytes):
        cmd = dati.decode().strip().split()
        if cmd[0] == "info":
            self.out += b"# debug che va saltato\nINFO id=2 mac=AA:BB:CC:DD:EE:02 fw=1.0.0 file= state=saved\n"
        elif cmd[0] == "ls":
            for n, d in self.file.items():
                self.out += f"FILE {n} {len(d)} {zlib.crc32(d):08X}\n".encode()
            self.out += b"END\n"
        elif cmd[0] == "get":
            n = cmd[1]
            if n not in self.file:
                self.out += b"ERR file inesistente\n"
                return
            d = self.file[n]
            self.out += f"BEGIN {n} {len(d)} {zlib.crc32(d):08X}\n".encode()
            self.out += (b"X" + d[1:]) if self.corrompi else d
            self.out += b"END\n"

    def flush(self):
        pass

    def reset_input_buffer(self):
        pass

    def readline(self):
        i = self.out.find(b"\n")
        if i < 0:
            return b""
        riga = bytes(self.out[:i + 1])
        del self.out[:i + 1]
        return riga

    def read(self, n):
        dati = bytes(self.out[:n])
        del self.out[:n]
        return dati


def test_scarica_scrive_i_file_e_non_ricopia(tmp_path, monkeypatch):
    monkeypatch.setattr(sc.time, "sleep", lambda s: None)
    contenuto = {"run_001.csv": b"TX,1,0\n" * 3000, "run_002.csv": b"\x00\x0a\xff" * 700}
    errori = sc.scarica_scheda(SerialeFinta(contenuto), tmp_path)
    assert errori == 0
    cartella = tmp_path / "scheda_2"
    for n, d in contenuto.items():
        assert (cartella / n).read_bytes() == d
    # seconda volta: identici, nessun file nuovo
    assert sc.scarica_scheda(SerialeFinta(contenuto), tmp_path) == 0
    assert sorted(p.name for p in cartella.iterdir()) == ["run_001.csv", "run_002.csv"]


def test_scarica_file_diverso_con_stesso_nome_va_in_suffisso(tmp_path, monkeypatch):
    monkeypatch.setattr(sc.time, "sleep", lambda s: None)
    sc.scarica_scheda(SerialeFinta({"run_001.csv": b"a\n"}), tmp_path)
    sc.scarica_scheda(SerialeFinta({"run_001.csv": b"b\n"}), tmp_path)
    cartella = tmp_path / "scheda_2"
    assert (cartella / "run_001.csv").read_bytes() == b"a\n"
    assert (cartella / "run_001_2.csv").read_bytes() == b"b\n"


def test_scarica_rileva_crc_errato(tmp_path, monkeypatch):
    monkeypatch.setattr(sc.time, "sleep", lambda s: None)
    errori = sc.scarica_scheda(SerialeFinta({"run_001.csv": b"abcdef\n"}, corrompi=True), tmp_path)
    assert errori == 1
    assert not (tmp_path / "scheda_2" / "run_001.csv").exists()


def test_crc32_come_il_firmware():
    assert sc.crc32(b"123456789") == 0xCBF43926

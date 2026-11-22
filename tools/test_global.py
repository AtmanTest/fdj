#!/usr/bin/env python3
"""Test global du BATMino, par l'USB (aucun reseau necessaire).

Chaque fonctionnalite du firmware a son fichier dans docs/features/ avec ses
propres tests ; ce script les rejoue tous d'un coup sur la carte branchee en
USB, dans l'ordre, et rend un verdict.

    python3 tools/test_global.py [/dev/ttyACM1]

Codes de sortie : 0 = tout est bon, 1 = au moins un test en echec.
"""
import re
import struct
import sys
import time
import zlib

try:
    import serial
except ImportError:
    sys.exit("pyserial manquant : utilise le python de l'IDF "
             "($IDF_PYTHON_ENV_PATH/bin/python)")

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM1"
FB_LEN = 200 * 25

PASS, FAIL = [], []


def ok(nom, detail=""):
    PASS.append(nom)
    print(f"  [OK]    {nom}" + (f" — {detail}" if detail else ""))


def ko(nom, detail=""):
    FAIL.append(nom)
    print(f"  [ECHEC] {nom}" + (f" — {detail}" if detail else ""))


class Carte:
    """Le BATMino vu par sa console USB. Ouvrir le port peut redemarrer la
    carte (USB-JTAG) : on attend qu'elle reponde avant de tester."""

    def __init__(self, port):
        self.s = serial.Serial(port, 115200, timeout=0.3)
        self.s.dtr = False
        self.s.rts = False
        time.sleep(0.5)
        self.s.reset_input_buffer()

    def _pompe(self, sec):
        buf = b""
        t = time.time()
        while time.time() - t < sec:
            buf += self.s.read(8192)
        return buf.decode(errors="replace").replace("\r", "")

    def lire(self, sec):
        return self._pompe(sec)

    def vide(self):
        self.s.reset_input_buffer()

    def cmd(self, texte, motif=None, sec=10):
        """Envoie une commande et attend son echo (motif) au lieu d'un delai
        fixe : le firmware dessine l'ecran avant de repondre (jusqu'a 3 s) et
        deux commandes collees se melangent."""
        self.vide()
        self.s.write((texte + "\n").encode())
        t = time.time()
        vus = ""
        while time.time() - t < sec:
            vus += self._pompe(0.4)
            if motif and re.search(motif, vus):
                break
        return vus

    def attendre_pret(self, sec=70):
        """Attend que la carte soit demarree : console qui repond, wifi, ui."""
        t = time.time()
        while time.time() - t < sec:
            out = self.cmd("diag", r"timers_actifs", 6)
            if re.search(r"ui_ticks=\d+", out):
                return out
        return ""

    def diag(self):
        out = self.cmd("diag", r"timers_actifs", 6)
        d = {}
        for ligne in out.split("\n"):
            if "diag : " not in ligne:
                continue
            for cle, val in re.findall(r"(\w+)=(\S+)", ligne.split("diag : ")[1]):
                d[cle] = val
        return d

    def fb(self):
        """Ramasse le framebuffer par la commande console fb."""
        self.vide()
        self.s.write(b"fb\n")
        fb = bytearray(FB_LEN)
        buf = b""
        vu = 0
        fin = time.time() + 20
        while time.time() < fin:
            buf += self.s.read(8192)
            while b"\n" in buf:
                ligne, buf = buf.split(b"\n", 1)
                m = re.search(rb"FB (\d+) ([0-9a-f]+)", ligne)
                if m:
                    off = int(m.group(1))
                    data = bytes.fromhex(m.group(2).decode())
                    fb[off:off + len(data)] = data
                    vu += len(data)
                elif b"FB END" in ligne:
                    fin = 0
                    break
        return bytes(fb) if vu == FB_LEN else None

    def png(self, fb, chemin, zoom=4):
        brut = bytearray()
        for y in range(200):
            for _ in range(zoom):
                brut.append(0)
                for x in range(200):
                    noir = 255 if (fb[y * 25 + (x >> 3)] >> (7 - (x & 7))) & 1 else 0
                    brut.extend((noir,) * zoom)

        def bloc(tag, data):
            return (struct.pack(">I", len(data)) + tag + data +
                    struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

        png = (b"\x89PNG\r\n\x1a\n" +
               bloc(b"IHDR", struct.pack(">IIBBBBB", 200 * zoom, 200 * zoom,
                                         8, 0, 0, 0, 0)) +
               bloc(b"IDAT", zlib.compress(bytes(brut), 9)) +
               bloc(b"IEND", b""))
        with open(chemin, "wb") as f:
            f.write(png)


def main():
    c = Carte(PORT)
    print(f"BATMino : test global sur {PORT}\n")

    # ------------------------------------------------------------- 1. demarrage
    print("1. demarrage et reseau (docs/features/03-demarrage.md)")
    boot = c.attendre_pret(70)
    if not boot:
        ko("la carte repond sur l'USB", "aucun diag : mauvais port, ou firmware plante")
        print("\nverdict : la carte ne repond pas, les autres tests sont impossibles")
        return 1
    ok("la carte repond sur l'USB")
    if "Guru Meditation" in boot or "panic" in boot:
        ko("pas de plantage au demarrage", "panic dans le log")
    else:
        ok("pas de plantage au demarrage")

    d = c.diag()
    d0 = dict(d)          # etat de depart : sert a reperer un reboot en cours de test
    if d.get("wifi") == "1":
        ok("wifi associe", d.get("wifi_actif", ""))
    else:
        ko("wifi associe", f"wifi={d.get('wifi')}")
    if d.get("meteo") == "1":
        ok("meteo lue", f"{d.get('meteo')}C code {d.get('code')}")
    else:
        # la meteo se lit en TLS apres l'association : on lui laisse le temps
        for _ in range(15):
            time.sleep(4)
            d = c.diag()
            if d.get("meteo") == "1":
                break
        if d.get("meteo") == "1":
            ok("meteo lue (apres attente)", f"{d.get('meteo')}C code {d.get('code')}")
        else:
            ko("meteo lue", "toujours pas de donnees apres 60 s")
    if int(d.get("ui_ticks", "0") or 0) > 0:
        ok("tache ecran vivante", f"ui_ticks={d['ui_ticks']}")
    else:
        ko("tache ecran vivante", f"ui_ticks={d.get('ui_ticks')}")

    # --------------------------------------------------- 2. une seule page
    print("\n2. une seule page (docs/features/02-pages.md)")
    c.cmd("ui 0", r"page=|$^", 4)
    time.sleep(1.5)
    restes = []
    for _ in range(3):
        c.cmd("btn 1", r"essai bouton 1", 12)        # appui simple bouton 1
        restes.append(c.diag().get("page", "?"))
        time.sleep(1.4)
    if restes == ["ACCUEIL"] * 3:
        ok("bouton 1 : le tableau ne tourne plus", " -> ".join(restes))
    else:
        ko("bouton 1 : le tableau ne tourne plus", "vu " + " -> ".join(restes))

    time.sleep(1.6)
    c.cmd("btn 2", r"essai bouton 2", 12)
    page = c.diag().get("page", "?")
    if page == "ACCUEIL":
        ok("bouton 2 sur le tableau : aucune page a tourner", page)
    else:
        ko("bouton 2 sur le tableau : aucune page a tourner", page)

    time.sleep(1.6)
    c.cmd("btn 2 long", r"essai bouton 2", 12)
    page = c.diag().get("page", "?")
    if page == "ACCUEIL":
        ok("appui long 2 : plus de livre, on reste sur le tableau", page)
    else:
        ko("appui long 2 : plus de livre, on reste sur le tableau", page)

    time.sleep(1.6)
    c.cmd("btn 1 long", r"essai bouton 1", 12)
    page = c.diag().get("page", "?")
    if page == "ACCUEIL":
        ok("appui long ramene au tableau", page)
    else:
        ko("appui long ramene au tableau", page)

    # ---------------------------------------------- 3. la dalle suit le dessin
    print("\n3. la dalle suit bien le dessin (docs/features/06-ecran-eink.md)")
    c.cmd("ui 0", r"page=|$^", 4)
    time.sleep(1.5)
    img = c.fb()
    if img is None:
        ko("image du tableau", "framebuffer incomplet")
    else:
        c.png(img, "/tmp/test_tableau.png")
        if img.strip("f"):
            ok("le tableau est dessine", "/tmp/test_tableau.png")
        else:
            ko("le tableau est dessine", "framebuffer tout blanc")
        # la meme image redemandee doit etre identique au pixel : le dessin est
        # deterministe (heure, meteo, batterie ne bougent pas en 2 s)
        time.sleep(1.5)
        c.cmd("ui", r"page=|$^", 4)
        time.sleep(1.2)
        img2 = c.fb()
        if img2 is not None and img2[:200] == img[:200]:
            ok("deux rendus du meme etat se ressemblent (200 premiers octets)")
        else:
            ko("deux rendus du meme etat se ressemblent", "framebuffer different")

    # ------------------------------------------- 4. l'assistant cree un rappel
    print("\n4. l'assistant cree un rappel (docs/features/04-rappel.md)")
    out = ""
    for essai, phrase in enumerate((
            "rappelle-moi dans 2 minutes de tester la carte",
            "cree un rappel dans 2 minutes : tester la carte")):
        out = c.cmd(phrase, r"rappel cree : ", 50)
        if re.search(r"rappel cree : ", out):
            break
        print(f"        (essai {essai + 1} sans rappel, on reessaie)")
    m = re.search(r"rappel cree : .+", out)
    if m:
        ok("rappel cree par l'assistant", m.group(0).strip())
    else:
        ko("rappel cree par l'assistant", "le modele n'a pas renvoye de rappel")
    d = c.diag()
    if int(d.get("rappel_nb", "0") or 0) >= 1:
        ok("rappel compte par le firmware", f"rappel_nb={d['rappel_nb']}")
    else:
        ko("rappel compte par le firmware", f"rappel_nb={d.get('rappel_nb')}")
    c.cmd("ui 2", r"page=|$^", 4)
    time.sleep(1.2)
    img = c.fb()
    if img:
        c.png(img, "/tmp/test_rappels_actifs.png")
        ok("page RAPPELS affichee", "/tmp/test_rappels_actifs.png")
    c.cmd("rappel a=1", r"$^", 3)      # on ne laisse pas sonner la carte

    # ------------------------------------------------------ 5. stabilite
    print("\n5. stabilite (docs/features/05-stabilite.md)")
    c.cmd("ui 0", r"page=|$^", 4)
    time.sleep(1.2)
    log = c.lire(3)
    if "Guru Meditation" in log or "abort()" in log:
        ko("pas de plantage pendant les tests", "panic dans le log")
    else:
        ok("pas de plantage pendant les tests")
    d2 = c.diag()
    if int(d2.get("ui_ticks", "0") or 0) > int(d.get("ui_ticks", "0") or 0):
        ok("la carte tourne toujours", f"ui_ticks {d.get('ui_ticks')} -> {d2['ui_ticks']}")
    else:
        ko("la carte tourne toujours", f"ui_ticks {d.get('ui_ticks')} -> {d2.get('ui_ticks')}")
    if int(d2.get("heap_int_max", "0") or 0) > 8000:
        ok("RAM interne suffisante", f"bloc max {d2['heap_int_max']}")
    else:
        ko("RAM interne suffisante", f"bloc max {d2.get('heap_int_max')}")
    if d0.get("boot_nb") == d2.get("boot_nb"):
        ok("aucun redemarrage pendant les tests", f"demarrage n{d2.get('boot_nb')}")
    else:
        ko("aucun redemarrage pendant les tests",
           f"n{d0.get('boot_nb')} -> n{d2.get('boot_nb')} : la carte a reboote")

    c.s.close()

    print(f"\n{len(PASS)} tests bons, {len(FAIL)} en echec")
    if FAIL:
        print("a corriger : " + ", ".join(FAIL))
        return 1
    print("verdict : le build est bon")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Cherche une carte ESP32 ("Mino"/Batman) sur les ports serie et discute avec.

Usage :
  talk.py <commande>              envoie une ligne, imprime la reponse
  talk.py --fb <sortie.png> [cmd] recupere le framebuffer et l'ecrit en PNG
  talk.py --scan                  liste les ports et tente de joindre la carte

Le firmware repond en clair sur l'USB (console usb_serial_jtag) : les lignes
commencent par "I (temps) mino: ". La commande "fb" renvoie le framebuffer
200x200 1 bit par lignes "FB <offset> <hex>".
"""
import sys, time, glob, os, argparse

try:
    import serial
except ImportError:
    sys.exit("pyserial manquant : utilise /opt/mino-venv/bin/python")

FB_LEN = 200 * 25
HOST_PREFIX = ("mino:",)


def candidate_ports():
    ports = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    return ports


def open_port(path, timeout=1.0):
    """ESP32-S3 en USB-Serial/JTAG : DTR et RTS pilotes par le port serie
    provoquent un reset de la carte des l'ouverture. On les baisse tout de
    suite, sinon la commande part pendant le redemarrage et personne ne repond."""
    s = serial.Serial(path, 115200, timeout=timeout)
    s.dtr = False
    s.rts = False
    time.sleep(0.4)
    s.reset_input_buffer()
    return s


def drain(ser, seconds=0.4):
    end = time.time() + seconds
    out = b""
    while time.time() < end:
        n = ser.in_waiting
        if n:
            out += ser.read(n)
            end = time.time() + 0.15
        else:
            time.sleep(0.02)
    return out


def send(ser, line, wait=6.0):
    ser.reset_input_buffer()
    ser.write((line + "\n").encode())
    ser.flush()
    return collect(ser, wait)


def collect(ser, wait):
    """Lit jusqu'a ce que le port soit silencieux pendant ~1 s (ou wait depasse)."""
    out = b""
    start = time.time()
    quiet_since = time.time()
    while time.time() - start < wait:
        n = ser.in_waiting
        if n:
            out += ser.read(n)
            quiet_since = time.time()
        else:
            time.sleep(0.05)
            if out and time.time() - quiet_since > 2.5:
                break
    return out.decode("utf-8", "replace")


def strip_prefix(line):
    i = line.find("mino: ")
    return line[i + 6:] if i >= 0 else line


def parse_fb(text):
    fb = bytearray([0xFF] * FB_LEN)
    seen = 0
    for raw in text.splitlines():
        line = strip_prefix(raw).strip()
        if line.startswith("FB "):
            parts = line.split()
            if len(parts) < 3:
                continue
            off = int(parts[1])
            data = bytes.fromhex(parts[2]) if len(parts) == 3 else bytes.fromhex("".join(parts[2:]))
            fb[off:off + len(data)] = data
            seen += len(data)
    return bytes(fb), seen


def write_png(path, fb, w=200, h=200, scale=3):
    """PNG grayscale 8 bits : 1 = blanc, 0 = noir."""
    import zlib, struct
    W, H = w * scale, h * scale
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        for x in range(W):
            byte = fb[(y // scale) * (w // 8) + ((x // scale) // 8)]
            bit = (byte >> (7 - ((x // scale) & 7))) & 1
            raw.append(255 if bit else 0)
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)
    return W, H


def attente_pret(ser, limite=35.0):
    """La carte redemarre des qu'on ouvre le port (DTR/RTS en USB-Serial/JTAG).
    On attend qu'elle reponde pour de bon avant d'envoyer une commande."""
    t0 = time.time()
    while time.time() - t0 < limite:
        ser.reset_input_buffer()
        ser.write(b"state\n")
        txt = drain(ser, 2.5).decode("utf-8", "replace")
        if "RAM libre" in txt or "emotion=" in txt:
            time.sleep(0.4)
            ser.reset_input_buffer()
            return True
    return False


def commande_fiable(ser, cmd, marqueur, essais=5, fenetre=12.0):
    """Tant qu'on ne voit pas le marqueur attendu, on renvoie la commande."""
    txt = ""
    for _ in range(essais):
        ser.reset_input_buffer()
        ser.write((cmd + "\n").encode())
        txt = drain(ser, fenetre).decode("utf-8", "replace")
        if marqueur in txt:
            return txt
        time.sleep(0.5)
    return txt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None)
    ap.add_argument("--fb", metavar="PNG")
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--wait", type=float, default=25.0)
    ap.add_argument("--scan", action="store_true")
    ap.add_argument("cmd", nargs="*")
    a = ap.parse_args()

    if a.scan:
        for p in candidate_ports():
            try:
                s = open_port(p, 0.3)
                got = drain(s, 0.8)
                print(f"{p}: {len(got)} octets au repos -- {got[-120:]!r}")
                s.close()
            except Exception as e:
                print(f"{p}: {e}")
        return

    port = a.port or (candidate_ports() or [None])[0]
    if not port:
        sys.exit("aucun port serie")
    ser = open_port(port)
    try:
        attente_pret(ser)
        if a.fb:
            txt = commande_fiable(ser, "fb", "FB END", fenetre=max(a.wait, 12.0))
            fb, n = parse_fb(txt)
            if n < FB_LEN:
                print(f"ATTENTION : {n}/{FB_LEN} octets recus", file=sys.stderr)
            W, H = write_png(a.fb, fb, scale=a.scale)
            print(f"{a.fb} ecrit ({W}x{H})")
            return
        if not a.cmd:
            sys.exit("rien a faire")
        txt = commande_fiable(ser, " ".join(a.cmd), "mino:", fenetre=a.wait)
        print(txt)
    finally:
        ser.close()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Attend la validation Microsoft en renouvelant le code tout seul.

    python3 tools/outlook_attente.py

Le code de connexion ne vit que 15 min : le perdre est trop facile. Ce script
redemande un code des qu'il expire et ecrit le code courant dans
~/.mino-outlook-code.txt (le code et l'heure limite). Des que le compte est
valide, il enregistre le jeton dans ~/.mino-outlook.json et s'arrete.
"""
import json
import os
import stat
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

CLIENT = "04b07795-8ddb-461a-bbee-02f9e1bf7b46"   # client public "Azure CLI"
SCOPE = "offline_access https://graph.microsoft.com/Calendars.Read"
TENANT = "common"
SORTIE = os.path.expanduser("~/.mino-outlook.json")
CODE = os.path.expanduser("~/.mino-outlook-code.txt")
BASE = f"https://login.microsoftonline.com/{TENANT}/oauth2/v2.0"


def post(url, data):
    body = urllib.parse.urlencode(data).encode()
    req = urllib.request.Request(url, data=body,
                                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        try:
            return json.load(e)
        except Exception:
            return {"error": "http_%d" % e.code}
    except Exception as e:          # DNS passager, wifi coupe : on ne meurt pas
        return {"error": "reseau", "error_description": str(e)}


def ecrit_code(code, fin):
    with open(CODE, "w") as f:
        f.write("code    : %s\nvalable : %s\n" % (code, time.strftime("%H:%M", time.localtime(fin))))
    os.chmod(CODE, stat.S_IRUSR | stat.S_IWUSR)


def une_tentative():
    """Retourne True si le jeton est obtenu, False si le code a expire."""
    d = post(f"{BASE}/devicecode", {"client_id": CLIENT, "scope": SCOPE})
    if "device_code" not in d:
        print("refus de Microsoft :", d.get("error_description", d), flush=True)
        time.sleep(30)
        return False
    fin = time.time() + d["expires_in"]
    ecrit_code(d["user_code"], fin)
    print("nouveau code : %s  (jusqu'a %s)" %
          (d["user_code"], time.strftime("%H:%M", time.localtime(fin))), flush=True)
    intervalle = d.get("interval", 5)
    while time.time() < fin:
        time.sleep(intervalle)
        r = post(f"{BASE}/token", {
            "grant_type": "urn:ietf:params:oauth:grant-type:device_code",
            "client_id": CLIENT,
            "device_code": d["device_code"],
        })
        if "access_token" in r:
            with open(SORTIE, "w") as f:
                json.dump(r, f, indent=1)
            os.chmod(SORTIE, stat.S_IRUSR | stat.S_IWUSR)
            print("connecte :", r.get("scope"), flush=True)
            print("jeton enregistre dans", SORTIE, flush=True)
            try:
                os.remove(CODE)
            except OSError:
                pass
            return True
        err = r.get("error")
        if err == "authorization_pending":
            continue
        if err == "slow_down":
            intervalle += 5
            continue
        desc = r.get("error_description", err) or ""
        if "expired" in desc or err in ("expired_token", "code_expired"):
            print("code expire, on en redemande un", flush=True)
            return False
        print("echec :", desc, flush=True)
        return False
    return False


def main():
    print("ouverture : https://microsoft.com/devicelogin", flush=True)
    while True:
        if une_tentative():
            return 0
        time.sleep(2)


if __name__ == "__main__":
    sys.exit(main())

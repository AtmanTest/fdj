#!/usr/bin/env python3
"""Obtient UNE FOIS un jeton de rafraichissement Microsoft Graph, pour que le
robot puisse lire le calendrier tout seul ensuite.

    python3 tools/outlook_token.py

Le script affiche un code a recopier sur https://microsoft.com/devicelogin.
Le jeton obtenu est ecrit dans ~/.mino-outlook.json (chmod 600) : ne pas le
committer, il vaut un acces en lecture au calendrier.

Le robot n'a besoin que de la valeur "refresh_token".
"""
import json
import os
import stat
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# client public "Azure CLI" : accepte le device code et les scopes Graph,
# donc pas d'inscription d'application a faire. Si l'organisation le bloque,
# il faudra creer une app dans Entra ID et mettre son identifiant ici.
CLIENT = "04b07795-8ddb-461a-bbee-02f9e1bf7b46"
SCOPE = "offline_access https://graph.microsoft.com/Calendars.Read"
TENANT = "common"
SORTIE = os.path.expanduser("~/.mino-outlook.json")


def post(url, data):
    body = urllib.parse.urlencode(data).encode()
    req = urllib.request.Request(url, data=body,
                                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        return json.load(e)


def main():
    base = f"https://login.microsoftonline.com/{TENANT}/oauth2/v2.0"
    d = post(f"{base}/devicecode", {"client_id": CLIENT, "scope": SCOPE})
    if "device_code" not in d:
        print("refus de Microsoft :", d.get("error_description", d))
        return 1
    print()
    print("  1. ouvre  https://microsoft.com/devicelogin")
    print("  2. tape   " + d["user_code"])
    print()
    print("  (valable %d minutes, j'attends...)" % (d["expires_in"] // 60))
    fin = time.time() + d["expires_in"]
    intervalle = d.get("interval", 5)
    while time.time() < fin:
        time.sleep(intervalle)
        r = post(f"{base}/token", {
            "grant_type": "urn:ietf:params:oauth:grant-type:device_code",
            "client_id": CLIENT,
            "device_code": d["device_code"],
        })
        if "access_token" in r:
            with open(SORTIE, "w") as f:
                json.dump(r, f, indent=1)
            os.chmod(SORTIE, stat.S_IRUSR | stat.S_IWUSR)
            print("connecte :", r.get("scope"))
            print("jeton enregistre dans", SORTIE)
            return 0
        err = r.get("error")
        if err == "authorization_pending":
            continue
        if err == "slow_down":
            intervalle += 5
            continue
        print("echec :", r.get("error_description", err))
        return 1
    print("delai depasse, relance le script")
    return 1


if __name__ == "__main__":
    sys.exit(main())

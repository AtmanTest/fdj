#!/usr/bin/env python3
"""Verifie la sonnerie du rendez-vous sur le robot.

1. rendez-vous dans 25 min  -> la sonnerie doit partir (<= 30 min)
2. appui sur le bouton 1    -> elle doit se taire
3. rendez-vous dans 2 h     -> elle ne doit pas partir (> 30 min)
4. on remet le vrai calendrier

    python3 tools/test_alarme.py --restore "<url ics>"
"""
import argparse
import datetime as dt
import json
import sys
import time
import urllib.parse
import urllib.request

IP = "192.168.8.208"


def http(url, data=None, timeout=25):
    req = urllib.request.Request(url, data=data,
                                 method="POST" if data is not None else "GET")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def etat():
    return json.loads(http("http://%s/state" % IP))


def ics(dans_min, nom):
    t = (dt.datetime.now() + dt.timedelta(minutes=dans_min)).strftime("%Y%m%dT%H%M%S")
    l = ["BEGIN:VCALENDAR", "VERSION:2.0",
         "BEGIN:VEVENT", "UID:alarme@mino.test", "DTSTAMP:" + t,
         "DTSTART:" + t, "SUMMARY:Test alarme",
         "DESCRIPTION:Organisateur\\nJeanne Lesbordes<mailto:jl@exemple.fr>",
         "END:VEVENT", "END:VCALENDAR"]
    return "\r\n".join(l) + "\r\n"


def pose(nom, contenu):
    http("http://%s/fichier?p=%s" % (IP, urllib.parse.quote("/" + nom)),
         data=contenu.encode())


def lire(nom):
    url = "http://%s/fichier?p=%s" % (IP, urllib.parse.quote("/" + nom))
    http("http://%s/ical?u=%s" % (IP, urllib.parse.quote(url, safe="")))


def attendre_alarme(veut, limite):
    fin = time.time() + limite
    while time.time() < fin:
        s = etat()
        if bool(s.get("alarme")) == veut:
            return s, True
        time.sleep(3)
    return etat(), False


def main():
    global IP
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", default=IP)
    ap.add_argument("--restore", default="")
    a = ap.parse_args()
    IP = a.ip
    echecs = 0

    # 0. on part d'une sonnerie eteinte
    http("http://%s/alarme" % IP)
    time.sleep(1)
    s0 = etat()
    if s0.get("alarme"):
        print("ECHEC  la sonnerie ne s'eteint pas au depart")
        echecs += 1

    # 1. rendez-vous dans 25 min : la sonnerie doit partir
    pose("t_alarme.ics", ics(25, "Test alarme"))
    lire("t_alarme.ics")
    s, ok = attendre_alarme(True, 150)
    if ok:
        print("ok     1. rdv dans 25 min -> sonnerie (dans %s min)" % s.get("cal_dans"))
    else:
        print("ECHEC  1. rdv dans 25 min -> pas de sonnerie (cal_dans=%s)" % s.get("cal_dans"))
        echecs += 1
    time.sleep(6)
    s = etat()
    if s.get("alarme_jouee", 0) > s0.get("alarme_jouee", 0):
        print("ok     2. la sonnerie est jouee (%d fois)" % s["alarme_jouee"])
    else:
        print("ECHEC  2. aucune sonnerie jouee par le haut-parleur")
        echecs += 1

    # 2. le bouton 1 la coupe
    http("http://%s/bouton?n=0&m=court" % IP)
    time.sleep(2)
    s = etat()
    if not s.get("alarme"):
        print("ok     3. bouton 1 -> sonnerie coupee")
    else:
        print("ECHEC  3. le bouton 1 ne coupe pas la sonnerie")
        echecs += 1

    # 3. rendez-vous dans 2 h : elle ne doit pas partir
    pose("t_loin.ics", ics(120, "Test loin"))
    lire("t_loin.ics")
    time.sleep(70)                      # ms_boucle passe une fois par minute
    s = etat()
    if not s.get("alarme"):
        print("ok     4. rdv dans 2 h -> pas de sonnerie (dans %s min)" % s.get("cal_dans"))
    else:
        print("ECHEC  4. la sonnerie part trop tot (dans %s min)" % s.get("cal_dans"))
        echecs += 1
        http("http://%s/alarme" % IP)

    # 4. on remet le vrai calendrier
    if a.restore:
        http("http://%s/ical?u=%s" % (IP, urllib.parse.quote(a.restore, safe="")))
        fin = time.time() + 45
        s = {}
        while time.time() < fin:
            time.sleep(3)
            try:
                s = etat()
            except Exception:
                continue
            if s.get("cal_titre"):
                break
        print("\nvrai calendrier : %r invite par %r le %s"
              % (s.get("cal_titre"), s.get("cal_invitant"), s.get("cal_heure")))
        if not s.get("cal_titre"):
            print("ECHEC  le vrai calendrier n'est pas revenu")
            echecs += 1

    print("\n%d echec(s)" % echecs)
    return 1 if echecs else 0


if __name__ == "__main__":
    sys.exit(main())

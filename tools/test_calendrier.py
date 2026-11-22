#!/usr/bin/env python3
"""Batterie de tests du calendrier de Mino, sans toucher au vrai calendrier.

Principe : on depose un .ics de test sur la carte SD du robot, on lui dit de
lire ce fichier (au lieu de l'URL Outlook), et on verifie ce qu'il publie dans
/state. A la fin, on remet l'URL reelle (--restore) et on controle qu'il
retrouve le vrai rendez-vous.

    python3 tools/test_calendrier.py --restore "<url ics>"

Chaque cas est independant : un echec n'arrete pas la serie, un code de sortie
non nul signale au moins un echec.
"""
import argparse
import datetime as dt
import json
import sys
import time
import urllib.parse
import urllib.request

IP = "192.168.8.208"


def http(url, data=None, timeout=20):
    req = urllib.request.Request(url, data=data,
                                 method="POST" if data is not None else "GET")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def etat():
    return json.loads(http("http://%s/state" % IP))


def calendrier(evenements):
    s = ["BEGIN:VCALENDAR", "VERSION:2.0", "PRODID:-//mino//test//FR"]
    for e in evenements:
        s += e if isinstance(e, list) else [e]
    s.append("END:VCALENDAR")
    return "\r\n".join(s) + "\r\n"


def ev(summary, debut, description="", rrule=None, utc=False):
    stamp = debut.strftime("%Y%m%dT%H%M%SZ" if utc else "%Y%m%dT%H%M%S")
    l = ["BEGIN:VEVENT",
         "UID:%s@mino.test" % summary.replace(" ", "-"),
         "DTSTAMP:%s" % stamp,
         "DTSTART:%s" % stamp,
         "SUMMARY:%s" % summary]
    if rrule:
        l.append("RRULE:%s" % rrule)
    if description:
        l.append("DESCRIPTION:%s" % description)
    l.append("END:VEVENT")
    return l


def deposer(nom, contenu):
    http("http://%s/fichier?p=%s" % (IP, urllib.parse.quote("/" + nom)),
         data=contenu.encode("utf-8"))


def lire(nom):
    """fait relire au robot le fichier depose, et attend que /state change"""
    avant = etat()
    url = "http://%s/fichier?p=%s" % (IP, urllib.parse.quote("/" + nom))
    try:
        http("http://%s/ical?u=%s" % (IP, urllib.parse.quote(url, safe="")))
    except Exception as e:                       # le robot redemarre le httpd
        print("    (avertissement envoi : %s)" % e)
    fin = time.time() + 25
    while time.time() < fin:
        time.sleep(1.5)
        try:
            s = etat()
        except Exception:
            continue
        if (s.get("cal_titre"), s.get("cal_dans")) != (avant.get("cal_titre"),
                                                       avant.get("cal_dans")):
            return s
    return etat()


def check(s, attendu):
    """attendu : dict champ -> valeur, ou ('~', min, max), ou callable"""
    problemes = []
    for k, v in attendu.items():
        reel = s.get(k)
        if callable(v):
            if not v(reel):
                problemes.append("%s=%r (attendu : %s)" % (k, reel, v.__doc__))
        elif isinstance(v, tuple) and v[0] == "~":
            if reel is None or not (v[1] <= reel <= v[2]):
                problemes.append("%s=%r (attendu entre %s et %s)" % (k, reel, v[1], v[2]))
        elif reel != v:
            problemes.append("%s=%r (attendu %r)" % (k, reel, v))
    return problemes


def main():
    global IP
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", default=IP)
    ap.add_argument("--restore", default="", help="URL .ics reelle a remettre a la fin")
    a = ap.parse_args()
    IP = a.ip

    maintenant = dt.datetime.now()
    plus_tard = lambda h=0, j=1: (maintenant + dt.timedelta(days=j, hours=h)).replace(
        minute=15, second=0, microsecond=0)

    cas = []

    cas.append(("calendrier vide -> rien a l'ecran", calendrier([]),
                {"cal_dans": -1, "cal_titre": ""}))

    d = plus_tard(j=1)
    cas.append(("demain 09:15, organisateur style Google", calendrier([
        ev("Point projet", d.replace(hour=9, minute=15),
           "Organisateur\\nJeanne Lesbordes<mailto:jl@exemple.fr>")]),
        {"cal_heure": "09:15", "cal_invitant": "Jeanne Lesbordes",
         "cal_dans": ("~", 60, 60 * 36)}))

    cas.append(("organisateur style Outlook ('- organisateur')", calendrier([
        ev("Echange", plus_tard(j=2).replace(hour=11, minute=0),
           "Invites\\nMoussa BEN BALLA<mailto:mb@exemple.fr>- organisateur")]),
        {"cal_heure": "11:00", "cal_invitant": "Moussa BEN BALLA"}))

    cas.append(("organisateur style Zoom ('vous invite')", calendrier([
        ev("Reunion", plus_tard(j=3).replace(hour=16, minute=30),
           "Participer avec Google Meet - \u2500\u2500 Marie Dupont vous invite "
           "a une reunion Zoom programmee.")]),
        {"cal_heure": "16:30", "cal_invitant": "Marie Dupont"}))

    cas.append(("invitation sans nom d'organisateur", calendrier([
        ev("Consultation", plus_tard(j=2).replace(hour=10, minute=0),
           "Rendez-vous au cabinet, apporter la carte vitale.")]),
        {"cal_invitant": "", "cal_titre": "Consultation"}))

    cas.append(("evenement passe -> ignore", calendrier([
        ev("Vieux rdv", maintenant - dt.timedelta(days=1))]),
        {"cal_dans": -1, "cal_titre": ""}))

    cas.append(("rendez-vous dans 30 min -> DANS n MIN", calendrier([
        ev("Point rapide", maintenant + dt.timedelta(minutes=30),
           "Organisateur\\nPaul Martin<mailto:pm@exemple.fr>")]),
        {"cal_invitant": "Paul Martin", "cal_dans": ("~", 20, 40)}))

    cas.append(("deux rendez-vous -> le plus proche gagne", calendrier([
        ev("Lointain", plus_tard(j=5).replace(hour=9, minute=0)),
        ev("Proche", plus_tard(j=2).replace(hour=9, minute=0))]),
        {"cal_titre": "Proche"}))

    # DTSTART en UTC : 12:00Z doit s'afficher a l'heure locale (14:00 en CEST)
    u = plus_tard(j=4).replace(hour=12, minute=0, tzinfo=dt.timezone.utc)
    locale = u.astimezone().strftime("%H:%M")
    cas.append(("DTSTART en UTC -> heure locale", calendrier([
        ev("RdV UTC", u.replace(tzinfo=None), utc=True)]),
        {"cal_heure": locale}))

    cas.append(("evenement recurrent -> prochaine occurrence", calendrier([
        ev("Hebdo", maintenant - dt.timedelta(days=21), rrule="FREQ=WEEKLY")]),
        {"cal_dans": ("~", 1, 60 * 24 * 8)}))

    cas.append(("nom tres long (corps 2 au lieu de 3)", calendrier([
        ev("Entretien", plus_tard(j=1).replace(hour=14, minute=0),
           "Organisateur\\nJean-Baptiste De La Tour Du Pin<mailto:jb@exemple.fr>")]),
        {"cal_invitant": "Jean-Baptiste De La Tour Du Pin"}))

    cas.append(("texte 'je vous invite' piege -> pas de faux nom", calendrier([
        ev("Preparation", plus_tard(j=6).replace(hour=15, minute=0),
           "Bonjour, je vous invite a preparer les documents.")]),
        {"cal_invitant": ""}))

    echecs = 0
    for i, (nom, contenu, attendu) in enumerate(cas, 1):
        fichier = "t%02d.ics" % i
        deposer(fichier, contenu)
        s = lire(fichier)
        pb = check(s, attendu)
        if pb:
            echecs += 1
            print("ECHEC  %2d. %s" % (i, nom))
            for p in pb:
                print("          %s" % p)
        else:
            print("ok     %2d. %s" % (i, nom))

    # on remet le vrai calendrier
    s = {}
    if a.restore:
        try:
            http("http://%s/ical?u=%s" % (IP, urllib.parse.quote(a.restore, safe="")))
        except Exception as e:
            print("avertissement envoi URL reelle : %s" % e)
        fin = time.time() + 40
        while time.time() < fin:
            time.sleep(2)
            try:
                s = etat()
            except Exception:
                continue
            if s.get("cal_titre"):
                break
        print("\nvrai calendrier : titre=%r invite par=%r le %s dans %s min"
              % (s.get("cal_titre"), s.get("cal_invitant"),
                 s.get("cal_heure"), s.get("cal_dans")))
        if not s.get("cal_titre"):
            echecs += 1
            print("ECHEC  le vrai calendrier n'est pas revenu")

    print("\n%d cas, %d echec(s)" % (len(cas), echecs))
    return 1 if echecs else 0


if __name__ == "__main__":
    sys.exit(main())

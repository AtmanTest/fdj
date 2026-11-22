#!/usr/bin/env python3
"""Sert un calendrier .ics de test sur le port 8099 (verifie le parseur du robot).

    python3 tools/ical_test_serveur.py

Deux evenements : une reunion hebdomadaire commencee il y a 3 semaines dont
l'occurrence tombe dans 10 minutes, et une autre demain. Le robot doit choisir
la premiere (celle de dans 10 minutes).
"""
import datetime
import http.server

PORT = 8099

def ics(t0):
    def f(d):
        return d.strftime("%Y%m%dT%H%M%S")

    debut_hebdo = t0 - datetime.timedelta(weeks=3)
    demain = (t0 + datetime.timedelta(days=1)).replace(hour=9, minute=30, second=0)
    return ("BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Mino//Test//FR\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:hebdo@mino\r\n"
            "DTSTART;TZID=Europe/Paris:" + f(debut_hebdo) + "\r\n"
            "DTEND;TZID=Europe/Paris:" + f(debut_hebdo + datetime.timedelta(minutes=30)) + "\r\n"
            "RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR\r\n"
            "SUMMARY:Point equipe hebdomadaire\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:demain@mino\r\n"
            "DTSTART:" + f(demain) + "\r\n"
            "SUMMARY:Dentiste (test demain)\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n")


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        t0 = datetime.datetime.now() + datetime.timedelta(minutes=10)
        corps = ics(t0).encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/calendar; charset=utf-8")
        self.send_header("Content-Length", str(len(corps)))
        self.end_headers()
        self.wfile.write(corps)

    def log_message(self, *a):
        print("requete :", self.path)


if __name__ == "__main__":
    print("calendrier de test sur http://0.0.0.0:%d/test.ics" % PORT)
    http.server.HTTPServer(("0.0.0.0", PORT), H).serve_forever()

#!/usr/bin/env python3
"""epub2txt.py -- convertit un EPUB en texte simple pour le robot.

    python3 tools/epub2txt.py livre.epub            -> livre.txt
    python3 tools/epub2txt.py livre.epub sortie.txt

Le lecteur du robot ne lit que du texte : pas de ZIP, pas de XML a bord.
On fait donc le travail ici, une fois :

  * les chapitres sont pris DANS L'ORDRE du spine (l'ordre de lecture) ;
  * les balises partent, les entites HTML reviennent en UTF-8 ;
  * les paragraphes deviennent des lignes, les lignes coupent a 32 colonnes
    cote firmware (aucune mise en page n'est perdue : le texte se reflowe) ;
  * les coupures de mots en fin de ligne ("con-\\ntinuer") sont recollees.

Puis on pousse le fichier sur la carte :
    curl --data-binary @livre.txt 'http://192.168.8.209/fichier?p=/livres/livre.txt'
"""
import os
import re
import sys
import zipfile
from html.parser import HTMLParser
from xml.etree import ElementTree as ET

BLOCS = {'p', 'div', 'br', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'li',
         'tr', 'blockquote', 'section', 'article', 'pre', 'td'}


class Texte(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.morceaux = []
        self.saut = False

    def handle_starttag(self, tag, attrs):
        if tag in BLOCS:
            self.saut = True

    def handle_endtag(self, tag):
        if tag in BLOCS:
            self.saut = True

    def handle_data(self, data):
        # la mise en page du XHTML (retours a la ligne, indentation) n'est pas
        # du texte : on l'ignore. Un paragraphe = un bloc.
        if data.strip(' \t\r\n') == '':
            return
        if self.saut:
            self.morceaux.append('\n')
            self.saut = False
        self.morceaux.append(re.sub(r'[ \t\r\n]+', ' ', data))

    def texte(self):
        return ''.join(self.morceaux)


def nettoie(txt):
    txt = txt.replace('\r', '\n').replace('\u00ad', '')
    # mots coupes en fin de ligne : "con-\ntinuer" -> "continuer"
    txt = re.sub(r'([a-zà-ÿ])-\n([a-zà-ÿ])', r'\1\2', txt)

    # Un bloc = une ligne. Les vers s'enchainent serres ; un paragraphe de
    # prose (long) se detache par une ligne vide, comme dans un livre ; un
    # bloc vide (espace insecable) marque une strophe.
    lignes = []
    for brut in txt.split('\n'):
        s = re.sub(r'\s+', ' ', brut.replace('\u00a0', ' ')).strip()
        if not s:
            if lignes and lignes[-1] != '':
                lignes.append('')
            continue
        if len(s) > 110 and lignes and lignes[-1] != '':
            lignes.append('')
        lignes.append(s)

    while lignes and lignes[-1] == '':
        lignes.pop()
    return '\n'.join(lignes) + '\n'


def convertir(epub, sortie):
    with zipfile.ZipFile(epub) as z:
        racine = ''
        conteneur = z.read('META-INF/container.xml').decode('utf-8', 'replace')
        m = re.search(r'full-path="([^"]+)"', conteneur)
        if m:
            racine = os.path.dirname(m.group(1))
        opf_path = m.group(1) if m else 'OEBPS/content.opf'
        opf = ET.fromstring(z.read(opf_path))
        ns = {'o': 'http://www.idpf.org/2007/opf'}
        manifest = {}
        for item in opf.findall('.//o:manifest/o:item', ns) or \
                    opf.findall('.//{http://www.idpf.org/2007/opf}manifest/'
                                '{http://www.idpf.org/2007/opf}item'):
            manifest[item.get('id')] = item.get('href')
        spine = []
        for ref in opf.findall('.//o:spine/o:itemref', ns) or \
                   opf.findall('.//{http://www.idpf.org/2007/opf}spine/'
                               '{http://www.idpf.org/2007/opf}itemref'):
            href = manifest.get(ref.get('idref'))
            if href:
                spine.append(href if not racine else os.path.join(racine, href))

        morceaux = []
        for href in spine:
            href = href.replace('\\', '/')
            try:
                brut = z.read(href).decode('utf-8', 'replace')
            except KeyError:
                continue
            p = Texte()
            p.feed(brut)
            morceaux.append(p.texte())

    texte = nettoie('\n\n'.join(morceaux))
    with open(sortie, 'w', encoding='utf-8') as f:
        f.write(texte)
    mots = len(texte.split())
    print('%s -> %s : %d mots, %d pages environ'
          % (epub, sortie, mots, max(1, int(len(texte) / 490))))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    entree = sys.argv[1]
    sortie = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(entree)[0] + '.txt'
    convertir(entree, sortie)

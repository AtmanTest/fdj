#!/usr/bin/env python3
"""gen_font5.py -- fabrique main/font5.cpp / main/font5.h.

Police 5x10 (5 colonnes, 10 lignes) pour l'ecran 200x200 : minuscules,
majuscules, chiffres, ponctuation et lettres accentuees francaises.
La base ASCII vient de la police 5x7 classique d'Adafruit (glcdfont.c,
domaine public) ; les accents sont ajoutes ici, au-dessus de la lettre.

    python3 tools/gen_font5.py <chemin/vers/glcdfont.c>

Regles de dessin (une ligne de texte = 11 px, 1 px d'air) :
    t[0]      accent des MAJUSCULES accentuees
    t[1..8]   la lettre (donc t[1..2] libres au-dessus des minuscules)
    t[9]      cedille des majuscules
Chaque ligne est un masque de 5 bits (bit 0 = colonne de gauche).
"""
import re
import sys

ACCENT_LOW = {           # (lignes 1 et 2)
    'acute':      (0b00110, 0b01100),
    'grave':      (0b01100, 0b00110),
    'circumflex': (0b00100, 0b01010),
    'trema':      (0b01010, 0b00000),
}
ACCENT_UP = {            # (ligne 0 seulement)
    'acute':      (0b00110, 0b00000),
    'grave':      (0b01100, 0b00000),
    'circumflex': (0b00100, 0b00000),
    'trema':      (0b01010, 0b00000),
}

# (codepoint, lettre de base, accent)
LOWER = [
    ('é', 'e', 'acute'), ('è', 'e', 'grave'), ('ê', 'e', 'circumflex'),
    ('ë', 'e', 'trema'),
    ('à', 'a', 'grave'), ('â', 'a', 'circumflex'), ('ä', 'a', 'trema'),
    ('ù', 'u', 'grave'), ('û', 'u', 'circumflex'), ('ü', 'u', 'trema'),
    ('î', 'i', 'circumflex'), ('ï', 'i', 'trema'),
    ('ô', 'o', 'circumflex'), ('ö', 'o', 'trema'),
]
UPPER = [
    ('É', 'E', 'acute'), ('È', 'E', 'grave'), ('Ê', 'E', 'circumflex'),
    ('À', 'A', 'grave'), ('Â', 'A', 'circumflex'),
    ('Î', 'I', 'circumflex'), ('Ï', 'I', 'trema'),
    ('Ô', 'O', 'circumflex'), ('Ù', 'U', 'grave'), ('Ü', 'U', 'trema'),
]

GUARDS = {   # dessines a la main : (code, [10 masques])
    '«': [0b01000, 0b00100, 0b00010, 0b00100, 0b01000],   # rangee x-height
    '»': [0b00010, 0b00100, 0b01000, 0b00100, 0b00010],
}


def charge_glcd(chemin):
    src = open(chemin, encoding='utf-8', errors='replace').read()
    i = src.find('font')
    body = re.sub(r'//.*', '', src[i:])
    b = [int(h, 16) for h in re.findall(r'0x([0-9A-Fa-f]{2})', body)]
    if len(b) < 256 * 5:
        raise SystemExit('glcdfont inattendu (%d octets)' % len(b))
    return [b[k * 5:k * 5 + 5] for k in range(256)]


def lignes(cols):
    """5 octets en colonnes -> 8 masques de ligne."""
    return [sum(1 << x for x in range(5) if (cols[x] >> r) & 1) for r in range(8)]


def glyphe_base(table, ch):
    return lignes(table[ord(ch)])


def place(base, offset):
    t = [0] * 10
    for r in range(8):
        if 0 <= offset + r < 10:
            t[offset + r] = base[r]
    return t


def sans_point(base):
    """enleve le point du i pour y mettre un accent."""
    b = list(base)
    b[0] = 0
    b[1] = 0
    return b


def construit(table):
    out = []        # (codepoint, 10 masques)

    # ASCII imprimable
    for cp in range(0x20, 0x7f):
        out.append((cp, place(glyphe_base(table, chr(cp)), 1)))

    # minuscules accentuees
    for ch, base, acc in LOWER:
        b = glyphe_base(table, base)
        if base == 'i':
            b = sans_point(b)
        t = place(b, 1)
        m1, m2 = ACCENT_LOW[acc]
        t[1] |= m1
        t[2] |= m2
        out.append((ord(ch), t))

    # majuscules accentuees : lettre en t[1..8], accent sur t[0]
    for ch, base, acc in UPPER:
        t = place(glyphe_base(table, base), 1)
        t[0] |= ACCENT_UP[acc][0]
        out.append((ord(ch), t))

    # cedilles
    c_bas = place(glyphe_base(table, 'c'), 1)
    t = list(c_bas)
    t[8] |= 0b00110
    t[9] |= 0b01100
    out.append((ord('ç'), t))
    t = place(glyphe_base(table, 'C'), 1)
    t[9] |= 0b00100
    out.append((ord('Ç'), t))

    # guillemets francais
    for ch, masques in GUARDS.items():
        t = [0] * 10
        for k, m in enumerate(masques):
            t[3 + k] = m
        out.append((ord(ch), t))

    return out


def ecrit(glyphes, h_path, c_path):
    vus = {}
    table_finale = []
    for cp, t in glyphes:
        if cp in vus:
            continue
        vus[cp] = len(table_finale) + 1      # 0 = caractere inconnu
        table_finale.append((cp, t))

    h = ['/* font5.h -- police 5x10 du lecteur (genere par tools/gen_font5.py)',
         ' * NE PAS EDITER A LA MAIN : relancer le generateur. */',
         '#ifndef FONT5_H_INCLUDED', '#define FONT5_H_INCLUDED',
         '#include <stdint.h>', '',
         '#define FONT5_LARGEUR  5', '#define FONT5_HAUTEUR  10',
         '#define FONT5_PAS      6      /* avance d\'un caractere */',
         '#define FONT5_INTERLIGNE 11   /* pas d\'une ligne a l\'autre */',
         '#define FONT5_N %d' % len(table_finale), '',
         'extern const uint8_t FONT5[FONT5_N][FONT5_HAUTEUR];',
         'uint8_t font5_code(uint32_t cp);   /* 0 = inconnu */',
         'int     font5_cols(int largeur_px);',
         '', '#endif', '']
    open(h_path, 'w', encoding='utf-8').write('\n'.join(h))

    c = ['/* font5.cpp -- genere par tools/gen_font5.py ; ne pas editer. */',
         '#include "font5.h"', '',
         'const uint8_t FONT5[FONT5_N][FONT5_HAUTEUR] = {']
    for cp, t in table_finale:
        nom = chr(cp) if 0x20 <= cp < 0x7f else 'U+%04X' % cp
        if nom == '\\':
            nom = '\\\\'
        c.append('    {%s},   /* %s */' % (', '.join('0x%02x' % m for m in t), nom))
    c.append('};')
    c.append('')
    c.append('/* table code -> indice : triee par point de code, dichotomie */')
    c.append('typedef struct { uint16_t cp; uint8_t idx; } font5_entree_t;')
    c.append('static const font5_entree_t FONT5_INDEX[FONT5_N] = {')
    for i, (cp, t) in sorted(enumerate(table_finale), key=lambda e: e[1][0]):
        c.append('    {0x%04x, %d},' % (cp, i + 1))
    c.append('};')
    c.append('')
    c.append('uint8_t font5_code(uint32_t cp)')
    c.append('{')
    c.append('    int lo = 0, hi = FONT5_N - 1;')
    c.append('    if (cp > 0xffff) return 0;')
    c.append('    while (lo <= hi) {')
    c.append('        int m = (lo + hi) / 2;')
    c.append('        if (FONT5_INDEX[m].cp == cp) return FONT5_INDEX[m].idx;')
    c.append('        if (FONT5_INDEX[m].cp < cp) lo = m + 1; else hi = m - 1;')
    c.append('    }')
    c.append('    return 0;')
    c.append('}')
    c.append('')
    c.append('int font5_cols(int largeur_px) { return (largeur_px + 1) / FONT5_PAS; }')
    c.append('')
    open(c_path, 'w', encoding='utf-8').write('\n'.join(c))
    print('%s : %d glyphes' % (h_path, len(table_finale)))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    table = charge_glcd(sys.argv[1])
    ecrit(construit(table), 'main/font5.h', 'main/font5.cpp')

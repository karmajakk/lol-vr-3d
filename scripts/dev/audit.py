#!/usr/bin/env python3
"""Audit of the 3D dungeon, level by level (dev bench, no headset needed).

For every level: every door series used in the level, seen from the block in front of it,
closed, half open and open, then as it is in the game, in 3D and as the game draws it (one
picture sheet per level), and every open block in every direction checked for empty views.
Also: every wall type seen from the block in front of it, clicks checked against what is shown
(the point the app would click, in the game's own picture, has the colour of the 3D view there);
and every entrance (shop door, picture of a place, way out) seen from inside, as the party stands
there when it leaves a place (entrees_niveauN.png).

Usage: audit.py OUT_DIR [LEVEL ...]      (default: all levels; AUDIT_DOORS_ONLY=1: doors only)
Needs LOLVR_GAME, and the core and benches built (build_core.sh, build_tests.sh). Uses the
test save of e2e_save.sh.
"""
import os
import re
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
E2E = os.path.join(HERE, "e2e_save.sh")
LEVELS = list(range(1, 28))
DX = [0, 1, 0, -1]
DZ = [-1, 0, 1, 0]


def run(out, actions, env_extra=None):
    env = dict(os.environ)
    env.update(env_extra or {})
    p = subprocess.run([E2E, out, actions], env=env, capture_output=True, text=True)
    return p.stdout + p.stderr


def level_info(level, out):
    text = run(out, f"goto {level} 0 0; wait 3; carte; dungeonat info 0 0", {"LOLVR_DUMP_TYPE": "tous"})
    rows = [l.split()[1] for l in text.splitlines() if re.match(r"^  [ 0-9][0-9] [#.@]{32}$", l)]
    types, pos = {}, {}
    cur = None
    for l in text.splitlines():
        m = re.match(r"^    type (\d+) : drapeaux \w+ vmp \d+ genre (\d+) couche -?\d+ porte (-?\d+) .* plan ([\d.]+) levée ([\d.]+)(.*)", l)
        if m:
            cur = int(m.group(1))
            types[cur] = (int(m.group(3)), float(m.group(4)), float(m.group(5)), int(m.group(2)), m.group(6).strip())
            continue
        m = re.match(r"^      en case (\d+) face (\d+)", l)
        if m and cur is not None:
            pos.setdefault(cur, []).append((int(m.group(1)), int(m.group(2))))
    return rows, types, pos


def open_block(rows, b):
    z, x = b >> 5, b & 31
    return z < len(rows) and rows[z][x] in ".@"


def audit_doors(level, rows, types, pos, out):
    series = {}
    for t, places in pos.items():
        if t not in types or types[t][0] < 0:
            continue
        step = round(types[t][2] * 4)
        base = t - step
        if base in types and types[base][2] == 0.0:
            for b, k in places:
                nb = (b + DX[k] + DZ[k] * 32) & 1023
                if open_block(rows, nb):
                    series.setdefault(base, (b, k, nb, (k + 2) & 3))
    ops, names = [], []
    for base, (b, k, nb, d) in sorted(series.items()):
        row = []
        for i, step in enumerate((0, 2, 4)):
            t = base + step
            if t not in types:
                continue
            n = f"L{level}_t{base}_{i}"
            ops.append(f"dungeonat {n} {nb} {d} {b} {k} {t}")
            row.append(n)
        # La même porte telle qu'elle est dans la partie, vue par le jeu et en 3D, pour comparer.
        n = f"L{level}_t{base}_jeu"
        ops.append(f"goto {level} {nb} {d}; wait 3; dungeonat {n} {nb} {d}")
        names.append((base, row, n))
    if not ops:
        return []
    run(out, f"goto {level} {series[min(series)][2]} 0; wait 3; " + "; ".join(ops))
    w, h = 320, 240
    sheet = Image.new("RGB", (w * 4 + h * 176 // 120, h * len(names)), (40, 0, 40))
    for r, (base, row, game) in enumerate(names):
        for c, n in enumerate(row + [game]):
            f = os.path.join(out, n + "_3d.ppm")
            if os.path.exists(f):
                sheet.paste(Image.open(f).resize((w, h)), (c * w, r * h))
        f = os.path.join(out, game + "_jeu.ppm")
        if os.path.exists(f):
            j = Image.open(f)
            W, H = j.size  # la vue du jeu : 176 x 120 en haut de l'écran de 320 x 200
            view = j.crop((W * 112 // 320, 0, W * 288 // 320, H * 120 // 200)).resize((h * 176 // 120, h))
            sheet.paste(view, (4 * w, r * h))
    path = os.path.join(out, f"portes_niveau{level}.png")
    sheet.save(path)
    return [(base, types[base][1]) for base, _, _ in names]


def batches(level, ops, out, env, size=12):
    """Runs the ops by batches (each from the test save), returns the bench's output."""
    text = ""
    for i in range(0, len(ops), size):
        text += run(out, f"goto {level} 0 0; wait 2; " + "; ".join(ops[i:i + size]), env)
    return text


def audit_clicks(level, rows, types, pos, out):
    """Every wall type (once), from the block in front of it: do clicks fall on what is shown?"""
    ops = []
    for t in sorted(pos):
        if t < 3 or t not in types or types[t][3] != 0:
            continue
        for b, k in pos[t]:
            nb = (b + DX[k] + DZ[k] * 32) & 1023
            if open_block(rows, nb):
                ops.append(f"goto {level} {nb} {(k + 2) & 3}; wait 1.5; dungeon c{t}")
                break
    text = batches(level, ops, out, {"LOLVR_CLIC": "1"})
    bad = re.findall(r"CLIC case (\d+) dir (\d) : \d+ rayons sur le mur devant, écart (\d+) % \(décalé de 8 px : (\d+) %\) MAUVAIS", text)
    checked = len(re.findall(r"CLIC case", text))
    return checked, len(ops), bad


def audit_entrances(level, types, pos, out):
    """Entrances (a picture of a place, a way out, a shop door) seen from inside, as the party
    stands there when it leaves a place."""
    ops, names = [], []
    for t in sorted(pos):
        if t not in types or not types[t][4]:
            continue
        for b, k in pos[t][:2]:
            n = f"e{t}_{b}"
            ops.append(f"dungeonat {n} {b} {k}")
            names.append(n)
    if not ops:
        return [], 0
    text = run(out, f"goto {level} 0 0; wait 2; " + "; ".join(ops))
    text += run(out, f"goto {level} 0 0; wait 2; " + "; ".join(ops), {"LOLVR_NOIR": "1"})
    empty = re.findall(r"VIDE case (\d+) dir (\d) : (\d+) %", text)
    w, h = 320, 180
    cols = 4
    sheet = Image.new("RGB", (w * cols, h * ((len(names) + cols - 1) // cols)), (40, 0, 40))
    for i, n in enumerate(names):
        f = os.path.join(out, n + "_3d.ppm")
        if os.path.exists(f):
            sheet.paste(Image.open(f).resize((w, h)), ((i % cols) * w, (i // cols) * h))
    sheet.save(os.path.join(out, f"entrees_niveau{level}.png"))
    return empty, len(names)


def audit_empty(level, rows, out):
    ops = []
    for z, row in enumerate(rows):
        for x, c in enumerate(row):
            if c == ".":
                b = z * 32 + x
                if not ops:
                    ops.append(f"goto {level} {b} 0; wait 3")
                for d in range(4):
                    ops.append(f"dungeonat s {b} {d}")
    if not ops:
        return []
    text = run(out, "; ".join(ops), {"LOLVR_NOIR": "1"})
    return re.findall(r"VIDE case (\d+) dir (\d) : (\d+) %", text)


def main():
    out = sys.argv[1]
    levels = [int(a) for a in sys.argv[2:]] or LEVELS
    os.makedirs(out, exist_ok=True)
    for level in levels:
        d = os.path.join(out, f"niveau{level}")
        os.makedirs(d, exist_ok=True)
        rows, types, pos = level_info(level, d)
        if not rows:
            print(f"niveau {level} : pas de carte (niveau absent ?)")
            continue
        doors = audit_doors(level, rows, types, pos, d)
        empty = [] if os.environ.get("AUDIT_DOORS_ONLY") else audit_empty(level, rows, d)
        print(f"niveau {level} : {len(doors)} série(s) de portes " +
              ", ".join(f"type {b} ({'plan %.2f' % p if p > 0 else 'debout'})" for b, p in doors) +
              f" ; vues vides : {len(empty)}" + (" " + " ".join(f"{b}/{dd}:{pc}%" for b, dd, pc in empty[:12]) if empty else ""))
        if not os.environ.get("AUDIT_DOORS_ONLY"):
            checked, wanted, bad = audit_clicks(level, rows, types, pos, d)
            print(f"  clics : {checked} mur(s) sur {wanted} vérifié(s), {len(bad)} mal visé(s)" +
                  (" " + " ".join(f"{b}/{dd} ({e}% contre {e8}%)" for b, dd, e, e8 in bad) if bad else ""))
            ent_empty, ent = audit_entrances(level, types, pos, d)
            print(f"  entrées vues de l'intérieur : {ent}, vides : {len(ent_empty)}" +
                  (" " + " ".join(f"{b}/{dd}:{pc}%" for b, dd, pc in ent_empty) if ent_empty else ""))
        sys.stdout.flush()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Cardboard mock-up templates of the piano box, 1:1, as a 2-page A4 PDF.

Lifted from ../esp32-desk-display/enclosure/cardboard.py (PDF helpers as they were).

Sizes come from the model (part="dims"), so the templates follow it.
Print at 100% / "Actual size" and check the 50 mm bar with a ruler.

Usage: enclosure/cardboard.py [cardboard mm, default 2] [file.scad] [var=value ...]
  enclosure/cardboard.py   -> enclosure/cardboard.pdf
Writes into enclosure/ (gitignored).
"""
import math
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CT = float(sys.argv[1]) if len(sys.argv) > 1 else 2.0  # cardboard thickness
SCAD = sys.argv[2] if len(sys.argv) > 2 else "enclosure.scad"
DEFS = sys.argv[3:]  # var=value, passed to openscad as strings
NAME = "-".join([Path(SCAD).stem] + [kv.split("=", 1)[1] for kv in DEFS])
PT = 72 / 25.4  # PDF points per mm
PAGE_W, PAGE_H = 210, 297


def dims():
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "d.echo"
        defs = [a for kv in DEFS for a in ("-D", '{}="{}"'.format(*kv.split("=", 1)))]
        subprocess.run(["openscad", *defs, "-D", 'part="dims"', "-o", str(out), "--export-format=echo",
                        str(HERE / SCAD)], check=True, capture_output=True)
        line = next(l for l in out.read_text().splitlines() if l.startswith("ECHO: W ="))
    return {k: float(v) for k, v in re.findall(r"(\w+) = ([-\d.e]+)", line)}


class Page:
    """Drawing in mm, origin top-left, y down."""

    def __init__(self):
        self.ops = []

    def _p(self, x, y):
        return f"{x * PT:.2f} {(PAGE_H - y) * PT:.2f}"

    def style(self, width=0.3, dash=False, grey=0.0):
        self.ops.append(f"{width * PT:.2f} w {'[3 2] 0 d' if dash else '[] 0 d'} {grey} G")

    def poly(self, pts, close=True, **st):
        self.style(**st)
        self.ops.append(" ".join([f"{self._p(*pts[0])} m"] + [f"{self._p(*p)} l" for p in pts[1:]])
                        + (" h S" if close else " S"))

    def rect(self, x, y, w, h, **st):
        self.poly([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], **st)

    def circle(self, cx, cy, r, **st):
        k = 0.5523 * r
        self.style(**st)
        pts = [(cx + r, cy), (cx, cy - r), (cx - r, cy), (cx, cy + r)]
        ctl = [((cx + r, cy - k), (cx + k, cy - r)), ((cx - k, cy - r), (cx - r, cy - k)),
               ((cx - r, cy + k), (cx - k, cy + r)), ((cx + k, cy + r), (cx + r, cy + k))]
        s = f"{self._p(*pts[0])} m"
        for i in range(4):
            s += f" {self._p(*ctl[i][0])} {self._p(*ctl[i][1])} {self._p(*pts[(i + 1) % 4])} c"
        self.ops.append(s + " S")

    def stadium(self, cx, cy, w, h, **st):  # rounded ends left/right
        r = h / 2
        self.poly([(cx - w / 2 + r, cy - r), (cx + w / 2 - r, cy - r)], close=False, **st)
        self.poly([(cx - w / 2 + r, cy + r), (cx + w / 2 - r, cy + r)], close=False, **st)
        for sx in (-1, 1):  # half circles as short polylines
            c = cx + sx * (w / 2 - r)
            self.poly([(c + sx * r * math.sin(a), cy - r * math.cos(a))
                       for a in [i * math.pi / 16 for i in range(17)]], close=False, **st)

    def text(self, x, y, s, size=3.0, center=False):
        s = s.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")
        if center:
            x -= len(s) * size * 0.25  # rough Helvetica width
        self.ops.append(f"BT /F1 {size * PT:.1f} Tf {self._p(x, y)} Td ({s}) Tj ET")

    def scale_bar(self, x, y):
        self.poly([(x, y), (x + 50, y)], close=False, width=0.5)
        for i in range(0, 51, 10):
            self.poly([(x + i, y - (2 if i % 50 else 3)), (x + i, y)], close=False, width=0.3)
        self.text(x + 53, y + 1, "50 mm: check with a ruler, else print at 100% / Actual size", 3)


def write_pdf(pages, path):
    objs = ["<< /Type /Catalog /Pages 2 0 R >>", None,
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"]
    kids = []
    for pg in pages:
        stream = "\n".join(pg.ops).encode("latin-1")
        objs.append(f"<< /Length {len(stream)} >>\nstream\n".encode("latin-1") + stream + b"\nendstream")
        objs.append(f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {PAGE_W * PT:.2f} {PAGE_H * PT:.2f}] "
                    f"/Resources << /Font << /F1 3 0 R >> >> /Contents {len(objs)} 0 R >>")
        kids.append(f"{len(objs)} 0 R")
    objs[1] = f"<< /Type /Pages /Kids [{' '.join(kids)}] /Count {len(pages)} >>"
    out = bytearray(b"%PDF-1.4\n")
    offsets = []
    for i, o in enumerate(objs, 1):
        offsets.append(len(out))
        out += f"{i} 0 obj\n".encode() + (o if isinstance(o, bytes) else o.encode("latin-1")) + b"\nendobj\n"
    xref = len(out)
    out += f"xref\n0 {len(objs) + 1}\n0000000000 65535 f \n".encode()
    out += "".join(f"{o:010d} 00000 n \n" for o in offsets).encode()
    out += f"trailer\n<< /Size {len(objs) + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
    path.write_bytes(out)


def main():
    d = dims()
    W, D, H = d["W"], d["D"], d["H"]
    iw = W - 2 * CT  # pieces that sit between the two sides
    M = 12  # page margin
    solid = dict(width=0.35)
    dash = dict(width=0.25, dash=True, grey=0.4)

    def notes(pg, x, y, title, lines):
        pg.text(x, y + 8, title, 4)
        for i, s in enumerate(lines):
            pg.text(x, y + 14 + i * 5, s, 3)

    # ---- page 1: sides, front strip, screen panel
    p1 = Page()
    p1.text(M, 10, f"Piano box: cardboard mock-up, page 1/2  (case {W:.0f} x {D:.0f} x {H:.0f} mm, "
                   f"cardboard {CT:g} mm)", 3.5)
    p1.scale_bar(M, 17)
    y = 30
    for n in (1, 2):
        pts = [(0, 0), (0, d["skirt_h"]), (d["run"], H), (D, H), (D, 0)]  # side profile, front on the left
        p1.poly([(M + a, y + H - b) for a, b in pts], **solid)
        notes(p1, M + D + 4, y, f"SIDE {n} of 2", ["front edge on the left", f"the slope sets the screen's {d['tilt']:.0f} deg",
                                                    "cut out along the solid line"])
        y += H + 10
    p1.rect(M, y, iw, d["skirt_h"], **solid)
    notes(p1, M + iw + 4, y - 4, "FRONT STRIP", ["under the screen, between the sides"])
    y += d["skirt_h"] + 10
    pl = d["panel_len"]
    p1.rect(M, y, iw, pl, **solid)
    cx = M + iw / 2
    wy = y + pl - (d["tft_v"] + d["lit_z"])  # window centre; the panel's bottom edge is at y + pl
    p1.rect(cx - d["win_w"] / 2, wy - d["win_h"] / 2, d["win_w"], d["win_h"], **solid)
    ty = y + pl - d["tft_v"]
    p1.rect(cx - d["tft_w"] / 2, ty - d["tft_h"] / 2, d["tft_w"], d["tft_h"], **dash)
    p1.text(cx - 6, ty + d["tft_h"] / 2 - 1, "pins", 2.5)
    notes(p1, M + iw + 4, y, "SCREEN PANEL", ["cut out the window (solid)", "bottom edge meets the front strip",
                                             "dashed: the TFT board behind it,", "pins at the BOTTOM",
                                             "tape the TFT on from behind"])

    # ---- page 2: top, back, floor layout
    p2 = Page()
    p2.text(M, 10, "Piano box: cardboard mock-up, page 2/2", 3.5)
    p2.scale_bar(M, 17)
    y = 28
    tl = D - d["run"]  # top, from the panel's top edge to the back; front edge drawn at the bottom
    ty = lambda wy: y + tl - (wy - d["run"])  # noqa: E731
    p2.rect(M, y, iw, tl, **solid)
    p2.circle(M + d["knob_x"] - CT, ty(d["knob_y"]), d["knob_hole"] / 2, **solid)
    p2.rect(M + d["knob_x"] - d["ky_w"] / 2 - CT, ty(d["ky_y0"] + d["ky_l"]), d["ky_w"], d["ky_l"], **dash)
    for x0, x1 in ((d["boss_in"] + 1, d["knob_x"] - d["ky_w"] / 2 - 1.5),
                   (d["knob_x"] + d["ky_w"] / 2 + 1.5, W - d["boss_in"] - 1)):
        p2.rect(M + x0 - CT, ty(D - d["boss_d"] - 4), x1 - x0, D - d["boss_d"] - 4 - d["ky_y0"] - 4, **dash)
    notes(p2, M + iw + 4, y, "TOP", ["front edge (to the screen panel)", "at the BOTTOM; cut the knob hole",
                                     "dashed: KY-040 board under it", "(pins to the back), vent slots"])
    y += tl + 8
    p2.rect(M, y, iw, H, **solid)
    p2.stadium(M + (W - CT) - d["usb_x"], y + H - d["usb_z"], d["usb_w"], d["usb_h"], **solid)
    notes(p2, M + iw + 4, y, "BACK (seen from behind)", ["cut out the USB-C hole"])
    y += H + 8
    # floor, top view, front at the bottom: outlines to lay the real modules on
    fy = lambda wy: y + D - wy  # noqa: E731
    p2.rect(M, y, W, D, **solid)
    p2.rect(M + d["wall"], y + d["wall"], W - 2 * d["wall"], D - 2 * d["wall"], **dash)
    for bx in (d["boss_in"], W - d["boss_in"]):
        for by in (d["boss_in"], D - d["boss_in"]):
            p2.circle(M + bx, fy(by), d["boss_d"] / 2, **dash)

    def part(x0, y0, w, l, name, style=solid, bottom=False, tx=1):
        p2.rect(M + x0, fy(y0 + l), w, l, **style)
        p2.text(M + x0 + tx, fy(y0) - 1.2 if bottom else fy(y0 + l) + 3.5, name, 2.5)

    part(d["mini_x0"], d["mini_y0"], d["mini_w"], d["mini_l"], "ESP32 mini (USB at the back)")
    p2.text(M + d["mini_x0"] + 1, fy(d["mini_y0"] + 2), "antenna", 2.5)
    p2.text(M + 2, fy(-4) + 0.5, "FRONT", 3)
    notes(p2, M + W + 4, y, "FLOOR", ["cut the outer border only: the", "boxes inside are outlines, not holes",
                                       "top view, front at the bottom", "lay the real modules on it",
                                       "solid: ESP32 mini; dashed: inside", "walls, screw posts",
                                       "the TFT's plug hangs down in", "front of the ESP32"])

    out = HERE / ("cardboard.pdf" if NAME == "enclosure" else f"cardboard-{NAME}.pdf")
    write_pdf([p1, p2], out)
    print(out)


main()

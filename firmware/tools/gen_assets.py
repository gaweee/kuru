#!/usr/bin/env python3
"""Pre-render the image assets of the knob UI for LVGL (stdlib only).

Design source of truth: reference/knob-ui-prototype.html, drawn only in colours an RGB565 panel can show (the display runs at
16 bit). Every colour here is snapped to the RGB565 palette exactly as the prototype's snapRGB() does.

Why pre-rendered: on this board LVGL's software renderer is CPU-bound (see firmware/README.md). Rounded lines, arcs and
rings cost milliseconds each per redraw; blitting an image is cheap.

Shared by every device:
  tick_xy / arc_*_xy   positions of the ticks and of the arc wedges (geometry only)

Per device (table DEVICES below):
  img_icon_<dev>        30x30 A8 mask of the device icon
  pleats_<dev>          (curtains) 272x272 RGB565: the scene with both curtain panels closed; the UI draws it in two clipped bands
  ripple_<dev>_<n>      272x272 RGB565 opaque: the concentric ripple rings at fill level n (the design's "glow") with the
                        static 2 px track ring (r=134, -135..135 deg) baked in
  ticks_<dev>           48 ticks x 16 brightness levels as ARGB8888 in colours from the unlit tick to the accent, so a
                        tick is drawn as a plain image (a tinted A8 mask costs ~0.8 ms per draw)
  ripples_<dev>         table of the ripple images
  arc_<dev>_{a,b,c}     the thin reported-state arc as ARGB8888 wedges in the accent colour at 0.6 opacity: 9 x 30 degrees,
                        54 x 5 degrees, 270 x 1 degree. Any arc length is drawn with at most 8 + 5 + 4 = 17 images
                        (a tinted A8 image costs ~0.85 ms per draw however small, so 160 one-degree segments were
                        half the cost of a full-screen redraw)

Also writes assets.h (declarations) and assets.cmake (source list) next to the generated files.

To add a device (e.g. fan): add an entry to DEVICES. Nothing else here changes.

Usage: python3 tools/gen_assets.py        (writes components/ui/assets/)
"""
import math, os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'components', 'ui', 'assets')
LEVELS = 16  # ripple fill levels per device; the UI quantises the ripple amount to this many steps
RINGS = 9    # prototype: 9 concentric rings, r = 34 + 11 k, 1.25 px stroke
RIP = 136    # half-size of the ripple image: covers the track ring (r=134 + 1 px stroke) and the outer ripple (r=122)
TICK_LEVELS = 16   # brightness steps per tick between the unlit tick and the accent
BG = (0x18, 0x14, 0x10)       # prototype background #181410 (already an exact RGB565 value)
TRACK = (0x29, 0x24, 0x21)    # prototype .track #292421


WRITTEN = set()


def put(name, text):
    """Write an output file, but not when it is already identical: a rewritten file is recompiled by the build, and
    the generated C is ~25 MB (a full rebuild takes tens of minutes)."""
    path = os.path.join(OUT, name)
    WRITTEN.add(name)
    if os.path.exists(path):
        with open(path) as f:
            if f.read() == text:
                return
    with open(path, 'w') as f:
        f.write(text)


def rep5(v): q = int(max(0, min(255, v)) * 31 / 255 + 0.5); return (q << 3) | (q >> 2)
def rep6(v): q = int(max(0, min(255, v)) * 63 / 255 + 0.5); return (q << 2) | (q >> 4)
def snap(c): return (rep5(c[0]), rep6(c[1]), rep5(c[2]))     # nearest RGB565 value, expanded back to 8 bits (prototype snapRGB)
def mix(a, b, t): return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))
def rgb565(c): return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)


def arc_pts(cx, cy, r, a0, a1, n=12):
    """Flatten a circular arc (angles in degrees, 0 = +x, y down) into a polyline."""
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def rounded_rect(x, y, w, h, r, n=6):
    """Closed polyline of a rounded rectangle (SVG <rect rx>), starting at the top edge."""
    pts = [(x + r, y), (x + w - r, y)]
    pts += arc_pts(x + w - r, y + r, r, -90, 0, n)[1:]
    pts += [(x + w, y + h - r)] + arc_pts(x + w - r, y + h - r, r, 0, 90, n)[1:]
    pts += [(x + r, y + h)] + arc_pts(x + r, y + h - r, r, 90, 180, n)[1:]
    pts += [(x, y + r)] + arc_pts(x + r, y + r, r, 180, 270, n)[1:]
    return pts


def cubic(p0, p1, p2, p3, n=10):
    """Flatten a cubic Bezier (SVG C command) into a polyline."""
    out = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        out.append(tuple(u**3 * a + 3 * u * u * t * b + 3 * u * t * t * c + t**3 * d for a, b, c, d in zip(p0, p1, p2, p3)))
    return out


def rotate(pts, deg, cx=12, cy=12):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [(cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c) for x, y in pts]


# One fan blade: the prototype's path M12 10.3 C11.4 7.1 12.6 3.9 15.1 3.6 c2.2-.3 3.1 2.1 1.6 3.7 -1.2 1.3 -3 2.3 -4.7 3 z
BLADE = (cubic((12, 10.3), (11.4, 7.1), (12.6, 3.9), (15.1, 3.6))
         + cubic((15.1, 3.6), (17.3, 3.3), (18.2, 5.7), (16.7, 7.3))[1:]
         + cubic((16.7, 7.3), (15.5, 8.6), (13.7, 9.6), (12, 10.3))[1:])


# Icons are polylines in a 24-unit viewBox (same geometry as reference/knob-ui-prototype.html), stroked with round caps/joins.
DEVICES = {
    'fan': {
        'accent': (0xB4, 0xD4, 0x8F),
        'icon_stroke': 1.5,
        'icon': [arc_pts(12, 12, 1.7, 0, 360, 24), BLADE, rotate(BLADE, 120), rotate(BLADE, 240)],
    },
    'lights': {
        'accent': (0xFF, 0xC7, 0x66),
        'icon_stroke': 1.5,
        'icon': [
            [(12, 2.5), (12, 7)],                                           # stem
            [(9.6, 7), (14.4, 7), (17, 13), (7, 13), (9.6, 7)],             # shade
            arc_pts(12, 13.2, 1.7, 180, 0),                                 # bulb cap (lower half circle)
            [(7.8, 16.4), (6.6, 18)], [(16.2, 16.4), (17.4, 18)], [(12, 17), (12, 19.2)],   # rays
        ],
    },
    'curtains': {
        'accent': (0xF3, 0xAF, 0x8F),
        'icon_stroke': 1.5,
        'ripple_levels': 1,      # the curtains have no ripples: their scene is the pleat image
        'pleats': True,
        'icon': [
            [(3, 4), (21, 4)],                                                                              # rail
            [(5, 4), (5, 20), (6.6, 20)] + cubic((6.6, 20), (8.2, 17.6), (9.2, 14.3), (9.5, 10.4))[1:] + [(9.5, 4)],     # left curtain
            [(19, 4), (19, 20), (17.4, 20)] + cubic((17.4, 20), (15.8, 17.6), (14.8, 14.3), (14.5, 10.4))[1:] + [(14.5, 4)],   # right
        ],
    },
    'aircon': {
        'accent': (0x8F, 0xD9, 0xD2),
        'icon_stroke': 1.5,
        'icon': [
            rounded_rect(3, 5, 18, 7.5, 2),                                   # unit body
            [(6.5, 10), (17.5, 10)],                                          # louvre
            cubic((8, 15.4), (8, 16.7), (7, 17.1), (7, 18.4)),                # three airflow drips
            cubic((12, 15.4), (12, 16.7), (11, 17.1), (11, 18.4)),
            cubic((16, 15.4), (16, 16.7), (15, 17.1), (15, 18.4)),
        ],
    },
}


def emit(name, cf, w, h, stride, data, extra=''):
    rows = ['  ' + ', '.join('0x%02x' % b for b in data[i:i + 24]) + ',' for i in range(0, len(data), 24)]
    src = f'''// Generated by tools/gen_assets.py. Do not edit.
#include "lvgl.h"
static const uint8_t {name}_map[] __attribute__((aligned(4))) = {{
{chr(10).join(rows)}
}};
const lv_image_dsc_t {name} = {{
  .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = {cf}, .flags = 0, .w = {w}, .h = {h}, .stride = {stride} }},
  .data_size = {len(data)},
  .data = {name}_map,
}};
'''
    put(name + '.c', src)


def lerp(a, b, t): return a + (b - a) * t


PLEAT_R = 127        # prototype: clipPath circle r=127
PLEAT_PERIOD = 11    # prototype: pattern 11 px wide, the 1.5 px line at its left edge, anchored at screen x = 0


def ripple(dev, d, idx):
    """272x272 RGB565 (native little-endian): ring k is lit to clamp(reach - k, 0, 1) * (0.3 - 0.02 k) of the way from the
    background to the accent, with reach = amount * 9 (prototype drawAmbient). The static track ring is baked in;
    everything else is the flat background."""
    acc = snap(d['accent'])
    amt = idx / (LEVELS - 1)
    reach = amt * RINGS
    ring_col = []
    for k in range(RINGS):
        v = max(0.0, min(1.0, reach - k)) * (0.3 - 0.02 * k)
        ring_col.append(snap(mix(BG, acc, v)))
    emit(f'ripple_{dev}_{idx}', 'LV_COLOR_FORMAT_RGB565', 2 * RIP, 2 * RIP, 4 * RIP, scene(ring_col, None))


def pleats(dev, d):
    """The curtains scene with both panels fully closed: the pleat pattern (two flat colours from the accent) inside the
    r=127 circle, the track ring outside. The UI draws it clipped to a band at each side, so the pleats stay put and the
    panels reveal them as they close (the prototype's pattern is anchored to the screen, not to the panel)."""
    acc = snap(d['accent'])
    cols = (snap(mix(BG, acc, 0.07)), snap(mix(BG, acc, 0.2)))      # prototype: pleatBase / pleatLine swatches
    emit(f'pleats_{dev}', 'LV_COLOR_FORMAT_RGB565', 2 * RIP, 2 * RIP, 4 * RIP, scene([BG] * RINGS, cols))


def scene(ring_col, pleat_cols):
    out = bytearray()
    for y in range(2 * RIP):
        for x in range(2 * RIP):
            col = BG
            dx, dy = x + .5 - RIP, y + .5 - RIP
            rr = math.hypot(dx, dy)
            k = int(round((rr - 34) / 11))
            if 0 <= k < RINGS and abs(rr - (34 + 11 * k)) < 1.6 and ring_col[k] != BG:
                cov = 0
                for sy in range(4):
                    for sx in range(4):
                        q = math.hypot(x + (sx + .5) / 4 - RIP, y + (sy + .5) / 4 - RIP)
                        if abs(q - (34 + 11 * k)) <= 0.625:
                            cov += 1
                col = snap(mix(BG, ring_col[k], cov / 16))
            if pleat_cols and rr < PLEAT_R + 1.5:
                acc3 = [0, 0, 0]
                for sy in range(4):
                    for sx in range(4):
                        qx, qy = x + (sx + .5) / 4 - RIP, y + (sy + .5) / 4 - RIP
                        if math.hypot(qx, qy) <= PLEAT_R:
                            xs = x + (sx + .5) / 4 + (180 - RIP)            # screen x of this sample
                            c = pleat_cols[1] if xs % PLEAT_PERIOD < 1.5 else pleat_cols[0]
                        else:
                            c = BG
                        for i in range(3):
                            acc3[i] += c[i]
                col = snap(tuple(a / 16 for a in acc3))
            if 132.0 < rr < 136.5:                       # static track ring r=134, 2 px, -135..135 deg
                tc = 0
                for sy in range(4):
                    for sx in range(4):
                        qx, qy = x + (sx + .5) / 4 - RIP, y + (sy + .5) / 4 - RIP
                        if abs(math.hypot(qx, qy) - 134) <= 1.0 and abs(math.degrees(math.atan2(qx, -qy))) <= 135:
                            tc += 1
                if tc:
                    col = snap(mix(BG, TRACK, tc / 16))
            v = rgb565(col)
            out += bytes((v & 0xFF, v >> 8))
    return bytes(out)


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def icon(dev, d):
    """30x30 A8 mask: 24-unit viewBox scaled by 30/24, polylines stroked with round caps/joins."""
    S, SS = 30.0 / 24.0, 6
    half = d['icon_stroke'] * S / 2
    segs = [(a[0] * S, a[1] * S, b[0] * S, b[1] * S) for pl in d['icon'] for a, b in zip(pl, pl[1:])]
    out = bytearray()
    for y in range(30):
        for x in range(30):
            hit = 0
            for sy in range(SS):
                for sx in range(SS):
                    px, py = x + (sx + .5) / SS, y + (sy + .5) / SS
                    if any(seg_dist(px, py, *s) <= half for s in segs):
                        hit += 1
            out.append(round(255 * hit / (SS * SS)))
    emit(f'img_icon_{dev}', 'LV_COLOR_FORMAT_A8', 30, 30, 30, bytes(out))


def wifi_mask(name, segs, half):
    """13x12 A8 mask of the status line's Wi-Fi icon part (prototype WIFI svg, viewBox 13x12, stroke 1.6)."""
    out = bytearray()
    for y in range(12):
        for x in range(13):
            hit = 0
            for sy in range(6):
                for sx in range(6):
                    px, py = x + (sx + .5) / 6, y + (sy + .5) / 6
                    if any(seg_dist(px, py, *g) <= half for g in segs):
                        hit += 1
            out.append(round(255 * hit / 36))
    emit(name, 'LV_COLOR_FORMAT_A8', 13, 12, 13, bytes(out))


def wifi_icons():
    # The three arcs are centred on the dot (6.5, 10.2) and span -135..-45 degrees; radii from the svg paths.
    wifi_mask('img_wifi_dot', [(6.5, 10.2, 6.5, 10.2)], 1.1)
    for k, r in enumerate((2.55, 5.09, 7.64), 1):
        pts = arc_pts(6.5, 10.2, r, -135, -45, 12)
        wifi_mask(f'img_wifi_arc{k}', [(a[0], a[1], b[0], b[1]) for a, b in zip(pts, pts[1:])], 0.8)


TICK_GEOM = []     # (bx, by, w, h, coverage list) for each of the 48 ticks, computed once


def tick_geometry():
    """48 ring ticks (r 147..161, 3 px, round caps): anti-aliased coverage and position."""
    N, A0, A1, SS = 48, -135.0, 135.0, 8
    for i in range(N):
        a = math.radians(A0 + i * (A1 - A0) / (N - 1))
        pt = lambda r: (180 + r * math.sin(a), 180 - r * math.cos(a))
        (x1, y1), (x2, y2) = pt(147), pt(161)
        bx, by = int(math.floor(min(x1, x2) - 2)), int(math.floor(min(y1, y2) - 2))
        w, h = int(math.ceil(max(x1, x2) + 2)) - bx, int(math.ceil(max(y1, y2) + 2)) - by
        cov = []
        for py in range(h):
            for px in range(w):
                hit = 0
                for sy in range(SS):
                    for sx in range(SS):
                        if seg_dist(bx + px + (sx + .5) / SS, by + py + (sy + .5) / SS, x1, y1, x2, y2) <= 1.5:
                            hit += 1
                cov.append(hit / (SS * SS))
        TICK_GEOM.append((bx, by, w, h, cov))


UNLIT = (0x39, 0x34, 0x29)   # prototype .tk #393429


INK = (0xF7, 0xEF, 0xE7)     # prototype INK #F7EFE7: the scan over the ring while Wi-Fi connects, the landing wave


def ticks_device(dev, d):
    """48 ticks x TICK_LEVELS as ARGB8888: level l is the tick in snap(mix(unlit, accent, l / (levels - 1)))."""
    ticks_ramp(f'ticks_{dev}', snap(d['accent']))


def ticks_ramp(name, end):
    """The same ticks along the ramp from the unlit tick to `end` (shared `ticks_ink`: the connecting scan and the wave)."""
    acc = end
    src = ['// Generated by tools/gen_assets.py. Do not edit.', '#include "lvgl.h"']
    for l in range(TICK_LEVELS):
        col = snap(mix(UNLIT, acc, l / (TICK_LEVELS - 1)))
        for i, (bx, by, w, h, cov) in enumerate(TICK_GEOM):
            data = bytearray()
            for c in cov:
                data += bytes((col[2], col[1], col[0], round(255 * c)))
            src.append(f'static const uint8_t t{l}_{i}_map[] __attribute__((aligned(4))) = {{ ' + ', '.join('0x%02x' % x for x in data) + ' };')
            src.append(f'static const lv_image_dsc_t t{l}_{i} = {{ .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, .flags = 0, .w = {w}, .h = {h}, .stride = {w * 4} }}, .data_size = {len(data)}, .data = t{l}_{i}_map }};')
    src.append(f'const lv_image_dsc_t *const {name}[{TICK_LEVELS * 48}] = {{ ' + ', '.join(f'&t{l}_{i}' for l in range(TICK_LEVELS) for i in range(48)) + ' };')
    put(f'{name}.c', '\n'.join(src) + '\n')


ARC_OPACITY = 0.6    # prototype: .actual stroke at 0.6 opacity


def wedge(t0, t1, SS=8, r0=133, r1=135):
    """Anti-aliased coverage of the arc wedge r 133..135, angles t0..t1 (degrees clockwise from 12 o'clock)."""
    pts = [(180 + r * math.sin(math.radians(t)), 180 - r * math.cos(math.radians(t))) for r in (r0, r1) for t in (t0, t1)]
    # a wedge can bulge past its corner points: add the extreme angles that fall inside it
    for t in range(math.ceil(t0), math.floor(t1) + 1):
        pts += [(180 + r * math.sin(math.radians(t)), 180 - r * math.cos(math.radians(t))) for r in (133, 135)]
    bx, by = int(math.floor(min(p[0] for p in pts) - 1)), int(math.floor(min(p[1] for p in pts) - 1))
    w, h = int(math.ceil(max(p[0] for p in pts) + 1)) - bx, int(math.ceil(max(p[1] for p in pts) + 1)) - by
    cov = []
    for py in range(h):
        for px in range(w):
            hit = 0
            for sy in range(SS):
                for sx in range(SS):
                    qx, qy = bx + px + (sx + .5) / SS - 180, by + py + (sy + .5) / SS - 180
                    if r0 <= math.hypot(qx, qy) < r1 and t0 <= math.degrees(math.atan2(qx, -qy)) < t1:
                        hit += 1
            cov.append(hit / (SS * SS))
    return bx, by, w, h, cov


def arc_set(dev, d, level, step_deg, count):
    """One granularity of the arc: `count` wedges of `step_deg` degrees as ARGB8888 (byte order B,G,R,A)."""
    acc = snap(d['accent'])
    parts, pos = [], []
    for k in range(count):
        bx, by, w, h, cov = wedge(-135.0 + k * step_deg, -135.0 + (k + 1) * step_deg)
        out = bytearray()
        for c in cov:
            out += bytes((acc[2], acc[1], acc[0], round(255 * ARC_OPACITY * c)))
        parts.append((k, w, h, bytes(out)))
        pos.append((bx, by))
    argb_table(f'arc_{dev}_{level}', f'arc{dev}{level}', parts, count)
    ARC_POS[level] = pos


def mask_table(table, prefix, parts, pos, n):
    src = ['// Generated by tools/gen_assets.py. Do not edit.', '#include "lvgl.h"']
    for k, w, h, d in parts:
        src.append(f'static const uint8_t {prefix}{k}_map[] __attribute__((aligned(4))) = {{ ' + ', '.join('0x%02x' % b for b in d) + ' };')
        src.append(f'static const lv_image_dsc_t {prefix}{k} = {{ .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0, .w = {w}, .h = {h}, .stride = {w} }}, .data_size = {len(d)}, .data = {prefix}{k}_map }};')
    src.append(f'const lv_image_dsc_t *const {table}[{n}] = {{ ' + ', '.join(f'&{prefix}{k}' for k in range(n)) + ' };')
    src.append(f'const int16_t {table}_xy[{n}][2] = {{ ' + ', '.join('{%d,%d}' % p for p in pos) + ' };')
    put(table + '.c', '\n'.join(src) + '\n')


def argb_table(table, prefix, parts, n):
    src = ['// Generated by tools/gen_assets.py. Do not edit.', '#include "lvgl.h"']
    for k, w, h, d in parts:
        src.append(f'static const uint8_t {prefix}{k}_map[] __attribute__((aligned(4))) = {{ ' + ', '.join('0x%02x' % b for b in d) + ' };')
        src.append(f'static const lv_image_dsc_t {prefix}{k} = {{ .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, .flags = 0, .w = {w}, .h = {h}, .stride = {w * 4} }}, .data_size = {len(d)}, .data = {prefix}{k}_map }};')
    src.append(f'const lv_image_dsc_t *const {table}[{n}] = {{ ' + ', '.join(f'&{prefix}{k}' for k in range(n)) + ' };')
    put(table + '.c', '\n'.join(src) + '\n')


ARC_POS = {}      # level -> [(x, y)], identical for every device (geometry only)


def arc_positions():
    src = ['// Generated by tools/gen_assets.py. Do not edit.', '#include <stdint.h>']
    for lv, n in (('a', 9), ('b', 54), ('c', 270)):
        src.append(f'const int16_t arc_{lv}_xy[{n}][2] = {{ ' + ', '.join('{%d,%d}' % p for p in ARC_POS[lv]) + ' };')
    src.append('const int16_t tick_xy[48][2] = { ' + ', '.join('{%d,%d}' % (g[0], g[1]) for g in TICK_GEOM) + ' };')
    put('arc_xy.c', '\n'.join(src) + '\n')


def ripples_table(dev, n):
    """The UI indexes ripples by LEVELS; a device with fewer levels repeats its last image."""
    src = ['// Generated by tools/gen_assets.py. Do not edit.', '#include "lvgl.h"']
    src += [f'extern const lv_image_dsc_t ripple_{dev}_{i};' for i in range(n)]
    src.append(f'const lv_image_dsc_t *const ripples_{dev}[{LEVELS}] = {{ ' + ', '.join(f'&ripple_{dev}_{min(i, n - 1)}' for i in range(LEVELS)) + ' };')
    put(f'ripples_{dev}.c', '\n'.join(src) + '\n')


def index_files():
    hdr = ['// Generated by tools/gen_assets.py. Do not edit.', '#pragma once', '#include "lvgl.h"', '',
           '#define ASSET_RIPPLE_LEVELS %d' % LEVELS, '#define ASSET_RIPPLE_HALF %d' % RIP,
           '#define ASSET_TICK_LEVELS %d' % TICK_LEVELS, 'extern const int16_t tick_xy[48][2];',
           f'extern const lv_image_dsc_t *const ticks_ink[{TICK_LEVELS * 48}];',
           'extern const lv_image_dsc_t img_wifi_dot, img_wifi_arc1, img_wifi_arc2, img_wifi_arc3;',
           'extern const int16_t arc_a_xy[9][2];', 'extern const int16_t arc_b_xy[54][2];', 'extern const int16_t arc_c_xy[270][2];',
           f'extern const lv_image_dsc_t *const ticks_ink[{TICK_LEVELS * 48}];',
           'extern const lv_image_dsc_t img_wifi_dot, img_wifi_arc1, img_wifi_arc2, img_wifi_arc3;']
    srcs = ['assets/arc_xy.c', 'assets/ticks_ink.c'] + [f'assets/img_wifi_{n}.c' for n in ('dot', 'arc1', 'arc2', 'arc3')]
    for dev, d in DEVICES.items():
        if d.get('pleats'):
            hdr.append(f'extern const lv_image_dsc_t pleats_{dev};')
            srcs.append(f'assets/pleats_{dev}.c')
        hdr += [f'extern const lv_image_dsc_t img_icon_{dev};', f'extern const lv_image_dsc_t *const ripples_{dev}[{LEVELS}];',
                f'extern const lv_image_dsc_t *const ticks_{dev}[{TICK_LEVELS * 48}];']
        for lv, n in (('a', 9), ('b', 54), ('c', 270)):
            hdr += [f'extern const lv_image_dsc_t *const arc_{dev}_{lv}[{n}];']
            srcs.append(f'assets/arc_{dev}_{lv}.c')
        srcs += [f'assets/img_icon_{dev}.c', f'assets/ripples_{dev}.c', f'assets/ticks_{dev}.c'] + [f'assets/ripple_{dev}_{i}.c' for i in range(d.get('ripple_levels', LEVELS))]
    put('assets.h', '\n'.join(hdr) + '\n')
    put('assets.cmake', '# Generated by tools/gen_assets.py. Do not edit.\nset(ASSET_SRCS\n' + ''.join(f'    "{s}"\n' for s in srcs) + ')\n')


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    tick_geometry()
    ticks_ramp('ticks_ink', snap(INK))
    wifi_icons()
    for dev, d in DEVICES.items():
        n = d.get('ripple_levels', LEVELS)
        for i in range(n):
            ripple(dev, d, i)
        ripples_table(dev, n)
        if d.get('pleats'):
            pleats(dev, d)
        ticks_device(dev, d)
        icon(dev, d)
        arc_set(dev, d, 'a', 30, 9)
        arc_set(dev, d, 'b', 5, 54)
        arc_set(dev, d, 'c', 1, 270)
    arc_positions()
    index_files()
    for f in os.listdir(OUT):                       # drop stale outputs (e.g. renamed devices)
        if f.endswith(('.c', '.h', '.cmake')) and f not in WRITTEN:
            os.remove(os.path.join(OUT, f))
    print('ok:', ', '.join(DEVICES))

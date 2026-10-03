#!/usr/bin/env python3
"""Иконка приложения в стиле Авроры: скруглённый квадрат с диагональным
градиентом (минт -> синий -> фиолет) и белым кольцом активности.
Рендер в 4x суперсэмплинге, даунскейл box-фильтром. Зависимостей нет."""
import math
import os
import struct
import sys
import zlib

SS = 4          # суперсэмплинг
MASTER = 688    # мастер-растр (172 * 4)

# Градиент: три стопа по диагонали (левый верх -> правый низ)
STOPS = [
    (0.00, (53, 224, 194)),    # минт
    (0.55, (46, 124, 246)),    # синий
    (1.00, (139, 92, 246)),    # фиолет
]

CORNER_R = 0.0      # радиус скругления, доля размера (0 = квадрат)
RING_R = 0.295     # радиус кольца
RING_W = 0.088     # толщина штриха кольца
GAP_CENTER = 45    # центр разрыва кольца, градусы (0 = вправо, вниз по часовой)
GAP_SIZE = 62      # ширина разрыва, градусы


def lerp(a, b, t):
    return a + (b - a) * t


def gradient(t):
    t = max(0.0, min(1.0, t))
    for i in range(len(STOPS) - 1):
        t0, c0 = STOPS[i]
        t1, c1 = STOPS[i + 1]
        if t <= t1:
            k = (t - t0) / (t1 - t0)
            return tuple(lerp(c0[j], c1[j], k) for j in range(3))
    return STOPS[-1][1]


def angle_diff(a, b):
    """Кратчайшая разница углов, градусы."""
    return (a - b + 180) % 360 - 180


def render_master():
    s = MASTER
    cx = cy = s / 2
    corner = s * CORNER_R
    ring_r = s * RING_R
    ring_w = s * RING_W
    cap_r = ring_w / 2
    # Концы дуги для круглых кэпов
    a0 = math.radians(GAP_CENTER + GAP_SIZE / 2)
    a1 = math.radians(GAP_CENTER + 360 - GAP_SIZE / 2)
    cap0 = (cx + ring_r * math.cos(a0), cy + ring_r * math.sin(a0))
    cap1 = (cx + ring_r * math.cos(a1), cy + ring_r * math.sin(a1))

    buf = bytearray(s * s * 4)
    for y in range(s):
        for x in range(s):
            # Скруглённый квадрат: расстояние до внутреннего прямоугольника
            dx = max(abs(x - cx) - (s / 2 - corner), 0)
            dy = max(abs(y - cy) - (s / 2 - corner), 0)
            if dx * dx + dy * dy > corner * corner:
                continue  # прозрачный угол
            t = (x + y) / (2 * s)
            r, g, b = gradient(t)

            # Кольцо активности
            d = math.hypot(x - cx, y - cy)
            ang = math.degrees(math.atan2(y - cy, x - cx)) % 360
            on_arc = abs(angle_diff(ang, GAP_CENTER)) > GAP_SIZE / 2
            in_ring = abs(d - ring_r) <= ring_w / 2 and on_arc
            in_cap = (math.hypot(x - cap0[0], y - cap0[1]) <= cap_r
                      or math.hypot(x - cap1[0], y - cap1[1]) <= cap_r)
            a = 255 if (in_ring or in_cap) else 0

            o = (y * s + x) * 4
            if a:  # белый глиф поверх градиента
                buf[o:o + 4] = bytes((255, 255, 255, 255))
            else:
                buf[o:o + 4] = bytes((int(r), int(g), int(b), 255))
    return buf


def downsample(buf, target):
    """Box-даунскейл MASTER -> target с учётом альфы (premultiplied)."""
    s = MASTER
    out = bytearray(target * target * 4)
    for y in range(target):
        y0 = y * s // target
        y1 = (y + 1) * s // target
        for x in range(target):
            x0 = x * s // target
            x1 = (x + 1) * s // target
            sr = sg = sb = sa = 0
            n = 0
            for yy in range(y0, y1):
                base = (yy * s + x0) * 4
                for xx in range(x0, x1):
                    o = base + (xx - x0) * 4
                    a = buf[o + 3]
                    sr += buf[o] * a
                    sg += buf[o + 1] * a
                    sb += buf[o + 2] * a
                    sa += a
                    n += 1
            o = (y * target + x) * 4
            if sa:
                out[o] = sr // sa
                out[o + 1] = sg // sa
                out[o + 2] = sb // sa
                out[o + 3] = sa // n
    return out


def write_png(path, size, rgba):
    rows = []
    stride = size * 4
    for y in range(size):
        rows.append(b"\x00" + bytes(rgba[y * stride:(y + 1) * stride]))
    raw = b"".join(rows)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data)))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


if __name__ == "__main__":
    out_dir, name = sys.argv[1], sys.argv[2]
    master = render_master()
    for size in (86, 108, 128, 172):
        d = os.path.join(out_dir, f"{size}x{size}")
        os.makedirs(d, exist_ok=True)
        write_png(os.path.join(d, f"{name}.png"), size, downsample(master, size))
        print("icon", size)

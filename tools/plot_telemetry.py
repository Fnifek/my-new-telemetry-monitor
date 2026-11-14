#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Строит SVG-график телеметрии без matplotlib.

Использование:
    python tools/plot_telemetry.py data/sample_telemetry.csv data/telemetry.svg
"""

import csv
import html
import math
import os
import sys


def read_rows(path):
    rows = []
    with open(path, newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            rows.append((float(row["time_s"]), float(row["raw"]), float(row["filtered"])))
    return rows


def nice_step(span, target=6):
    if span <= 0:
        return 1.0
    raw = span / target
    power = 10 ** math.floor(math.log10(raw))
    for mult in (1, 2, 2.5, 5, 10):
        if raw <= mult * power:
            return mult * power
    return 10 * power


def ticks(lo, hi, step):
    start = math.ceil(lo / step) * step
    out = []
    x = start
    while x <= hi + 1e-9:
        out.append(round(x, 10))
        x += step
    return out


def points(rows, index, sx, sy):
    return " ".join(f"{sx(t):.1f},{sy(v):.1f}" for t, raw, filt in rows
                    for v in ([raw] if index == 1 else [filt]))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    src, dst = sys.argv[1], sys.argv[2]
    rows = read_rows(src)
    if not rows:
        print("Нет данных в", src)
        return 1

    # Для SVG не нужны десятки тысяч точек: прореживаем, сохраняя концы.
    stride = max(1, len(rows) // 3000)
    plot_rows = rows[::stride]
    if plot_rows[-1] != rows[-1]:
        plot_rows.append(rows[-1])

    t_max = max(r[0] for r in rows)
    values = [v for _, raw, filt in rows for v in (raw, filt)]
    v_min, v_max = min(values), max(values)
    pad = max(0.5, (v_max - v_min) * 0.08)
    v_min -= pad
    v_max += pad

    width, height = 1200, 640
    left, right, top, bottom = 90, 40, 60, 80
    plot_w = width - left - right
    plot_h = height - top - bottom

    def sx(t):
        return left + (t / t_max if t_max > 0 else 0) * plot_w

    def sy(v):
        return top + (1 - (v - v_min) / (v_max - v_min)) * plot_h

    step_x = nice_step(t_max, 6)
    step_y = nice_step(v_max - v_min, 6)
    tx = ticks(0, t_max, step_x)
    if tx and t_max - tx[-1] > step_x * 0.25:
        tx.append(t_max)
    ty = ticks(v_min, v_max, step_y)

    parts = []
    add = parts.append
    add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">')
    add('<rect width="100%" height="100%" fill="#ffffff"/>')
    add('<text x="90" y="30" font-family="Segoe UI, Arial" font-size="22" '
        'font-weight="600" fill="#202124">Телеметрия системы охлаждения</text>')
    add('<text x="90" y="52" font-family="Segoe UI, Arial" font-size="14" '
        'fill="#5f6368">сырой сигнал и результат КИХ + EMA</text>')

    # Сетка
    for x in tx:
        px = sx(x)
        add(f'<line x1="{px:.1f}" y1="{top}" x2="{px:.1f}" y2="{top + plot_h}" '
            f'stroke="#e8eaed" stroke-width="1"/>')
        add(f'<text x="{px:.1f}" y="{top + plot_h + 24}" text-anchor="middle" '
            f'font-family="Segoe UI, Arial" font-size="13" fill="#5f6368">{x:g}</text>')
    for y in ty:
        py = sy(y)
        add(f'<line x1="{left}" y1="{py:.1f}" x2="{left + plot_w}" y2="{py:.1f}" '
            f'stroke="#e8eaed" stroke-width="1"/>')
        add(f'<text x="{left - 12}" y="{py + 4:.1f}" text-anchor="end" '
            f'font-family="Segoe UI, Arial" font-size="13" fill="#5f6368">{y:.1f}</text>')

    # Оси
    add(f'<line x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}" '
        f'stroke="#3c4043" stroke-width="1.5"/>')
    add(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" '
        f'stroke="#3c4043" stroke-width="1.5"/>')
    add(f'<text x="{left + plot_w / 2}" y="{height - 20}" text-anchor="middle" '
        f'font-family="Segoe UI, Arial" font-size="15" fill="#202124">Время, с</text>')
    add(f'<text x="{left}" y="{top - 14}" font-family="Segoe UI, Arial" font-size="15" '
        f'fill="#202124">Температура, °C</text>')

    # Линии
    raw_points = points(plot_rows, 1, sx, sy)
    filt_points = points(plot_rows, 2, sx, sy)
    add(f'<polyline points="{raw_points}" fill="none" stroke="#9aa0a6" '
        f'stroke-width="1.2" stroke-linejoin="round"/>')
    add(f'<polyline points="{filt_points}" fill="none" stroke="#1a73e8" '
        f'stroke-width="2.2" stroke-linejoin="round"/>')

    # Легенда
    lx, ly = left + plot_w - 300, top + 16
    add(f'<rect x="{lx - 12}" y="{ly - 14}" width="292" height="58" rx="6" '
        f'fill="#ffffff" stroke="#dadce0"/>')
    add(f'<line x1="{lx}" y1="{ly}" x2="{lx + 36}" y2="{ly}" stroke="#9aa0a6" stroke-width="2"/>')
    add(f'<text x="{lx + 46}" y="{ly + 4}" font-family="Segoe UI, Arial" font-size="13" '
        f'fill="#3c4043">Сырой сигнал (АЦП + шум + наводка)</text>')
    add(f'<line x1="{lx}" y1="{ly + 24}" x2="{lx + 36}" y2="{ly + 24}" stroke="#1a73e8" stroke-width="2.6"/>')
    add(f'<text x="{lx + 46}" y="{ly + 28}" font-family="Segoe UI, Arial" font-size="13" '
        f'fill="#3c4043">После КИХ + EMA</text>')

    # Подпись с диапазоном
    add(f'<text x="{left}" y="{height - 44}" font-family="Segoe UI, Arial" font-size="12" '
        f'fill="#80868b">{html.escape(os.path.basename(src))}: '
        f'{len(rows)} отсчётов, {t_max:.1f} с</text>')

    add('</svg>')

    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    with open(dst, "w", encoding="utf-8") as fh:
        fh.write("\n".join(parts))
    print(f"SVG: {dst}; точек: {len(plot_rows)} из {len(rows)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

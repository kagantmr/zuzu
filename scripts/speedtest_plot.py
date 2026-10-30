#!/usr/bin/env python3
"""Render speedtest CSV results into a self-contained HTML report.

    python3 scripts/speedtest_plot.py run.csv [other.csv ...] [-o report.html]

Pass several CSVs to overlay runs (e.g. before/after a kernel change).
"""

import argparse
import html
import math
import os
import sys

PALETTE_LIGHT = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]
PALETTE_DARK = ["#3987e5", "#d95926", "#199e70", "#c98500", "#d55181", "#008300", "#9085e9", "#e66767"]
PCT_KEYS = ["min", "p1", "p5", "p10", "p25", "p50", "p75", "p90", "p95", "p99", "p999", "max"]


class Run:
    def __init__(self, path):
        self.path = path
        self.label = os.path.splitext(os.path.basename(path))[0]
        self.meta = {}
        self.summary = []
        self.raw = {}
        self.points = []
        self.sections = []
        self._parse()
        self.khz = float(self.meta.get(("init", "clock_khz"), 0) or 0)

    def _parse(self):
        with open(self.path, "r", errors="replace") as f:
            for line in f:
                line = line.rstrip("\r\n")
                if not line:
                    continue
                if line.startswith("#section,"):
                    sec = line.split(",", 1)[1]
                    if sec not in self.sections and sec != "end":
                        self.sections.append(sec)
                    continue
                if line.startswith("#"):
                    continue
                f_ = line.split(",")
                kind = f_[0]
                try:
                    if kind == "M":
                        self.meta[(f_[1], f_[2])] = ",".join(f_[3:])
                    elif kind == "S":
                        sec, name, unit, n = f_[1], f_[2], f_[3], int(f_[4])
                        vals = [int(v) for v in f_[5:5 + len(PCT_KEYS) + 2]]
                        row = dict(zip(PCT_KEYS + ["mean", "sd"], vals))
                        row.update(section=sec, name=name, unit=unit, n=n)
                        self.summary.append(row)
                    elif kind == "R":
                        sec, name, unit, n = f_[1], f_[2], f_[3], int(f_[4])
                        self.raw[(sec, name)] = (unit, [int(v) for v in f_[5:5 + n]])
                    elif kind == "P":
                        sec, series, xl, yl = f_[1], f_[2], f_[3], f_[4]
                        self.points.append((sec, series, xl, yl, float(f_[5]), float(f_[6])))
                except (ValueError, IndexError):
                    continue

    def to_ns(self, v, unit):
        if unit == "us":
            return v * 1000.0
        if self.khz <= 0:
            return float(v)
        return v * 1e6 / self.khz


def esc(s):
    return html.escape(str(s), quote=True)


def fmt_ns(v):
    if v < 1:
        return "%.2f ns" % v
    if v < 1000:
        return "%.0f ns" % v if v >= 100 else "%.1f ns" % v
    if v < 1e6:
        return "%.1f µs" % (v / 1e3) if v < 1e5 else "%.0f µs" % (v / 1e3)
    if v < 1e9:
        return "%.2f ms" % (v / 1e6)
    return "%.2f s" % (v / 1e9)


def fmt_bytes(v):
    v = float(v)
    for unit, div in (("M", 1 << 20), ("K", 1 << 10)):
        if v >= div:
            q = v / div
            return ("%d%s" % (q, unit)) if q == int(q) else ("%.1f%s" % (q, unit))
    return "%dB" % v


def fmt_num(v, label=""):
    if label in ("bytes", "chunk bytes"):
        return fmt_bytes(v)
    if label == "ns":
        return fmt_ns(v)
    if abs(v) >= 100:
        return "%.0f" % v
    if abs(v) >= 10:
        return "%.1f" % v
    return "%.2f" % v


class Scale:
    def __init__(self, lo, hi, a, b, log=False, base=10):
        self.log = log
        self.base = base
        if log:
            lo = max(lo, 1e-9)
            hi = max(hi, lo * 1.0001)
            self.lo, self.hi = math.log(lo, base), math.log(hi, base)
        else:
            if hi <= lo:
                hi = lo + 1
            self.lo, self.hi = lo, hi
        self.a, self.b = a, b

    def __call__(self, v):
        if self.log:
            v = math.log(max(v, 1e-9), self.base)
        t = (v - self.lo) / (self.hi - self.lo)
        return self.a + t * (self.b - self.a)

    def ticks(self, max_ticks=7):
        if self.log:
            lo, hi = math.floor(self.lo), math.ceil(self.hi)
            step = max(1, int(math.ceil((hi - lo + 1) / max_ticks)))
            out = [self.base ** e for e in range(int(lo), int(hi) + 1, step)]
            return [t for t in out if self.lo - 1e-9 <= math.log(t, self.base) <= self.hi + 1e-9]
        span = self.hi - self.lo
        raw = span / max(1, max_ticks - 1)
        mag = 10 ** math.floor(math.log10(raw)) if raw > 0 else 1
        step = next(m * mag for m in (1, 2, 2.5, 5, 10) if m * mag >= raw)
        t = math.ceil(self.lo / step) * step
        out = []
        while t <= self.hi + step * 1e-6:
            out.append(round(t, 10))
            t += step
        return out


def nice_hi(v):
    if v <= 0:
        return 1
    mag = 10 ** math.floor(math.log10(v))
    for m in (1, 1.2, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10):
        if m * mag >= v:
            return m * mag
    return v


def legend(names):
    if len(names) < 2:
        return ""
    items = "".join(
        '<span class="lg"><i style="background:var(--s%d)"></i>%s</span>' % (i % 8 + 1, esc(n))
        for i, n in enumerate(names))
    return '<div class="legend">%s</div>' % items


def svg_open(w, h, title):
    return ('<svg viewBox="0 0 %d %d" width="100%%" role="img" aria-label="%s" '
            'preserveAspectRatio="xMinYMin meet">' % (w, h, esc(title)))


def x_axis(sx, y0, x0, x1, label, fmt, top=None):
    out = ['<line class="axis" x1="%.1f" x2="%.1f" y1="%.1f" y2="%.1f"/>' % (x0, x1, y0, y0)]
    for t in sx.ticks():
        x = sx(t)
        if top is not None:
            out.append('<line class="grid" x1="%.1f" x2="%.1f" y1="%.1f" y2="%.1f"/>' % (x, x, top, y0))
        out.append('<text class="tick" x="%.1f" y="%.1f" text-anchor="middle">%s</text>' % (x, y0 + 16, esc(fmt(t))))
    if label:
        out.append('<text class="alabel" x="%.1f" y="%.1f" text-anchor="end">%s</text>' % (x1, y0 + 32, esc(label)))
    return "".join(out)


def y_axis(sy, x0, x1, label, fmt):
    out = []
    for t in sy.ticks(6):
        y = sy(t)
        out.append('<line class="grid" x1="%.1f" x2="%.1f" y1="%.1f" y2="%.1f"/>' % (x0, x1, y, y))
        out.append('<text class="tick" x="%.1f" y="%.1f" text-anchor="end">%s</text>' % (x0 - 6, y + 4, esc(fmt(t))))
    if label:
        out.append('<text class="alabel" x="%.1f" y="%.1f">%s</text>' % (x0, sy.b - 10, esc(label)))
    return "".join(out)


def range_chart(rows, multi):
    """rows: list of (label, run_idx, run_label, stats_ns dict, unit_note)."""
    if not rows:
        return ""
    W, LEFT, RIGHT, TOP = 980, 300, 90, 10
    rh = 20
    H = TOP + rh * len(rows) + 44
    lo = min(r[3]["p1"] for r in rows)
    hi = max(r[3]["p99"] for r in rows)
    lo = max(lo, 0.5)
    lo = 10 ** math.floor(math.log10(lo))
    hi = 10 ** math.ceil(math.log10(max(hi, lo * 10)))
    sx = Scale(lo, hi, LEFT, W - RIGHT, log=True)
    out = [svg_open(W, H, "median and spread per benchmark")]
    y_end = TOP + rh * len(rows)
    out.append(x_axis(sx, y_end + 4, LEFT, W - RIGHT, "time per op (log)", fmt_ns, top=TOP))
    for i, (label, ri, rlabel, s, _) in enumerate(rows):
        y = TOP + rh * i + rh / 2
        c = "var(--s%d)" % (ri % 8 + 1)
        x1, x99 = sx(max(s["p1"], lo)), sx(min(max(s["p99"], lo), hi))
        x25, x75 = sx(max(s["p25"], lo)), sx(min(max(s["p75"], lo), hi))
        xm = sx(min(max(s["p50"], lo), hi))
        tip = "%s%s&#10;median %s · mean %s&#10;p1 %s · p25 %s · p75 %s&#10;p99 %s · p99.9 %s&#10;min %s · max %s · n=%d" % (
            esc(label), (" (%s)" % esc(rlabel)) if multi else "", fmt_ns(s["p50"]), fmt_ns(s["mean"]),
            fmt_ns(s["p1"]), fmt_ns(s["p25"]), fmt_ns(s["p75"]), fmt_ns(s["p99"]), fmt_ns(s["p999"]),
            fmt_ns(s["min"]), fmt_ns(s["max"]), s["n"])
        show_label = (not multi) or ri == 0 or rows[i - 1][0] != label
        if show_label:
            out.append('<text class="rlabel" x="%d" y="%.1f" text-anchor="end">%s</text>' % (LEFT - 10, y + 4, esc(label)))
        out.append('<g class="hit" data-tip="%s">' % tip)
        out.append('<rect x="%d" y="%.1f" width="%d" height="%d" fill="transparent"/>' % (LEFT, y - rh / 2, W - RIGHT - LEFT, rh))
        out.append('<line x1="%.1f" x2="%.1f" y1="%.1f" y2="%.1f" stroke="%s" stroke-width="2" stroke-linecap="round" opacity=".55"/>' % (x1, x99, y, y, c))
        out.append('<rect x="%.1f" y="%.1f" width="%.1f" height="8" rx="4" fill="%s" opacity=".35"/>' % (x25, y - 4, max(x75 - x25, 2), c))
        out.append('<circle cx="%.1f" cy="%.1f" r="4.5" fill="%s" class="ring"/>' % (xm, y, c))
        out.append('</g>')
        out.append('<text class="vlabel" x="%d" y="%.1f">%s</text>' % (W - RIGHT + 8, y + 4, fmt_ns(s["p50"])))
    out.append("</svg>")
    return "".join(out)


def line_chart(title, xlabel, ylabel, series, scatter=False):
    """series: list of (name, color_idx, [(x, y)])."""
    pts = [p for _, _, ps in series for p in ps]
    if not pts:
        return ""
    W, H, L, R, T, B = 980, 340, 70, 20, 20, 48
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    xlog = xlabel in ("bytes", "chunk bytes") and min(xs) > 0 and max(xs) / max(min(xs), 1) > 8
    if xlabel == "handles" or xlabel == "pages":
        xlog = False
    ypos = [y for y in ys if y > 0]
    ylog = bool(ypos) and (max(ypos) / min(ypos) > 60 or scatter)
    if xlog:
        sx = Scale(min(xs), max(xs), L, W - R, log=True, base=2)
    else:
        sx = Scale(min(0, min(xs)), max(xs) if max(xs) > 0 else 1, L, W - R)
    if ylog:
        lo = 10 ** math.floor(math.log10(min(ypos)))
        hi = 10 ** math.ceil(math.log10(max(ypos) * 1.01))
        sy = Scale(lo, hi, H - B, T, log=True)
    else:
        sy = Scale(0, nice_hi(max(ys) * 1.05), H - B, T)
    xfmt = (lambda v: fmt_num(v, xlabel))
    if xlabel == "us":
        xfmt = (lambda v: fmt_ns(v * 1000))
    yfmt = (lambda v: fmt_num(v, ylabel))
    out = [svg_open(W, H, title)]
    out.append(y_axis(sy, L, W - R, ylabel, yfmt))
    if xlog:
        ticks = sorted(set(2 ** round(math.log2(x)) for x in xs))
        step = max(1, len(ticks) // 10)
        out.append('<line class="axis" x1="%d" x2="%d" y1="%d" y2="%d"/>' % (L, W - R, H - B, H - B))
        for t in ticks[::step]:
            out.append('<text class="tick" x="%.1f" y="%d" text-anchor="middle">%s</text>' % (sx(t), H - B + 16, esc(xfmt(t))))
        out.append('<text class="alabel" x="%d" y="%d" text-anchor="end">%s</text>' % (W - R, H - B + 32, esc(xlabel)))
    else:
        out.append(x_axis(sx, H - B, L, W - R, xlabel, xfmt))
    for name, ci, ps in series:
        c = "var(--s%d)" % (ci % 8 + 1)
        ps = sorted(ps)
        if not scatter and len(ps) > 1:
            d = " ".join("%s%.1f,%.1f" % ("M" if i == 0 else "L", sx(x), sy(max(y, sy.lo if not sy.log else 1e-9))) for i, (x, y) in enumerate(ps))
            out.append('<path d="%s" fill="none" stroke="%s" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>' % (d, c))
        r = 2.5 if scatter else 4
        for x, y in ps:
            tip = "%s&#10;%s: %s&#10;%s: %s" % (esc(name), esc(xlabel), esc(xfmt(x)), esc(ylabel), esc(yfmt(y)))
            out.append('<g class="hit" data-tip="%s"><circle cx="%.1f" cy="%.1f" r="9" fill="transparent"/>'
                       '<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" class="ring"/></g>' % (tip, sx(x), sy(y), sx(x), sy(y), r, c))
        if not scatter and ps:
            x, y = ps[-1]
            if len(series) <= 4:
                out.append('<text class="vlabel" x="%.1f" y="%.1f" text-anchor="end">%s</text>' % (sx(x) - 8, sy(y) - 8, esc(yfmt(y))))
    out.append("</svg>")
    return legend([s[0] for s in series]) + "".join(out)


def cdf_chart(entries):
    """entries: list of (name, ci, sorted ns values)."""
    if not entries:
        return ""
    W, H, L, R, T, B = 980, 300, 60, 20, 16, 48
    lo = min(e[2][0] for e in entries if e[2])
    hi = max(e[2][-1] for e in entries if e[2])
    lo = 10 ** math.floor(math.log10(max(lo, 0.5)))
    hi = 10 ** math.ceil(math.log10(max(hi, lo * 10)))
    sx = Scale(lo, hi, L, W - R, log=True)
    sy = Scale(0, 1, H - B, T)
    out = [svg_open(W, H, "cumulative distribution")]
    out.append(y_axis(sy, L, W - R, "fraction ≤ x", lambda v: "%d%%" % round(v * 100)))
    out.append(x_axis(sx, H - B, L, W - R, "time per op (log)", fmt_ns))
    for name, ci, vals in entries:
        n = len(vals)
        if not n:
            continue
        steps = min(n, 300)
        pts = []
        for k in range(steps + 1):
            q = k / steps
            v = vals[min(n - 1, int(q * (n - 1)))]
            pts.append((sx(max(v, lo)), sy(q)))
        d = " ".join("%s%.1f,%.1f" % ("M" if i == 0 else "L", x, y) for i, (x, y) in enumerate(pts))
        c = "var(--s%d)" % (ci % 8 + 1)
        out.append('<path d="%s" fill="none" stroke="%s" stroke-width="2" stroke-linejoin="round"/>' % (d, c))
        for q in (0.5, 0.99):
            v = vals[min(n - 1, int(q * (n - 1)))]
            tip = "%s&#10;p%s: %s" % (esc(name), "50" if q == 0.5 else "99", fmt_ns(v))
            out.append('<g class="hit" data-tip="%s"><circle cx="%.1f" cy="%.1f" r="9" fill="transparent"/>'
                       '<circle cx="%.1f" cy="%.1f" r="4" fill="%s" class="ring"/></g>' % (tip, sx(max(v, lo)), sy(q), sx(max(v, lo)), sy(q), c))
    out.append("</svg>")
    return legend([e[0] for e in entries]) + "".join(out)


def spark(name, vals_ns, ci):
    """Sample-order trace: per-bucket min/max band plus median line."""
    n = len(vals_ns)
    if n < 2:
        return ""
    W, H, P = 300, 74, 4
    buckets = min(150, n)
    band = []
    for b in range(buckets):
        chunk = vals_ns[b * n // buckets:(b + 1) * n // buckets] or [vals_ns[min(n - 1, b * n // buckets)]]
        s = sorted(chunk)
        band.append((s[0], s[len(s) // 2], s[-1]))
    srt = sorted(vals_ns)
    lo = max(srt[0], 0.5)
    hi = max(srt[min(n - 1, int(n * 0.999))], lo * 1.5)
    sx = Scale(0, buckets - 1, P, W - P)
    sy = Scale(lo, hi, H - 16, P, log=True)
    clamp = lambda v: min(max(v, lo), hi)
    c = "var(--s%d)" % (ci % 8 + 1)
    top = " ".join("%.1f,%.1f" % (sx(i), sy(clamp(b[2]))) for i, b in enumerate(band))
    bot = " ".join("%.1f,%.1f" % (sx(i), sy(clamp(b[0]))) for i, b in reversed(list(enumerate(band))))
    med = " ".join("%s%.1f,%.1f" % ("M" if i == 0 else "L", sx(i), sy(clamp(b[1]))) for i, b in enumerate(band))
    tip = "%s&#10;%d samples in run order&#10;band = min..max per bucket&#10;median %s · p99.9 %s" % (
        esc(name), n, fmt_ns(srt[n // 2]), fmt_ns(hi))
    return ('<figure class="spark hit" data-tip="%s">%s<polygon points="%s %s" fill="%s" opacity=".18"/>'
            '<path d="%s" fill="none" stroke="%s" stroke-width="1.5"/>'
            '<text class="tick" x="%d" y="%d">%s</text></svg><figcaption>%s</figcaption></figure>') % (
        tip, svg_open(W, H, name), top, bot, c, med, c, P, H - 3,
        esc("%s – %s" % (fmt_ns(lo), fmt_ns(hi))), esc(name))


def summary_table(runs, sec):
    head = "<tr><th>benchmark</th>%s<th>n</th><th>min</th><th>p50</th><th>mean</th><th>p99</th><th>p99.9</th><th>max</th><th>sd</th></tr>" % (
        "<th>run</th>" if len(runs) > 1 else "")
    body = []
    for ri, run in enumerate(runs):
        for s in run.summary:
            if s["section"] != sec:
                continue
            cv = lambda k: fmt_ns(run.to_ns(s[k], s["unit"]))
            body.append("<tr><td>%s</td>%s<td>%d</td>%s</tr>" % (
                esc(s["name"]), ("<td>%s</td>" % esc(run.label)) if len(runs) > 1 else "", s["n"],
                "".join("<td>%s</td>" % cv(k) for k in ("min", "p50", "mean", "p99", "p999", "max", "sd"))))
    if not body:
        return ""
    return '<details><summary>table view</summary><table>%s%s</table></details>' % (head, "".join(body))


CSS = """
.viz-root{color-scheme:light;--surface:#fcfcfb;--page:#f9f9f7;--ink:#0b0b0b;--ink2:#52514e;--muted:#898781;
--grid:#e1e0d9;--axis:#c3c2b7;--border:rgba(11,11,11,.10);
%s}
@media (prefers-color-scheme:dark){.viz-root{color-scheme:dark;--surface:#1a1a19;--page:#0d0d0d;--ink:#f0efec;--ink2:#c3c2b7;
--muted:#898781;--grid:#2c2c2a;--axis:#383835;--border:rgba(255,255,255,.10);%s}}
body{margin:0;background:var(--page);color:var(--ink);font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
.viz-root{background:var(--page);padding:24px 32px 64px;max-width:1080px;margin:0 auto}
h1{font-size:22px;margin:0 0 4px}h2{font-size:17px;margin:36px 0 4px}h3{font-size:14px;margin:18px 0 6px;color:var(--ink2);font-weight:600}
.sub{color:var(--ink2);margin:0 0 12px}
.card{background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:14px 16px;margin:10px 0}
svg text{fill:var(--ink2);font-size:11px;font-variant-numeric:tabular-nums}
svg .rlabel{fill:var(--ink);font-size:12px}svg .vlabel{fill:var(--ink2);font-size:11px}
svg .alabel{fill:var(--muted)}svg .tick{fill:var(--muted)}
.grid{stroke:var(--grid);stroke-width:1}.axis{stroke:var(--axis);stroke-width:1}
.ring{stroke:var(--surface);stroke-width:2}
.legend{display:flex;flex-wrap:wrap;gap:6px 16px;margin:2px 0 8px;color:var(--ink2);font-size:12px}
.lg i{display:inline-block;width:10px;height:10px;border-radius:3px;margin-right:6px;vertical-align:-1px}
.meta{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:8px 18px}
.meta div{font-size:12px;color:var(--ink2)}.meta b{display:block;color:var(--ink);font-size:16px;font-weight:600}
.sparks{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:8px}
.spark{margin:0;background:var(--surface);border:1px solid var(--border);border-radius:8px;padding:4px 6px}
.spark figcaption{font-size:11px;color:var(--ink2);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
details{margin-top:8px;color:var(--ink2)}summary{cursor:pointer;font-size:12px}
table{border-collapse:collapse;font-size:12px;margin-top:6px;width:100%%;font-variant-numeric:tabular-nums}
th,td{text-align:right;padding:3px 8px;border-bottom:1px solid var(--grid)}th:first-child,td:first-child{text-align:left}
#tip{position:fixed;pointer-events:none;background:var(--surface);color:var(--ink);border:1px solid var(--border);
border-radius:8px;padding:6px 10px;font-size:12px;white-space:pre;box-shadow:0 4px 16px rgba(0,0,0,.12);display:none;z-index:9}
nav a{color:var(--ink2);margin-right:12px;font-size:12px}
"""

JS = """
const tip=document.getElementById('tip');
document.addEventListener('mousemove',e=>{const g=e.target.closest('[data-tip]');
if(!g){tip.style.display='none';return;}tip.textContent=g.getAttribute('data-tip');tip.style.display='block';
const w=tip.offsetWidth,h=tip.offsetHeight;let x=e.clientX+14,y=e.clientY+14;
if(x+w>innerWidth-8)x=e.clientX-w-14;if(y+h>innerHeight-8)y=e.clientY-h-14;tip.style.left=x+'px';tip.style.top=y+'px';});
"""


def build(runs):
    var_l = "".join("--s%d:%s;" % (i + 1, c) for i, c in enumerate(PALETTE_LIGHT))
    var_d = "".join("--s%d:%s;" % (i + 1, c) for i, c in enumerate(PALETTE_DARK))
    multi = len(runs) > 1
    sections = []
    for r in runs:
        for s in r.sections:
            if s not in sections:
                sections.append(s)
    o = ['<!doctype html><html><head><meta charset="utf-8"><title>zuzu speedtest</title>'
         '<style>%s</style></head><body><div class="viz-root">' % (CSS % (var_l, var_d))]
    o.append("<h1>zuzu speedtest</h1>")
    o.append('<p class="sub">%s</p>' % esc(" vs ".join(r.label for r in runs)))
    o.append(legend([r.label for r in runs]))
    for r in runs:
        m = r.meta
        g = lambda k, d="–": m.get(("init", k), d)
        tot = m.get(("end", "total_ms"))
        o.append('<div class="card meta">')
        o.append('<div><b>%s</b>run</div>' % esc(r.label))
        o.append('<div><b>%.1f MHz</b>cycle counter</div>' % (r.khz / 1000.0 if r.khz else 0))
        o.append('<div><b>%s cyc</b>timer overhead (subtracted)</div>' % esc(g("timer_overhead_cyc")))
        o.append('<div><b>%s</b>samples / bench</div>' % esc(g("samples")))
        o.append('<div><b>%s</b>total run</div>' % (fmt_ns(float(tot) * 1e6) if tot else "–"))
        drift = []
        for s in r.sections:
            a, b = m.get((s, "clock_khz_before")), m.get((s, "clock_khz_after"))
            if a and b and r.khz and max(abs(float(a) - r.khz), abs(float(b) - r.khz)) * 200 > r.khz:
                drift.append(s)
        o.append('<div><b>%s</b>clock drift sections</div>' % (esc(", ".join(drift)) if drift else "none"))
        if (("noise", "noise_events")) in m:
            o.append('<div><b>%s</b>noise: interruptions in %s ms</div>' % (esc(m[("noise", "noise_events")]), esc(m.get(("noise", "noise_ms"), "?"))))
            o.append('<div><b>%s%%</b>noise: CPU stolen</div>' % esc(m.get(("noise", "noise_stolen_pct"), "?")))
            o.append('<div><b>%s</b>noise: worst gap</div>' % fmt_ns(float(m.get(("noise", "noise_worst_ns"), 0))))
        o.append("</div>")
    o.append("<nav>%s</nav>" % "".join('<a href="#s-%s">%s</a>' % (esc(s), esc(s)) for s in sections))

    for sec in sections:
        o.append('<h2 id="s-%s">%s</h2>' % (esc(sec), esc(sec)))
        rows = []
        names = []
        for ri, r in enumerate(runs):
            for s in r.summary:
                if s["section"] == sec and s["name"] not in names:
                    names.append(s["name"])
        for name in names:
            for ri, r in enumerate(runs):
                for s in r.summary:
                    if s["section"] == sec and s["name"] == name:
                        st = {k: r.to_ns(s[k], s["unit"]) for k in PCT_KEYS + ["mean"]}
                        st["n"] = s["n"]
                        rows.append((name, ri, r.label, st, s["unit"]))
        if rows:
            o.append('<h3>median (dot), p25–p75 (bar), p1–p99 (whisker)</h3><div class="card">%s</div>' % range_chart(rows, multi))

        groups = {}
        order = []
        for ri, r in enumerate(runs):
            for (psec, series, xl, yl, x, y) in r.points:
                if psec != sec:
                    continue
                key = (xl, yl)
                if key not in groups:
                    groups[key] = {}
                    order.append(key)
                sname = series if not multi else "%s · %s" % (series, r.label)
                groups[key].setdefault(sname, (ri, series, []))[2].append((x, y))
        for key in order:
            xl, yl = key
            ss = groups[key]
            series = [(sname, i, pts) for i, (sname, (ri, sbase, pts)) in enumerate(ss.items())][:8]
            base_names = []
            for _, sbase, _ in ss.values():
                if sbase not in base_names:
                    base_names.append(sbase)
            title = " / ".join(base_names)
            scatter = "noise timeline" in base_names
            o.append('<h3>%s</h3><div class="card">%s</div>' % (esc(title), line_chart(title, xl, yl, series, scatter)))

        entries = []
        sparks = []
        for ri, r in enumerate(runs):
            for (rsec, name), (unit, vals) in r.raw.items():
                if rsec != sec:
                    continue
                ns = [r.to_ns(v, unit) for v in vals]
                lab = name if not multi else "%s · %s" % (name, r.label)
                entries.append((lab, len(entries), sorted(ns)))
                sparks.append(spark(lab, ns, ri if multi else len(sparks)))
        if entries:
            if len(entries) <= 8:
                o.append('<h3>distribution (CDF)</h3><div class="card">%s</div>' % cdf_chart(entries))
            else:
                for i in range(0, len(entries), 8):
                    chunk = [(n, j, v) for j, (n, _, v) in enumerate(entries[i:i + 8])]
                    o.append('<h3>distribution (CDF) %d–%d</h3><div class="card">%s</div>' % (i + 1, i + len(chunk), cdf_chart(chunk)))
            o.append('<h3>samples in run order (spot warmup, drift, periodic stalls)</h3><div class="sparks">%s</div>' % "".join(sparks))
        o.append(summary_table(runs, sec))

    o.append('<div id="tip"></div><script>%s</script></div></body></html>' % JS)
    return "".join(o)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="+", help="speedtest -o output file(s)")
    ap.add_argument("-o", "--out", help="output HTML (default: <first csv>.html)")
    a = ap.parse_args()
    runs = [Run(p) for p in a.csv]
    for r in runs:
        if not r.summary and not r.points:
            print("warning: %s has no results" % r.path, file=sys.stderr)
    out = a.out or os.path.splitext(a.csv[0])[0] + ".html"
    with open(out, "w") as f:
        f.write(build(runs))
    print(out)


if __name__ == "__main__":
    main()

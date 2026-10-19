// Thin wrapper over uPlot with the lab's visual language.
import uPlot, { type AlignedData, type Options } from "uplot";
import "uplot/dist/uPlot.min.css";

export interface SeriesSpec {
  label: string;
  color: string;
  width?: number;
  dash?: number[];
  /** Index of a [lo, hi] pair of series to draw as a shaded band behind this one. */
  band?: [number, number];
  hidden?: boolean;
  /** Scale key; series on "right" are plotted against the right-hand axis. */
  scale?: string;
}

export interface ChartSpec {
  title?: string;
  xLabel?: string;
  yLabel?: string;
  height?: number;
  series: SeriesSpec[];
  yRange?: [number, number];
  /** Optional second y axis on the right for series with scale "right". */
  rightAxis?: { label: string };
  onCursor?: (idx: number | null) => void;
  onClick?: (idx: number) => void;
  markers?: () => number[];  // x values to mark (checkpoints)
}

const AXIS = {
  stroke: "#7c90ab",
  grid: { stroke: "rgba(124,144,171,0.12)", width: 1 },
  ticks: { stroke: "rgba(124,144,171,0.25)", width: 1, size: 4 },
  font: "11px 'Instrument Sans', system-ui, sans-serif",
  labelFont: "11px 'Instrument Sans', system-ui, sans-serif",
};

function hexA(hex: string, a: number): string {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a})`;
}

/** Integer with thousands separators (locale-independent). */
export function fmtInt(v: number): string {
  if (!Number.isFinite(v)) return "–";
  const s = Math.round(Math.abs(v)).toString().replace(/\B(?=(\d{3})+(?!\d))/g, ",");
  return v < 0 ? `-${s}` : s;
}

export function fmt(v: number | null | undefined, digits = 2): string {
  if (v === null || v === undefined || !Number.isFinite(v)) return "–";
  const a = Math.abs(v);
  if (a >= 10000) return fmtInt(Math.round(v));
  if (a >= 100) return v.toFixed(0);
  if (a >= 10) return v.toFixed(1);
  return v.toFixed(digits);
}

export class Chart {
  plot: uPlot;
  private ro: ResizeObserver;

  constructor(readonly el: HTMLElement, readonly spec: ChartSpec) {
    const series: Options["series"] = [{ label: spec.xLabel ?? "tick" }];
    const bands: Options["bands"] = [];
    spec.series.forEach((s, i) => {
      series.push({
        label: s.label,
        stroke: s.color,
        scale: s.scale ?? "y",
        width: s.width ?? 1.5,
        dash: s.dash,
        show: !s.hidden,
        spanGaps: false,
        points: { show: false },
        value: (_u, v) => fmt(v),
      });
      if (s.band) bands.push({ series: [s.band[1] + 1, s.band[0] + 1], fill: hexA(spec.series[i].color, 0.16) });
    });
    const opts: Options = {
      width: Math.max(200, el.clientWidth),
      height: spec.height ?? 160,
      title: spec.title,
      series,
      bands,
      legend: { show: true, live: true },
      cursor: { drag: { x: false, y: false }, points: { size: 5 } },
      scales: {
        x: { time: false },
        y: spec.yRange ? { range: spec.yRange } : { auto: true },
        ...(spec.rightAxis ? { right: { auto: true } } : {}),
      },
      axes: [
        { ...AXIS, label: spec.xLabel, labelSize: spec.xLabel ? 16 : 0, size: 26 },
        { ...AXIS, label: spec.yLabel, labelSize: spec.yLabel ? 16 : 0, size: 48 },
        ...(spec.rightAxis
          ? [{ ...AXIS, scale: "right", side: 1 as const, grid: { show: false }, label: spec.rightAxis.label, labelSize: 16, size: 52 }]
          : []),
      ],
      hooks: {
        setCursor: [(u) => spec.onCursor?.(u.cursor.idx ?? null)],
        draw: [(u) => this.drawMarkers(u)],
        ready: [(u) => {
          if (spec.onClick) u.over.addEventListener("click", () => {
            if (u.cursor.idx !== null && u.cursor.idx !== undefined) spec.onClick!(u.cursor.idx);
          });
        }],
      },
    };
    // Band series ([lo, hi] helpers) are hidden from the legend.
    this.plot = new uPlot(opts, [[], ...spec.series.map(() => [])] as AlignedData, el);
    spec.series.forEach((s, i) => {
      if (s.label.endsWith(" lo") || s.label.endsWith(" hi")) {
        const row = this.plot.root.querySelectorAll(".u-legend .u-series")[i + 1] as HTMLElement | undefined;
        if (row) row.style.display = "none";
      }
    });
    this.ro = new ResizeObserver(() => this.plot.setSize({ width: Math.max(200, el.clientWidth), height: spec.height ?? 160 }));
    this.ro.observe(el);
  }

  private drawMarkers(u: uPlot) {
    const xs = this.spec.markers?.();
    if (!xs || !xs.length) return;
    const ctx = u.ctx;
    ctx.save();
    ctx.strokeStyle = "rgba(240,181,74,0.55)";
    ctx.lineWidth = 1;
    ctx.setLineDash([2, 3]);
    for (const x of xs) {
      const px = Math.round(u.valToPos(x, "x", true));
      if (px < u.bbox.left || px > u.bbox.left + u.bbox.width) continue;
      ctx.beginPath();
      ctx.moveTo(px + 0.5, u.bbox.top);
      ctx.lineTo(px + 0.5, u.bbox.top + u.bbox.height);
      ctx.stroke();
    }
    ctx.restore();
  }

  set(data: (number | null)[][]) {
    this.plot.setData(data as AlignedData);
  }

  destroy() {
    this.ro.disconnect();
    this.plot.destroy();
  }
}

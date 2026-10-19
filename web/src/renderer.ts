// Canvas renderer for the live world. Reads packed snapshots from the engine;
// never mutates simulation state.
import { RENDER, type WorldInfo } from "./engine";

export type ColorMode = "lineage" | "cluster" | "energy" | "generation";

export interface Layers {
  food: boolean;
  temperature: boolean;
  hazards: boolean;
  vision: boolean;
  grid: boolean;
}

export interface Snapshot {
  data: Float32Array;
  ids: Uint32Array;
  count: number;
  stride: number;
}

const PALETTE = ["#4cc9e0", "#7fb2ff", "#a191ff", "#d58cff", "#ff8cc6", "#f0b54a",
  "#c5df6a", "#6bd39e", "#47d1bd", "#a9c4d9", "#e9967a", "#b3a1e6"];
const PREDATOR = "#ee6e6b";

function hashColor(n: number): string {
  // Integer hash so neighbouring ids get unrelated colours.
  let h = Math.imul(n ^ (n >>> 16), 0x45d9f3b);
  h = Math.imul(h ^ (h >>> 16), 0x45d9f3b);
  return PALETTE[((h ^ (h >>> 16)) >>> 0) % PALETTE.length];
}

function ramp(t: number): string {
  // Dim teal → cyan → pale for energy/generation scales.
  const c = Math.max(0, Math.min(1, t));
  const r = Math.round(40 + 170 * c * c);
  const g = Math.round(90 + 140 * c);
  const b = Math.round(130 + 110 * c);
  return `rgb(${r},${g},${b})`;
}

export class Renderer {
  readonly ctx: CanvasRenderingContext2D;
  private foodCanvas = document.createElement("canvas");
  private foodCtx = this.foodCanvas.getContext("2d")!;
  private foodImage: ImageData | null = null;
  scale = 1;
  ox = 0;
  oy = 0;
  private dpr = 1;
  world: WorldInfo | null = null;
  colorMode: ColorMode = "lineage";
  layers: Layers = { food: true, temperature: false, hazards: true, vision: false, grid: true };
  selectedId = 0;
  highlightFounder = 0;
  pendingTool: { kind: string; radius: number } | null = null;
  pointer: { x: number; y: number } | null = null;

  constructor(readonly canvas: HTMLCanvasElement) {
    this.ctx = canvas.getContext("2d", { alpha: false })!;
  }

  setWorld(w: WorldInfo, refit: boolean) {
    const sizeChanged = !this.world || this.world.foodCols !== w.foodCols || this.world.foodRows !== w.foodRows;
    this.world = w;
    if (sizeChanged) {
      this.foodCanvas.width = w.foodCols;
      this.foodCanvas.height = w.foodRows;
      this.foodImage = this.foodCtx.createImageData(w.foodCols, w.foodRows);
    }
    if (refit) this.fit();
  }

  resize() {
    const rect = this.canvas.getBoundingClientRect();
    this.dpr = Math.min(2, window.devicePixelRatio || 1);
    this.canvas.width = Math.max(1, Math.round(rect.width * this.dpr));
    this.canvas.height = Math.max(1, Math.round(rect.height * this.dpr));
  }

  fit() {
    if (!this.world) return;
    const w = this.canvas.width / this.dpr, h = this.canvas.height / this.dpr;
    this.scale = Math.min(w / this.world.width, h / this.world.height) * 0.96;
    this.ox = (w - this.world.width * this.scale) / 2;
    this.oy = (h - this.world.height * this.scale) / 2;
  }

  toWorld(px: number, py: number): { x: number; y: number } {
    return { x: (px - this.ox) / this.scale, y: (py - this.oy) / this.scale };
  }

  zoomAt(px: number, py: number, factor: number) {
    const before = this.toWorld(px, py);
    this.scale = Math.max(0.2, Math.min(60, this.scale * factor));
    this.ox = px - before.x * this.scale;
    this.oy = py - before.y * this.scale;
  }

  centerOn(x: number, y: number) {
    const w = this.canvas.width / this.dpr, h = this.canvas.height / this.dpr;
    this.ox = w / 2 - x * this.scale;
    this.oy = h / 2 - y * this.scale;
  }

  updateFood(food: Uint8Array) {
    if (!this.foodImage) return;
    const px = this.foodImage.data;
    // Plate colour #0d1524 blended toward food green #6bd39e by density.
    for (let i = 0, n = food.length; i < n; i++) {
      const t = food[i] / 255;
      const a = Math.sqrt(t) * 0.85;
      const o = i * 4;
      px[o] = 13 + (107 - 13) * a;
      px[o + 1] = 21 + (211 - 21) * a;
      px[o + 2] = 36 + (158 - 36) * a;
      px[o + 3] = 255;
    }
    this.foodCtx.putImageData(this.foodImage, 0, 0);
  }

  draw(snap: Snapshot) {
    const { ctx, world } = this;
    if (!world) return;
    const W = this.canvas.width, H = this.canvas.height;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.fillStyle = "#0a111d";
    ctx.fillRect(0, 0, W, H);
    ctx.setTransform(this.dpr * this.scale, 0, 0, this.dpr * this.scale, this.dpr * this.ox, this.dpr * this.oy);
    const px = 1 / this.scale;  // one CSS pixel in world units

    // Plate.
    if (this.layers.food) {
      ctx.imageSmoothingEnabled = true;
      ctx.imageSmoothingQuality = "high";
      ctx.drawImage(this.foodCanvas, 0, 0, world.foodCols * world.foodCell, world.foodRows * world.foodCell);
    } else {
      ctx.fillStyle = "#0d1524";
      ctx.fillRect(0, 0, world.width, world.height);
    }
    if (this.layers.temperature) {
      const t = world.temperatureOffset + world.temperatureCycle;
      const g = ctx.createLinearGradient(0, 0, 0, world.height);
      const tint = (temp: number) => {
        const c = Math.max(-2, Math.min(2, temp)) / 2;
        return c < 0 ? `rgba(90,150,255,${-c * 0.32})` : `rgba(240,140,60,${c * 0.32})`;
      };
      g.addColorStop(0, tint(t - world.temperatureGradient));
      g.addColorStop(0.5, tint(t));
      g.addColorStop(1, tint(t + world.temperatureGradient));
      ctx.fillStyle = g;
      ctx.fillRect(0, 0, world.width, world.height);
    }
    if (this.layers.grid) {
      ctx.strokeStyle = "rgba(124,144,171,0.09)";
      ctx.lineWidth = px;
      ctx.beginPath();
      const step = world.width > 1200 ? 100 : 50;
      for (let x = step; x < world.width; x += step) { ctx.moveTo(x, 0); ctx.lineTo(x, world.height); }
      for (let y = step; y < world.height; y += step) { ctx.moveTo(0, y); ctx.lineTo(world.width, y); }
      ctx.stroke();
    }
    ctx.strokeStyle = "rgba(124,144,171,0.35)";
    ctx.lineWidth = px;
    ctx.strokeRect(0, 0, world.width, world.height);

    if (this.layers.hazards) {
      for (const h of world.hazards) {
        ctx.beginPath();
        ctx.arc(h.x, h.y, h.radius, 0, Math.PI * 2);
        ctx.fillStyle = "rgba(238,110,107,0.10)";
        ctx.fill();
        ctx.setLineDash([4 * px, 4 * px]);
        ctx.strokeStyle = "rgba(238,110,107,0.7)";
        ctx.stroke();
        ctx.setLineDash([]);
      }
    }

    // Organisms.
    const { data, ids, count, stride } = snap;
    const dimOthers = this.highlightFounder !== 0;
    if (this.layers.vision) {
      ctx.strokeStyle = "rgba(76,201,224,0.08)";
      ctx.lineWidth = px;
      ctx.beginPath();
      for (let i = 0; i < count; i++) {
        const o = i * stride;
        ctx.moveTo(data[o] + data[o + RENDER.vision], data[o + 1]);
        ctx.arc(data[o], data[o + 1], data[o + RENDER.vision], 0, Math.PI * 2);
      }
      ctx.stroke();
    }
    let maxGen = 1;
    if (this.colorMode === "generation") for (let i = 0; i < count; i++) maxGen = Math.max(maxGen, data[i * stride + RENDER.generation]);
    let selIndex = -1;
    for (let i = 0; i < count; i++) {
      const o = i * stride;
      const x = data[o], y = data[o + 1], hx = data[o + 2], hy = data[o + 3];
      const predator = data[o + RENDER.kind] === 1;
      const r = Math.max(2.2 * px, (predator ? 1.7 : 1.35) * data[o + RENDER.size]);
      const id = ids[i * 2], founder = ids[i * 2 + 1];
      if (id === this.selectedId) selIndex = i;
      let color: string;
      if (predator) color = PREDATOR;
      else if (this.colorMode === "lineage") color = hashColor(founder);
      else if (this.colorMode === "cluster") color = data[o + RENDER.cluster] < 0 ? "#7c90ab" : PALETTE[data[o + RENDER.cluster] % PALETTE.length];
      else if (this.colorMode === "energy") color = ramp(data[o + RENDER.energy]);
      else color = ramp(data[o + RENDER.generation] / maxGen);
      ctx.globalAlpha = dimOthers && founder !== this.highlightFounder ? 0.18 : 1;
      ctx.fillStyle = color;
      ctx.beginPath();
      // Teardrop: round body with a point in the heading direction.
      const nose = r * (predator ? 2.3 : 1.9);
      ctx.moveTo(x + hx * nose, y + hy * nose);
      const a = Math.atan2(hy, hx);
      ctx.arc(x, y, r, a + 0.9, a - 0.9 + Math.PI * 2);
      ctx.closePath();
      ctx.fill();
      if (predator) {
        ctx.strokeStyle = "rgba(255,210,205,0.85)";
        ctx.lineWidth = 0.8 * px;
        ctx.stroke();
      }
    }
    ctx.globalAlpha = 1;

    if (selIndex >= 0) {
      const o = selIndex * stride;
      ctx.strokeStyle = "#f0b54a";
      ctx.lineWidth = 1.5 * px;
      ctx.beginPath();
      ctx.arc(data[o], data[o + 1], Math.max(4 * px, 2.6 * data[o + RENDER.size]), 0, Math.PI * 2);
      ctx.stroke();
      ctx.setLineDash([3 * px, 3 * px]);
      ctx.strokeStyle = "rgba(240,181,74,0.55)";
      ctx.beginPath();
      ctx.arc(data[o], data[o + 1], data[o + RENDER.vision], 0, Math.PI * 2);
      ctx.stroke();
      ctx.setLineDash([]);
    }

    if (this.pendingTool && this.pointer) {
      ctx.strokeStyle = this.pendingTool.kind === "addHazard" ? "rgba(238,110,107,0.9)" : "rgba(240,181,74,0.9)";
      ctx.setLineDash([5 * px, 4 * px]);
      ctx.lineWidth = 1.2 * px;
      ctx.beginPath();
      ctx.arc(this.pointer.x, this.pointer.y, this.pendingTool.radius, 0, Math.PI * 2);
      ctx.stroke();
      ctx.setLineDash([]);
    }
  }
}

export { hashColor, PALETTE, PREDATOR };

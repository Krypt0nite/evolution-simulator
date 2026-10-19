// Screenshots at the three reference widths. Usage: node e2e/shots.mjs <url> <outDir> [prefix]
import puppeteer from "puppeteer-core";
import { chromePath } from "./chrome.mjs";
const [url = "http://localhost:4173/", out = ".", prefix = ""] = process.argv.slice(2);
const b = await puppeteer.launch({ executablePath: chromePath(), headless: true });
const p = await b.newPage();
for (const [w, h] of [[390, 844], [768, 1024], [1440, 900]]) {
  await p.setViewport({ width: w, height: h });
  await p.goto(url, { waitUntil: "networkidle0" });
  await new Promise((r) => setTimeout(r, 1200));
  await p.screenshot({ path: `${out}/${prefix}${w}.png` });
  await p.screenshot({ path: `${out}/${prefix}${w}-full.png`, fullPage: true });
}
await b.close();

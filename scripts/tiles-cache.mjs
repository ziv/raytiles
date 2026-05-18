#!/usr/bin/env node
// Pre-warm the on-disk tile cache for a single zoom-9 anchor tile.
//
// Usage: tiles-cache <x> <y>
//   <x> <y>  zoom-9 tile coordinates
//
// Downloads texture / heightmap / normals tiles for every zoom in
// [min-zoom, max-zoom] (from tiles-cache-config.json) covering the subtree
// rooted at the zoom-9 tile (x, y). Files are written under the configured
// *-path directories using the same `<zoom>/<x>/<y>.png` layout the C++
// raytiles::pool expects.
//
// Existing files are skipped, so this script is safe to re-run / resume.
// MAPBOX_TOKEN must be set in the environment for the satellite texture.
//
import * as process from "node:process";
import {mkdir, stat, rename, unlink} from "node:fs/promises";
import {createWriteStream} from "node:fs";
import {dirname} from "node:path";
import {Readable} from "node:stream";
import {pipeline} from "node:stream/promises";
import config from "./tiles-cache-config.json" with {type: "json"};

const args = process.argv.slice(2);

if (args.length !== 2) {
    console.error("Usage: tiles-cache <x> <y>");
    process.exit(1);
}

const baseX = parseInt(args[0], 10);
const baseY = parseInt(args[1], 10);
if (Number.isNaN(baseX) || Number.isNaN(baseY)) {
    console.error("Tile coordinates must be integers.");
    process.exit(2);
}

const token = process.env["MAPBOX_TOKEN"];
if (!token) {
    console.error("MAPBOX_TOKEN environment variable is not set.");
    process.exit(3);
}

const MIN_ZOOM = config["min-zoom"];
const MAX_ZOOM = config["max-zoom"];
const CONCURRENCY = 64;
const RETRIES = 3;

/**
 * By default, it configured to support Mapbox maps that require an access token
 * Replace the "tex_url" function with the commented one to use Esri maps
 */
// const tex_url = (zoom, x, y) => `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/${zoom}/$yx}/${x}`;
const tex_url = (zoom, x, y) => `https://api.mapbox.com/v4/mapbox.satellite/${zoom}/${x}/${y}.pngraw?access_token=${token}`;
const hm_url = (zoom, x, y) => `https://s3.amazonaws.com/elevation-tiles-prod/terrarium/${zoom}/${x}/${y}.png`;
const nl_url = (zoom, x, y) => `https://s3.amazonaws.com/elevation-tiles-prod/normal/${zoom}/${x}/${y}.png`;

const layers = [
    {name: "texture", base: config["tx-path"], url: tex_url},
    {name: "heightmap", base: config["hm-path"], url: hm_url},
    {name: "normals", base: config["nl-path"], url: nl_url},
];

async function fileExists(path) {
    try {
        const s = await stat(path);
        return s.isFile() && s.size > 0;
    } catch {
        return false;
    }
}

async function downloadOne(url, path) {
    if (await fileExists(path)) return "skipped";

    await mkdir(dirname(path), {recursive: true});
    const tmp = `${path}.part`;

    let lastErr;
    for (let attempt = 1; attempt <= RETRIES; attempt++) {
        try {
            const res = await fetch(url);
            if (!res.ok || !res.body) {
                // drain body to free the connection before retrying
                await res.body?.cancel().catch(() => {
                });
                throw new Error(`HTTP ${res.status} ${res.statusText}`);
            }
            // stream the response straight to disk, then atomically rename
            await pipeline(Readable.fromWeb(res.body), createWriteStream(tmp));
            await rename(tmp, path);
            return "downloaded";
        } catch (err) {
            lastErr = err;
            await unlink(tmp).catch(() => {
            });
            if (attempt < RETRIES) {
                await new Promise(r => setTimeout(r, 250 * attempt));
            }
        }
    }
    throw new Error(`${url} -> ${path}: ${lastErr?.message ?? lastErr}`);
}

// Build the full work list across all zooms / layers.
const tasks = [];
for (let zoom = MIN_ZOOM; zoom <= MAX_ZOOM; zoom++) {
    const scale = 1 << (zoom - MIN_ZOOM);
    const x0 = baseX * scale;
    const y0 = baseY * scale;
    for (let dx = 0; dx < scale; dx++) {
        for (let dy = 0; dy < scale; dy++) {
            const x = x0 + dx;
            const y = y0 + dy;
            for (const layer of layers) {
                tasks.push({
                    layer: layer.name,
                    url: layer.url(zoom, x, y),
                    path: `${layer.base}/${zoom}/${x}/${y}.png`,
                });
            }
        }
    }
}

console.log(`tiles-cache: ${tasks.length} tile downloads queued for base (${baseX}, ${baseY}) z${MIN_ZOOM}..z${MAX_ZOOM}`);

let downloaded = 0;
let skipped = 0;
let failed = 0;
let done = 0;
const total = tasks.length;
let cursor = 0;

async function worker() {
    while (cursor < tasks.length) {
        const idx = cursor++;
        const t = tasks[idx];
        try {
            const result = await downloadOne(t.url, t.path);
            if (result === "downloaded") downloaded++;
            else skipped++;
        } catch (err) {
            failed++;
            console.error(`  fail [${t.layer}] ${t.path}: ${err.message}`);
        }
        done++;
        if (done % 50 === 0 || done === total) {
            const pct = ((done / total) * 100).toFixed(1);
            console.log(`  progress: ${done}/${total} (${pct}%)  ok=${downloaded}  skip=${skipped}  fail=${failed}`);
        }
    }
}

const workers = Array.from({length: Math.min(CONCURRENCY, tasks.length)}, () => worker());
await Promise.all(workers);

console.log(`tiles-cache: done. downloaded=${downloaded} skipped=${skipped} failed=${failed}`);
process.exit(failed === 0 ? 0 : 4);

const MOVIES = { from: 0.02, to: 0.36, text: "Converting the movies" };
const PHASES = {
  database: { from: 0.36, to: 0.42, text: "Reading the game's data" },
  maps: { from: 0.42, to: 0.5, text: "Converting maps and events" },
  images: { from: 0.5, to: 0.88, text: "Scaling the artwork for the PSP screen" },
  audio: { from: 0.88, to: 0.9, text: "Copying music and sounds" },
  movies: { from: 0.9, to: 0.91, text: "Adding the movies" },
  fonts: { from: 0.91, to: 0.98, text: "Rendering the fonts" },
  eboot: { from: 0.98, to: 0.99, text: "Writing EBOOT.PBP" },
};
const MAX_MOVIE_WORKERS = 8;
const MOVIE_FILE = /(^|\/)www\/movies\/([^/]+\.webm)$/i;

function timeLeft(started, fraction) {
  const elapsed = (performance.now() - started) / 1000;
  if (fraction < 0.04 || elapsed < 8) return "";
  const seconds = (elapsed * (1 - fraction)) / fraction;
  if (seconds < 50) return ", less than a minute left";
  const minutes = Math.round(seconds / 60);
  return minutes === 1 ? ", about a minute left" : `, about ${minutes} minutes left`;
}

const EBML_SEGMENT = 0x18538067;
const EBML_INFO = 0x1549a966;
const EBML_CLUSTER = 0x1f43b675;
const EBML_TIMECODE_SCALE = 0x2ad7b1;
const EBML_DURATION = 0x4489;
const HEADER_BYTES = 65536;

function readVint(bytes, at, keepMarker) {
  const first = bytes[at];
  if (first === undefined || first === 0) return null;
  const length = Math.clz32(first) - 23;
  if (at + length > bytes.length) return null;
  let value = keepMarker ? first : first & (0xff >> length);
  let unknown = value === (0xff >> length);
  for (let i = 1; i < length; i++) {
    value = value * 256 + bytes[at + i];
    unknown &&= bytes[at + i] === 0xff;
  }
  return { value, length, unknown };
}

function readUnsigned(bytes, at, size) {
  let value = 0;
  for (let i = 0; i < size; i++) value = value * 256 + bytes[at + i];
  return value;
}

function webmSeconds(bytes) {
  let at = 0;
  let scale = 1000000;
  let duration = null;
  const ends = [bytes.length];
  while (at < bytes.length) {
    const id = readVint(bytes, at, true);
    const size = id && readVint(bytes, at + id.length, false);
    if (!id || !size) break;
    const body = at + id.length + size.length;
    if (id.value === EBML_CLUSTER) break;
    if (id.value === EBML_SEGMENT || id.value === EBML_INFO) {
      at = body;
      continue;
    }
    if (id.value === EBML_TIMECODE_SCALE) scale = readUnsigned(bytes, body, size.value);
    if (id.value === EBML_DURATION && body + size.value <= bytes.length) {
      const view = new DataView(bytes.buffer, bytes.byteOffset + body, size.value);
      duration = size.value === 4 ? view.getFloat32(0) : view.getFloat64(0);
    }
    if (size.unknown) break;
    at = body + size.value;
  }
  return duration && duration > 0 ? (duration * scale) / 1e9 : null;
}

async function movieWeight(blob) {
  try {
    const seconds = webmSeconds(new Uint8Array(await blob.slice(0, HEADER_BYTES).arrayBuffer()));
    if (seconds) return seconds;
  } catch {}
  return blob.size / 200000;
}

function movieCount() {
  return Math.max(1, Math.min(MAX_MOVIE_WORKERS, navigator.hardwareConcurrency || 4));
}

export class Builder {
  constructor() {
    this.worker = null;
    this.module = null;
  }

  compile() {
    this.module ??= WebAssembly.compileStreaming(fetch(new URL("../omori-builder.wasm", import.meta.url), { cache: "no-cache" })).catch((error) => {
      this.module = null;
      throw error;
    });
    return this.module;
  }

  async run({ gameFiles, env, onProgress, onLine }) {
    const started = performance.now();
    const lines = [];
    const log = (line) => {
      lines.push(line);
      onLine(line);
    };
    let module;
    try {
      module = await this.compile();
    } catch (error) {
      return { ok: false, crashed: true, message: String(error?.message ?? error), lines };
    }
    const movies = await this.convertMovies({ module, gameFiles, env, onProgress, log });
    if (movies.crackReason) return { ok: false, code: 3, crackReason: movies.crackReason, lines };
    const result = await this.build({ module, gameFiles, env, movies: movies.files, onProgress, log, lines });
    return { ...result, seconds: (performance.now() - started) / 1000 };
  }

  async convertMovies({ module, gameFiles, env, onProgress, log }) {
    const found = gameFiles
      .map((file) => ({ file, name: file.blob && file.path.match(MOVIE_FILE)?.[2] }))
      .filter((movie) => movie.name);
    if (found.length === 0) return { files: [] };
    for (const movie of found) movie.weight = await movieWeight(movie.file.blob);
    const queue = found.sort((a, b) => b.weight - a.weight);

    const count = queue.length;
    const totalWeight = queue.reduce((sum, movie) => sum + movie.weight, 0) || 1;
    const progress = new Map(queue.map((movie) => [movie, 0]));
    const files = [];
    const workers = new Set();
    const started = performance.now();
    let finished = 0;
    let crackReason = null;

    const report = () => {
      let fraction = 0;
      for (const [movie, done] of progress) fraction += (movie.weight / totalWeight) * done;
      onProgress(
        MOVIES.from + (MOVIES.to - MOVIES.from) * fraction,
        `${MOVIES.text}: ${finished} of ${count}${timeLeft(started, fraction)}`,
      );
    };

    const convert = (worker, movie) =>
      new Promise((resolve) => {
        let cracked = null;
        worker.onmessage = ({ data }) => {
          if (data.type === "line") {
            const frames = data.line.trim().match(/^frames (\d+)\/(\d+)$/);
            if (frames) {
              progress.set(movie, Number(frames[1]) / Number(frames[2]));
              report();
              return;
            }
            const found = data.line.match(/looks cracked: (.*)$/);
            if (found) cracked = found[1];
            else if (/^\s+movies\//i.test(data.line) || /error/i.test(data.line)) log(data.line);
            return;
          }
          if (data.type === "crash") log(`${movie.name}: ${data.message.split("\n")[0]}`);
          if (data.type === "done" && data.code === 0) files.push(...data.files);
          if (data.type === "done" && data.code === 3 && cracked) crackReason ??= cracked;
          resolve();
        };
        worker.postMessage({ module, gameFiles, env, movie: movie.name });
      });

    const lane = async () => {
      const worker = new Worker(new URL("./worker.js", import.meta.url), { type: "module" });
      workers.add(worker);
      while (queue.length > 0 && !crackReason) {
        const movie = queue.shift();
        await convert(worker, movie);
        if (crackReason) break;
        progress.set(movie, 1);
        finished++;
        report();
      }
      worker.terminate();
      workers.delete(worker);
    };

    log("movies");
    report();
    await Promise.all(Array.from({ length: Math.min(movieCount(), queue.length) }, lane));
    for (const worker of workers) worker.terminate();
    if (crackReason) return { crackReason };
    if (files.length === 0 && count > 0) log("  the movies will be converted in the main build instead");
    return { files };
  }

  build({ module, gameFiles, env, movies, onProgress, log, lines }) {
    this.worker ??= new Worker(new URL("./worker.js", import.meta.url), { type: "module" });

    return new Promise((resolve) => {
      let phase = PHASES.database;
      let phaseStarted = performance.now();

      this.worker.onmessage = ({ data }) => {
        if (data.type === "line") {
          log(data.line);

          const word = data.line.trim();
          if (PHASES[word]) {
            phase = PHASES[word];
            phaseStarted = performance.now();
            onProgress(phase.from, `${phase.text}...`);
          }

          const step = word.match(/^progress (\d+)\/(\d+)$/);
          if (step && (phase === PHASES.maps || phase === PHASES.images || phase === PHASES.movies)) {
            const fraction = Number(step[1]) / Number(step[2]);
            onProgress(
              phase.from + (phase.to - phase.from) * fraction,
              `${phase.text}: ${step[1]} of ${step[2]}${timeLeft(phaseStarted, fraction)}`,
            );
          }
          return;
        }

        if (data.type === "crash") {
          this.worker.terminate();
          this.worker = null;
          resolve({ ok: false, crashed: true, message: data.message.split("\n")[0], lines });
          return;
        }

        const cracked = lines.map((line) => line.match(/looks cracked: (.*)$/)).find(Boolean);
        resolve({
          ok: data.code === 0 && Boolean(data.output),
          code: data.code,
          seconds: data.seconds,
          output: data.output ? { name: data.output, size: data.size, crc: data.crc } : null,
          crackReason: data.code === 3 && cracked ? cracked[1] : null,
          lines,
        });
      };

      this.worker.postMessage({ module, gameFiles, env, movies });
    });
  }
}

const MOVIES = { from: 0.02, to: 0.36, text: "Converting the movies" };
const PHASES = {
  database: { from: 0.36, to: 0.42, text: "Reading the game's data" },
  maps: { from: 0.42, to: 0.5, text: "Converting maps and events" },
  images: {
    from: 0.5,
    to: 0.88,
    text: { psp: "Scaling the artwork for the PSP screen", vita: "Converting the artwork", "3ds": "Scaling the artwork for the 3DS screen", wii: "Converting the artwork", ps3: "Converting the artwork", ps2: "Scaling the artwork for the PS2 screen", xbox360: "Converting the artwork" },
  },
  audio: {
    from: 0.88,
    to: 0.9,
    text: { psp: "Copying music and sounds", vita: "Copying music and sounds", "3ds": "Copying music and sounds", wii: "Copying music and sounds", ps3: "Copying music and sounds", ps2: "Converting music and sounds", xbox360: "Copying music and sounds" },
  },
  movies: { from: 0.9, to: 0.91, text: "Adding the movies" },
  fonts: { from: 0.91, to: 0.98, text: "Rendering the fonts" },
  write: { from: 0.98, to: 0.985, text: { psp: "Writing EBOOT.PBP", vita: "Writing the game data", "3ds": "Writing omori.3dsx", wii: "Writing the game data", ps3: "Writing OMORI.pkg", ps2: "Writing OMORI.iso", xbox360: "Writing the game data" } },
  finish: { from: 0.985, to: 0.99, text: { psp: "Finishing up", vita: "Writing OMORI.vpk", "3ds": "Finishing up", wii: "Writing boot.dol", ps3: "Sealing OMORI.pkg", ps2: "Finishing up", xbox360: "Writing xenon.elf" } },
};
const SEALED = { ps3: 0.12 };
const SPANS = { ps2: { images: { from: 0.5, to: 0.76 }, audio: { from: 0.76, to: 0.9 } } };

function phaseText(phase, target) {
  return typeof phase.text === "string" ? phase.text : phase.text[target];
}

function phaseRange(phase, target) {
  const key = Object.keys(PHASES).find((name) => PHASES[name] === phase);
  const span = SPANS[target]?.[key] ?? phase;
  const seal = SEALED[target];
  if (!seal) return span;
  const end = PHASES.finish.from - seal;
  if (phase === PHASES.finish) return { from: end, to: PHASES.finish.to };
  const squeeze = (value) => MOVIES.to + ((value - MOVIES.to) * (end - MOVIES.to)) / (PHASES.finish.from - MOVIES.to);
  return { from: squeeze(span.from), to: squeeze(span.to) };
}
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

const BUILDER_FILE = "omori-builder.wasm";
const ENGINES = {
  psp: { console: "PSP", files: ["EBOOT.PBP"] },
  vita: { console: "PS Vita", files: ["eboot.bin", "param.sfo"] },
  "3ds": { console: "3DS", files: ["omori.3dsx"] },
  wii: { console: "Wii", files: ["omori.dol"] },
  ps3: { console: "PS3", files: ["EBOOT.BIN"] },
  ps2: { console: "PS2", files: ["OMORI.ELF"] },
  xbox360: { console: "Xbox 360", files: ["xenon.elf"] },
};

function siteFile(path) {
  return new URL(`../${path}`, import.meta.url);
}

async function fetchFile(path, onBytes) {
  const response = await fetch(siteFile(path), { cache: "no-cache" });
  if (!response.ok) throw new Error(`${path} answered ${response.status}`);
  const reader = response.body.getReader();
  const chunks = [];
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    chunks.push(value);
    onBytes(value.length);
  }
  return new Blob(chunks);
}

function megabytes(bytes) {
  return (bytes / 1048576).toFixed(1);
}

export class Builder {
  constructor() {
    this.worker = null;
    this.module = null;
    this.sizes = null;
    this.engines = new Map();
  }

  fileSizes() {
    this.sizes ??= fetch(siteFile("files.json"), { cache: "no-cache" })
      .then((response) => {
        if (!response.ok) throw new Error(`files.json answered ${response.status}`);
        return response.json();
      })
      .catch((error) => {
        this.sizes = null;
        throw error;
      });
    return this.sizes;
  }

  async prepare(target, onProgress) {
    const { console: name, files } = ENGINES[target];
    const enginePaths = files.map((file) => `engines/${target}/${file}`);
    const paths = enginePaths.filter((path) => !this.engines.has(path));
    if (!this.module) paths.unshift(BUILDER_FILE);
    if (paths.length > 0) {
      const sizes = await this.fileSizes();
      const total = paths.reduce((sum, path) => sum + sizes[path], 0);
      const what = this.module ? `the ${name} engine` : `the builder and the ${name} engine`;
      let received = 0;
      const report = () => onProgress(received / total, `Downloading ${what}: ${megabytes(Math.min(received, total))} of ${megabytes(total)} MB`);
      report();
      const blobs = await Promise.all(
        paths.map((path) =>
          fetchFile(path, (bytes) => {
            received += bytes;
            report();
          }),
        ),
      );
      paths.forEach((path, index) => {
        if (path === BUILDER_FILE) return;
        this.engines.set(path, blobs[index]);
      });
      if (!this.module) {
        onProgress(1, "Getting ready...");
        try {
          this.module = await WebAssembly.compile(await blobs[0].arrayBuffer());
        } catch (error) {
          throw Object.assign(new Error(String(error?.message ?? error)), { compileFailed: true });
        }
      }
    }
    return { module: this.module, engine: files.map((file, index) => ({ name: file, blob: this.engines.get(enginePaths[index]) })) };
  }

  async run({ gameFiles, env, target, onProgress, onLine }) {
    const started = performance.now();
    const lines = [];
    const log = (line) => {
      lines.push(line);
      onLine(line);
    };
    let prepared;
    try {
      prepared = await this.prepare(target, onProgress);
    } catch (error) {
      const message = String(error?.message ?? error);
      return error?.compileFailed ? { ok: false, crashed: true, message, lines } : { ok: false, downloadFailed: true, message, lines };
    }
    const { module, engine } = prepared;
    const movies = await this.convertMovies({ module, gameFiles, env, target, onProgress, log });
    if (movies.crackReason) return { ok: false, code: 3, crackReason: movies.crackReason, lines };
    const result = await this.build({ module, gameFiles, env, target, engine, movies: movies.files, onProgress, log, lines });
    return { ...result, seconds: (performance.now() - started) / 1000 };
  }

  async convertMovies({ module, gameFiles, env, target, onProgress, log }) {
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
        worker.postMessage({ module, gameFiles, env, target, movie: movie.name });
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

  build({ module, gameFiles, env, target, engine, movies, onProgress, log, lines }) {
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
            onProgress(phaseRange(phase, target).from, `${phaseText(phase, target)}...`);
          }

          const step = word.match(/^progress (\d+)\/(\d+)$/);
          if (step && (phase === PHASES.maps || phase === PHASES.images || phase === PHASES.audio || phase === PHASES.movies || phase === PHASES.finish)) {
            const fraction = Number(step[1]) / Number(step[2]);
            const range = phaseRange(phase, target);
            const count = phase === PHASES.finish ? `${step[1]}%` : `${step[1]} of ${step[2]}`;
            onProgress(range.from + (range.to - range.from) * fraction, `${phaseText(phase, target)}: ${count}${timeLeft(phaseStarted, fraction)}`);
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
          output: data.output ? { name: data.output, path: data.path, size: data.size, crc: data.crc, extras: data.extras } : null,
          crackReason: data.code === 3 && cracked ? cracked[1] : null,
          lines,
        });
      };

      this.worker.postMessage({ module, gameFiles, env, target, engine, movies });
    });
  }
}

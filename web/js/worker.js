import { MemoryDirectory, SyncAccessFile, WasiProcess } from "./wasi.js";

const OUTPUT_NAME = "EBOOT.PBP";
const MOVIES_DIR = "movies-done";

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(handle, size) {
  const chunk = new Uint8Array(4 << 20);
  let crc = 0xffffffff;
  for (let at = 0; at < size; ) {
    const got = handle.read(chunk, { at });
    if (got <= 0) break;
    for (let i = 0; i < got; i++) crc = CRC_TABLE[(crc ^ chunk[i]) & 0xff] ^ (crc >>> 8);
    at += got;
  }
  return (crc ^ 0xffffffff) >>> 0;
}

async function openOutput() {
  const root = await navigator.storage.getDirectory();
  const file = await root.getFileHandle(OUTPUT_NAME, { create: true });
  const handle = await file.createSyncAccessHandle();
  handle.truncate(0);
  return handle;
}

function gameDirectory(gameFiles) {
  const root = new MemoryDirectory();
  const game = root.makeDirectories(["game"]);
  root.makeDirectories(["out"]);
  for (const file of gameFiles) game.addFile(file.path, file.blob);
  return root;
}

function collectFiles(directory, prefix, out) {
  for (const [name, node] of directory.children) {
    const path = prefix ? `${prefix}/${name}` : name;
    if (node.isDirectory) collectFiles(node, path, out);
    else out.push({ path, blob: new Blob([node.contents()]) });
  }
  return out;
}

function wasiProcess(args, env, root) {
  return new WasiProcess({ args, env, root, onLine: (line) => self.postMessage({ type: "line", line }) });
}

function convertMovie({ module, gameFiles, env, movie }) {
  const root = gameDirectory(gameFiles);
  const code = wasiProcess(["omori-builder", "/game", "--movie", movie, "-o", "/out"], env, root).run(module);
  const files = code === 0 ? collectFiles(root.find(["out"]), "", []) : [];
  self.postMessage({ type: "done", code, files });
}

async function build({ module, gameFiles, env, movies }) {
  const output = await openOutput();
  try {
    const root = gameDirectory(gameFiles);
    root.addNode(`out/${OUTPUT_NAME}`, new SyncAccessFile(output));
    const args = ["omori-builder", "/game", "-o", `/out/${OUTPUT_NAME}`];
    if (movies?.length) {
      for (const file of movies) root.addFile(`${MOVIES_DIR}/${file.path}`, file.blob);
      args.push("--movies-from", `/${MOVIES_DIR}`);
    }

    const started = performance.now();
    const code = wasiProcess(args, env, root).run(module);
    const seconds = (performance.now() - started) / 1000;
    output.flush();
    const size = output.getSize();
    if (code !== 0 || size === 0) {
      self.postMessage({ type: "done", code, seconds });
      return;
    }
    self.postMessage({ type: "line", line: "checking the output" });
    const crc = crc32(output, size);
    self.postMessage({ type: "done", code, seconds, output: OUTPUT_NAME, size, crc });
  } finally {
    output.close();
  }
}

self.onmessage = async (event) => {
  try {
    if (event.data.movie) convertMovie(event.data);
    else await build(event.data);
  } catch (error) {
    self.postMessage({ type: "crash", message: String(error?.stack ?? error) });
  }
};

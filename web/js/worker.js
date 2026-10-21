import { MemoryDirectory, SyncAccessFile, WasiProcess } from "./wasi.js";

const OUTPUTS = {
  psp: { name: "EBOOT.PBP", path: "EBOOT.PBP", args: ["-o", "/out/EBOOT.PBP"], extras: [] },
  vita: { name: "omori.pak", path: "data/OMORI/omori.pak", args: ["--target", "vita", "-o", "/out"], extras: ["OMORI.vpk"] },
  "3ds": { name: "omori.3dsx", path: "omori.3dsx", args: ["--target", "3ds", "-o", "/out/omori.3dsx"], extras: [] },
  wii: {
    name: "omori.pak",
    path: "apps/omori/omori.pak",
    args: ["--target", "wii", "-o", "/out"],
    extras: ["apps/omori/boot.dol", "apps/omori/meta.xml", "apps/omori/icon.png"],
  },
  ps3: { name: "OMORI.pkg", path: "OMORI.pkg", args: ["--target", "ps3", "-o", "/out/OMORI.pkg"], extras: [], zipped: false },
  ps2: { name: "OMORI.iso", path: "OMORI.iso", args: ["--target", "ps2", "-o", "/out/OMORI.iso"], extras: [], zipped: false },
  xbox360: { name: "omori.pak", path: "omori/omori.pak", args: ["--target", "xbox360", "-o", "/out"], extras: ["xenon.elf"], split: true },
};
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

async function openOutput(name) {
  const root = await navigator.storage.getDirectory();
  const file = await root.getFileHandle(name, { create: true });
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

function convertMovie({ module, gameFiles, env, movie, target }) {
  const root = gameDirectory(gameFiles);
  const code = wasiProcess(["omori-builder", "/game", "--target", target, "--movie", movie, "-o", "/out"], env, root).run(module);
  const files = code === 0 ? collectFiles(root.find(["out"]), "", []) : [];
  self.postMessage({ type: "done", code, files });
}

async function build({ module, gameFiles, env, movies, target }) {
  const spec = OUTPUTS[target];
  const output = await openOutput(spec.name);
  try {
    const root = gameDirectory(gameFiles);
    root.addNode(`out/${spec.path}`, new SyncAccessFile(output));
    const args = ["omori-builder", "/game", ...spec.args];
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
    const extras = [];
    for (const name of spec.extras) {
      const node = root.find(["out", ...name.split("/")]);
      if (!node) {
        self.postMessage({ type: "done", code: 2, seconds });
        return;
      }
      extras.push({ path: name, blob: new Blob([node.contents()]) });
    }
    for (let part = 1; spec.split; part++) {
      const name = `${spec.path}.${part}`;
      const node = root.find(["out", ...name.split("/")]);
      if (!node) break;
      extras.push({ path: name, blob: new Blob([node.contents()]) });
    }
    let crc = 0;
    if (spec.zipped !== false) {
      self.postMessage({ type: "line", line: "checking the output" });
      crc = crc32(output, size);
    }
    self.postMessage({ type: "done", code, seconds, output: spec.name, path: spec.path, size, crc, extras });
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

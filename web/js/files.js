import { Unzip, UnzipInflate } from "../vendor/fflate.js";

export const STORE = "store";
export const LIST = "list";
export const IGNORE = "ignore";

const STORED_FOLDERS = new Set(["data", "maps", "img", "audio", "languages"]);
const SKIPPED_FOLDERS = new Set(["data", "img", "audio", "locales", "swiftshader", "maps", "languages", "movies", "plugins"]);
const MAX_LISTED = 20000;

export function classifyPath(path) {
  const parts = path.toLowerCase().split("/").filter(Boolean);
  const name = parts[parts.length - 1];
  const www = parts.lastIndexOf("www");

  if (www >= 0) {
    const [top, second] = parts.slice(www + 1);
    const depth = parts.length - www - 1;

    if (STORED_FOLDERS.has(top) && depth > 1) return STORE;
    if (top === "movies" && depth === 2 && name.endsWith(".webm")) return STORE;
    if (top === "fonts" && depth === 2 && name.endsWith(".ttf")) return STORE;
    if (top === "js" && depth === 2 && name === "plugins.js") return STORE;
    if (top === "js" && second === "plugins" && depth > 2) return STORE;
  }

  if (www < 0 && parts.length <= 3 && /\.(bat|cmd|sh)$/.test(name)) return STORE;
  if (/\.(ini|nfo)$/.test(name)) return STORE;
  if (/^steam(client|_api).*\.dll$/.test(name)) return STORE;
  if (parts.includes("steam_settings")) return STORE;
  if (parts.length <= 3 && /\.(txt|url)$/.test(name)) return STORE;

  if (parts.slice(0, -1).some((part) => SKIPPED_FOLDERS.has(part))) return IGNORE;
  return LIST;
}

export function sortFiles(files) {
  const stored = [];
  const listed = [];

  for (const file of files) {
    const kind = classifyPath(file.path);
    if (kind === STORE) stored.push(file);
    if (kind === LIST && listed.length < MAX_LISTED) listed.push(file.path);
  }

  return { stored, listed };
}

export function filesFromInput(fileList) {
  return [...fileList].map((file) => ({ path: file.webkitRelativePath || file.name, blob: file }));
}

function readDirectoryBatch(reader) {
  return new Promise((resolve, reject) => reader.readEntries(resolve, reject));
}

function fileOfEntry(entry) {
  return new Promise((resolve, reject) => entry.file(resolve, reject));
}

async function walk(entry, prefix, out, onProgress) {
  const path = prefix ? `${prefix}/${entry.name}` : entry.name;

  if (entry.isFile) {
    out.push({ path, blob: await fileOfEntry(entry) });
    if (out.length % 200 === 0) onProgress(out.length);
    return;
  }

  const reader = entry.createReader();
  for (;;) {
    const batch = await readDirectoryBatch(reader);
    if (batch.length === 0) break;
    for (const child of batch) await walk(child, path, out, onProgress);
  }
}

export async function filesFromDrop(items, onProgress) {
  const entries = [...items].map((item) => item.webkitGetAsEntry()).filter(Boolean);
  const out = [];
  for (const entry of entries) await walk(entry, "", out, onProgress);
  return out;
}

export function isZip(file) {
  return file.name.toLowerCase().endsWith(".zip");
}

export async function filesFromZip(zipFile, onProgress) {
  const out = [];
  const pending = [];

  const unzip = new Unzip((entry) => {
    const path = entry.name.replace(/\\/g, "/");
    if (path.endsWith("/")) return;

    const kind = classifyPath(path);
    if (kind === IGNORE) return;
    if (kind === LIST) {
      out.push({ path, blob: new Blob() });
      return;
    }

    const chunks = [];
    pending.push(
      new Promise((resolve, reject) => {
        entry.ondata = (error, chunk, final) => {
          if (error) return reject(error);
          chunks.push(chunk);
          if (final) {
            out.push({ path, blob: new Blob(chunks) });
            resolve();
          }
        };
      }),
    );
    entry.start();
  });
  unzip.register(UnzipInflate);

  const reader = zipFile.stream().getReader();
  let read = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    unzip.push(value);
    read += value.length;
    onProgress(read / zipFile.size);
  }
  unzip.push(new Uint8Array(0), true);
  await Promise.all(pending);

  return out;
}

export function installPrefix(path) {
  const match = path.match(/^(.*\/)?www\//i);
  return match ? match[1] ?? "" : null;
}

export function isLaunchScript(path, prefix) {
  return path.toLowerCase().startsWith(prefix.toLowerCase()) && /^[^/]+\.(bat|cmd|sh)$/i.test(path.slice(prefix.length));
}

export function missingGameParts(paths) {
  const lower = paths.map((path) => path.toLowerCase());
  const has = (pattern) => lower.some((path) => pattern.test(path));
  const required = [
    ["www/js/rpg_managers.js", /(^|\/)www\/js\/rpg_managers\.js$/],
    ["www/js/plugins.js", /(^|\/)www\/js\/plugins\.js$/],
    ["www/js/plugins", /(^|\/)www\/js\/plugins\//],
    ["www/data", /(^|\/)www\/data\/system\.kel$/],
    ["www/maps", /(^|\/)www\/maps\//],
    ["www/img", /(^|\/)www\/img\//],
    ["www/audio", /(^|\/)www\/audio\//],
    ["www/languages", /(^|\/)www\/languages\//],
    ["www/fonts/OMORI_GAME.ttf", /(^|\/)www\/fonts\/omori_game\.ttf$/],
    ["www/fonts/OMORI_GAME2.ttf", /(^|\/)www\/fonts\/omori_game2\.ttf$/],
  ];
  return required.filter(([, pattern]) => !has(pattern)).map(([name]) => name);
}

function isKeyChar(byte) {
  return (byte >= 0x30 && byte <= 0x39) || (byte >= 0x61 && byte <= 0x66);
}

export function keysIn(bytes) {
  const keys = new Set();
  for (let i = 0; i + 34 <= bytes.length; i++) {
    if (bytes[i] !== 0x2d || bytes[i + 1] !== 0x2d) continue;
    let length = 0;
    while (length < 32 && isKeyChar(bytes[i + 2 + length])) length++;
    if (length < 32 || isKeyChar(bytes[i + 34])) continue;
    keys.add(String.fromCharCode(...bytes.subarray(i + 2, i + 34)));
  }
  return keys;
}

async function opensSystem(key, system) {
  const plain = await decrypt(key, system);
  if (!plain || plain[0] !== 0x7b) return false;
  try {
    JSON.parse(new TextDecoder().decode(plain));
    return true;
  } catch {
    return false;
  }
}

export async function findGameKey(files, extraSources = []) {
  const system = files.find((file) => /(^|\/)www\/data\/system\.kel$/i.test(file.path));
  if (!system) return null;
  const prefix = installPrefix(system.path);
  const systemBytes = new Uint8Array(await system.blob.arrayBuffer());
  const sources = [...files.filter((file) => isLaunchScript(file.path, prefix)).map((file) => file.blob), ...extraSources];
  for (const source of sources) {
    for (const key of keysIn(new Uint8Array(await source.arrayBuffer()))) {
      if (await opensSystem(key, systemBytes)) return key;
    }
  }
  return null;
}

export async function decrypt(key, bytes) {
  if (bytes.length < 16) return null;
  const cryptoKey = await crypto.subtle.importKey("raw", new TextEncoder().encode(key), "AES-CTR", false, ["decrypt"]);
  const plain = await crypto.subtle.decrypt({ name: "AES-CTR", counter: bytes.slice(0, 16), length: 128 }, cryptoKey, bytes.slice(16));
  return new Uint8Array(plain);
}

export async function readGameVersion(files, key) {
  const title = files.find((file) => /(^|\/)www\/js\/plugins\/omori title screen\.omori$/i.test(file.path));
  if (!title) return "unknown";
  const plain = await decrypt(key, new Uint8Array(await title.blob.arrayBuffer()));
  const match = plain && new TextDecoder().decode(plain).match(/drawText\("V([^"]{1,15})"/);
  return match ? match[1] : "unknown";
}

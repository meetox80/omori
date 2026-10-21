import { decrypt } from "./files.js";

const MV_HEADER = 16;

function indexByGamePath(files) {
  const index = new Map();
  for (const file of files) {
    const match = file.path.match(/(?:^|\/)www\/(.+)$/i);
    if (match && !index.has(match[1].toLowerCase())) index.set(match[1].toLowerCase(), file);
  }
  return index;
}

function hexBytes(hex) {
  const bytes = new Uint8Array(hex.length / 2);
  for (let i = 0; i < bytes.length; i++) bytes[i] = parseInt(hex.substr(i * 2, 2), 16);
  return bytes;
}

function unquoteYaml(value) {
  const text = value.trim();
  if (!text.startsWith('"')) return text;
  try {
    return JSON.parse(text);
  } catch {
    return text.slice(1, -1).replace(/\\\\/g, "\\").replace(/\\"/g, '"');
  }
}

export function parseMessages(text) {
  const messages = new Map();
  let current = null;
  for (const line of text.split(/\r?\n/)) {
    const name = line.match(/^([A-Za-z0-9_]+):\s*$/);
    if (name) {
      current = name[1];
      continue;
    }
    const value = line.match(/^\s+text:\s*(.*)$/);
    if (value && current) messages.set(current, unquoteYaml(value[1]));
  }
  return messages;
}

export async function openGameAssets(files, key) {
  const index = indexByGamePath(files);
  const urls = [];

  const bytesOf = async (path) => {
    const file = index.get(path.toLowerCase());
    return file ? new Uint8Array(await file.blob.arrayBuffer()) : null;
  };

  const readData = async (encryptedPath, plainPath) => {
    const encrypted = await bytesOf(encryptedPath);
    if (encrypted && key) {
      const plain = await decrypt(key, encrypted);
      if (plain) return new TextDecoder().decode(plain);
    }
    const plain = plainPath ? await bytesOf(plainPath) : null;
    return plain ? new TextDecoder().decode(plain) : null;
  };

  const systemText = await readData("data/System.KEL", "data/System.json");
  if (!systemText) return null;
  const system = JSON.parse(systemText);
  const mvKey = system.encryptionKey ? hexBytes(system.encryptionKey) : null;

  const readMedia = async (path, encryptedExtension, plainExtension, type) => {
    const encrypted = mvKey ? await bytesOf(`${path}.${encryptedExtension}`) : null;
    if (encrypted && encrypted.length > MV_HEADER) {
      const data = encrypted.slice(MV_HEADER);
      for (let i = 0; i < MV_HEADER; i++) data[i] ^= mvKey[i];
      return new Blob([data], { type });
    }
    const plain = await bytesOf(`${path}.${plainExtension}`);
    return plain ? new Blob([plain], { type }) : null;
  };

  return {
    async image(path) {
      const blob = await readMedia(path, "rpgmvp", "png", "image/png");
      if (!blob) throw new Error(`missing ${path}`);
      const bitmap = await createImageBitmap(blob);
      const url = URL.createObjectURL(blob);
      urls.push(url);
      return { url, bitmap, width: bitmap.width, height: bitmap.height };
    },

    async sound(path) {
      return readMedia(path, "rpgmvo", "ogg", "audio/ogg");
    },

    async font(path) {
      const bytes = await bytesOf(path);
      if (!bytes) throw new Error(`missing ${path}`);
      return bytes.buffer;
    },

    async messages(path) {
      const text = await readData(`${path}.HERO`, `${path}.yaml`);
      return text ? parseMessages(text) : new Map();
    },

    release() {
      for (const url of urls) URL.revokeObjectURL(url);
      urls.length = 0;
    },
  };
}

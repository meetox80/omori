const PACK_NAME = "game.pack";
const INDEX_NAME = "game.json";

async function storageRoot() {
  if (!navigator.storage?.getDirectory) return null;
  try {
    return await navigator.storage.getDirectory();
  } catch {
    return null;
  }
}

async function writeText(root, name, text) {
  const handle = await root.getFileHandle(name, { create: true });
  const writable = await handle.createWritable();
  await writable.write(text);
  await writable.close();
}

export async function saveGame(files, listed, version, key, onProgress) {
  const root = await storageRoot();
  if (!root) return null;

  const totalBytes = files.reduce((sum, file) => sum + file.blob.size, 0) || 1;
  const entries = [];
  let offset = 0;

  try {
    const handle = await root.getFileHandle(PACK_NAME, { create: true });
    const writable = await handle.createWritable();
    for (const file of files) {
      await writable.write(file.blob);
      entries.push({ path: file.path, offset, size: file.blob.size });
      offset += file.blob.size;
      onProgress(offset / totalBytes);
    }
    await writable.close();

    const index = { version, key, savedAt: new Date().toISOString(), entries, listed };
    await writeText(root, INDEX_NAME, JSON.stringify(index));
    navigator.storage.persist?.().catch(() => {});
    return index;
  } catch {
    await clearGame();
    return null;
  }
}

export async function loadGame() {
  const root = await storageRoot();
  if (!root) return null;

  try {
    const indexFile = await (await root.getFileHandle(INDEX_NAME)).getFile();
    const index = JSON.parse(await indexFile.text());
    const pack = await (await root.getFileHandle(PACK_NAME)).getFile();
    const files = index.entries.map((entry) => ({
      path: entry.path,
      blob: pack.slice(entry.offset, entry.offset + entry.size),
    }));
    return { index, files };
  } catch {
    return null;
  }
}

export async function clearGame() {
  const root = await storageRoot();
  if (!root) return;
  for (const name of [PACK_NAME, INDEX_NAME, "game", "manifest.json"]) {
    try {
      await root.removeEntry(name, { recursive: true });
    } catch {}
  }
}

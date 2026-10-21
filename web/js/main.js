import { Builder } from "./build.js";
import { showCrackWarning } from "./crack-warning.js";
import { filesFromDrop, filesFromInput, filesFromZip, findGameKey, isZip, missingGameParts, readGameVersion, sortFiles } from "./files.js";
import { clearGame, loadGame, saveGame } from "./storage.js";
import { Theme } from "./theme.js";

const SUPPORTED_VERSION = "1.0.8";

const ui = Object.fromEntries(
  [
    "drop", "pick-folder", "pick-zip", "game-empty", "game-key", "key-drop", "pick-vdf", "cancel-key", "game-ready", "game-summary", "game-error", "replace-game", "remove-game",
    "import-status", "import-bar", "import-text", "build", "build-status", "build-bar", "build-text",
    "log-box", "log", "build-error", "step-done", "download-zip", "download-eboot", "download-vita", "download-vpk", "download-3ds", "download-3dsx", "download-wii", "download-ps3", "download-ps2", "download-xbox360",
  ].map((id) => [id.replace(/-(\w)/g, (_, c) => c.toUpperCase()), document.getElementById(id)]),
);

const CONSOLES = { psp: "OMORI for PSP", vita: "OMORI for PS Vita", "3ds": "OMORI for 3DS", wii: "OMORI for Wii", ps3: "OMORI for PS3", ps2: "OMORI for PS2", xbox360: "OMORI for Xbox 360" };
const CONSOLE_KEY = "omori-console";

const state = {
  game: null,
  pending: null,
  output: null,
  building: false,
  target: "psp",
};

const builder = new Builder();
const theme = new Theme();

function showProgress(status, bar, label, fraction, text, running) {
  status.hidden = false;
  bar.style.width = `${Math.round(Math.min(1, Math.max(0, fraction)) * 100)}%`;
  label.textContent = text;
  theme.progress(status, fraction, running);
}

function showImportProgress(fraction, text) {
  showProgress(ui.importStatus, ui.importBar, ui.importText, fraction, text, true);
}

function hideImportProgress() {
  ui.importStatus.hidden = true;
  theme.progress(ui.importStatus, 0, false);
}

function showBuildProgress(fraction, text, running = true) {
  showProgress(ui.buildStatus, ui.buildBar, ui.buildText, fraction, text, running);
}

function setError(element, message) {
  element.hidden = !message;
  element.textContent = message ?? "";
}

function savedConsole() {
  try {
    const value = localStorage.getItem(CONSOLE_KEY);
    return CONSOLES[value] ? value : "psp";
  } catch {
    return "psp";
  }
}

function renderConsole() {
  for (const element of document.querySelectorAll("[data-console]")) element.hidden = element.dataset.console !== state.target;
  for (const input of document.querySelectorAll('input[name="console"]')) input.checked = input.value === state.target;
  document.title = CONSOLES[state.target];
  if (state.output && state.output.target !== state.target) ui.stepDone.hidden = true;
}

function chooseConsole(target) {
  state.target = target;
  try {
    localStorage.setItem(CONSOLE_KEY, target);
  } catch {}
  renderConsole();
}

function renderGame() {
  const game = state.game;
  ui.gameEmpty.hidden = Boolean(game) || Boolean(state.pending);
  ui.gameKey.hidden = !state.pending;
  ui.gameReady.hidden = !game || Boolean(state.pending);
  ui.build.disabled = !game || state.building;
  if (!game) {
    theme.sleep();
    return;
  }
  theme.wake(game.files, game.key);

  const where = game.stored ? "" : " (only for this visit; this browser can't keep it)";
  ui.gameSummary.textContent = `Game ready: version ${game.version}, ${game.files.length} files${where}`;
  setError(
    ui.gameError,
    game.version === SUPPORTED_VERSION ? "" : `This port is made for version ${SUPPORTED_VERSION} and your copy is ${game.version}, so the build will most likely refuse it.`,
  );
}

async function keepGame(stored, listed, key) {
  const version = await readGameVersion(stored, key);
  const megabytes = Math.round(stored.reduce((sum, file) => sum + file.blob.size, 0) / 1048576);
  const index = await saveGame(stored, listed, version, key, (fraction) =>
    showImportProgress(fraction, `Saving the game in this browser: ${Math.round(fraction * megabytes)} of ${megabytes} MB`),
  );

  state.pending = null;
  state.game = { version, key, files: stored, listed, stored: Boolean(index) };
  hideImportProgress();
  renderGame();
}

async function importGame(readFiles) {
  setError(ui.gameError, "");
  state.pending = null;
  showImportProgress(0, "Reading your files...");
  try {
    const { stored, listed } = sortFiles(await readFiles());
    const missing = missingGameParts([...stored.map((file) => file.path), ...listed]);
    if (missing.length > 0) throw new Error(`This doesn't look like the game folder. Missing: ${missing.join(", ")}.`);

    const key = await findGameKey(stored);
    if (key) {
      await keepGame(stored, listed, key);
      return;
    }
    state.pending = { stored, listed };
    hideImportProgress();
    renderGame();
  } catch (error) {
    hideImportProgress();
    setError(ui.gameError, error.message ?? String(error));
  }
}

async function importSteamKey(file) {
  setError(ui.gameError, "");
  showImportProgress(0, `Looking for the key in ${file.name}...`);
  try {
    const { stored, listed } = state.pending;
    const key = await findGameKey(stored, [file]);
    if (!key) throw new Error(`OMORI's key isn't in ${file.name}. Pick the appinfo.vdf of the Steam that OMORI is installed with.`);
    await keepGame(stored, listed, key);
  } catch (error) {
    hideImportProgress();
    setError(ui.gameError, error.message ?? String(error));
  }
}

function gameFilesForBuild() {
  const listed = state.game.listed.map((path) => ({ path, blob: null }));
  return [...state.game.files, ...listed];
}

async function runBuild(env) {
  ui.log.textContent = "";
  ui.logBox.hidden = false;
  setError(ui.buildError, "");
  showBuildProgress(0, "Getting ready...");

  return builder.run({
    gameFiles: gameFilesForBuild(),
    env: { OMORI_KEY: state.game.key, ...env },
    target: state.target,
    onProgress: showBuildProgress,
    onLine: (line) => {
      ui.log.textContent += `${line}\n`;
    },
  });
}

function finishBuild(result) {
  if (result.ok) {
    state.output = { ...result.output, target: state.target };
    const megabytes = (result.output.size / 1048576).toFixed(1);
    showBuildProgress(1, `Done in ${Math.round(result.seconds)} seconds: ${megabytes} MB`, false);
    ui.stepDone.hidden = false;
    theme.celebrate();
    ui.stepDone.scrollIntoView({ behavior: "smooth", block: "start" });
    return;
  }

  showBuildProgress(0, "Stopped.", false);
  if (result.crashed) {
    setError(ui.buildError, `The build crashed in this browser. Please try a current Chrome, Edge or Firefox. (${result.message})`);
    return;
  }
  const reason = result.lines.filter((line) => /error/i.test(line)).pop() ?? result.lines.at(-1) ?? "";
  setError(ui.buildError, `The build failed. ${reason}`);
}

async function startBuild() {
  state.building = true;
  ui.build.disabled = true;
  for (const input of document.querySelectorAll('input[name="console"]')) input.disabled = true;
  ui.stepDone.hidden = true;

  try {
    let result = await runBuild({});
    if (result.crackReason) {
      theme.warn();
      const accepted = await showCrackWarning(result.crackReason);
      if (!accepted) {
        showBuildProgress(0, "Stopped. Buy the game on Steam, then build again.", false);
        return;
      }
      result = await runBuild({ OMORI_CRACK_OK: "1" });
    }
    finishBuild(result);
  } catch (error) {
    showBuildProgress(0, "Stopped.", false);
    setError(ui.buildError, `Couldn't read the stored game files. Try choosing the game again. (${error.message ?? error})`);
  } finally {
    state.building = false;
    ui.build.disabled = !state.game;
    for (const input of document.querySelectorAll('input[name="console"]')) input.disabled = false;
  }
}

function download(blob, name) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 60000);
}

async function outputFile() {
  const root = await navigator.storage.getDirectory();
  return (await root.getFileHandle(state.output.name)).getFile();
}

async function downloadEboot() {
  download(await outputFile(), "EBOOT.PBP");
}

function zipRecord(signature, fields) {
  const size = fields.reduce((sum, [bytes]) => sum + bytes, 4);
  const view = new DataView(new ArrayBuffer(size));
  view.setUint32(0, signature, true);
  let at = 4;
  for (const [bytes, value] of fields) {
    if (bytes === 2) view.setUint16(at, value, true);
    else view.setUint32(at, value, true);
    at += bytes;
  }
  return new Uint8Array(view.buffer);
}

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

async function blobCrc(blob) {
  const bytes = new Uint8Array(await blob.arrayBuffer());
  let crc = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) crc = CRC_TABLE[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function storedZip(entries) {
  const now = new Date();
  const time = (now.getHours() << 11) | (now.getMinutes() << 5) | (now.getSeconds() >> 1);
  const date = ((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate();
  const parts = [];
  const centrals = [];
  let offset = 0;
  for (const { path, blob, crc } of entries) {
    const name = new TextEncoder().encode(path);
    const size = blob.size;
    const local = zipRecord(0x04034b50, [[2, 20], [2, 0], [2, 0], [2, time], [2, date], [4, crc], [4, size], [4, size], [2, name.length], [2, 0]]);
    const central = zipRecord(0x02014b50, [
      [2, 20], [2, 20], [2, 0], [2, 0], [2, time], [2, date], [4, crc], [4, size], [4, size],
      [2, name.length], [2, 0], [2, 0], [2, 0], [2, 0], [4, 0], [4, offset],
    ]);
    parts.push(local, name, blob);
    centrals.push(central, name);
    offset += local.length + name.length + size;
  }
  const centralSize = centrals.reduce((sum, part) => sum + part.length, 0);
  const end = zipRecord(0x06054b50, [[2, 0], [2, 0], [2, entries.length], [2, entries.length], [4, centralSize], [4, offset], [2, 0]]);
  return new Blob([...parts, ...centrals, end], { type: "application/zip" });
}

async function downloadZip() {
  const { crc } = state.output;
  download(storedZip([{ path: "PSP/GAME/OMORI/EBOOT.PBP", blob: await outputFile(), crc }]), "OMORI-PSP.zip");
}

function vpkBlob() {
  return state.output.extras.find((extra) => extra.path === "OMORI.vpk").blob;
}

async function downloadBundle(name) {
  const entries = [];
  for (const extra of state.output.extras) entries.push({ path: extra.path, blob: extra.blob, crc: await blobCrc(extra.blob) });
  entries.push({ path: state.output.path, blob: await outputFile(), crc: state.output.crc });
  download(storedZip(entries), name);
}

function downloadVita() {
  return downloadBundle("OMORI-Vita.zip");
}

function downloadWii() {
  return downloadBundle("OMORI-Wii.zip");
}

function downloadXbox360() {
  return downloadBundle("OMORI-Xbox360.zip");
}

function downloadVpk() {
  download(vpkBlob(), "OMORI.vpk");
}

async function download3ds() {
  const { crc } = state.output;
  download(storedZip([{ path: "3ds/omori/omori.3dsx", blob: await outputFile(), crc }]), "OMORI-3DS.zip");
}

async function download3dsx() {
  download(await outputFile(), "omori.3dsx");
}

async function downloadPs3() {
  download(await outputFile(), "OMORI.pkg");
}

async function downloadPs2() {
  download(await outputFile(), "OMORI.iso");
}

function setUpDropZone(zone, onDrop) {
  const highlight = (on) => (event) => {
    event.preventDefault();
    zone.toggleAttribute("data-over", on);
  };
  zone.addEventListener("dragenter", highlight(true));
  zone.addEventListener("dragover", highlight(true));
  zone.addEventListener("dragleave", highlight(false));
  zone.addEventListener("drop", (event) => {
    highlight(false)(event);
    onDrop(event.dataTransfer);
  });
}

function setUpDropZones() {
  setUpDropZone(ui.drop, (transfer) => {
    const items = [...transfer.items];
    const single = transfer.files.length === 1 ? transfer.files[0] : null;
    if (single && isZip(single)) {
      importGame(() => filesFromZip(single, (fraction) => showImportProgress(fraction, `Unpacking ${single.name}: ${Math.round(fraction * 100)}%`)));
    } else {
      importGame(() => filesFromDrop(items, (count) => showImportProgress(0, `Reading folder: ${count} files found`)));
    }
  });
  setUpDropZone(ui.keyDrop, (transfer) => {
    const file = transfer.files[0];
    if (file) importSteamKey(file);
  });
}

function setUpControls() {
  ui.pickFolder.addEventListener("change", (event) => {
    const files = filesFromInput(event.target.files);
    event.target.value = "";
    if (files.length > 0) importGame(async () => files);
  });

  ui.pickZip.addEventListener("change", (event) => {
    const zip = event.target.files[0];
    event.target.value = "";
    if (zip) importGame(() => filesFromZip(zip, (fraction) => showImportProgress(fraction, `Unpacking ${zip.name}: ${Math.round(fraction * 100)}%`)));
  });

  ui.pickVdf.addEventListener("change", (event) => {
    const file = event.target.files[0];
    event.target.value = "";
    if (file) importSteamKey(file);
  });

  ui.cancelKey.addEventListener("click", () => {
    state.pending = null;
    setError(ui.gameError, "");
    renderGame();
    ui.gameEmpty.hidden = false;
    ui.gameReady.hidden = true;
  });

  ui.replaceGame.addEventListener("click", () => {
    ui.gameEmpty.hidden = false;
    ui.gameReady.hidden = true;
  });

  ui.removeGame.addEventListener("click", async () => {
    if (!confirm("Remove the stored game files from this browser? Your saves on the console are not affected.")) return;
    await clearGame();
    state.game = null;
    renderGame();
  });

  ui.build.addEventListener("click", startBuild);
  ui.downloadEboot.addEventListener("click", downloadEboot);
  ui.downloadZip.addEventListener("click", downloadZip);
  ui.downloadVita.addEventListener("click", downloadVita);
  ui.downloadVpk.addEventListener("click", downloadVpk);
  ui.download3ds.addEventListener("click", download3ds);
  ui.download3dsx.addEventListener("click", download3dsx);
  ui.downloadWii.addEventListener("click", downloadWii);
  ui.downloadPs3.addEventListener("click", downloadPs3);
  ui.downloadPs2.addEventListener("click", downloadPs2);
  ui.downloadXbox360.addEventListener("click", downloadXbox360);
  for (const input of document.querySelectorAll('input[name="console"]')) {
    input.addEventListener("change", () => {
      if (input.checked) chooseConsole(input.value);
    });
  }

  window.addEventListener("beforeunload", (event) => {
    if (state.building) event.preventDefault();
  });
}

async function restoreGame() {
  const saved = await loadGame();
  if (!saved) return null;
  const key = saved.index.key ?? (await findGameKey(saved.files));
  if (!key) return null;
  return { version: saved.index.version, key, files: saved.files, listed: saved.index.listed, stored: true };
}

async function start() {
  setUpDropZones();
  setUpControls();
  state.target = savedConsole();
  renderConsole();
  state.game = await restoreGame();
  renderGame();
}

start();

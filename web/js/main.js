import { Builder } from "./build.js";
import { showCrackWarning } from "./crack-warning.js";
import { filesFromDrop, filesFromInput, filesFromZip, isZip, missingGameParts, readGameVersion, sortFiles } from "./files.js";
import { clearGame, loadGame, saveGame } from "./storage.js";
import { Theme } from "./theme.js";

const SUPPORTED_VERSION = "1.0.8";

const ui = Object.fromEntries(
  [
    "drop", "pick-folder", "pick-zip", "game-empty", "game-ready", "game-summary", "game-error", "replace-game", "remove-game",
    "import-status", "import-bar", "import-text", "build", "build-status", "build-bar", "build-text",
    "log-box", "log", "build-error", "step-done", "download-zip", "download-eboot",
  ].map((id) => [id.replace(/-(\w)/g, (_, c) => c.toUpperCase()), document.getElementById(id)]),
);

const state = {
  game: null,
  output: null,
  building: false,
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

function renderGame() {
  const game = state.game;
  ui.gameEmpty.hidden = Boolean(game);
  ui.gameReady.hidden = !game;
  ui.build.disabled = !game || state.building;
  if (!game) {
    theme.sleep();
    return;
  }
  theme.wake(game.files);

  const where = game.stored ? "" : " (only for this visit; this browser can't keep it)";
  ui.gameSummary.textContent = `Game ready: version ${game.version}, ${game.files.length} files${where}`;
  setError(
    ui.gameError,
    game.version === SUPPORTED_VERSION ? "" : `This port is made for version ${SUPPORTED_VERSION} and your copy is ${game.version}, so the build will most likely refuse it.`,
  );
}

async function importGame(readFiles) {
  setError(ui.gameError, "");
  showImportProgress(0, "Reading your files...");
  try {
    const { stored, listed } = sortFiles(await readFiles());
    const missing = missingGameParts([...stored.map((file) => file.path), ...listed]);
    if (missing.length > 0) throw new Error(`This doesn't look like the game folder. Missing: ${missing.join(", ")}.`);

    const version = await readGameVersion(stored);
    const megabytes = Math.round(stored.reduce((sum, file) => sum + file.blob.size, 0) / 1048576);
    const index = await saveGame(stored, listed, version, (fraction) =>
      showImportProgress(fraction, `Saving the game in this browser: ${Math.round(fraction * megabytes)} of ${megabytes} MB`),
    );

    state.game = { version, files: stored, listed, stored: Boolean(index) };
    hideImportProgress();
    renderGame();
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
    env,
    onProgress: showBuildProgress,
    onLine: (line) => {
      ui.log.textContent += `${line}\n`;
    },
  });
}

function finishBuild(result) {
  if (result.ok) {
    state.output = result.output;
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

async function downloadZip() {
  const file = await outputFile();
  const name = new TextEncoder().encode("PSP/GAME/OMORI/EBOOT.PBP");
  const now = new Date();
  const time = (now.getHours() << 11) | (now.getMinutes() << 5) | (now.getSeconds() >> 1);
  const date = ((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate();
  const { crc, size } = state.output;
  const local = zipRecord(0x04034b50, [[2, 20], [2, 0], [2, 0], [2, time], [2, date], [4, crc], [4, size], [4, size], [2, name.length], [2, 0]]);
  const centralOffset = local.length + name.length + size;
  const central = zipRecord(0x02014b50, [
    [2, 20], [2, 20], [2, 0], [2, 0], [2, time], [2, date], [4, crc], [4, size], [4, size],
    [2, name.length], [2, 0], [2, 0], [2, 0], [2, 0], [4, 0], [4, 0],
  ]);
  const end = zipRecord(0x06054b50, [[2, 0], [2, 0], [2, 1], [2, 1], [4, central.length + name.length], [4, centralOffset], [2, 0]]);
  download(new Blob([local, name, file, central, name, end], { type: "application/zip" }), "OMORI-PSP.zip");
}

function setUpDropZone() {
  const highlight = (on) => (event) => {
    event.preventDefault();
    ui.drop.toggleAttribute("data-over", on);
  };
  ui.drop.addEventListener("dragenter", highlight(true));
  ui.drop.addEventListener("dragover", highlight(true));
  ui.drop.addEventListener("dragleave", highlight(false));
  ui.drop.addEventListener("drop", (event) => {
    highlight(false)(event);
    const items = [...event.dataTransfer.items];
    const single = event.dataTransfer.files.length === 1 ? event.dataTransfer.files[0] : null;
    if (single && isZip(single)) {
      importGame(() => filesFromZip(single, (fraction) => showImportProgress(fraction, `Unpacking ${single.name}: ${Math.round(fraction * 100)}%`)));
    } else {
      importGame(() => filesFromDrop(items, (count) => showImportProgress(0, `Reading folder: ${count} files found`)));
    }
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

  ui.replaceGame.addEventListener("click", () => {
    ui.gameEmpty.hidden = false;
    ui.gameReady.hidden = true;
  });

  ui.removeGame.addEventListener("click", async () => {
    if (!confirm("Remove the stored game files from this browser? Your PSP saves are not affected.")) return;
    await clearGame();
    state.game = null;
    renderGame();
  });

  ui.build.addEventListener("click", startBuild);
  ui.downloadEboot.addEventListener("click", downloadEboot);
  ui.downloadZip.addEventListener("click", downloadZip);

  window.addEventListener("beforeunload", (event) => {
    if (state.building) event.preventDefault();
  });
}

async function restoreGame() {
  const saved = await loadGame();
  if (!saved) return null;
  return { version: saved.index.version, files: saved.files, listed: saved.index.listed, stored: true };
}

async function start() {
  setUpDropZone();
  setUpControls();
  state.game = await restoreGame();
  renderGame();
}

start();

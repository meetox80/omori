const SOUND_PREF = "omori-sound";
const MUSIC_PREF = "omori-music";

function readPref(name, fallback) {
  try {
    const value = localStorage.getItem(name);
    return value === null ? fallback : value === "on";
  } catch {
    return fallback;
  }
}

function writePref(name, on) {
  try {
    localStorage.setItem(name, on ? "on" : "off");
  } catch {}
}

export class Sounds {
  constructor(assets) {
    this.assets = assets;
    this.buffers = new Map();
    this.context = null;
    this.enabled = readPref(SOUND_PREF, true);
    this.musicOn = readPref(MUSIC_PREF, false);
    this.music = null;
    this.musicPath = null;
    this.musicUrls = new Map();
    this.unlock = () => this.context?.resume().catch(() => {});
    try {
      this.context = new AudioContext();
    } catch {
      this.context = null;
    }
    window.addEventListener("pointerdown", this.unlock, true);
    window.addEventListener("keydown", this.unlock, true);
  }

  get available() {
    return Boolean(this.context);
  }

  preload(names) {
    for (const name of names) this.buffer(name);
  }

  buffer(name) {
    if (!this.buffers.has(name)) {
      this.buffers.set(
        name,
        this.assets
          .sound(name.includes("/") ? name : `audio/se/${name}`)
          .then((blob) => (blob ? blob.arrayBuffer() : null))
          .then((bytes) => (bytes && this.context ? this.context.decodeAudioData(bytes) : null))
          .catch(() => null),
      );
    }
    return this.buffers.get(name);
  }

  async play(name, { volume = 0.5, rate = 1 } = {}) {
    if (!this.enabled || !this.context || this.context.state !== "running") return;
    const buffer = await this.buffer(name);
    if (!buffer) return;
    const source = this.context.createBufferSource();
    const gain = this.context.createGain();
    source.buffer = buffer;
    source.playbackRate.value = rate;
    gain.gain.value = volume;
    source.connect(gain).connect(this.context.destination);
    source.start();
  }

  setEnabled(on) {
    this.enabled = on;
    writePref(SOUND_PREF, on);
  }

  async musicUrl(path) {
    if (!this.musicUrls.has(path)) {
      this.musicUrls.set(
        path,
        this.assets.sound(path).then((blob) => (blob ? URL.createObjectURL(blob) : null)),
      );
    }
    return this.musicUrls.get(path);
  }

  canPlayMusic() {
    return document.createElement("audio").canPlayType('audio/ogg; codecs="vorbis"') !== "";
  }

  async setTrack(path) {
    this.musicPath = path;
    await this.applyMusic();
  }

  async setMusic(on) {
    this.musicOn = on;
    writePref(MUSIC_PREF, on);
    await this.applyMusic();
  }

  async applyMusic() {
    const path = this.musicPath;
    if (!this.musicOn || !path) {
      this.music?.pause();
      return;
    }
    const url = await this.musicUrl(path);
    if (!url || path !== this.musicPath || !this.musicOn) return;
    if (!this.music) {
      this.music = new Audio();
      this.music.loop = true;
      this.music.volume = 0.45;
    }
    if (this.music.src !== url) this.music.src = url;
    this.music.play().catch(() => {});
  }

  release() {
    window.removeEventListener("pointerdown", this.unlock, true);
    window.removeEventListener("keydown", this.unlock, true);
    this.music?.pause();
    this.music = null;
    this.context?.close().catch(() => {});
    this.musicUrls.forEach((promise) => promise.then((url) => url && URL.revokeObjectURL(url)));
    this.musicUrls.clear();
  }
}

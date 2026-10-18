import { openGameAssets } from "./assets.js";
import { TitleHero } from "./hero.js";
import { MessageBox } from "./message.js";
import { Sounds } from "./sound.js";
import { Direction, Sprite, Ticker, every, prefersStill } from "./sprite.js";

const SHEETS = {
  atlas: "img/atlases/Omori_TitleScreen",
  objects: "img/characters/DW_OBJECTS_WSNB",
  omori: "img/characters/DW_OMORI",
  something: "img/characters/$bs_en_something",
  string: "img/characters/[SF]WS_String",
  cursor: "img/system/cursor_menu",
  runOmori: "img/characters/$DW_OMORI_RUN%(8)",
  runAubrey: "img/characters/$DW_AUBREY_RUN%(8)",
  runKel: "img/characters/$DW_KEL_RUN%(8)",
  runHero: "img/characters/$DW_HERO_RUN%(8)",
};
const PARTY = ["runOmori", "runAubrey", "runKel", "runHero"];
const SOUNDS = ["SYS_cursor", "SYS_select", "sys_cancel", "SYS_text", "SE_meow", "SE_lightbulb", "SYS_quest_complete", "sys_buzzer"];
const MEWO_LINES = ["message_4", "message_404", "message_405", "message_406", "message_407", "message_409"];
const WHITE_SPACE_MUSIC = "audio/bgm/white_space";
const BLACK_SPACE_MUSIC = "audio/bgs/boss_something_heartbeat";
const KONAMI = ["ArrowUp", "ArrowUp", "ArrowDown", "ArrowDown", "ArrowLeft", "ArrowRight", "ArrowLeft", "ArrowRight", "b", "a"];
const SECRET_WORD = "something";
const IDLE_MS = 90000;
const RUN_FRAME_MS = 70;

function isTyping(target) {
  return target instanceof HTMLElement && (target.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName));
}

export class Theme {
  constructor() {
    this.awake = null;
    this.generation = 0;
    this.introPlayed = false;
    this.runners = new Map();
    this.keys = [];
    this.letters = "";
    this.toggles = document.getElementById("toggles");
    this.soundToggle = document.getElementById("toggle-sound");
    this.musicToggle = document.getElementById("toggle-music");
    this.heroSlot = document.getElementById("hero-slot");
    this.whitespace = document.getElementById("whitespace");
    this.partySlot = document.getElementById("party");

    this.soundToggle.addEventListener("click", () => {
      const sounds = this.awake?.sounds;
      if (!sounds) return;
      sounds.setEnabled(!sounds.enabled);
      this.renderToggles();
    });
    this.musicToggle.addEventListener("click", () => {
      const sounds = this.awake?.sounds;
      if (!sounds) return;
      sounds.setMusic(!sounds.musicOn);
      this.renderToggles();
    });

    document.addEventListener("pointerover", (event) => this.hoverButton(event));
    document.addEventListener("focusin", (event) => this.focusButton(event));
    document.addEventListener("click", (event) => this.clickButton(event), true);
    window.addEventListener("keydown", (event) => this.secretKeys(event));
    for (const name of ["pointermove", "pointerdown", "keydown", "scroll", "wheel"]) {
      window.addEventListener(name, () => this.poke(), { passive: true });
    }
  }

  wake(files) {
    if (this.awake?.files === files) return Promise.resolve();
    const generation = ++this.generation;
    return this.load(files)
      .then((awake) => {
        if (generation !== this.generation) {
          awake.release();
          return;
        }
        awake.world = this.awake?.world ?? awake.world;
        this.teardown();
        this.awake = awake;
        this.show();
      })
      .catch((error) => console.warn("The page couldn't borrow the game's look:", error));
  }

  async load(files) {
    const assets = await openGameAssets(files);
    if (!assets) throw new Error("no readable System data");
    const entries = await Promise.all(Object.entries(SHEETS).map(async ([name, path]) => [name, await assets.image(path)]));
    const sheets = Object.fromEntries(entries);
    const font = new FontFace("OMORI_GAME2", await assets.font("fonts/OMORI_GAME2.ttf"));
    await font.load();
    const messages = await assets.messages("languages/en/01_map_whitespace");
    const sounds = new Sounds(assets);
    sounds.preload(SOUNDS);
    const ticker = new Ticker();
    const box = new MessageBox(sounds);
    return {
      files,
      assets,
      sheets,
      font,
      messages,
      sounds,
      ticker,
      box,
      mewo: 0,
      world: "ws",
      release() {
        ticker.clear();
        box.release();
        sounds.release();
        document.fonts.delete(font);
        assets.release();
      },
    };
  }

  sleep() {
    this.generation++;
    this.teardown();
  }

  teardown() {
    const awake = this.awake;
    if (!awake) return;
    this.awake = null;
    awake.release();
    document.documentElement.classList.remove("awake");
    delete document.documentElement.dataset.world;
    document.documentElement.style.removeProperty("--hand");
    this.heroSlot.replaceChildren();
    this.whitespace.replaceChildren();
    this.whitespace.hidden = true;
    this.partySlot.replaceChildren();
    this.toggles.hidden = true;
    for (const runner of this.runners.values()) runner.sprite.el.remove();
    this.runners.clear();
  }

  show() {
    const awake = this.awake;
    document.fonts.add(awake.font);
    document.documentElement.classList.add("awake");
    document.documentElement.style.setProperty("--hand", `url(${awake.sheets.cursor.url})`);

    awake.hero = new TitleHero(awake.sheets.atlas, {
      onBulb: () => this.lookIntoBulb(),
      onOmori: () => this.talk("message_120"),
    });
    if (this.introPlayed) awake.hero.intro = 1;
    this.introPlayed = true;
    this.heroSlot.replaceChildren(awake.hero.canvas);
    awake.ticker.add((time) => awake.hero.update(time));

    this.buildWhiteSpace();
    this.renderToggles();
    this.applyWorld(awake);
  }

  applyWorld(awake) {
    if (awake.world === "bs") document.documentElement.dataset.world = "bs";
    else delete document.documentElement.dataset.world;
    awake.hero.setWorld(awake.world);
    awake.something.el.toggleAttribute("data-shown", awake.world === "bs");
    awake.sounds.setTrack(awake.world === "bs" ? BLACK_SPACE_MUSIC : WHITE_SPACE_MUSIC);
  }

  renderToggles() {
    const sounds = this.awake?.sounds;
    this.toggles.hidden = !sounds?.available;
    if (!sounds) return;
    this.soundToggle.textContent = sounds.enabled ? "sound on" : "sound off";
    this.soundToggle.setAttribute("aria-pressed", String(sounds.enabled));
    this.musicToggle.hidden = !sounds.canPlayMusic();
    this.musicToggle.textContent = sounds.musicOn ? "music on" : "music off";
    this.musicToggle.setAttribute("aria-pressed", String(sounds.musicOn));
  }

  message(name) {
    return this.awake?.messages.get(name) ?? null;
  }

  async talk(...names) {
    const box = this.awake?.box;
    if (!box || box.open) return;
    for (const name of names) {
      const text = this.message(name);
      if (text) await box.say(text);
    }
  }

  item(label, sprite, x, onClick) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "absolute bottom-5 cursor-pointer rounded-md focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-dashed bs:*:invert bs:*:hue-rotate-180";
    button.setAttribute("aria-label", label);
    button.style.left = `${x}px`;
    button.append(sprite.el);
    button.addEventListener("click", onClick);
    return button;
  }

  buildWhiteSpace() {
    const awake = this.awake;
    const { objects, omori, something, string } = awake.sheets;
    const still = prefersStill();
    const stage = document.createElement("div");
    stage.className = "relative h-[200px] w-[480px] flex-none origin-bottom max-sm:-mt-[68px] max-sm:scale-66";

    const cord = document.createElement("div");
    cord.className = "pixelated absolute top-0 left-[239px] h-6 w-0.5 bg-bottom bg-size-[2px_868px] bs:invert bs:hue-rotate-180";
    cord.style.backgroundImage = `url(${string.url})`;

    const bulb = new Sprite(objects, { width: 32, height: 32 });
    bulb.character(7, 0, Direction.down);
    const bulbButton = this.item("Lightbulb", bulb, 208, () => this.lookIntoBulb());
    bulbButton.classList.replace("bottom-5", "top-[22px]");

    const sketchbook = new Sprite(objects, { width: 32, height: 32 });
    sketchbook.character(5, 1, Direction.down);
    sketchbook.el.classList.add("absolute", "bottom-5", "bs:invert", "bs:hue-rotate-180");
    sketchbook.el.style.left = "30px";

    const laptop = new Sprite(objects, { width: 32, height: 32 });
    laptop.character(0, 1, Direction.down);
    const laptopButton = this.item("Laptop", laptop, 110, () => this.talk(this.message("message_11") ? "message_11" : "message_105"));

    awake.omoriSprite = new Sprite(omori, { width: 32, height: 32 });
    awake.omoriSprite.character(3, 0, Direction.down);
    const omoriButton = this.item("Omori", awake.omoriSprite, 208, () => this.talk("message_120"));

    awake.something = new Sprite(something, { width: 32, height: 64, className: "pointer-events-none absolute bottom-5 opacity-0 transition-opacity duration-3000 data-shown:opacity-100 data-peek:animate-peek motion-reduce:data-peek:animate-none motion-reduce:data-peek:opacity-85" });
    awake.something.frame(1, 0);
    awake.something.el.style.left = "276px";
    awake.something.el.setAttribute("aria-hidden", "true");

    const mewo = new Sprite(objects, { width: 32, height: 32 });
    mewo.character(2, 0, Direction.down);
    const mewoButton = this.item("Mewo", mewo, 340, () => this.talkToMewo(mewo));

    const tissue = new Sprite(objects, { width: 32, height: 32 });
    tissue.character(5, 0, Direction.down);
    const tissueButton = this.item("Tissue box", tissue, 410, () => this.talk("message_16"));

    stage.append(cord, bulbButton, sketchbook.el, laptopButton, awake.something.el, omoriButton, mewoButton, tissueButton);
    this.whitespace.replaceChildren(stage);
    this.whitespace.hidden = false;

    if (still) return;
    awake.ticker.add(every(260, (count) => bulb.character(7, [0, 1, 2, 1][count % 4], Direction.down)));
    awake.ticker.add(every(140, (count) => laptop.character(0, count % 3, Direction.down)));
    awake.ticker.add(
      every(420, (count) => {
        if (!awake.mewoTalking) mewo.character(2, [0, 1, 2, 1][count % 4], Direction.down);
      }),
    );
    awake.ticker.add(every(300, (count) => awake.something.frame([0, 1, 2, 1][count % 4], 0)));
    this.poke();
  }

  async talkToMewo(mewo) {
    const awake = this.awake;
    if (!awake || awake.box.open) return;
    awake.sounds.play("SE_meow", { volume: 0.5 });
    awake.mewoTalking = true;
    mewo.character(2, 1, Direction.right);
    const line = MEWO_LINES[awake.mewo % MEWO_LINES.length];
    awake.mewo++;
    await this.talk(line);
    awake.mewoTalking = false;
    mewo.character(2, 0, Direction.down);
  }

  async lookIntoBulb() {
    const awake = this.awake;
    if (!awake || awake.box.open) return;
    if (awake.world === "bs") {
      this.setWorld("ws");
      return;
    }
    const question = this.message("message_17");
    const yes = this.message("message_14") ?? "YES";
    const no = this.message("message_15") ?? "NO";
    if (!question) {
      this.setWorld("bs");
      return;
    }
    const answer = await awake.box.ask(question, [yes, no]);
    if (answer !== 0 || this.awake !== awake) return;
    this.setWorld("bs");
    await this.talk("message_18");
  }

  setWorld(world) {
    const awake = this.awake;
    if (!awake || awake.world === world) return;
    awake.world = world;
    awake.sounds.play("SE_lightbulb", { volume: 0.55 });
    const apply = () => this.applyWorld(awake);
    awake.curtain?.remove();
    if (prefersStill()) {
      apply();
      return;
    }
    const curtain = document.createElement("div");
    curtain.className = "pointer-events-none fixed inset-0 z-90 opacity-0";
    curtain.style.background = world === "bs" ? "#000" : "#fff";
    document.body.append(curtain);
    awake.curtain = curtain;
    curtain
      .animate([{ opacity: 0 }, { opacity: 1 }], { duration: 140, fill: "forwards" })
      .finished.then(() => {
        apply();
        return curtain.animate([{ opacity: 1 }, { opacity: 0 }], { duration: 750, easing: "ease-in", fill: "forwards" }).finished;
      })
      .catch(() => {})
      .finally(() => {
        curtain.remove();
        if (awake.curtain === curtain) awake.curtain = null;
      });
  }

  poke() {
    const awake = this.awake;
    if (!awake?.omoriSprite) return;
    if (awake.sleeping) {
      awake.sleeping = false;
      awake.omoriSprite.character(3, 0, Direction.down);
    }
    clearTimeout(awake.idleTimer);
    awake.idleTimer = setTimeout(() => {
      if (this.awake !== awake) return;
      awake.sleeping = true;
      awake.omoriSprite.character(3, 0, Direction.left);
    }, IDLE_MS);
  }

  secretKeys(event) {
    const awake = this.awake;
    if (!awake || awake.box.open || isTyping(event.target) || event.ctrlKey || event.metaKey || event.altKey) return;
    const key = event.key.length === 1 ? event.key.toLowerCase() : event.key;
    this.keys = [...this.keys, key].slice(-KONAMI.length);
    if (this.keys.join(",") === KONAMI.join(",")) {
      this.keys = [];
      this.partyRun();
    }
    if (key.length === 1) {
      this.letters = (this.letters + key).slice(-SECRET_WORD.length);
      if (this.letters === SECRET_WORD) {
        this.letters = "";
        this.somethingPeeks();
      }
    }
  }

  runSprite(name, scale = 2) {
    const sprite = new Sprite(this.awake.sheets[name], { width: 32, height: 32, scale });
    sprite.frame(0, Direction.right);
    return sprite;
  }

  partyRun() {
    const awake = this.awake;
    if (!awake || awake.partyRunning || prefersStill()) return;
    awake.partyRunning = true;
    const lane = document.createElement("div");
    lane.className = "pointer-events-none fixed inset-x-0 bottom-1.5 z-50 h-16 overflow-hidden *:absolute *:bottom-0 *:left-0";
    lane.setAttribute("aria-hidden", "true");
    const sprites = PARTY.map((name) => this.runSprite(name));
    lane.append(...sprites.map((sprite) => sprite.el));
    document.body.append(lane);
    awake.sounds.play("SYS_select", { volume: 0.4 });

    const distance = window.innerWidth + 4 * 56 + 64;
    const duration = distance * 2.6;
    let start = null;
    awake.ticker.add((time) => {
      start ??= time;
      const elapsed = time - start;
      const frame = Math.floor(elapsed / RUN_FRAME_MS) % 8;
      sprites.forEach((sprite, index) => {
        sprite.frame((frame + index * 2) % 8, Direction.right);
        sprite.el.style.transform = `translateX(${(elapsed / duration) * distance - (index + 1) * 56}px)`;
      });
      if (elapsed < duration && this.awake === awake) return true;
      lane.remove();
      awake.partyRunning = false;
      return false;
    });
  }

  somethingPeeks() {
    const awake = this.awake;
    if (!awake || awake.world === "bs") return;
    awake.sounds.play("audio/bgs/SE_HEARTBEAT", { volume: 0.6 });
    const element = awake.something.el;
    element.toggleAttribute("data-peek", true);
    document.documentElement.toggleAttribute("data-shiver", true);
    setTimeout(() => {
      element.toggleAttribute("data-peek", false);
      document.documentElement.toggleAttribute("data-shiver", false);
    }, 2600);
  }

  celebrate() {
    const awake = this.awake;
    if (!awake) return;
    awake.sounds.play("SYS_quest_complete", { volume: 0.5 });
    const sprites = PARTY.map((name) => this.runSprite(name));
    this.partySlot.replaceChildren(...sprites.map((sprite) => sprite.el));
    const stand = () => sprites.forEach((sprite) => sprite.frame(0, Direction.down));
    if (prefersStill()) {
      stand();
      return;
    }
    const duration = 1400;
    let start = null;
    awake.ticker.add((time) => {
      start ??= time;
      const elapsed = Math.min(duration, time - start);
      const frame = Math.floor(elapsed / RUN_FRAME_MS) % 8;
      const left = 1 - elapsed / duration;
      sprites.forEach((sprite, index) => {
        sprite.frame((frame + index * 2) % 8, Direction.right);
        sprite.el.style.transform = `translateX(${-left * (this.partySlot.clientWidth + 64)}px)`;
      });
      if (elapsed < duration && this.awake === awake) return true;
      sprites.forEach((sprite) => (sprite.el.style.transform = ""));
      stand();
      return false;
    });
  }

  warn() {
    this.awake?.sounds.play("sys_buzzer", { volume: 0.5 });
  }

  progress(status, fraction, running) {
    const awake = this.awake;
    if (!awake) return;
    let runner = this.runners.get(status);
    if (!runner) {
      const sprite = this.runSprite("runOmori", 1);
      sprite.el.classList.add("absolute", "top-0.5", "left-[calc((100%-32px)*var(--at,0))]", "transition-[left]", "duration-250", "ease-linear", "motion-reduce:transition-none", "bs:invert", "bs:hue-rotate-180");
      sprite.el.setAttribute("aria-hidden", "true");
      status.prepend(sprite.el);
      runner = { sprite, stop: null };
      this.runners.set(status, runner);
    }
    runner.sprite.el.style.setProperty("--at", String(Math.min(1, Math.max(0, fraction))));
    if (running && !runner.stop && !prefersStill()) {
      runner.stop = awake.ticker.add((time) => runner.sprite.frame(Math.floor(time / RUN_FRAME_MS) % 8, Direction.right));
    }
    if (!running) {
      runner.stop?.();
      runner.stop = null;
      runner.sprite.frame(0, Direction.down);
    }
  }

  hoverButton(event) {
    const button = event.target.closest?.(".btn");
    if (!button || button.disabled || button.contains(event.relatedTarget)) return;
    this.awake?.sounds.play("SYS_cursor", { volume: 0.3 });
  }

  focusButton(event) {
    const button = event.target.closest?.(".btn");
    if (!button || !button.matches(":focus-visible")) return;
    this.awake?.sounds.play("SYS_cursor", { volume: 0.3 });
  }

  clickButton(event) {
    const button = event.target.closest?.(".btn, #toggles button");
    if (!button || button.disabled || button.getAttribute("aria-disabled") === "true") return;
    this.awake?.sounds.play(button.dataset.sound ?? "SYS_select", { volume: 0.4 });
  }
}

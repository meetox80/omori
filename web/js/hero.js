import { prefersStill } from "./sprite.js";

const WIDTH = 640;
const HEIGHT = 480;
const PORTRAIT = { width: 306, height: 350 };
const LINES = { width: 68, height: 150 };
const TITLE = { x: 167, from: 130, to: -30 };
const BULB = { x: 120, y: -2 };
const STEP = 1000 / 60;

const PARTS = {
  ws: {
    portrait: { x: 0, y: 0, width: 918, height: 351 },
    title: { x: 918, y: 0, width: 277, height: 150 },
    bulb: { x: 1399, y: 20, width: 65, height: 130, offset: 20 },
    lines: { x: 1195, y: 0, width: 204, height: 150 },
    string: { x: 1399, y: 0, width: 65, height: 20 },
  },
  bs: {
    portrait: { x: 0, y: 351, width: 918, height: 351 },
    title: { x: 918, y: 150, width: 277, height: 150 },
    bulb: { x: 1399, y: 170, width: 65, height: 150, offset: 20 },
    lines: { x: 1195, y: 150, width: 204, height: 150 },
    string: { x: 1399, y: 151, width: 65, height: 20 },
  },
};

function easeOut(t) {
  return 1 - (1 - t) ** 3;
}

export class TitleHero {
  constructor(atlas, { onBulb, onOmori }) {
    this.atlas = atlas;
    this.world = "ws";
    this.canvas = document.createElement("canvas");
    this.canvas.width = WIDTH;
    this.canvas.height = HEIGHT;
    this.canvas.className = "mx-auto mb-3 block aspect-4/3 h-auto w-full max-w-[460px]";
    this.canvas.setAttribute("role", "img");
    this.canvas.setAttribute("aria-label", "OMORI title screen");
    this.context = this.canvas.getContext("2d");
    this.tick = 0;
    this.started = null;
    this.intro = prefersStill() ? 1 : 0;

    this.canvas.addEventListener("click", (event) => {
      const point = this.toCanvas(event);
      if (this.hitBulb(point)) onBulb();
      else if (this.hitPortrait(point)) onOmori();
    });
    this.canvas.addEventListener("pointermove", (event) => {
      const point = this.toCanvas(event);
      this.canvas.style.cursor = this.hitBulb(point) || this.hitPortrait(point) ? "pointer" : "";
    });
  }

  toCanvas(event) {
    const box = this.canvas.getBoundingClientRect();
    return { x: ((event.clientX - box.left) / box.width) * WIDTH, y: ((event.clientY - box.top) / box.height) * HEIGHT };
  }

  titleY() {
    return TITLE.from + (TITLE.to - TITLE.from) * easeOut(Math.min(1, this.intro / 0.75));
  }

  hitBulb({ x, y }) {
    const left = TITLE.x + BULB.x;
    const top = this.titleY() + BULB.y;
    return x >= left - 10 && x <= left + 75 && y >= top - 20 && y <= top + 150;
  }

  hitPortrait({ x, y }) {
    return x >= (WIDTH - PORTRAIT.width) / 2 + 40 && x <= (WIDTH + PORTRAIT.width) / 2 - 40 && y >= HEIGHT - PORTRAIT.height + 30;
  }

  setWorld(world) {
    this.world = world;
    this.draw();
  }

  update(time) {
    this.started ??= time;
    const ticks = Math.floor((time - this.started) / STEP);
    if (ticks === this.tick && this.intro >= 1) return;
    this.tick = ticks;
    const settled = this.intro >= 1;
    if (!settled) this.intro = Math.min(1, (time - this.started) / 2600);
    const frames = this.frames();
    if (settled && frames === this.drawn) return;
    this.draw();
  }

  frames() {
    if (prefersStill()) return "0,0";
    return `${Math.floor(this.tick / 21) % 3},${[0, 1, 2, 1][Math.floor(this.tick / 16) % 4]}`;
  }

  draw() {
    const parts = PARTS[this.world];
    const image = this.atlas.bitmap;
    const context = this.context;
    this.drawn = this.frames();
    const [portraitFrame, linesFrame] = this.drawn.split(",").map(Number);
    const portraitAlpha = Math.min(1, this.intro / 0.5);
    const titleAlpha = Math.min(1, Math.max(0, (this.intro - 0.35) / 0.4));
    const linesAlpha = Math.min(1, Math.max(0, (this.intro - 0.75) / 0.25));
    const titleY = this.titleY();
    const left = TITLE.x + BULB.x;

    context.clearRect(0, 0, WIDTH, HEIGHT);

    context.globalAlpha = portraitAlpha;
    const portrait = parts.portrait;
    context.drawImage(
      image,
      portrait.x + portraitFrame * PORTRAIT.width, portrait.y, PORTRAIT.width, PORTRAIT.height,
      (WIDTH - PORTRAIT.width) / 2, HEIGHT - PORTRAIT.height, PORTRAIT.width, PORTRAIT.height,
    );

    context.globalAlpha = Math.min(1, this.intro * 4);
    const string = parts.string;
    context.drawImage(image, string.x, string.y, string.width, string.height, left, titleY - 282, string.width, 300);
    const bulb = parts.bulb;
    context.drawImage(image, bulb.x, bulb.y, bulb.width, bulb.height, left, titleY + BULB.y + bulb.offset, bulb.width, bulb.height);

    context.globalAlpha = linesAlpha;
    const lines = parts.lines;
    context.drawImage(image, lines.x + linesFrame * LINES.width, lines.y, LINES.width, LINES.height, left, titleY, LINES.width, LINES.height);

    context.globalAlpha = titleAlpha;
    const title = parts.title;
    context.drawImage(image, title.x, title.y, title.width, title.height, TITLE.x, titleY, title.width, title.height);
    context.globalAlpha = 1;
  }
}

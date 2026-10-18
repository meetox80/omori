const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)");

export function prefersStill() {
  return reducedMotion.matches;
}

export class Sprite {
  constructor(sheet, { width, height, scale = 2, className = "" }) {
    this.sheet = sheet;
    this.width = width;
    this.height = height;
    this.scale = scale;
    this.el = document.createElement("div");
    this.el.className = `pixelated bg-no-repeat ${className}`.trim();
    Object.assign(this.el.style, {
      width: `${width * scale}px`,
      height: `${height * scale}px`,
      backgroundImage: `url(${sheet.url})`,
      backgroundSize: `${sheet.width * scale}px ${sheet.height * scale}px`,
    });
    this.frame(0, 0);
  }

  frame(column, row) {
    this.el.style.backgroundPosition = `${-column * this.width * this.scale}px ${-row * this.height * this.scale}px`;
  }

  character(index, pattern, direction) {
    this.frame((index % 4) * 3 + pattern, Math.floor(index / 4) * 4 + direction);
  }
}

export const Direction = { down: 0, left: 1, right: 2, up: 3 };

export class Ticker {
  constructor() {
    this.jobs = new Set();
    this.frame = null;
    this.step = (time) => {
      this.frame = null;
      for (const job of [...this.jobs]) {
        if (job(time) === false) this.jobs.delete(job);
      }
      if (this.jobs.size > 0) this.frame = requestAnimationFrame(this.step);
    };
  }

  add(job) {
    this.jobs.add(job);
    this.frame ??= requestAnimationFrame(this.step);
    return () => this.jobs.delete(job);
  }

  clear() {
    this.jobs.clear();
    if (this.frame) cancelAnimationFrame(this.frame);
    this.frame = null;
  }
}

export function every(milliseconds, callback) {
  let last = null;
  let count = 0;
  return (time) => {
    last ??= time;
    if (time - last >= milliseconds) {
      last = time;
      count++;
      return callback(count);
    }
    return true;
  };
}

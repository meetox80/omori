const CHARACTER_DELAY = 22;
const PAUSES = { "\\!": 450, "\\|": 600, "\\.": 200 };
const CONFIRM_KEYS = new Set(["Enter", " ", "z", "Z"]);
const CANCEL_KEYS = new Set(["Escape", "x", "X", "Backspace"]);

export function parseMessage(raw) {
  let name = null;
  const body = raw.replace(/\\n<([^>]*)>/, (_, found) => {
    name = found;
    return "";
  });
  const tokens = [];
  const pattern = /\\[!|.]|<br>|\\lb|\\[A-Za-z]+(?:\[[^\]]*\])?|\\[{}<>^$]|[\s\S]/g;
  for (const [token] of body.matchAll(pattern)) {
    if (PAUSES[token]) tokens.push({ pause: PAUSES[token] });
    else if (token === "<br>" || token === "\\lb") {
      if (tokens.at(-1)?.text !== "\n") tokens.push({ text: "\n" });
    } else if (!token.startsWith("\\")) tokens.push({ text: token });
  }
  while (tokens.length && /^\s$/.test(tokens[0].text ?? "")) tokens.shift();
  return { name, tokens };
}

export class MessageBox {
  constructor(sounds) {
    this.sounds = sounds;
    this.layer = document.createElement("div");
    this.layer.className = "fixed inset-0 z-100 flex items-end justify-center p-4 font-omori";
    this.layer.hidden = true;
    this.layer.innerHTML = `
      <div class="message relative w-full max-w-[640px] outline-none" role="dialog" aria-modal="true" aria-live="polite" tabindex="-1">
        <div class="message-name omori-box mb-2 ml-1.5 inline-block px-4 py-0.5 text-2xl" hidden></div>
        <div class="message-choices omori-box absolute right-1.5 bottom-[calc(100%+10px)] flex flex-col py-2 pr-[22px] pl-[46px]" role="listbox" hidden></div>
        <div class="omori-box relative min-h-28 px-[22px] pt-3.5 pb-[18px]">
          <p class="message-text text-[26px]/[1.3] whitespace-pre-wrap max-sm:text-[22px]"></p>
          <span class="message-next absolute right-[18px] bottom-2.5 size-0 border-7 border-b-0 border-transparent border-t-white animate-blink motion-reduce:animate-none" hidden></span>
        </div>
      </div>`;
    document.body.append(this.layer);
    this.dialog = this.layer.querySelector(".message");
    this.nameEl = this.layer.querySelector(".message-name");
    this.textEl = this.layer.querySelector(".message-text");
    this.nextEl = this.layer.querySelector(".message-next");
    this.choicesEl = this.layer.querySelector(".message-choices");
    this.queue = Promise.resolve();
    this.active = null;

    this.layer.addEventListener("pointerdown", (event) => {
      if (event.target.closest(".message-choice")) return;
      event.preventDefault();
      this.confirm();
    });
    this.onKey = (event) => {
      if (!this.active) return;
      if (CONFIRM_KEYS.has(event.key)) this.confirm();
      else if (CANCEL_KEYS.has(event.key)) this.cancel();
      else if (/^Arrow/.test(event.key)) this.move(event.key === "ArrowUp" || event.key === "ArrowLeft" ? -1 : 1);
      else return;
      event.preventDefault();
      event.stopPropagation();
    };
    window.addEventListener("keydown", this.onKey, true);
  }

  get open() {
    return Boolean(this.active);
  }

  say(raw) {
    return this.enqueue(raw, null);
  }

  ask(raw, choices) {
    return this.enqueue(raw, choices);
  }

  enqueue(raw, choices) {
    const run = this.queue.then(() => this.show(parseMessage(raw), choices));
    this.queue = run.catch(() => {});
    return run;
  }

  show({ name, tokens }, choices) {
    return new Promise((resolve) => {
      const previousFocus = document.activeElement;
      this.layer.hidden = false;
      this.nameEl.hidden = !name;
      this.nameEl.textContent = name ?? "";
      this.textEl.textContent = "";
      this.nextEl.hidden = true;
      this.choicesEl.hidden = true;
      this.choicesEl.replaceChildren();
      this.dialog.focus({ preventScroll: true });

      const state = { tokens, at: 0, timer: null, typing: true, choices, selected: 0, resolve, previousFocus };
      this.active = state;
      this.typeNext(state);
    });
  }

  typeNext(state) {
    if (state !== this.active) return;
    if (state.at >= state.tokens.length) {
      this.finishTyping(state);
      return;
    }
    const token = state.tokens[state.at++];
    if (token.pause) {
      state.timer = setTimeout(() => this.typeNext(state), token.pause);
      return;
    }
    this.textEl.textContent += token.text;
    if (state.at % 3 === 0 && /\S/.test(token.text)) this.sounds?.play("SYS_text", { volume: 0.25 });
    state.timer = setTimeout(() => this.typeNext(state), CHARACTER_DELAY);
  }

  finishTyping(state) {
    clearTimeout(state.timer);
    state.typing = false;
    this.textEl.textContent = state.tokens.map((token) => token.text ?? "").join("");
    if (!state.choices) {
      this.nextEl.hidden = false;
      return;
    }
    this.choicesEl.hidden = false;
    state.choices.forEach((label, index) => {
      const option = document.createElement("button");
      option.type = "button";
      option.className = "relative cursor-pointer py-0.5 text-left text-2xl text-white [--hand-x:-36px] aria-selected:hand";
      option.textContent = label;
      option.setAttribute("role", "option");
      option.addEventListener("pointerenter", () => this.select(index));
      option.addEventListener("click", () => {
        this.select(index);
        this.confirm();
      });
      this.choicesEl.append(option);
    });
    this.select(0, true);
  }

  select(index, quiet = false) {
    const state = this.active;
    if (!state?.choices) return;
    if (index !== state.selected && !quiet) this.sounds?.play("SYS_cursor", { volume: 0.4 });
    state.selected = index;
    [...this.choicesEl.children].forEach((option, at) => {
      option.setAttribute("aria-selected", String(at === index));
    });
  }

  move(step) {
    const state = this.active;
    if (!state?.choices || state.typing) return;
    this.select((state.selected + step + state.choices.length) % state.choices.length);
  }

  confirm() {
    const state = this.active;
    if (!state) return;
    if (state.typing) {
      this.finishTyping(state);
      return;
    }
    this.sounds?.play("SYS_select", { volume: 0.4 });
    this.close(state.choices ? state.selected : 0);
  }

  cancel() {
    const state = this.active;
    if (!state) return;
    if (state.typing) {
      this.finishTyping(state);
      return;
    }
    if (state.choices) this.sounds?.play("sys_cancel", { volume: 0.4 });
    this.close(state.choices ? -1 : 0);
  }

  close(result) {
    const state = this.active;
    clearTimeout(state.timer);
    this.active = null;
    this.layer.hidden = true;
    state.previousFocus?.focus?.({ preventScroll: true });
    state.resolve(result);
  }

  release() {
    if (this.active) this.close(-1);
    window.removeEventListener("keydown", this.onKey, true);
    this.layer.remove();
  }
}

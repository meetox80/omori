const CONTINUE_LABEL = "i either dont have money to buy the game or hate it";
const WAIT_SECONDS = 5;

export function showCrackWarning(reason) {
  const dialog = document.getElementById("crack");
  const continueButton = document.getElementById("crack-continue");
  document.getElementById("crack-reason").textContent = `Found: ${reason}`;

  return new Promise((resolve) => {
    let remaining = WAIT_SECONDS;

    const updateButton = () => {
      continueButton.disabled = remaining > 0;
      continueButton.textContent = remaining > 0 ? `${CONTINUE_LABEL} (${remaining})` : CONTINUE_LABEL;
    };

    const timer = setInterval(() => {
      remaining--;
      updateButton();
      if (remaining <= 0) clearInterval(timer);
    }, 1000);

    const finish = (accepted) => {
      clearInterval(timer);
      dialog.close();
      resolve(accepted);
    };

    document.getElementById("crack-buy").onclick = () => finish(false);
    document.getElementById("crack-quit").onclick = () => finish(false);
    continueButton.onclick = () => finish(true);
    dialog.oncancel = (event) => {
      event.preventDefault();
      finish(false);
    };

    updateButton();
    dialog.showModal();
  });
}

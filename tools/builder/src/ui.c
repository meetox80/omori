#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util.h"

#define CRACK_OK_LABEL "i either dont have money to buy the game or hate it"

#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <io.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#elif defined(__wasi__)
#include <unistd.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

enum { B_TERMINAL, B_WIN32, B_OSASCRIPT, B_ZENITY, B_KDIALOG };

static int g_backend = -1;
static int g_native = 0;

static void trim(char* s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

int ui_stdin_is_terminal(void) {
#ifdef _WIN32
    return _isatty(_fileno(stdin));
#else
    return isatty(0);
#endif
}

#ifdef __wasi__
static int in_path(const char* name) {
    (void)name;
    return 0;
}

static int run_cmd(char* const argv[], char** out) {
    (void)argv;
    if (out) *out = NULL;
    return -1;
}
#elif !defined(_WIN32)
static int in_path(const char* name) {
    const char* path = getenv("PATH");
    if (!path) return 0;
    char* copy = xstrdup(path);
    int found = 0;
    for (char* tok = strtok(copy, ":"); tok && !found; tok = strtok(NULL, ":")) {
        char* full = path_join(tok, name);
        found = access(full, X_OK) == 0;
        free(full);
    }
    free(copy);
    return found;
}

static int run_cmd(char* const argv[], char** out) {
    int fds[2];
    if (pipe(fds) != 0) return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    Buf b = {0};
    if (rc == 0) {
        char tmp[4096];
        ssize_t n;
        while ((n = read(fds[0], tmp, sizeof(tmp))) > 0) buf_put(&b, tmp, (size_t)n);
    }
    close(fds[0]);
    int status = -1;
    if (rc == 0) {
        int st;
        if (waitpid(pid, &st, 0) == pid && WIFEXITED(st)) status = WEXITSTATUS(st);
    }
    buf_u8(&b, 0);
    if (out) *out = (char*)b.data;
    else buf_free(&b);
    return status;
}

#endif

#ifndef _WIN32
static char* esc_markup(const char* s) {
    Buf b = {0};
    for (; *s; s++) {
        if (*s == '&') buf_put(&b, "&amp;", 5);
        else if (*s == '<') buf_put(&b, "&lt;", 4);
        else if (*s == '>') buf_put(&b, "&gt;", 4);
        else if (*s == '\\') buf_put(&b, "\\\\", 2);
        else buf_u8(&b, (uint8_t)*s);
    }
    buf_u8(&b, 0);
    return (char*)b.data;
}

static char* esc_applescript(const char* s) {
    Buf b = {0};
    buf_u8(&b, '"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') buf_u8(&b, '\\');
        buf_u8(&b, (uint8_t)*s);
    }
    buf_u8(&b, '"');
    buf_u8(&b, 0);
    return (char*)b.data;
}

static int osa(const char* script, char** out) {
    char* argv[] = {"osascript", "-e", (char*)script, NULL};
    return run_cmd(argv, out);
}
#endif

static int detect_backend(void) {
#ifdef _WIN32
    return B_WIN32;
#elif defined(__APPLE__)
    return in_path("osascript") ? B_OSASCRIPT : B_TERMINAL;
#else
    if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) return B_TERMINAL;
    if (in_path("zenity")) return B_ZENITY;
    if (in_path("kdialog")) return B_KDIALOG;
    return B_TERMINAL;
#endif
}

int ui_native_available(void) {
    if (g_backend < 0) g_backend = detect_backend();
    if (getenv("OMORI_NO_GUI")) return 0;
    return g_backend != B_TERMINAL;
}

void ui_use_native(int native) { g_native = native && ui_native_available(); }
int ui_is_native(void) { return g_native; }

static char* read_line(void) {
    char tmp[4096];
    fflush(stdout);
    if (!fgets(tmp, sizeof(tmp), stdin)) return NULL;
    trim(tmp);
    return xstrdup(tmp);
}

static int term_yes_no(const char* text, const char* yes, const char* no) {
    printf("\n%s\n  y = %s\n  n = %s\n> ", text, yes, no);
    for (;;) {
        char* l = read_line();
        if (!l) return 0;
        int c = l[0];
        free(l);
        if (c == 'y' || c == 'Y') return 1;
        if (c == 'n' || c == 'N') return 0;
        printf("type y or n\n> ");
    }
}

static char* term_folder(const char* prompt) {
    printf("\n%s\nType or drag the folder here and press enter (empty line cancels):\n> ", prompt);
    char* l = read_line();
    if (!l || !*l) {
        free(l);
        return NULL;
    }
    size_t n = strlen(l);
    if (n >= 2 && ((l[0] == '"' && l[n - 1] == '"') || (l[0] == '\'' && l[n - 1] == '\''))) {
        memmove(l, l + 1, n - 2);
        l[n - 2] = 0;
    }
#ifndef _WIN32
    char* w = l;
    for (char* r = l; *r; r++) {
        if (*r == '\\' && r[1]) r++;
        *w++ = *r;
    }
    *w = 0;
#endif
    return l;
}

static CrackChoice term_crack(const char* text) {
    printf("\n==============================================================\n%s\n==============================================================\n", text);
    for (int i = 5; i > 0; i--) {
        printf("\rplease read this... %d ", i);
        fflush(stdout);
        sleep_ms(1000);
    }
    printf("\r                       \n  1 = buy on Steam (opens the store page)\n  2 = quit\n  3 = %s\n> ", CRACK_OK_LABEL);
    for (;;) {
        char* l = read_line();
        if (!l) return CRACK_QUIT;
        int c = l[0];
        free(l);
        if (c == '1') return CRACK_BUY;
        if (c == '2') return CRACK_QUIT;
        if (c == '3') return CRACK_CONTINUE;
        printf("type 1, 2 or 3\n> ");
    }
}

#ifdef _WIN32
static int wbox(const char* title, const char* text, UINT flags) {
    wchar_t* wt = utf8_to_wide(title);
    wchar_t* wx = utf8_to_wide(text);
    int r = MessageBoxW(NULL, wx, wt, flags | MB_SETFOREGROUND | MB_TOPMOST);
    free(wt);
    free(wx);
    return r;
}

static char* win_folder(const char* title) {
    char* out = NULL;
    HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* dlg = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void**)&dlg))) {
        DWORD opts = 0;
        IFileOpenDialog_GetOptions(dlg, &opts);
        IFileOpenDialog_SetOptions(dlg, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        wchar_t* wt = utf8_to_wide(title);
        IFileOpenDialog_SetTitle(dlg, wt);
        free(wt);
        if (SUCCEEDED(IFileOpenDialog_Show(dlg, NULL))) {
            IShellItem* item = NULL;
            if (SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item))) {
                PWSTR p = NULL;
                if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &p))) {
                    out = wide_to_utf8(p);
                    CoTaskMemFree(p);
                }
                IShellItem_Release(item);
            }
        }
        IFileOpenDialog_Release(dlg);
    } else {
        BROWSEINFOW bi;
        memset(&bi, 0, sizeof(bi));
        wchar_t* wt = utf8_to_wide(title);
        bi.lpszTitle = wt;
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
        free(wt);
        if (pidl) {
            wchar_t buf[MAX_PATH];
            if (SHGetPathFromIDListW(pidl, buf)) out = wide_to_utf8(buf);
            CoTaskMemFree(pidl);
        }
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return out;
}

typedef struct {
    CrackChoice result;
    int remaining;
    HWND ok;
    HFONT font;
} CrackState;

static CrackState g_cs;

static void set_ok_label(void) {
    char* label = g_cs.remaining > 0 ? str_fmt("%s (%d)", CRACK_OK_LABEL, g_cs.remaining) : xstrdup(CRACK_OK_LABEL);
    wchar_t* w = utf8_to_wide(label);
    SetWindowTextW(g_cs.ok, w);
    free(w);
    free(label);
}

static LRESULT CALLBACK crack_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TIMER:
        g_cs.remaining--;
        if (g_cs.remaining <= 0) {
            KillTimer(h, 1);
            EnableWindow(g_cs.ok, TRUE);
        }
        set_ok_label();
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == 1) g_cs.result = CRACK_BUY;
        else if (LOWORD(w) == 2) g_cs.result = CRACK_QUIT;
        else if (LOWORD(w) == 3 && g_cs.remaining <= 0) g_cs.result = CRACK_CONTINUE;
        else return 0;
        DestroyWindow(h);
        return 0;
    case WM_CLOSE:
        g_cs.result = CRACK_QUIT;
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static CrackChoice win_crack(const char* title, const char* text) {
    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = crack_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_WARNING);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"OmoriCrackWarning";
    RegisterClassW(&wc);
    HDC dc = GetDC(NULL);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
#define S(v) MulDiv((v), dpi, 96)
    NONCLIENTMETRICSW ncm;
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_cs.font = CreateFontIndirectW(&ncm.lfMessageFont);
    g_cs.result = CRACK_QUIT;
    g_cs.remaining = 5;
    wchar_t* wx = utf8_to_wide(text);
    HDC mdc = GetDC(NULL);
    HGDIOBJ old = SelectObject(mdc, g_cs.font);
    RECT tr = {0, 0, S(512), 0};
    DrawTextW(mdc, wx, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(mdc, old);
    ReleaseDC(NULL, mdc);
    int text_h = tr.bottom - tr.top;
    int by = S(20) + text_h + S(24);
    if (by < S(76)) by = S(76);
    RECT r = {0, 0, S(600), by + S(54)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&r, style, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
    wchar_t* wt = utf8_to_wide(title);
    HWND win = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, wc.lpszClassName, wt, style, x, y, ww, wh, NULL, NULL, inst, NULL);
    free(wt);
    HWND icon = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ICON, S(20), S(20), S(32), S(32), win, NULL, inst, NULL);
    SendMessageW(icon, STM_SETICON, (WPARAM)LoadIcon(NULL, IDI_WARNING), 0);
    HWND label = CreateWindowExW(0, L"STATIC", wx, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, S(68), S(20), S(512), text_h + S(4), win, NULL, inst, NULL);
    free(wx);
    g_cs.ok = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED | BS_PUSHBUTTON, S(20), by, S(350), S(34), win,
                              (HMENU)(INT_PTR)3, inst, NULL);
    HWND buy = CreateWindowExW(0, L"BUTTON", L"Buy on Steam", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, S(382), by, S(116), S(34), win,
                               (HMENU)(INT_PTR)1, inst, NULL);
    HWND quit = CreateWindowExW(0, L"BUTTON", L"Quit", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, S(508), by, S(72), S(34), win,
                                (HMENU)(INT_PTR)2, inst, NULL);
#undef S
    HWND kids[] = {label, g_cs.ok, buy, quit};
    for (int i = 0; i < 4; i++) SendMessageW(kids[i], WM_SETFONT, (WPARAM)g_cs.font, TRUE);
    set_ok_label();
    MessageBeep(MB_ICONWARNING);
    ShowWindow(win, SW_SHOW);
    SetForegroundWindow(win);
    SetFocus(buy);
    SetTimer(win, 1, 1000, NULL);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(win, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DeleteObject(g_cs.font);
    UnregisterClassW(wc.lpszClassName, inst);
    return g_cs.result;
}
#endif

int ui_yes_no(const char* title, const char* text, const char* yes, const char* no) {
    if (!g_native) return term_yes_no(text, yes, no);
#ifdef _WIN32
    char* full = str_fmt("%s\n\nYes = %s\nNo = %s", text, yes, no);
    int r = wbox(title, full, MB_YESNO | MB_ICONQUESTION);
    free(full);
    return r == IDYES;
#else
    if (g_backend == B_OSASCRIPT) {
        char *t = esc_applescript(title), *x = esc_applescript(text), *y = esc_applescript(yes), *n = esc_applescript(no);
        char* script = str_fmt("display dialog %s with title %s buttons {%s, %s} default button %s", x, t, n, y, y);
        char* out = NULL;
        int rc = osa(script, &out);
        int r = rc == 0 && out && strstr(out, yes) != NULL && strstr(out, "button returned:") != NULL &&
                !strncmp(strstr(out, "button returned:") + 16, yes, strlen(yes));
        free(out);
        free(script);
        free(t);
        free(x);
        free(y);
        free(n);
        return r;
    }
    if (g_backend == B_ZENITY) {
        char* x = esc_markup(text);
        char* ok = str_fmt("--ok-label=%s", yes);
        char* cancel = str_fmt("--cancel-label=%s", no);
        char* argv[] = {"zenity", "--question", "--title", (char*)title, "--text", x, "--width=520", ok, cancel, NULL};
        int rc = run_cmd(argv, NULL);
        free(x);
        free(ok);
        free(cancel);
        return rc == 0;
    }
    char* argv[] = {"kdialog", "--title", (char*)title, "--yes-label", (char*)yes, "--no-label", (char*)no, "--yesno", (char*)text, NULL};
    return run_cmd(argv, NULL) == 0;
#endif
}

char* ui_pick_folder(const char* title, const char* prompt) {
    if (!g_native) return term_folder(prompt);
#ifdef _WIN32
    (void)title;
    return win_folder(prompt);
#else
    char* out = NULL;
    int rc;
    if (g_backend == B_OSASCRIPT) {
        char* p = esc_applescript(prompt);
        char* script = str_fmt("POSIX path of (choose folder with prompt %s)", p);
        rc = osa(script, &out);
        free(script);
        free(p);
    } else if (g_backend == B_ZENITY) {
        char* t = str_fmt("%s", prompt);
        char* argv[] = {"zenity", "--file-selection", "--directory", "--title", t, NULL};
        rc = run_cmd(argv, &out);
        free(t);
    } else {
        const char* home = getenv("HOME");
        char* argv[] = {"kdialog", "--title", (char*)prompt, "--getexistingdirectory", (char*)(home ? home : "/"), NULL};
        rc = run_cmd(argv, &out);
    }
    (void)title;
    if (rc != 0 || !out) {
        free(out);
        return NULL;
    }
    trim(out);
    if (!*out) {
        free(out);
        return NULL;
    }
    size_t n = strlen(out);
    if (n > 1 && out[n - 1] == '/') out[n - 1] = 0;
    return out;
#endif
}

void ui_message(const char* title, const char* text, int error) {
    if (!g_native) {
        fprintf(error ? stderr : stdout, "\n%s\n", text);
        return;
    }
#ifdef _WIN32
    wbox(title, text, MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
#else
    if (g_backend == B_OSASCRIPT) {
        char *t = esc_applescript(title), *x = esc_applescript(text);
        char* script = str_fmt("display dialog %s with title %s buttons {\"OK\"} default button \"OK\" with icon %s", x, t, error ? "stop" : "note");
        osa(script, NULL);
        free(script);
        free(t);
        free(x);
    } else if (g_backend == B_ZENITY) {
        char* x = esc_markup(text);
        char* argv[] = {"zenity", error ? "--error" : "--info", "--title", (char*)title, "--text", x, "--width=520", NULL};
        run_cmd(argv, NULL);
        free(x);
    } else {
        char* argv[] = {"kdialog", "--title", (char*)title, error ? "--error" : "--msgbox", (char*)text, NULL};
        run_cmd(argv, NULL);
    }
#endif
}

CrackChoice ui_crack_warning(const char* title, const char* text) {
    if (!g_native) return term_crack(text);
#ifdef _WIN32
    return win_crack(title, text);
#else
    char* out = NULL;
    int rc;
    if (g_backend == B_OSASCRIPT) {
        char *t = esc_applescript(title), *x = esc_applescript(text);
        char* wait = str_fmt("%s\n\n(more options in 5 seconds)", text);
        char* xw = esc_applescript(wait);
        char* s1 = str_fmt("display dialog %s with title %s buttons {\"Quit\", \"Buy on Steam\"} default button \"Buy on Steam\" with icon caution giving up after 5", xw, t);
        rc = osa(s1, &out);
        CrackChoice res = CRACK_QUIT;
        int gave_up = rc == 0 && out && strstr(out, "gave up:true");
        if (rc == 0 && out && !gave_up && strstr(out, "Buy on Steam")) res = CRACK_BUY;
        free(out);
        out = NULL;
        if (gave_up) {
            char* s2 = str_fmt("display dialog %s with title %s buttons {\"Quit\", \"Buy on Steam\", \"%s\"} default button \"Buy on Steam\" with icon caution", x, t,
                               CRACK_OK_LABEL);
            rc = osa(s2, &out);
            if (rc == 0 && out && strstr(out, CRACK_OK_LABEL)) res = CRACK_CONTINUE;
            else if (rc == 0 && out && strstr(out, "Buy on Steam")) res = CRACK_BUY;
            free(s2);
            free(out);
        }
        free(s1);
        free(wait);
        free(xw);
        free(t);
        free(x);
        return res;
    }
    if (g_backend == B_ZENITY) {
        char* x = esc_markup(text);
        char* wait = str_fmt("%s\n\n(more options in 5 seconds)", x);
        char* a1[] = {"zenity", "--question", "--title", (char*)title, "--text", wait, "--width=560", "--ok-label=Buy on Steam", "--cancel-label=Quit",
                      "--timeout=5", NULL};
        rc = run_cmd(a1, NULL);
        free(wait);
        if (rc == 0) {
            free(x);
            return CRACK_BUY;
        }
        if (rc != 5) {
            free(x);
            return CRACK_QUIT;
        }
        char* a2[] = {"zenity", "--question", "--title", (char*)title, "--text", x, "--width=560", "--ok-label=" CRACK_OK_LABEL, "--cancel-label=Quit",
                      "--extra-button=Buy on Steam", NULL};
        rc = run_cmd(a2, &out);
        free(x);
        CrackChoice res = rc == 0 ? CRACK_CONTINUE : (out && strstr(out, "Buy on Steam") ? CRACK_BUY : CRACK_QUIT);
        free(out);
        return res;
    }
    char* wait = str_fmt("%s\n\n(more options in 5 seconds)", text);
    char* a1[] = {"kdialog", "--title", (char*)title, "--passivepopup", wait, "5", NULL};
    time_t t0 = time(NULL);
    run_cmd(a1, NULL);
    free(wait);
    while (time(NULL) - t0 < 5) sleep_ms(200);
    char* a2[] = {"kdialog", "--title", (char*)title, "--yes-label", CRACK_OK_LABEL, "--no-label", "Buy on Steam", "--cancel-label", "Quit",
                  "--warningyesnocancel", (char*)text, NULL};
    rc = run_cmd(a2, NULL);
    return rc == 0 ? CRACK_CONTINUE : rc == 1 ? CRACK_BUY : CRACK_QUIT;
#endif
}

void ui_open_url(const char* url) {
    printf("opening %s\n", url);
#ifdef _WIN32
    wchar_t* w = utf8_to_wide(url);
    ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
    free(w);
#elif defined(__APPLE__)
    char* argv[] = {"open", (char*)url, NULL};
    run_cmd(argv, NULL);
#else
    char* argv[] = {"xdg-open", (char*)url, NULL};
    run_cmd(argv, NULL);
#endif
}

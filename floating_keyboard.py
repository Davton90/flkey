"""
Floating Virtual Keyboard for Windows
=====================================
Like the Windows touch keyboard, but WITH the missing keys:
  Shift, Ctrl, Alt, Win, Tab, CapsLock, Esc, F1-F12, arrows, etc.

Features
--------
- Always-on-top floating window (borderless, draggable)
- WS_EX_NOACTIVATE so clicking keys does NOT steal focus from your target app
  (this is what makes it usable as a real on-screen keyboard)
- Sticky modifiers: tap Shift/Ctrl/Alt/Win once = hold for next key only,
  tap twice = lock down, tap again = release. Needed for Ctrl+C, Win+L,
  Alt+Tab, Shift+arrows selection, etc.
- CapsLock with live indicator, Backspace/space/arrows auto-repeat on hold
- Opacity + size controls, function-row toggle
- Zero dependencies: only Python stdlib (tkinter + ctypes). No pip install.

Run
---
    python floating_keyboard.py

Tested on Windows 10/11, Python 3.10+.
"""

import ctypes
import tkinter as tk
from ctypes import wintypes

# --------------------------------------------------------------------------
# Win32 SendInput plumbing (no external deps)
# --------------------------------------------------------------------------

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

# --- focus-restore prototypes (HWND is 64-bit: set restypes explicitly) ---
user32.GetForegroundWindow.restype = wintypes.HWND
user32.GetForegroundWindow.argtypes = []
user32.SetForegroundWindow.restype = wintypes.BOOL
user32.SetForegroundWindow.argtypes = [wintypes.HWND]
user32.GetWindowThreadProcessId.restype = wintypes.DWORD
try:
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND,
                                                ctypes.POINTER(wintypes.DWORD)]
except Exception:
    pass
try:
    kernel32.GetCurrentThreadId.restype = wintypes.DWORD
    kernel32.GetCurrentThreadId.argtypes = []
except Exception:
    pass

_SHELL_CLASSES = {"Shell_TrayWnd", "Progman", "WorkerW"}


def _fg_class_name(hwnd):
    buf = ctypes.create_unicode_buffer(64)
    try:
        if user32.GetClassNameW(hwnd, buf, 64):
            return buf.value
    except Exception:
        pass
    return ""


def _fg_process_image(hwnd):
    """Lowercased exe path of the window owner, '' when unknown."""
    try:
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if not pid.value:
            return ""
        pr = kernel32.OpenProcess(0x1000, False, pid.value)  # QUERY_LIMITED
        if not pr:
            return ""
        try:
            n = wintypes.DWORD(260)
            buf = ctypes.create_unicode_buffer(260)
            if kernel32.QueryFullProcessImageNameW(pr, 0, buf,
                                                   ctypes.byref(n)):
                return buf.value.lower()
        finally:
            kernel32.CloseHandle(pr)
    except Exception:
        pass
    return ""


def _is_shell_chrome(hwnd, self_hwnd=None):
    if not hwnd:
        return True
    if self_hwnd and hwnd == self_hwnd:
        return True
    cls = _fg_class_name(hwnd)
    if cls in _SHELL_CLASSES:
        return True
    try:
        ex = user32.GetWindowLongW(hwnd, -20)  # GWL_EXSTYLE
        if ex & 0x08000000:  # WS_EX_NOACTIVATE: other overlays
            return True
    except Exception:
        pass
    if cls in ("Windows.UI.Core.CoreWindow",
               "XamlExplorerHostIslandWindow", "DV2ControlHost"):
        img = _fg_process_image(hwnd)
        if any(k in img for k in ("startmenu", "searchhost",
                                  "shellexperience", "textinputhost",
                                  "searchui")):
            return True
        if not img:
            return True
        return False
    return False


def _force_foreground(target):
    try:
        if not target or not user32.IsWindow(target):
            return
        if user32.IsIconic(target):
            user32.ShowWindow(target, 9)  # SW_RESTORE
        fg = user32.GetForegroundWindow()
        if fg == target:
            return
        our = kernel32.GetCurrentThreadId()
        fg_thread = 0
        if fg:
            pid = wintypes.DWORD()
            fg_thread = user32.GetWindowThreadProcessId(fg,
                                                        ctypes.byref(pid))
        pid2 = wintypes.DWORD()
        tgt_thread = user32.GetWindowThreadProcessId(target,
                                                     ctypes.byref(pid2))
        a_fg = a_tg = False
        try:
            if fg_thread and fg_thread != our:
                a_fg = bool(user32.AttachThreadInput(our, fg_thread, True))
            if tgt_thread and tgt_thread != our and tgt_thread != fg_thread:
                a_tg = bool(user32.AttachThreadInput(our, tgt_thread, True))
            if fg_thread and tgt_thread and fg_thread != tgt_thread:
                user32.AttachThreadInput(fg_thread, tgt_thread, True)
            user32.SetForegroundWindow(target)
        finally:
            try:
                if fg_thread and tgt_thread and fg_thread != tgt_thread:
                    user32.AttachThreadInput(fg_thread, tgt_thread, False)
                if a_tg:
                    user32.AttachThreadInput(our, tgt_thread, False)
                if a_fg:
                    user32.AttachThreadInput(our, fg_thread, False)
            except Exception:
                pass
    except Exception:
        pass

KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_UNICODE = 0x0004
KEYEVENTF_SCANCODE = 0x0008
KEYEVENTF_EXTENDEDKEY = 0x0001

INPUT_KEYBOARD = 1

PUL = ctypes.POINTER(ctypes.c_ulong)


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", wintypes.WORD),
        ("wScan", wintypes.WORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", PUL),
    ]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [
        ("dx", wintypes.LONG),
        ("dy", wintypes.LONG),
        ("mouseData", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", PUL),
    ]


class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [
        ("uMsg", wintypes.DWORD),
        ("wParamL", wintypes.WORD),
        ("wParamH", wintypes.WORD),
    ]


class INPUT_I(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT), ("hi", HARDWAREINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("ii", INPUT_I)]


def _send_input(*inputs):
    n = len(inputs)
    arr = (INPUT * n)(*inputs)
    user32.SendInput(n, arr, ctypes.sizeof(INPUT))


def _extra():
    return PUL(ctypes.c_ulong(0))


def vk_down(vk, extended=False):
    flags = KEYEVENTF_EXTENDEDKEY if extended else 0
    ki = KEYBDINPUT(wVk=vk, wScan=0, dwFlags=flags, time=0, dwExtraInfo=_extra())
    _send_input(INPUT(type=INPUT_KEYBOARD, ii=INPUT_I(ki=ki)))


def vk_up(vk, extended=False):
    flags = KEYEVENTF_KEYUP | (KEYEVENTF_EXTENDEDKEY if extended else 0)
    ki = KEYBDINPUT(wVk=vk, wScan=0, dwFlags=flags, time=0, dwExtraInfo=_extra())
    _send_input(INPUT(type=INPUT_KEYBOARD, ii=INPUT_I(ki=ki)))


def vk_tap(vk, extended=False):
    vk_down(vk, extended)
    vk_up(vk, extended)


def type_unicode(text):
    """Type text via KEYEVENTF_UNICODE (layout-independent, no Shift needed)."""
    for ch in text:
        code = ord(ch)
        # handle BMP only; emoji outside BMP would need surrogates - skip for keyboard
        if code > 0xFFFF:
            continue
        ki_dn = KEYBDINPUT(wVk=0, wScan=code, dwFlags=KEYEVENTF_UNICODE,
                           time=0, dwExtraInfo=_extra())
        ki_up = KEYBDINPUT(wVk=0, wScan=code,
                           dwFlags=KEYEVENTF_UNICODE | KEYEVENTF_KEYUP,
                           time=0, dwExtraInfo=_extra())
        _send_input(INPUT(type=INPUT_KEYBOARD, ii=INPUT_I(ki=ki_dn)),
                    INPUT(type=INPUT_KEYBOARD, ii=INPUT_I(ki=ki_up)))


def is_caps_on():
    return bool(user32.GetKeyState(0x14) & 0x0001)


# --------------------------------------------------------------------------
# Virtual-key constants
# --------------------------------------------------------------------------

VK_SHIFT = 0x10
VK_CTRL = 0x11
VK_ALT = 0x12          # VK_MENU
VK_LWIN = 0x5B
VK_APPS = 0x5D         # context-menu key
VK_ESC = 0x1B
VK_TAB = 0x09
VK_CAPS = 0x14
VK_ENTER = 0x0D
VK_BACK = 0x08
VK_SPACE = 0x20
VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN = 0x25, 0x26, 0x27, 0x28
VK_DEL = 0x2E
VK_PRTSC = 0x2C

# OEM keys for US layout (needed when sending Ctrl/Alt/Win combos as VK)
OEM = {
    ";": 0xBA, "=": 0xBB, ",": 0xBC, "-": 0xBD, ".": 0xBE,
    "/": 0xBF, "`": 0xC0, "[": 0xDB, "\\": 0xDC, "]": 0xDD, "'": 0xDE,
}

EXTENDED_VKS = {VK_LWIN, VK_APPS, VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN,
                VK_DEL, VK_PRTSC}
for _f in range(0x70, 0x7C):
    pass  # F-keys don't need extended flag


def char_to_vk(ch):
    """Map a printable char key to its base VK (for Ctrl/Alt/Win combos)."""
    if ch.isalpha():
        return ord(ch.upper())
    if ch.isdigit():
        return ord(ch)
    return OEM.get(ch, 0)


# ==========================================================================
# Keyboard app
# ==========================================================================

BG = "#1e1e1e"
BAR_BG = "#252526"
KEY_BG = "#2d2d2d"
KEY_FG = "#ffffff"
KEY_ACTIVE = "#3a3a3a"
SPECIAL_BG = "#333338"
MOD_HELD_BG = "#4cc2ff"
MOD_HELD_FG = "#000000"
MOD_LOCK_BG = "#0078d4"
MOD_LOCK_FG = "#ffffff"
CAPS_ON_BG = "#0078d4"


class FloatKeyboard:
    def __init__(self, root):
        self.root = root
        root.title("FloatKeys")
        root.configure(bg=BG)
        root.attributes("-topmost", True)
        root.overrideredirect(True)  # borderless floating feel
        try:
            root.attributes("-alpha", 0.96)
        except tk.TclError:
            pass

        # state: off | held (one-shot) | locked
        self.mods = {"shift": "off", "ctrl": "off", "alt": "off", "win": "off"}
        self.mod_vk = {"shift": VK_SHIFT, "ctrl": VK_CTRL,
                       "alt": VK_ALT, "win": VK_LWIN}
        self.mod_buttons = {"shift": [], "ctrl": [], "alt": [], "win": []}
        self.caps_btn = None
        self.all_keys = []       # (button, base_font_size_factor)
        self.font_size = 10
        self.opacity = 0.96
        self.show_fn = True
        self.fn_frame = None
        self.repeat_job = None
        self.repeat_key = None

        self._build_bar()
        self._build_rows()
        self._apply_noactivate()
        self._refresh_mods()
        self._refresh_caps()

        # Focus restore: opening this window via Explorer/Start steals
        # foreground from the browser search box the user just clicked.
        # Remember our hwnd + last target so the first tap lands right.
        self._hwnd = None
        self._last_fg = None
        self._startup_fg = None
        try:
            self._hwnd = self.root.winfo_id()
            self._startup_fg = user32.GetForegroundWindow()
            self._track_fg(self._startup_fg)
        except Exception:
            pass
        self._restore_startup_focus()

        # keep caps indicator fresh (user may press physical CapsLock)
        self._poll_caps()
        self._poll_fg()

        # start bottom-right-ish
        root.update_idletasks()
        w = root.winfo_reqwidth()
        h = root.winfo_reqheight()
        sw = root.winfo_screenwidth()
        sh = root.winfo_screenheight()
        root.geometry(f"+{sw - w - 40}+{sh - h - 80}")

    # ------------------------------------------------------------------
    # window chrome
    # ------------------------------------------------------------------
    def _build_bar(self):
        bar = tk.Frame(self.root, bg=BAR_BG)
        bar.pack(fill=tk.X)
        bar.bind("<Button-1>", self._drag_start)
        bar.bind("<B1-Motion>", self._drag_move)

        grip = tk.Label(bar, text="\u280ffloatkeys  \u2014 drag to move",
                        bg=BAR_BG, fg="#cccccc",
                        font=("Segoe UI", 9))
        grip.pack(side=tk.LEFT, padx=8, pady=4)
        grip.bind("<Button-1>", self._drag_start)
        grip.bind("<B1-Motion>", self._drag_move)

        def mini(text, tip, cmd):
            b = tk.Button(bar, text=text, bg=BAR_BG, fg="#cccccc",
                          activebackground="#3e3e42", activeforeground="#fff",
                          bd=0, relief="flat", font=("Segoe UI", 9),
                          takefocus=0, padx=6, pady=1, command=cmd)
            b.pack(side=tk.RIGHT, padx=1)
            return b

        mini("\u2715", "Close", self.root.destroy)
        mini("Fn", "Show/hide function row", self._toggle_fn)
        mini("A-", "Smaller", lambda: self._resize(-1))
        mini("A+", "Bigger", lambda: self._resize(1))
        mini("\u25d0", "Less opaque", lambda: self._set_opacity(-0.05))
        mini("\u25d1", "More opaque", lambda: self._set_opacity(0.05))

    def _drag_start(self, e):
        self._dx = e.x_root - self.root.winfo_x()
        self._dy = e.y_root - self.root.winfo_y()

    def _drag_move(self, e):
        self.root.geometry(f"+{e.x_root - self._dx}+{e.y_root - self._dy}")

    def _set_opacity(self, delta):
        self.opacity = min(1.0, max(0.4, self.opacity + delta))
        try:
            self.root.attributes("-alpha", self.opacity)
        except tk.TclError:
            pass

    def _resize(self, step):
        self.font_size = min(16, max(7, self.font_size + step))
        for btn, factor in self.all_keys:
            btn.configure(font=("Segoe UI", max(7, int(self.font_size * factor))))

    def _toggle_fn(self):
        self.show_fn = not self.show_fn
        if self.fn_frame is not None:
            if self.show_fn:
                self.fn_frame.pack(fill=tk.X, padx=6, pady=(6, 0), before=self.rows_container)
            else:
                self.fn_frame.pack_forget()

    def _apply_noactivate(self):
        """WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW: clicks never steal focus."""
        try:
            self.root.update_idletasks()
            hwnd = self.root.winfo_id()
            GWL_EXSTYLE = -20
            WS_EX_NOACTIVATE = 0x08000000
            WS_EX_TOOLWINDOW = 0x00000080
            style = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
            user32.SetWindowLongW(hwnd, GWL_EXSTYLE,
                                  style | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)
            # re-assert topmost without activating
            HWND_TOPMOST = -1
            SWP_NOMOVE, SWP_NOSIZE, SWP_NOACTIVATE = 0x0002, 0x0001, 0x0010
            SWP_SHOWWINDOW = 0x0040
            user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW)
        except Exception:
            pass  # non-Windows or restricted env; topmost attribute still helps

    # ------------------------------------------------------------------
    # focus restore (first-tap fix)
    # ------------------------------------------------------------------
    def _track_fg(self, fg):
        try:
            if not fg or (self._hwnd and fg == self._hwnd):
                return
            if not user32.IsWindow(fg) or not user32.IsWindowVisible(fg):
                return
            if user32.IsIconic(fg):
                return
            if _is_shell_chrome(fg, self._hwnd):
                return
            if getattr(self, "_startup_fg", None):
                if _fg_class_name(fg) in ("CabinetWClass",
                                          "ExploreWClass"):
                    return
            self._last_fg = fg
        except Exception:
            pass

    def _poll_fg(self):
        try:
            self._track_fg(user32.GetForegroundWindow())
        except Exception:
            pass
        try:
            self.root.after(500, self._poll_fg)
        except Exception:
            pass

    def _ensure_focus(self):
        """Restore typing target before SendInput (see keyboard.c)."""
        import time
        try:
            fg = user32.GetForegroundWindow()
        except Exception:
            return
        if self._hwnd and fg == self._hwnd:
            if self._last_fg and user32.IsWindow(self._last_fg):
                _force_foreground(self._last_fg)
                time.sleep(0.03)
            return
        try:
            shell = _is_shell_chrome(fg, self._hwnd)
        except Exception:
            shell = False
        if fg and not shell:
            try:
                visible = bool(user32.IsWindowVisible(fg))
            except Exception:
                visible = True
            if visible:
                if (self._startup_fg and fg == self._startup_fg
                        and self._last_fg and self._last_fg != fg
                        and user32.IsWindow(self._last_fg)):
                    _force_foreground(self._last_fg)
                    time.sleep(0.03)
                    return
                self._last_fg = fg
                self._startup_fg = None
                return
        if self._last_fg:
            try:
                if user32.IsWindow(self._last_fg):
                    _force_foreground(self._last_fg)
                    time.sleep(0.03)
            except Exception:
                pass

    def _restore_startup_focus(self):
        import time
        try:
            fg = self._startup_fg or user32.GetForegroundWindow()
        except Exception:
            return
        if not fg or (self._hwnd and fg == self._hwnd):
            return
        try:
            if _is_shell_chrome(fg, self._hwnd):
                pass
            else:
                cls = _fg_class_name(fg)
                if cls not in ("CabinetWClass", "ExploreWClass"):
                    self._last_fg = fg
                    self._startup_fg = None
                    return
            # Launcher foreground: peek beneath it for the real target.
            found = self._find_window_beneath(fg)
            if found:
                self._last_fg = found
                _force_foreground(found)
                time.sleep(0.03)
            elif not _is_shell_chrome(fg, self._hwnd):
                self._last_fg = fg
        except Exception:
            pass

    def _find_window_beneath(self, skip):
        found = []
        try:
            CMP = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND,
                                     wintypes.LPARAM)
        except Exception:
            return None

        def cb(h, _p):
            try:
                if h == skip or (self._hwnd and h == self._hwnd):
                    return True
                if not user32.IsWindowVisible(h) or user32.IsIconic(h):
                    return True
                if _is_shell_chrome(h, self._hwnd):
                    return True
                if _fg_class_name(h) in ("CabinetWClass",
                                         "ExploreWClass"):
                    return True
                try:
                    ex = user32.GetWindowLongW(h, -20)
                    if ex & 0x80:  # WS_EX_TOOLWINDOW
                        return True
                except Exception:
                    pass
                r = wintypes.RECT()
                try:
                    user32.GetWindowRect(h, ctypes.byref(r))
                    if r.right <= r.left or r.bottom <= r.top:
                        return True
                except Exception:
                    pass
                found.append(h)
                return False
            except Exception:
                return True

        try:
            user32.EnumWindows(CMP(cb), 0)
        except Exception:
            pass
        return found[0] if found else None

    # ------------------------------------------------------------------
    # rows
    # ------------------------------------------------------------------
    def _build_rows(self):
        self.rows_container = tk.Frame(self.root, bg=BG)
        self.rows_container.pack(fill=tk.BOTH, expand=True, padx=0, pady=0)

        # --- function row (toggleable) ---
        self.fn_frame = tk.Frame(self.rows_container, bg=BG)
        fn_keys = [("Esc", "special", VK_ESC, 1.2)]
        for i in range(1, 13):
            fn_keys.append((f"F{i}", "special", 0x6F + i, 1.0))
        fn_keys += [("PrtSc", "special", VK_PRTSC, 1.2),
                    ("Del", "special", VK_DEL, 1.2)]
        self._add_row(self.fn_frame, fn_keys, special_bg=True)
        self.fn_frame.pack(fill=tk.X, padx=6, pady=(6, 0))

        # --- number row ---
        num = [("`", "char", ("`", "~"), 1.0)]
        for d, s in [("1", "!"), ("2", "@"), ("3", "#"), ("4", "$"),
                     ("5", "%"), ("6", "^"), ("7", "&"), ("8", "*"),
                     ("9", "("), ("0", ")")]:
            num.append((d, "char", (d, s), 1.0))
        num += [("-", "char", ("-", "_"), 1.0),
                ("=", "char", ("=", "+"), 1.0),
                ("Bksp", "special", VK_BACK, 2.0)]
        self._add_row(self.rows_container,
                      num, pack_kwargs={"padx": 6, "pady": (6, 0)})

        # --- QWERTY top ---
        top = [("Tab", "special", VK_TAB, 1.6)]
        for ch in "QWERTYUIOP":
            top.append((ch, "char", (ch.lower(), ch.upper()), 1.0))
        top += [("[", "char", ("[", "{"), 1.0),
                ("]", "char", ("]", "}"), 1.0),
                ("\\", "char", ("\\", "|"), 1.5)]
        self._add_row(self.rows_container, top,
                      pack_kwargs={"padx": 6, "pady": (4, 0)})

        # --- home row ---
        home = [("Caps", "caps", None, 1.8)]
        for ch in "ASDFGHJKL":
            home.append((ch, "char", (ch.lower(), ch.upper()), 1.0))
        home += [(";", "char", (";", ":"), 1.0),
                 ("'", "char", ("'", '"'), 1.0),
                 ("Enter", "special", VK_ENTER, 2.2)]
        self._add_row(self.rows_container, home,
                      pack_kwargs={"padx": 6, "pady": (4, 0)})

        # --- shift row (+ Up arrow at the end for inverted-T) ---
        sh = [("Shift", "mod", "shift", 2.2)]
        for ch in "ZXCVBNM":
            sh.append((ch, "char", (ch.lower(), ch.upper()), 1.0))
        sh += [(",", "char", (",", "<"), 1.0),
               (".", "char", (".", ">"), 1.0),
               ("/", "char", ("/", "?"), 1.0),
               ("Shift", "mod", "shift", 2.2),
               ("\u25b2", "special", VK_UP, 1.0)]
        self._add_row(self.rows_container, sh,
                      pack_kwargs={"padx": 6, "pady": (4, 0)})

        # --- bottom row: the keys the Win touch keyboard hides ---
        bot = [("Ctrl", "mod", "ctrl", 1.5),
               ("Win", "mod", "win", 1.5),
               ("Alt", "mod", "alt", 1.5),
               ("Space", "space", None, 6.0),
               ("Alt", "mod", "alt", 1.3),
               ("\u2630", "special", VK_APPS, 1.3),
               ("Ctrl", "mod", "ctrl", 1.3),
               ("\u25c0", "special", VK_LEFT, 1.0),
               ("\u25bc", "special", VK_DOWN, 1.0),
               ("\u25b6", "special", VK_RIGHT, 1.0)]
        self._add_row(self.rows_container, bot,
                      pack_kwargs={"padx": 6, "pady": 6})

        hint = tk.Label(
            self.rows_container,
            text=("Tap Shift/Ctrl/Alt/Win once = hold for next key  \u2022  "
                  "twice = lock  \u2022  e.g. Ctrl+C, Win+L, Alt+Tab, Shift+\u25b6"),
            bg=BG, fg="#808080", font=("Segoe UI", 8))
        hint.pack(pady=(0, 4))

    def _add_row(self, parent, keys, special_bg=False, pack_kwargs=None):
        frame = tk.Frame(parent, bg=BG)
        frame.pack(fill=tk.X, **(pack_kwargs or {}))
        cols = 0
        for label, kind, payload, w in keys:
            span = max(1, int(round(w * 10)))
            if special_bg or kind in ("special", "caps", "mod"):
                bg = SPECIAL_BG
            else:
                bg = KEY_BG
            btn = tk.Button(
                frame, text=self._display_label(label, kind, payload),
                bg=bg,
                fg=KEY_FG, activebackground=KEY_ACTIVE,
                activeforeground=KEY_FG, bd=0, relief="flat",
                font=("Segoe UI", self.font_size),
                takefocus=0, padx=2, pady=6)
            btn.grid(row=0, column=cols, columnspan=span,
                     sticky="nsew", padx=2, pady=2)
            cols += span
            self.all_keys.append((btn, 1.0 if len(label) <= 2 else 0.85))

            # fire on press (faster + works with NOACTIVATE); no `command`
            btn.bind("<ButtonPress-1>",
                     lambda e, l=label, k=kind, p=payload, b=btn:
                     self._on_key_press(l, k, p, b))
            btn.bind("<ButtonRelease-1>", lambda e: self._stop_repeat())

            if kind == "mod":
                self.mod_buttons[payload].append(btn)
            if kind == "caps":
                self.caps_btn = btn

        for c in range(cols):
            frame.grid_columnconfigure(c, weight=1)

    @staticmethod
    def _display_label(label, kind, payload):
        if kind == "char":
            lo, hi = payload
            # show shifted char small above? keep simple: show "a A" style
            if lo.isalpha():
                return lo.upper()
            return f"{lo} {hi}" if len(lo) == 1 else label
        return label

    # ------------------------------------------------------------------
    # key handling
    # ------------------------------------------------------------------
    def _on_key_press(self, label, kind, payload, btn):
        self._flash(btn)
        if kind == "mod":
            self._toggle_mod(payload)
            return
        if kind == "caps":
            vk_tap(VK_CAPS)
            self.root.after(50, self._refresh_caps)
            return
        if kind == "space":
            if self._combo_active():
                # e.g. Ctrl+Space: send as VK combo, not unicode
                self._start_repeat_if_wanted("Space", VK_SPACE)
                self._emit_vk_with_mods(VK_SPACE)
            else:
                self._emit_text(" ")
            return
        if kind == "char":
            lo, hi = payload
            ch = hi if self._is_shifted_for_char(lo) else lo
            if self._combo_active():
                vk = char_to_vk(lo)
                if vk:
                    self._emit_vk_with_mods(vk, extended=False)
                else:
                    self._emit_text(ch)
            else:
                self._emit_text(ch)
            return
        if kind == "special":
            self._start_repeat_if_wanted(label, payload)
            self._emit_vk_with_mods(payload,
                                    extended=payload in EXTENDED_VKS)
            return

    # -- modifiers -----------------------------------------------------
    def _toggle_mod(self, name):
        cur = self.mods[name]
        vk = self.mod_vk[name]
        ext = name == "win"
        if cur == "off":
            self.mods[name] = "held"
            vk_down(vk, ext)
        elif cur == "held":
            self.mods[name] = "locked"  # already down, keep it
        else:
            self.mods[name] = "off"
            vk_up(vk, ext)
        self._refresh_mods()

    def _mod_state(self):
        return {"caps": is_caps_on(),
                "shift": self.mods["shift"] in ("held", "locked")}

    def _is_shifted_for_char(self, lo):
        st = self._mod_state()
        if lo.isalpha():
            return st["caps"] != st["shift"]
        return st["shift"]

    def _combo_active(self):
        return (self.mods["ctrl"] != "off" or self.mods["alt"] != "off"
                or self.mods["win"] != "off")

    def _consume_held(self):
        """Release one-shot ('held') modifiers after a key was sent."""
        for name, state in list(self.mods.items()):
            if state == "held":
                vk_up(self.mod_vk[name], extended=(name == "win"))
                self.mods[name] = "off"
        self._refresh_mods()

    # -- emit ----------------------------------------------------------
    def _emit_text(self, text):
        # Shift is consumed via the character choice itself; physically
        # release Shift while typing unicode so e.g. Shift+click doesn't
        # produce double-shift weirdness, then re-hold if locked.
        self._ensure_focus()
        shift_state = self.mods["shift"]
        shift_down = shift_state in ("held", "locked")
        if shift_down and not self._combo_active():
            vk_up(VK_SHIFT)
        try:
            type_unicode(text)
        finally:
            if shift_down and not self._combo_active():
                if shift_state == "locked":
                    vk_down(VK_SHIFT)
                else:
                    self.mods["shift"] = "off"
            self._consume_held()

    def _emit_vk_with_mods(self, vk, extended=False):
        # modifiers are already physically down (key_down on toggle),
        # so just tap the main key, then release one-shot mods.
        self._ensure_focus()
        vk_tap(vk, extended)
        self._consume_held()

    # -- visuals -------------------------------------------------------
    def _refresh_mods(self):
        colors = {"off": (SPECIAL_BG, KEY_FG),
                  "held": (MOD_HELD_BG, MOD_HELD_FG),
                  "locked": (MOD_LOCK_BG, MOD_LOCK_FG)}
        for name, btns in self.mod_buttons.items():
            bg, fg = colors[self.mods[name]]
            for b in btns:
                try:
                    b.configure(bg=bg, fg=fg)
                except tk.TclError:
                    pass

    def _refresh_caps(self):
        if self.caps_btn is None:
            return
        try:
            if is_caps_on():
                self.caps_btn.configure(bg=CAPS_ON_BG, fg="#ffffff")
            else:
                self.caps_btn.configure(bg=SPECIAL_BG, fg=KEY_FG)
        except tk.TclError:
            pass

    def _poll_caps(self):
        self._refresh_caps()
        self.root.after(500, self._poll_caps)

    def _flash(self, btn):
        try:
            orig = btn.cget("bg")
            btn.configure(bg=KEY_ACTIVE)
            self.root.after(80, lambda: self._restore_bg(btn, orig))
        except tk.TclError:
            pass

    def _restore_bg(self, btn, orig):
        try:
            # don't override modifier highlight
            for name, btns in self.mod_buttons.items():
                if btn in btns:
                    self._refresh_mods()
                    return
            if btn == self.caps_btn:
                self._refresh_caps()
                return
            btn.configure(bg=orig)
        except tk.TclError:
            pass

    # -- auto-repeat (hold Backspace / arrows / space) ------------------
    REPEATABLE = {"Bksp", "\u25c0", "\u25b6", "\u25b2", "\u25bc",
                  "Space", "Del"}

    def _start_repeat_if_wanted(self, label, vk):
        self._stop_repeat()
        if label not in self.REPEATABLE:
            return
        self.repeat_key = (label, vk)
        self.repeat_job = self.root.after(450, self._repeat_tick)

    def _repeat_tick(self):
        if not self.repeat_key:
            return
        label, vk = self.repeat_key
        vk_tap(vk, vk in EXTENDED_VKS)
        self.repeat_job = self.root.after(60, self._repeat_tick)

    def _stop_repeat(self):
        self.repeat_key = None
        if self.repeat_job is not None:
            try:
                self.root.after_cancel(self.repeat_job)
            except tk.TclError:
                pass
            self.repeat_job = None


def main():
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        pass
    root = tk.Tk()
    FloatKeyboard(root)
    root.mainloop()


if __name__ == "__main__":
    main()

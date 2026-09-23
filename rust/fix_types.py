#!/usr/bin/env python3
"""One-shot type/import fixes for floatkeyrusts."""
from pathlib import Path

ROOT = Path(r"Q:\test project\b\floating-keyboard\rust\src")

def load(name):
    return (ROOT / name).read_text(encoding="utf-8")

def save(name, text):
    (ROOT / name).write_text(text, encoding="utf-8")
    print(f"updated {name}")

# ---------- app.rs ----------
t = load("app.rs")
t = t.replace(
    "pub fn rgb(r: u32, g: u32, b: u32) -> u32 { r | (g << 8) | (b << 16) }",
    "pub const fn rgb(r: u32, g: u32, b: u32) -> u32 { r | (g << 8) | (b << 16) }",
)
if "Accessibility" not in t:
    t = t.replace(
        "use windows_sys::Win32::System::SystemInformation::GetTickCount;",
        "use windows_sys::Win32::System::SystemInformation::GetTickCount;\n"
        "use windows_sys::Win32::UI::Accessibility::HWINEVENTHOOK;",
    )
# is_ext_key: match on u16
t = t.replace(
    "fn is_ext_key(vk: u32) -> bool {\n    matches!(vk,\n",
    "fn is_ext_key(vk: u32) -> bool {\n    matches!(vk as u16,\n",
)
# char_to_vk returns
t = t.replace("if c.is_ascii_digit() { return lo; }", "if c.is_ascii_digit() { return lo as u32; }")
for name in [
    "VK_OEM_1", "VK_OEM_PLUS", "VK_OEM_COMMA", "VK_OEM_MINUS", "VK_OEM_PERIOD",
    "VK_OEM_2", "VK_OEM_3", "VK_OEM_4", "VK_OEM_5", "VK_OEM_6", "VK_OEM_7",
]:
    t = t.replace(f"=> {name},", f"=> {name} as u32,")
t = t.replace('("AltTab", 0x4, VK_TAB),', '("AltTab", 0x4, VK_TAB as u32),')
t = t.replace('("Shot", 0x8, VK_SNAPSHOT),', '("Shot", 0x8, VK_SNAPSHOT as u32),')
save("app.rs", t)

# ---------- layout.rs ----------
t = load("layout.rs")
if not t.startswith("use windows_sys::Win32::Foundation::{TRUE, FALSE};"):
    t = "use windows_sys::Win32::Foundation::{TRUE, FALSE};\n" + t
# add_key vk stays u32 (Key.vk is u32) — cast VK_* at call sites instead:
# actually easier: change VK_* passed to add_key with as u32 via regex on add_key lines
import re
t = re.sub(
    r'(self\.add_key\([^;]*?,\s*)VK_([A-Z0-9_]+)',
    r'\1VK_\2 as u32',
    t,
)
# fix double as u32 if any
t = t.replace(" as u32 as u32", " as u32")
# VK_F1 + i - 1
t = t.replace(
    "self.add_key(&t, Kind::Special, VK_F1 + i - 1, 0, 0, 0, 1.0, 0);",
    "self.add_key(&t, Kind::Special, (VK_F1 as u32) + i.wrapping_sub(1), 0, 0, 0, 1.0, 0);",
)
t = t.replace("SetBkMode(dc, TRANSPARENT)", "SetBkMode(dc, TRANSPARENT as i32)")
t = t.replace("self.pop_rc[i]", "self.pop_rc[i as usize]")
save("layout.rs", t)

# ---------- state.rs ----------
t = load("state.rs")
if "Foundation::{TRUE, FALSE}" not in t and "Foundation::*" not in t:
    t = t.replace(
        "use windows_sys::Win32::UI::WindowsAndMessaging::*;",
        "use windows_sys::Win32::UI::WindowsAndMessaging::*;\n"
        "use windows_sys::Win32::Foundation::{TRUE, FALSE};",
    )
if "System::Threading::Sleep" not in t and "System::Threading::*" not in t:
    t = t.replace(
        "use windows_sys::Win32::UI::WindowsAndMessaging::*;",
        "use windows_sys::Win32::UI::WindowsAndMessaging::*;\n"
        "use windows_sys::Win32::System::Threading::Sleep;",
    )
# Cast VK_* when passed to functions expecting u32 / comparisons with u32 vk
# Common patterns: vk_tap(VK_x), vk_up(VK_x), vk_down(VK_x), emit_vk(VK_x), == VK_x, VK_BACK, etc.
def cast_vk(m):
    return f"{m.group(1)}VK_{m.group(2)} as u32"

# only cast bare VK_ tokens that are NOT already followed by as u32
t = re.sub(r'(?<![\w.])VK_([A-Z0-9_]+)(?!\s*as)', lambda m: f"VK_{m.group(1)} as u32", t)
# fix bad places: matches!(vk as u16, ...) style not here; fix double casts
t = t.replace(" as u32 as u32", " as u32")
# matches! arms where scrutinee is u16? if vk is u32 and arms cast to u32 that's fine
# but if arms became `VK_X as u32` inside matches!(vk, ...) with u32 - good
# Fix: if there is matches!(vk as u16 ... already
# F-key arithmetic: vk - VK_F1 as u32  might parse as (vk - VK_F1) as u32
# Fix: vk as u32 - VK_F1 as u32 -> already if vk is u32 and VK_F1 as u32
t = t.replace("vk - VK_F1 as u32 + 1", "(vk.wrapping_sub(VK_F1 as u32)) + 1")
t = t.replace("vk >= VK_F1 as u32 && vk <= VK_F12 as u32", "vk >= VK_F1 as u32 && vk <= VK_F12 as u32")
# KEY_SET_VALUE etc registry flags already u32
# GetKeyState already has as i32 - might have become as u32 as i32
t = t.replace("VK_CAPITAL as u32 as i32", "VK_CAPITAL as i32")
t = t.replace("VK_CAPITAL as u32", "VK_CAPITAL as i32") if "GetKeyState(VK_CAPITAL" in t else t
# Fix GetKeyState specifically
t = re.sub(r"GetKeyState\(VK_CAPITAL as u32\)", "GetKeyState(VK_CAPITAL as i32)", t)
t = re.sub(r"GetKeyState\(VK_CAPITAL as u32 as i32\)", "GetKeyState(VK_CAPITAL as i32)", t)
# array of modifiers
t = t.replace(
    "[VK_SHIFT as u32, VK_CONTROL as u32, VK_MENU as u32, VK_LWIN as u32]",
    "[VK_SHIFT as u32, VK_CONTROL as u32, VK_MENU as u32, VK_LWIN as u32]",
)
save("state.rs", t)

# ---------- dict.rs ----------
t = load("dict.rs")
t = re.sub(r'(?<![\w.])VK_([A-Z0-9_]+)(?!\s*as)', lambda m: f"VK_{m.group(1)} as u32", t)
t = t.replace(" as u32 as u32", " as u32")
save("dict.rs", t)

# ---------- clip.rs ----------
t = load("clip.rs")
if "const CF_UNICODETEXT" not in t:
    t = t.replace(
        "use windows_sys::Win32::UI::WindowsAndMessaging::*;",
        "use windows_sys::Win32::UI::WindowsAndMessaging::*;\n"
        "const CF_UNICODETEXT: u32 = 13;\n"
        "const CF_BITMAP: u32 = 2;\n"
        "const CF_DIB: u32 = 8;",
    )
if "Foundation::{TRUE, FALSE}" not in t and "Foundation::*" not in t:
    t = t.replace(
        "use windows_sys::Win32::Foundation::*;",
        "use windows_sys::Win32::Foundation::*;",
    )
t = t.replace("GetDIBits(dc, src, 0, bm.bmHeight,", "GetDIBits(dc, src, 0, bm.bmHeight as u32,")
t = t.replace("src_bits, &bi, DIB_RGB_COLORS, SRCCOPY);", "src_bits as *const _, &bi, DIB_RGB_COLORS, SRCCOPY);")
save("clip.rs", t)

# ---------- settings.rs ----------
t = load("settings.rs")
t = t.replace("!= ERROR_SUCCESS as i32", "!= 0")
t = t.replace("== ERROR_SUCCESS as i32", "== 0")
# RegCreateKeyExW may not exist — use RegCreateKeyW + RegSetValueExW path
# Check: if RegCreateKeyExW missing from crate, fall back
save("settings.rs", t)

# ---------- main.rs ----------
t = load("main.rs")
t = t.replace('w!("shcore.dll")', 'w("shcore.dll")')
t = t.replace('w!("FloatKeys")', 'w("FloatKeys")')
# imports
header_adds = []
if "Input::KeyboardAndMouse" not in t:
    header_adds.append("use windows_sys::Win32::UI::Input::KeyboardAndMouse::*;")
if "System::Threading" not in t:
    header_adds.append("use windows_sys::Win32::System::Threading::Sleep;")
if "GetTickCount" not in t:
    header_adds.append("use windows_sys::Win32::System::SystemInformation::GetTickCount;")
if "TRUE" not in t.split("fn ")[0]:
    header_adds.append("use windows_sys::Win32::Foundation::{TRUE, FALSE};")
if "AddClipboardFormatListener" not in t:
    header_adds.append(
        "use windows_sys::Win32::UI::WindowsAndMessaging::{AddClipboardFormatListener, SetCapture, ReleaseCapture, InvalidateRect};"
    )
if header_adds:
    anchor = "use windows_sys::Win32::UI::WindowsAndMessaging::*;"
    if anchor in t:
        t = t.replace(anchor, anchor + "\n" + "\n".join(header_adds), 1)
    else:
        t = t.replace("use dict::*;", "use dict::*;\n" + "\n".join(header_adds), 1)

# Cast VK_* in main
t = re.sub(r'(?<![\w.])VK_([A-Z0-9_]+)(?!\s*as)', lambda m: f"VK_{m.group(1)} as u32", t)
t = t.replace(" as u32 as u32", " as u32")
t = re.sub(r"GetKeyState\(VK_CAPITAL as u32\)", "GetKeyState(VK_CAPITAL as i32)", t)
t = re.sub(r"GetKeyState\(VK_CAPITAL as u32 as i32\)", "GetKeyState(VK_CAPITAL as i32)", t)
t = t.replace("id_child == CHILDID_SELF", "id_child as u32 == CHILDID_SELF")
save("main.rs", t)

print("ALL DONE")

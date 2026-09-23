use std::sync::atomic::{AtomicPtr, Ordering};
use windows_sys::Win32::Foundation::RECT;
use windows_sys::Win32::Graphics::Gdi::{DeleteObject, RoundRect, CreateSolidBrush, CreatePen, SelectObject, HDC, HFONT, PS_NULL};
use windows_sys::Win32::UI::Input::KeyboardAndMouse::*;
use windows_sys::Win32::UI::WindowsAndMessaging::*;
use windows_sys::Win32::Foundation::*;
use windows_sys::Win32::Graphics::Gdi::*;
use windows_sys::Win32::System::Threading::*;
use windows_sys::Win32::System::SystemInformation::GetTickCount;
use windows_sys::Win32::UI::Accessibility::HWINEVENTHOOK;

pub const fn rgb(r: u32, g: u32, b: u32) -> u32 { r | (g << 8) | (b << 16) }

pub const C_BG: u32 = rgb(30, 30, 30);
pub const C_BAR: u32 = rgb(37, 37, 38);
pub const C_KEY: u32 = rgb(45, 45, 45);
pub const C_SPEC: u32 = rgb(51, 51, 56);
pub const C_ACTIVE: u32 = rgb(74, 74, 74);
pub const C_HELD: u32 = rgb(76, 194, 255);
pub const C_LOCK: u32 = rgb(0, 120, 212);
pub const C_TXT: u32 = rgb(255, 255, 255);
pub const C_DIM: u32 = rgb(128, 128, 128);
pub const C_MISS: u32 = rgb(255, 110, 110);

pub fn get_x_lparam(l: isize) -> i32 { (l & 0xffff) as u16 as i16 as i32 }
pub fn get_y_lparam(l: isize) -> i32 { ((l >> 16) & 0xffff) as u16 as i16 as i32 }
pub fn loword(l: isize) -> i32 { (l & 0xffff) as u16 as i32 }
pub fn hiword(l: isize) -> i32 { ((l >> 16) & 0xffff) as u16 as i32 }

pub fn set_wstr(buf: &mut [u16], s: &str) {
    buf.fill(0);
    for (i, c) in s.encode_utf16().take(buf.len().saturating_sub(1)).enumerate() {
        buf[i] = c;
    }
}

pub fn wstr_eq(a: &[u16], b: &str) -> bool {
    let mut ai = a.iter().copied();
    let mut bi = b.encode_utf16();
    loop {
        let x = ai.next().unwrap_or(0);
        let y = bi.next().unwrap_or(0);
        if x != y { return false; }
        if x == 0 { return true; }
    }
}

pub fn wstr_to_string(buf: &[u16]) -> String {
    let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    String::from_utf16_lossy(&buf[..len])
}

pub fn wstr_contains_ci(buf: &[u16], needle: &str) -> bool {
    wstr_to_string(buf).to_lowercase().contains(&needle.to_lowercase())
}

pub fn wbuf8(s: &str) -> [u16; 8] {
    let mut b = [0u16; 8];
    set_wstr(&mut b, s);
    b
}

pub fn zero_rect() -> RECT {
    RECT { left: 0, top: 0, right: 0, bottom: 0 }
}

pub unsafe fn draw_text(hdc: HDC, s: &[u16], rc: *mut RECT, fmt: u32) {
    let len = s.iter().position(|&c| c == 0).map(|i| i as i32).unwrap_or(s.len() as i32);
    if len > 0 {
        DrawTextW(hdc, s.as_ptr(), len, rc, fmt);
    }
}

pub unsafe fn draw_text_n(hdc: HDC, s: &[u16], n: i32, rc: *mut RECT, fmt: u32) {
    if n > 0 {
        DrawTextW(hdc, s.as_ptr(), n, rc, fmt);
    }
}

pub unsafe fn fill_rrr(dc: HDC, rc: *const RECT, c: u32, r: i32) {
    let b = CreateSolidBrush(c);
    let p = CreatePen(PS_NULL, 0, 0);
    let ob = SelectObject(dc, b as _);
    let op = SelectObject(dc, p as _);
    RoundRect(dc, (*rc).left, (*rc).top, (*rc).right, (*rc).bottom, r, r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

pub unsafe fn fill_rr(dc: HDC, rc: *const RECT, c: u32) { fill_rrr(dc, rc, c, 8); }

pub unsafe fn pt_in_rect(rc: *const RECT, x: i32, y: i32) -> bool {
    x >= (*rc).left && x < (*rc).right && y >= (*rc).top && y < (*rc).bottom
}

pub unsafe fn is_rect_empty(rc: *const RECT) -> bool {
    (*rc).right <= (*rc).left || (*rc).bottom <= (*rc).top
}

pub unsafe fn set_rect_empty(rc: *mut RECT) {
    (*rc).left = 0; (*rc).top = 0; (*rc).right = 0; (*rc).bottom = 0;
}

pub unsafe fn set_rect(rc: *mut RECT, l: i32, t: i32, r: i32, b: i32) {
    (*rc).left = l; (*rc).top = t; (*rc).right = r; (*rc).bottom = b;
}

// SendInput
fn is_ext_key(vk: u32) -> bool {
    matches!(vk as u16,
        VK_LEFT | VK_UP | VK_RIGHT | VK_DOWN | VK_LWIN | VK_APPS | VK_DELETE |
        VK_SNAPSHOT | VK_HOME | VK_END | VK_PRIOR | VK_NEXT | VK_INSERT)
}

pub unsafe fn vk_down(vk: u32) {
    let mut in_: INPUT = std::mem::zeroed();
    in_.r#type = INPUT_KEYBOARD;
    in_.Anonymous.ki.wVk = vk as u16;
    if is_ext_key(vk) { in_.Anonymous.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY; }
    SendInput(1, &in_, std::mem::size_of::<INPUT>() as i32);
}

pub unsafe fn vk_up(vk: u32) {
    let mut in_: INPUT = std::mem::zeroed();
    in_.r#type = INPUT_KEYBOARD;
    in_.Anonymous.ki.wVk = vk as u16;
    in_.Anonymous.ki.dwFlags = KEYEVENTF_KEYUP
        | if is_ext_key(vk) { KEYEVENTF_EXTENDEDKEY } else { 0 };
    SendInput(1, &in_, std::mem::size_of::<INPUT>() as i32);
}

pub unsafe fn vk_tap(vk: u32) { vk_down(vk); vk_up(vk); }

pub unsafe fn type_unicode(s: &[u16]) {
    for &wc in s.iter().take_while(|&&c| c != 0) {
        let mut dn: INPUT = std::mem::zeroed();
        let mut up: INPUT = std::mem::zeroed();
        dn.r#type = INPUT_KEYBOARD;
        up.r#type = INPUT_KEYBOARD;
        dn.Anonymous.ki.wScan = wc;
        dn.Anonymous.ki.dwFlags = KEYEVENTF_UNICODE;
        up.Anonymous.ki.wScan = wc;
        up.Anonymous.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(1, &dn, std::mem::size_of::<INPUT>() as i32);
        SendInput(1, &up, std::mem::size_of::<INPUT>() as i32);
    }
}

pub fn char_to_vk(lo: u16) -> u32 {
    let c = lo as u8 as char;
    if c.is_ascii_alphabetic() { return c.to_ascii_uppercase() as u32; }
    if c.is_ascii_digit() { return lo as u32; }
    match c {
        ';' => VK_OEM_1 as u32,
        '=' => VK_OEM_PLUS as u32,
        ',' => VK_OEM_COMMA as u32,
        '-' => VK_OEM_MINUS as u32,
        '.' => VK_OEM_PERIOD as u32,
        '/' => VK_OEM_2 as u32,
        '`' => VK_OEM_3 as u32,
        '[' => VK_OEM_4 as u32,
        '\\' => VK_OEM_5 as u32,
        ']' => VK_OEM_6 as u32,
        '\'' => VK_OEM_7 as u32,
        _ => 0,
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum Kind {
    Char, Special, Mod, Caps, Space, Page, Macro, Set,
    Sugg, Clip, Cnav, Shcap, Scut, Info,
}

pub const CNAV_UP: u32 = 1;
pub const CNAV_DOWN: u32 = 2;
pub const CNAV_CLEAR: u32 = 3;
pub const CNAV_BACK: u32 = 4;
pub const CNAV_PIN: u32 = 5;
pub const CNAV_SDEL: u32 = 6;
pub const CNAV_SBACK: u32 = 7;
pub const CNAV_SSTAR: u32 = 8;

pub const M_OFF: i32 = 0;
pub const M_HELD: i32 = 1;
pub const M_LOCKED: i32 = 2;

pub const MX_SHIFT: usize = 0;
pub const MX_CTRL: usize = 1;
pub const MX_ALT: usize = 2;
pub const MX_WIN: usize = 3;

pub const SET_SM: u32 = 1;
pub const SET_BG: u32 = 2;
pub const SET_OPDN: u32 = 3;
pub const SET_OPUP: u32 = 4;
pub const SET_DONE: u32 = 5;
pub const SET_LANG: u32 = 6;
pub const SET_PRED: u32 = 7;
pub const SET_AUTO: u32 = 8;
pub const SET_SPELL: u32 = 9;
pub const SET_PINWIN: u32 = 10;
pub const SET_BIGRAM: u32 = 11;
pub const SET_PINBAR: u32 = 12;

pub const BAR_NONE: i32 = 0;
pub const BAR_FN: i32 = 1;
pub const BAR_STAR: i32 = 2;
pub const BAR_MODE: i32 = 3;
pub const BAR_X: i32 = 4;
pub const BAR_GEAR: i32 = 5;
pub const BAR_CLIP: i32 = 6;
pub const BAR_SC: i32 = 7;

pub const TIMER_CAPS: usize = 1;
pub const TIMER_REPEAT: usize = 2;
pub const TIMER_FLASH: usize = 3;
pub const TIMER_HOLD: usize = 4;
pub const TIMER_SHCAP: usize = 5;
pub const TIMER_PIN: usize = 6;

pub const MAXCLIP: usize = 25;
pub const MAXCLIPIMG: usize = 8;
pub const MAXSCUT: usize = 10;
pub const MAXKEYS: usize = 160;
pub const MAXWLEN: usize = 31;
pub const MAXPINWIN: usize = 8;
pub const NMACROS: usize = 9;
pub const BKSP_DBL_MS: u32 = 400;
pub const CLIP_MAGIC: u32 = 0x464B434C;
pub const CLIP_VER: u32 = 1;

pub const MACROS: [(&str, i32, u32); NMACROS] = [
    ("Copy", 0x2, 0x43),
    ("Paste", 0x2, 0x56),
    ("Cut", 0x2, 0x58),
    ("Undo", 0x2, 0x5A),
    ("Save", 0x2, 0x53),
    ("All", 0x2, 0x41),
    ("AltTab", 0x4, VK_TAB as u32),
    ("Shot", 0x8, VK_SNAPSHOT as u32),
    ("Lock", 0x8, 0x4C),
];

#[derive(Clone, Copy)]
pub struct Key {
    pub text: [u16; 10],
    pub sub: [u16; 4],
    pub tag: u16,
    pub kind: Kind,
    pub vk: u32,
    pub mod_: i32,
    pub lo: u16,
    pub hi: u16,
    pub w: f32,
    pub row: i32,
    pub rc: RECT,
}

impl Default for Key {
    fn default() -> Self {
        Self {
            text: [0; 10], sub: [0; 4], tag: 0,
            kind: Kind::Char, vk: 0, mod_: 0, lo: 0, hi: 0,
            w: 1.0, row: 0, rc: zero_rect(),
        }
    }
}

#[derive(Clone, Copy)]
pub struct BigramRec { pub prev: u16, pub next: u16 }

pub struct ClipEnt {
    pub is_img: bool,
    pub text: Option<Vec<u16>>,
    pub hbmp: HBITMAP,
    pub iw: i32,
    pub ih: i32,
    pub pinned: bool,
}

impl ClipEnt {
    pub fn empty() -> Self {
        Self { is_img: false, text: None, hbmp: std::ptr::null_mut(), iw: 0, ih: 0, pinned: false }
    }
}

#[derive(Clone, Copy, Default)]
pub struct Shortcut {
    pub used: bool,
    pub mods: i32,
    pub vk: u32,
    pub name: [u16; 10],
    pub star: bool,
}

pub struct BarBtn {
    pub id: i32,
    pub t: [u16; 8],
    pub rc: RECT,
    pub w: i32,
    pub show: bool,
    pub side: i32,
}

pub struct App {
    pub keys: Vec<Key>,
    pub scale: f32,
    pub opacity: i32,
    pub show_fn: bool,
    pub compact: bool,
    pub page: i32,
    pub show_settings: bool,
    pub show_macros: bool,
    pub pos_x: i32,
    pub pos_y: i32,
    pub has_pos: bool,
    pub pending: i32,
    pub hold_popup: i32,
    pub shcap_armed: bool,
    pub pop_base: u16,
    pub pop_alt: u16,
    pub pop_rc: [RECT; 2],
    pub pop_all: RECT,
    pub pop_hover: i32,
    pub dict: [Vec<String>; 2],
    pub lang: usize,
    pub predict_on: bool,
    pub autocorrect: bool,
    pub highlight_on: bool,
    pub misspelt: bool,
    pub auto_active: bool,
    pub auto_trail: bool,
    pub auto_corr_len: i32,
    pub auto_orig: Vec<u16>,
    pub word_buf: Vec<u16>,
    pub sugg: [String; 3],
    pub bigram: [Vec<BigramRec>; 2],
    pub bigram_on: bool,
    pub prev_word: Vec<u16>,
    pub prev_idx: i32,
    pub clip: Vec<ClipEnt>,
    pub clip_off: usize,
    pub view_clip: bool,
    pub own_clip: bool,
    pub pin_arm: bool,
    pub mods: [i32; 4],
    pub caps_last: bool,
    pub pressed: i32,
    pub down_idx: i32,
    pub dragging: bool,
    pub drag_off: POINT,
    pub repeat_idx: i32,
    pub repeat_vk: u32,
    pub repeat_first: bool,
    pub last_fg: HWND,
    pub startup_fg: HWND,
    pub fg_hook: HWINEVENTHOOK,
    pub f_key: HFONT,
    pub f_small: HFONT,
    pub f_bar: HFONT,
    pub hwnd: HWND,
    pub pinwins: [HWND; MAXPINWIN],
    pub npinwins: usize,
    pub sc: [Shortcut; MAXSCUT],
    pub view_sc: bool,
    pub sc_del_arm: bool,
    pub rec_slot: i32,
    pub rec_step: i32,
    pub rec_mods: i32,
    pub rec_vk: u32,
    pub rec_name: [u16; 10],
    pub star_arm: bool,
    pub pin_bar_on: bool,
    pub sugg_row_on: i32,
    pub bksp_last_up: u32,
    pub bar: [BarBtn; 7],
}

static APP: AtomicPtr<App> = AtomicPtr::new(std::ptr::null_mut());

pub fn set_app_ptr(p: *mut App) { APP.store(p, Ordering::SeqCst); }
pub fn app_ptr() -> *mut App { APP.load(Ordering::SeqCst) }

pub unsafe fn app() -> &'static mut App { &mut *app_ptr() }

impl App {
    pub fn new() -> Self {
        Self {
            keys: Vec::new(),
            scale: 1.0,
            opacity: 245,
            show_fn: true,
            compact: false,
            page: 0,
            show_settings: false,
            show_macros: false,
            pos_x: 0, pos_y: 0, has_pos: false,
            pending: -1,
            hold_popup: -1,
            shcap_armed: false,
            pop_base: 0, pop_alt: 0,
            pop_rc: [zero_rect(), zero_rect()],
            pop_all: zero_rect(),
            pop_hover: -1,
            dict: [Vec::new(), Vec::new()],
            lang: 0,
            predict_on: true,
            autocorrect: true,
            highlight_on: true,
            misspelt: false,
            auto_active: false,
            auto_trail: false,
            auto_corr_len: 0,
            auto_orig: Vec::new(),
            word_buf: Vec::new(),
            sugg: [String::new(), String::new(), String::new()],
            bigram: [Vec::new(), Vec::new()],
            bigram_on: true,
            prev_word: Vec::new(),
            prev_idx: -1,
            clip: Vec::new(),
            clip_off: 0,
            view_clip: false,
            own_clip: false,
            pin_arm: false,
            mods: [M_OFF; 4],
            caps_last: false,
            pressed: -1,
            down_idx: -2,
            dragging: false,
            drag_off: POINT { x: 0, y: 0 },
            repeat_idx: -1,
            repeat_vk: 0,
            repeat_first: false,
            last_fg: std::ptr::null_mut(),
            startup_fg: std::ptr::null_mut(),
            fg_hook: std::ptr::null_mut(),
            f_key: std::ptr::null_mut(),
            f_small: std::ptr::null_mut(),
            f_bar: std::ptr::null_mut(),
            hwnd: std::ptr::null_mut(),
            pinwins: [std::ptr::null_mut(); MAXPINWIN],
            npinwins: 0,
            sc: [Shortcut::default(); MAXSCUT],
            view_sc: false,
            sc_del_arm: false,
            rec_slot: -1,
            rec_step: 0,
            rec_mods: 0,
            rec_vk: 0,
            rec_name: [0; 10],
            star_arm: false,
            pin_bar_on: true,
            sugg_row_on: -1,
            bksp_last_up: 0,
            bar: [
                BarBtn { id: BAR_FN, t: wbuf8("Fn"), rc: zero_rect(), w: 40, show: true, side: 0 },
                BarBtn { id: BAR_STAR, t: wbuf8("\u{2605}"), rc: zero_rect(), w: 36, show: true, side: 0 },
                BarBtn { id: BAR_SC, t: wbuf8("SC"), rc: zero_rect(), w: 36, show: true, side: 0 },
                BarBtn { id: BAR_CLIP, t: wbuf8("Clip"), rc: zero_rect(), w: 40, show: true, side: 0 },
                BarBtn { id: BAR_MODE, t: wbuf8("Compact"), rc: zero_rect(), w: 58, show: true, side: 0 },
                BarBtn { id: BAR_X, t: wbuf8("\u{2715}"), rc: zero_rect(), w: 40, show: true, side: 0 },
                BarBtn { id: BAR_GEAR, t: wbuf8("\u{2699}"), rc: zero_rect(), w: 36, show: true, side: 1 },
            ],
        }
    }

    pub fn star_count(&self) -> i32 {
        self.sc.iter().filter(|s| s.used && s.vk != 0 && s.star).count() as i32
    }

    pub fn pin_slot_by_bar(&self, i: i32) -> i32 {
        let mut n = 0;
        for (s, sc) in self.sc.iter().enumerate() {
            if sc.used && sc.vk != 0 && sc.star {
                if n == i { return s as i32; }
                n += 1;
            }
        }
        -1
    }

    pub fn set_mode_label(&mut self) {
        for b in self.bar.iter_mut() {
            if b.id == BAR_MODE {
                set_wstr(&mut b.t, if self.compact { "Full" } else { "Compact" });
            }
            if b.id == BAR_FN {
                b.show = !self.compact;
            }
        }
    }
}

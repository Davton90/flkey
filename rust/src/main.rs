#![windows_subsystem = "windows"]
#![allow(non_snake_case, non_upper_case_globals, dead_code, unused_variables, unused_assignments, unused_imports)]

mod app;
mod clip;
mod dict;
mod layout;
mod settings;
mod state;

use app::*;
use dict::*;
use windows_sys::Win32::Foundation::*;
use windows_sys::Win32::Graphics::Gdi::*;
use windows_sys::Win32::System::LibraryLoader::*;
use windows_sys::Win32::UI::Accessibility::*;
use windows_sys::Win32::UI::WindowsAndMessaging::*;
use windows_sys::Win32::UI::Input::KeyboardAndMouse::*;
use windows_sys::Win32::System::Threading::Sleep;
use windows_sys::Win32::System::DataExchange::*;
use windows_sys::Win32::System::SystemInformation::*;
use windows_sys::Win32::Foundation::{TRUE, FALSE};

struct FindData {
    skip: HWND,
    found: HWND,
}

unsafe extern "system" fn find_prev_fg(h: HWND, p: isize) -> i32 {
    let d = &mut *(p as *mut FindData);
    if h == d.skip || h == app().hwnd { return 1; }
    if IsWindowVisible(h) == 0 { return 1; }
    if IsIconic(h) != 0 { return 1; }
    if app().is_shell_chrome(h) { return 1; }
    let mut cls = [0u16; 64];
    GetClassNameW(h, cls.as_mut_ptr(), 64);
    if wstr_eq(&cls, "CabinetWClass") || wstr_eq(&cls, "ExploreWClass") {
        return 1;
    }
    let ex = GetWindowLongW(h, GWL_EXSTYLE);
    if ex & WS_EX_TOOLWINDOW as i32 != 0 { return 1; }
    let mut r: RECT = std::mem::zeroed();
    GetWindowRect(h, &mut r);
    if r.right <= r.left || r.bottom <= r.top { return 1; }
    d.found = h;
    0
}

impl App {
    fn restore_startup_focus(&mut self) {
        unsafe {
            let fg = GetForegroundWindow();
            self.startup_fg = fg;
            if fg.is_null() || fg == self.hwnd || self.is_shell_chrome(fg) {
                let mut d = FindData { skip: fg, found: std::ptr::null_mut() };
                EnumWindows(Some(find_prev_fg), &mut d as *mut _ as isize);
                if !d.found.is_null() {
                    self.last_fg = d.found;
                    self.force_foreground(d.found);
                    Sleep(30);
                } else if !fg.is_null() && !self.is_shell_chrome(fg) {
                    self.last_fg = fg;
                }
                return;
            }
            let mut cls = [0u16; 64];
            GetClassNameW(fg, cls.as_mut_ptr(), 64);
            if wstr_eq(&cls, "CabinetWClass") || wstr_eq(&cls, "ExploreWClass") {
                let mut d = FindData { skip: fg, found: std::ptr::null_mut() };
                EnumWindows(Some(find_prev_fg), &mut d as *mut _ as isize);
                if !d.found.is_null() {
                    self.last_fg = d.found;
                    self.force_foreground(d.found);
                    Sleep(30);
                    return;
                }
            }
            self.last_fg = fg;
            self.startup_fg = std::ptr::null_mut();
        }
    }

    fn load_data(&mut self) {
        self.dict[0] = parse_dict(WORDS_ID);
        self.dict[1] = parse_dict(WORDS_EN);
        self.bigram[0] = load_bigram(BIGRAM_ID);
        self.bigram[1] = load_bigram(BIGRAM_EN);
        if !dict_avail(self, self.lang) {
            self.lang = if dict_avail(self, 0) { 0 } else { 1 };
        }
        if !dict_avail(self, 0) && !dict_avail(self, 1) {
            self.predict_on = false;
            self.autocorrect = false;
            self.highlight_on = false;
        }
        if self.bigram[0].is_empty() && self.bigram[1].is_empty() {
            self.bigram_on = false;
        }
    }
}

unsafe extern "system" fn fg_hook_proc(
    _hook: HWINEVENTHOOK,
    ev: u32,
    hwnd: HWND,
    id_obj: i32,
    id_child: i32,
    _tid: u32,
    _t: u32,
) {
    if ev == EVENT_SYSTEM_FOREGROUND && id_obj == OBJID_WINDOW && id_child as u32 == CHILDID_SELF {
        app().track_foreground(hwnd);
    }
}

unsafe extern "system" fn wnd_proc(h: HWND, m: u32, w: usize, l: isize) -> isize {
    match m {
        WM_MOUSEACTIVATE => MA_NOACTIVATE as isize,
        WM_NCACTIVATE => TRUE as isize,
        WM_CREATE => {
            let a = app();
            a.hwnd = h;
            a.caps_last = (GetKeyState(VK_CAPITAL as i32) & 1) != 0;
            SetTimer(h, TIMER_CAPS, 500, None);
            SetTimer(h, TIMER_PIN, 1000, None);
            AddClipboardFormatListener(h);
            a.fg_hook = SetWinEventHook(
                EVENT_SYSTEM_FOREGROUND,
                EVENT_SYSTEM_FOREGROUND,
                std::ptr::null_mut(),
                Some(fg_hook_proc),
                0,
                0,
                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS,
            );
            a.track_foreground(GetForegroundWindow());
            0
        }
        WM_CLIPBOARDUPDATE => {
            app().on_clipboard_update();
            0
        }
        WM_SIZE => {
            let cw = loword(l);
            let ch = hiword(l);
            app().compute_layout(cw, ch);
            0
        }
        WM_PAINT => {
            app().on_paint();
            0
        }
        WM_ERASEBKGND => 1,
        WM_LBUTTONDOWN => {
            let x = get_x_lparam(l);
            let y = get_y_lparam(l);
            let a = app();
            let b = a.hit_bar(x, y);
            SetCapture(h);
            if b != BAR_NONE {
                a.down_idx = 1000 + b;
                a.inv_bar(b);
            } else {
                let k = a.hit_key(x, y);
                if k >= 0 {
                    a.down_idx = k;
                    a.press_key(k);
                } else if y < a.bar_h() {
                    let mut pt: POINT = std::mem::zeroed();
                    GetCursorPos(&mut pt);
                    let mut wr: RECT = std::mem::zeroed();
                    GetWindowRect(h, &mut wr);
                    a.dragging = true;
                    a.drag_off.x = pt.x - wr.left;
                    a.drag_off.y = pt.y - wr.top;
                    a.down_idx = -3;
                } else {
                    a.down_idx = -2;
                }
            }
            0
        }
        WM_MOUSEMOVE => {
            let a = app();
            if a.dragging {
                let mut pt: POINT = std::mem::zeroed();
                GetCursorPos(&mut pt);
                SetWindowPos(h, HWND_TOPMOST,
                    pt.x - a.drag_off.x, pt.y - a.drag_off.y, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            } else if a.hold_popup >= 0 {
                let px = get_x_lparam(l);
                let py = get_y_lparam(l);
                let hv = if pt_in_rect(&a.pop_rc[1], px, py) { 1 }
                    else if pt_in_rect(&a.pop_rc[0], px, py) { 0 } else { -1 };
                if hv != a.pop_hover {
                    a.pop_hover = hv;
                    InvalidateRect(h, &a.pop_all, FALSE);
                }
            }
            0
        }
        WM_LBUTTONUP => {
            let a = app();
            KillTimer(h, TIMER_HOLD);
            if a.hold_popup >= 0 {
                a.resolve_popup();
            } else if a.pending >= 0 {
                a.flush_pending();
            }
            if a.down_idx >= 0 && (a.down_idx as usize) < a.keys.len() {
                let uk = a.keys[a.down_idx as usize];
                if uk.kind == Kind::Special && uk.vk == VK_BACK as u32 {
                    a.bksp_last_up = GetTickCount();
                }
            }
            if a.down_idx >= 1000 {
                let id = a.down_idx - 1000;
                let x = get_x_lparam(l);
                let y = get_y_lparam(l);
                a.down_idx = -2;
                a.inv_bar(id);
                if a.hit_bar(x, y) == id {
                    a.do_bar(id);
                }
            } else {
                a.down_idx = -2;
            }
            if a.dragging {
                a.save_settings();
            }
            a.dragging = false;
            ReleaseCapture();
            KillTimer(h, TIMER_REPEAT);
            a.repeat_idx = -1;
            0
        }
        WM_TIMER => {
            let a = app();
            match w {
                TIMER_CAPS => {
                    let cur = (GetKeyState(VK_CAPITAL as i32) & 1) != 0;
                    if cur != a.caps_last {
                        a.caps_last = cur;
                        a.inv_caps();
                        a.shcap_refresh();
                    }
                }
                TIMER_REPEAT => {
                    if a.repeat_idx >= 0 {
                        let vk = a.repeat_vk;
                        vk_tap(vk);
                        if a.repeat_first {
                            a.repeat_first = false;
                            KillTimer(h, TIMER_REPEAT);
                            SetTimer(h, TIMER_REPEAT, 60, None);
                        }
                    }
                }
                TIMER_FLASH => {
                    KillTimer(h, TIMER_FLASH);
                    let pi = a.pressed;
                    a.pressed = -1;
                    a.inv_key(pi);
                }
                TIMER_HOLD => {
                    KillTimer(h, TIMER_HOLD);
                    if a.pending >= 0 {
                        let p = a.pending;
                        a.show_popup(p);
                    }
                }
                TIMER_SHCAP => {
                    KillTimer(h, TIMER_SHCAP);
                    a.shcap_armed = false;
                }
                TIMER_PIN => {
                    a.repair_pins();
                }
                _ => {}
            }
            0
        }
        WM_DESTROY => {
            let a = app();
            a.save_settings();
            a.save_clip();
            a.clip_clear_all();
            if !a.fg_hook.is_null() {
                UnhookWinEvent(a.fg_hook);
                a.fg_hook = std::ptr::null_mut();
            }
            a.unpin_all();
            for i in 0..4 {
                if a.mods[i] != M_OFF {
                    vk_up([VK_SHIFT as u32, VK_CONTROL as u32, VK_MENU as u32, VK_LWIN as u32][i]);
                }
            }
            PostQuitMessage(0);
            0
        }
        WM_ENDSESSION => {
            if w != 0 {
                let a = app();
                a.save_settings();
                a.save_clip();
            }
            0
        }
        _ => DefWindowProcW(h, m, w, l),
    }
}

unsafe fn set_dpi_awareness() {
    type SetDpi = unsafe extern "system" fn(i32) -> i32;
    let sh = LoadLibraryW(w("shcore.dll").as_ptr());
    if !sh.is_null() {
        let f = GetProcAddress(sh, b"SetProcessDpiAwareness\0".as_ptr());
        if let Some(f) = f {
            let f: SetDpi = std::mem::transmute(f);
            f(1);
        }
        FreeLibrary(sh);
    } else {
        SetProcessDPIAware();
    }
}

fn w(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

unsafe fn make_int_resource(id: u16) -> *const u16 {
    id as usize as *const u16
}

fn main() {
    unsafe {
        set_dpi_awareness();

        let a = Box::new(App::new());
        let ptr = Box::into_raw(a);
        set_app_ptr(ptr);
        {
            let a = &mut *ptr;
            a.load_settings();
            a.load_data();
            a.load_clip();
            if a.compact { a.page = 0; }
            a.word_clear();
            a.build_keys();
            a.set_mode_label();
            a.rebuild_fonts();
        }

        let class_name = w("FloatKeys");
        let mut wc: WNDCLASSEXW = std::mem::zeroed();
        wc.cbSize = std::mem::size_of::<WNDCLASSEXW>() as u32;
        wc.lpfnWndProc = Some(wnd_proc);
        wc.hInstance = GetModuleHandleW(std::ptr::null());
        wc.lpszClassName = class_name.as_ptr();
        wc.hCursor = LoadCursorW(std::ptr::null_mut(), IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, make_int_resource(1));
        wc.hIconSm = wc.hIcon;
        wc.hbrBackground = CreateSolidBrush(C_BG);
        RegisterClassExW(&wc);

        let a = &mut *ptr;
        let sw = GetSystemMetrics(SM_CXSCREEN);
        let sh2 = GetSystemMetrics(SM_CYSCREEN);
        let bw = a.base_w();
        let bh = a.base_h();
        let mut px = sw - bw - 40;
        let mut py = sh2 - bh - 80;
        if a.has_pos {
            let vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
            let vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
            let vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            let vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            px = a.pos_x;
            py = a.pos_y;
            if px > vx + vw - 80 { px = vx + vw - 80; }
            if py > vy + vh - 60 { py = vy + vh - 60; }
            if px < vx { px = vx; }
            if py < vy { py = vy; }
        }
        let class = w("FloatKeys");
        let title = w("FloatKeys");
        let hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_COMPOSITED,
            class.as_ptr(),
            title.as_ptr(),
            WS_POPUP | WS_VISIBLE,
            px, py, bw, bh,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            wc.hInstance,
            std::ptr::null(),
        );
        if hwnd.is_null() {
            std::process::exit(1);
        }
        a.hwnd = hwnd;
        a.apply_opacity();
        let mut cl: RECT = std::mem::zeroed();
        GetClientRect(hwnd, &mut cl);
        a.compute_layout(cl.right, cl.bottom);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd);
        a.restore_startup_focus();

        let mut msg: MSG = std::mem::zeroed();
        while GetMessageW(&mut msg, std::ptr::null_mut(), 0, 0) != 0 {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

// keep raw pointer alive for the whole process
#[allow(dead_code)]
fn keep_alive() {
    let _ = app_ptr();
}

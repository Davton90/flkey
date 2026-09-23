use windows_sys::Win32::Foundation::{TRUE, FALSE};
use crate::app::*;
use windows_sys::Win32::Foundation::RECT;
use windows_sys::Win32::Graphics::Gdi::*;
use windows_sys::Win32::UI::Input::KeyboardAndMouse::*;
use windows_sys::Win32::UI::WindowsAndMessaging::*;

impl App {
    pub fn add_key(&mut self, t: &str, k: Kind, vk: u32, m: i32, lo: u16, hi: u16, w: f32, row: i32) {
        if self.keys.len() >= MAXKEYS { return; }
        let mut key = Key::default();
        set_wstr(&mut key.text, t);
        key.kind = k;
        key.vk = vk;
        key.mod_ = m;
        key.lo = lo;
        key.hi = hi;
        key.w = w;
        key.row = row;
        unsafe { set_rect_empty(&mut key.rc); }
        self.keys.push(key);
    }

    pub fn build_full(&mut self) {
        self.keys.clear();
        self.add_key("Esc", Kind::Special, VK_ESCAPE as u32, 0, 0, 0, 1.2, 0);
        for i in 1..=12u32 {
            let t = format!("F{}", i);
            self.add_key(&t, Kind::Special, VK_F1 as u32 + i - 1, 0, 0, 0, 1.0, 0);
        }
        self.add_key("PrtSc", Kind::Special, VK_SNAPSHOT as u32, 0, 0, 0, 1.2, 0);
        self.add_key("Del", Kind::Special, VK_DELETE as u32, 0, 0, 0, 1.2, 0);
        self.add_key("`", Kind::Char, 0, 0, '`' as u16, '~' as u16, 1.0, 1);
        let d: Vec<char> = "1234567890".chars().collect();
        let s: Vec<char> = "!@#$%^&*()".chars().collect();
        for i in 0..10 {
            let t = d[i].to_string();
            self.add_key(&t, Kind::Char, 0, 0, d[i] as u16, s[i] as u16, 1.0, 1);
        }
        self.add_key("-", Kind::Char, 0, 0, '-' as u16, '_' as u16, 1.0, 1);
        self.add_key("=", Kind::Char, 0, 0, '=' as u16, '+' as u16, 1.0, 1);
        self.add_key("Back", Kind::Special, VK_BACK as u32, 0, 0, 0, 2.0, 1);
        self.add_key("Tab", Kind::Special, VK_TAB as u32, 0, 0, 0, 1.6, 2);
        for r in "QWERTYUIOP".chars() {
            let t = r.to_string();
            self.add_key(&t, Kind::Char, 0, 0, r.to_ascii_lowercase() as u16, r as u16, 1.0, 2);
        }
        self.add_key("[", Kind::Char, 0, 0, '[' as u16, '{' as u16, 1.0, 2);
        self.add_key("]", Kind::Char, 0, 0, ']' as u16, '}' as u16, 1.0, 2);
        self.add_key("\\", Kind::Char, 0, 0, '\\' as u16, '|' as u16, 1.5, 2);
        self.add_key("Caps", Kind::Caps, 0, 0, 0, 0, 1.8, 3);
        for r in "ASDFGHJKL".chars() {
            let t = r.to_string();
            self.add_key(&t, Kind::Char, 0, 0, r.to_ascii_lowercase() as u16, r as u16, 1.0, 3);
        }
        self.add_key(";", Kind::Char, 0, 0, ';' as u16, ':' as u16, 1.0, 3);
        self.add_key("'", Kind::Char, 0, 0, '\'' as u16, '"' as u16, 1.0, 3);
        self.add_key("Enter", Kind::Special, VK_RETURN as u32, 0, 0, 0, 2.2, 3);
        self.add_key("Shift", Kind::Mod, 0, MX_SHIFT as i32, 0, 0, 2.2, 4);
        for r in "ZXCVBNM".chars() {
            let t = r.to_string();
            self.add_key(&t, Kind::Char, 0, 0, r.to_ascii_lowercase() as u16, r as u16, 1.0, 4);
        }
        self.add_key(",", Kind::Char, 0, 0, ',' as u16, '<' as u16, 1.0, 4);
        self.add_key(".", Kind::Char, 0, 0, '.' as u16, '>' as u16, 1.0, 4);
        self.add_key("/", Kind::Char, 0, 0, '/' as u16, '?' as u16, 1.0, 4);
        self.add_key("Shift", Kind::Mod, 0, MX_SHIFT as i32, 0, 0, 2.2, 4);
        self.add_key("\u{25B2}", Kind::Special, VK_UP as u32, 0, 0, 0, 1.0, 4);
        self.add_key("Ctrl", Kind::Mod, 0, MX_CTRL as i32, 0, 0, 1.5, 5);
        self.add_key("Win", Kind::Mod, 0, MX_WIN as i32, 0, 0, 1.5, 5);
        self.add_key("Alt", Kind::Mod, 0, MX_ALT as i32, 0, 0, 1.5, 5);
        self.add_key("Space", Kind::Space, 0, 0, 0, 0, 6.0, 5);
        self.add_key("Alt", Kind::Mod, 0, MX_ALT as i32, 0, 0, 1.3, 5);
        self.add_key("\u{2630}", Kind::Special, VK_APPS as u32, 0, 0, 0, 1.3, 5);
        self.add_key("Ctrl", Kind::Mod, 0, MX_CTRL as i32, 0, 0, 1.3, 5);
        self.add_key("PgUp", Kind::Special, VK_PRIOR as u32, 0, 0, 0, 1.2, 5);
        self.add_key("PgDn", Kind::Special, VK_NEXT as u32, 0, 0, 0, 1.2, 5);
        self.add_key("\u{25C0}", Kind::Special, VK_LEFT as u32, 0, 0, 0, 1.0, 5);
        self.add_key("\u{25BC}", Kind::Special, VK_DOWN as u32, 0, 0, 0, 1.0, 5);
        self.add_key("\u{25B6}", Kind::Special, VK_RIGHT as u32, 0, 0, 0, 1.0, 5);
    }

    pub fn build_compact_abc(&mut self) {
        self.keys.clear();
        self.add_key("Esc", Kind::Special, VK_ESCAPE as u32, 0, 0, 0, 1.2, 0);
        let q: Vec<char> = "QWERTYUIOP".chars().collect();
        let dg: Vec<char> = "1234567890".chars().collect();
        for i in 0..10 {
            let t = q[i].to_string();
            self.add_key(&t, Kind::Char, 0, 0, q[i].to_ascii_lowercase() as u16, q[i] as u16, 1.0, 0);
            if let Some(last) = self.keys.last_mut() { last.tag = dg[i] as u16; }
        }
        self.add_key("\u{232B}", Kind::Special, VK_BACK as u32, 0, 0, 0, 1.6, 0);
        self.add_key("\u{21E5}", Kind::Special, VK_TAB as u32, 0, 0, 0, 1.4, 1);
        for r in "ASDFGHJKL".chars() {
            let t = r.to_string();
            self.add_key(&t, Kind::Char, 0, 0, r.to_ascii_lowercase() as u16, r as u16, 1.0, 1);
        }
        self.add_key("\u{23CE}", Kind::Special, VK_RETURN as u32, 0, 0, 0, 1.8, 1);
        self.add_key("\u{21EA}", Kind::Shcap, 0, 0, 0, 0, 2.0, 2);
        for r in "ZXCVBNM".chars() {
            let t = r.to_string();
            self.add_key(&t, Kind::Char, 0, 0, r.to_ascii_lowercase() as u16, r as u16, 1.0, 2);
        }
        self.add_key(",", Kind::Char, 0, 0, ',' as u16, '<' as u16, 1.0, 2);
        self.add_key(".", Kind::Char, 0, 0, '.' as u16, '>' as u16, 1.0, 2);
        self.add_key("/", Kind::Char, 0, 0, '/' as u16, '?' as u16, 1.0, 2);
        self.add_key("\u{25B2}", Kind::Special, VK_UP as u32, 0, 0, 0, 1.0, 2);
        self.add_key("Ctrl", Kind::Mod, 0, MX_CTRL as i32, 0, 0, 1.2, 3);
        self.add_key("Win", Kind::Mod, 0, MX_WIN as i32, 0, 0, 1.2, 3);
        self.add_key("Alt", Kind::Mod, 0, MX_ALT as i32, 0, 0, 1.2, 3);
        self.add_key("&123", Kind::Page, 1, 0, 0, 0, 1.4, 3);
        self.add_key("Space", Kind::Space, 0, 0, 0, 0, 4.0, 3);
        self.add_key("Del", Kind::Special, VK_DELETE as u32, 0, 0, 0, 1.2, 3);
        self.add_key("PgUp", Kind::Special, VK_PRIOR as u32, 0, 0, 0, 1.0, 3);
        self.add_key("PgDn", Kind::Special, VK_NEXT as u32, 0, 0, 0, 1.0, 3);
        self.add_key("\u{25C0}", Kind::Special, VK_LEFT as u32, 0, 0, 0, 1.0, 3);
        self.add_key("\u{25BC}", Kind::Special, VK_DOWN as u32, 0, 0, 0, 1.0, 3);
        self.add_key("\u{25B6}", Kind::Special, VK_RIGHT as u32, 0, 0, 0, 1.0, 3);
    }

    pub fn build_compact_sym(&mut self) {
        self.keys.clear();
        self.add_key("`", Kind::Char, 0, 0, '`' as u16, '~' as u16, 1.0, 0);
        let d: Vec<char> = "1234567890".chars().collect();
        let s: Vec<char> = "!@#$%^&*()".chars().collect();
        for i in 0..10 {
            let t = d[i].to_string();
            self.add_key(&t, Kind::Char, 0, 0, d[i] as u16, s[i] as u16, 1.0, 0);
        }
        self.add_key("-", Kind::Char, 0, 0, '-' as u16, '_' as u16, 1.0, 0);
        self.add_key("=", Kind::Char, 0, 0, '=' as u16, '+' as u16, 1.0, 0);
        self.add_key("\u{232B}", Kind::Special, VK_BACK as u32, 0, 0, 0, 2.0, 0);
        self.add_key("\u{21E5}", Kind::Special, VK_TAB as u32, 0, 0, 0, 1.4, 1);
        self.add_key("[", Kind::Char, 0, 0, '[' as u16, '{' as u16, 1.0, 1);
        self.add_key("]", Kind::Char, 0, 0, ']' as u16, '}' as u16, 1.0, 1);
        self.add_key("\\", Kind::Char, 0, 0, '\\' as u16, '|' as u16, 1.0, 1);
        self.add_key(";", Kind::Char, 0, 0, ';' as u16, ':' as u16, 1.0, 1);
        self.add_key("'", Kind::Char, 0, 0, '\'' as u16, '"' as u16, 1.0, 1);
        self.add_key("Del", Kind::Special, VK_DELETE as u32, 0, 0, 0, 1.4, 1);
        self.add_key("\u{23CE}", Kind::Special, VK_RETURN as u32, 0, 0, 0, 1.8, 1);
        self.add_key("\u{21EA}", Kind::Shcap, 0, 0, 0, 0, 1.8, 2);
        self.add_key("<", Kind::Char, 0, 0, '<' as u16, '<' as u16, 1.0, 2);
        self.add_key(">", Kind::Char, 0, 0, '>' as u16, '>' as u16, 1.0, 2);
        self.add_key("?", Kind::Char, 0, 0, '?' as u16, '?' as u16, 1.0, 2);
        self.add_key("!", Kind::Char, 0, 0, '!' as u16, '!' as u16, 1.0, 2);
        self.add_key(":", Kind::Char, 0, 0, ':' as u16, ':' as u16, 1.0, 2);
        self.add_key("\"", Kind::Char, 0, 0, '"' as u16, '"' as u16, 1.0, 2);
        self.add_key("\u{25B2}", Kind::Special, VK_UP as u32, 0, 0, 0, 1.0, 2);
        self.add_key("Ctrl", Kind::Mod, 0, MX_CTRL as i32, 0, 0, 1.2, 3);
        self.add_key("Win", Kind::Mod, 0, MX_WIN as i32, 0, 0, 1.2, 3);
        self.add_key("Alt", Kind::Mod, 0, MX_ALT as i32, 0, 0, 1.2, 3);
        self.add_key("ABC", Kind::Page, 0, 0, 0, 0, 1.4, 3);
        self.add_key("Space", Kind::Space, 0, 0, 0, 0, 4.0, 3);
        self.add_key("\u{2630}", Kind::Special, VK_APPS as u32, 0, 0, 0, 1.2, 3);
        self.add_key("PgUp", Kind::Special, VK_PRIOR as u32, 0, 0, 0, 1.0, 3);
        self.add_key("PgDn", Kind::Special, VK_NEXT as u32, 0, 0, 0, 1.0, 3);
        self.add_key("\u{25C0}", Kind::Special, VK_LEFT as u32, 0, 0, 0, 1.0, 3);
        self.add_key("\u{25BC}", Kind::Special, VK_DOWN as u32, 0, 0, 0, 1.0, 3);
        self.add_key("\u{25B6}", Kind::Special, VK_RIGHT as u32, 0, 0, 0, 1.0, 3);
    }

    pub fn build_settings_row(&mut self) {
        self.add_key("A-", Kind::Set, SET_SM, 0, 0, 0, 1.5, 10);
        self.add_key("A+", Kind::Set, SET_BG, 0, 0, 0, 1.5, 10);
        self.add_key("\u{25D0}", Kind::Set, SET_OPDN, 0, 0, 0, 1.5, 10);
        self.add_key("\u{25D1}", Kind::Set, SET_OPUP, 0, 0, 0, 1.5, 10);
        let lang_lbl = if self.lang == 0 { "ID" } else { "EN" };
        self.add_key(lang_lbl, Kind::Set, SET_LANG, 0, 0, 0, 1.5, 10);
        self.add_key("Pred", Kind::Set, SET_PRED, 0, 0, 0, 1.5, 10);
        self.add_key("Auto", Kind::Set, SET_AUTO, 0, 0, 0, 1.5, 10);
        self.add_key("Spell", Kind::Set, SET_SPELL, 0, 0, 0, 1.5, 10);
        self.add_key("Bi", Kind::Set, SET_BIGRAM, 0, 0, 0, 1.5, 10);
        self.add_key("Pin", Kind::Set, SET_PINWIN, 0, 0, 0, 1.5, 10);
        self.add_key("\u{2605}", Kind::Set, SET_PINBAR, 0, 0, 0, 1.5, 10);
        self.add_key("Done", Kind::Set, SET_DONE, 0, 0, 0, 1.5, 10);
    }

    pub fn build_sc_rows(&mut self) {
        for i in 0..MAXSCUT {
            let name = if self.sc[i].used && self.sc[i].name[0] != 0 {
                wstr_to_string(&self.sc[i].name)
            } else {
                "+".to_string()
            };
            self.add_key(&name, Kind::Scut, i as u32, 0, 0, 0, 2.0, if i < 5 { 30 } else { 31 });
        }
        self.add_key("Del", Kind::Cnav, CNAV_SDEL, 0, 0, 0, 1.5, 32);
        self.add_key("\u{2605}", Kind::Cnav, CNAV_SSTAR, 0, 0, 0, 1.5, 32);
        self.add_key("Keys", Kind::Cnav, CNAV_SBACK, 0, 0, 0, 1.5, 32);
    }

    pub fn build_rec_row(&mut self) {
        self.add_key("", Kind::Info, 0, 0, 0, 0, 10.0, 13);
    }

    pub fn build_pin_row(&mut self) {
        for i in 0..3 {
            let s = self.pin_slot_by_bar(i);
            if s < 0 { break; }
            let name = if self.sc[s as usize].name[0] != 0 {
                wstr_to_string(&self.sc[s as usize].name)
            } else {
                "?".to_string()
            };
            self.add_key(&name, Kind::Scut, s as u32, 0, 0, 0, 2.0, 14);
        }
    }

    pub fn build_sugg_row(&mut self) {
        for i in 0..3 {
            self.add_key("", Kind::Sugg, i as u32, 0, 0, 0, 2.0, 12);
        }
    }

    pub fn build_macros_row(&mut self) {
        for &(label, m, vk) in MACROS.iter() {
            self.add_key(label, Kind::Macro, vk, m, 0, 0, 1.5, 11);
        }
    }

    pub fn build_clip_rows(&mut self) {
        for i in 0..5 {
            self.add_key("", Kind::Clip, i as u32, 0, 0, 0, 1.0, 20 + i as i32);
        }
        self.add_key("\u{25B2}", Kind::Cnav, CNAV_UP, 0, 0, 0, 1.2, 25);
        self.add_key("\u{25BC}", Kind::Cnav, CNAV_DOWN, 0, 0, 0, 1.2, 25);
        self.add_key("Clear", Kind::Cnav, CNAV_CLEAR, 0, 0, 0, 1.8, 25);
        self.add_key("Pin", Kind::Cnav, CNAV_PIN, 0, 0, 0, 1.8, 25);
        self.add_key("Keys", Kind::Cnav, CNAV_BACK, 0, 0, 0, 1.8, 25);
    }

    pub fn build_panels(&mut self) {
        self.build_settings_row();
        self.build_macros_row();
        self.build_sugg_row();
        self.build_clip_rows();
        self.build_sc_rows();
        self.build_rec_row();
        self.build_pin_row();
    }

    pub fn build_keys(&mut self) {
        if !self.compact {
            self.build_full();
            self.build_panels();
            return;
        }
        if self.page == 0 { self.build_compact_abc(); } else { self.build_compact_sym(); }
        self.build_panels();
    }

    pub fn bar_h(&self) -> i32 { (30.0 * self.scale) as i32 }

    pub fn rebuild_fonts(&mut self) {
        let k = self.scale * if self.compact { 0.9 } else { 1.0 };
        let face: Vec<u16> = "Segoe UI".encode_utf16().chain(std::iter::once(0)).collect();
        unsafe {
            if !self.f_key.is_null() { DeleteObject(self.f_key as _); }
            if !self.f_small.is_null() { DeleteObject(self.f_small as _); }
            if !self.f_bar.is_null() { DeleteObject(self.f_bar as _); }
            self.f_key = CreateFontW(-(16.0 * k) as i32, 0, 0, 0, FW_NORMAL as i32, 0, 0, 0,
                DEFAULT_CHARSET as u32, OUT_DEFAULT_PRECIS as u32, CLIP_DEFAULT_PRECIS as u32,
                DEFAULT_QUALITY as u32, DEFAULT_PITCH as u32, face.as_ptr());
            self.f_small = CreateFontW(-(10.0 * k) as i32, 0, 0, 0, FW_NORMAL as i32, 0, 0, 0,
                DEFAULT_CHARSET as u32, OUT_DEFAULT_PRECIS as u32, CLIP_DEFAULT_PRECIS as u32,
                DEFAULT_QUALITY as u32, DEFAULT_PITCH as u32, face.as_ptr());
            self.f_bar = CreateFontW(-(13.0 * self.scale) as i32, 0, 0, 0, FW_NORMAL as i32, 0, 0, 0,
                DEFAULT_CHARSET as u32, OUT_DEFAULT_PRECIS as u32, CLIP_DEFAULT_PRECIS as u32,
                DEFAULT_QUALITY as u32, DEFAULT_PITCH as u32, face.as_ptr());
        }
    }

    pub fn base_w(&self) -> i32 { ((if self.compact { 460.0 } else { 720.0 }) * self.scale) as i32 }

    pub fn n_rows(&self) -> i32 {
        if self.view_clip { return 6; }
        if self.view_sc { return 3; }
        let mut n = if self.compact { 4 } else if self.show_fn { 6 } else { 5 };
        if self.rec_slot >= 0 { n += 1; }
        if self.pin_bar_on && self.star_count() > 0 { n += 1; }
        if self.show_settings || self.show_macros { n += 1; }
        if self.sugg_row_on() { n += 1; }
        n
    }

    pub fn base_h(&self) -> i32 { ((38 + self.n_rows() * 54) as f32 * self.scale) as i32 }

    pub fn apply_opacity(&mut self) {
        unsafe {
            SetLayeredWindowAttributes(self.hwnd, 0, self.opacity as u8, LWA_ALPHA);
        }
    }

    pub fn resize_for_scale(&mut self) {
        let w = self.base_w();
        let h = self.base_h();
        unsafe {
            SetWindowPos(self.hwnd, HWND_TOPMOST, 0, 0, w, h,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        self.rebuild_fonts();
        let mut cl = zero_rect();
        unsafe { GetClientRect(self.hwnd, &mut cl); }
        self.compute_layout(cl.right, cl.bottom);
        unsafe { InvalidateRect(self.hwnd, std::ptr::null(), TRUE); }
    }

    pub fn rebuild_keys(&mut self) {
        self.build_keys();
        let mut cl = zero_rect();
        unsafe { GetClientRect(self.hwnd, &mut cl); }
        self.compute_layout(cl.right, cl.bottom);
        unsafe { InvalidateRect(self.hwnd, std::ptr::null(), TRUE); }
    }

    pub fn visible_row(&self, row: i32) -> bool {
        if self.view_clip { return (20..=25).contains(&row); }
        if self.view_sc { return (30..=32).contains(&row); }
        match row {
            10 => self.show_settings,
            11 => self.show_macros,
            12 => self.sugg_row_on(),
            13 => self.rec_slot >= 0,
            14 => self.pin_bar_on && self.star_count() > 0,
            r if r >= 20 => false,
            _ => {
                if self.compact { return true; }
                if row == 0 && !self.show_fn { return false; }
                true
            }
        }
    }

    pub fn compute_layout(&mut self, cw: i32, ch: i32) {
        let pad = 6;
        let gap = 4;
        let bh = self.bar_h();
        let mut rows = [0i32; 16];
        let mut nrows = 0usize;
        let mut x;
        x = cw - pad;
        for i in (0..=5).rev() {
            if !self.bar[i].show {
                unsafe { set_rect_empty(&mut self.bar[i].rc); }
            } else {
                let w = (self.bar[i].w as f32 * self.scale) as i32;
                self.bar[i].rc.right = x;
                self.bar[i].rc.left = x - w;
                self.bar[i].rc.top = 3;
                self.bar[i].rc.bottom = bh - 3;
                x -= w + 3;
            }
        }
        x = pad;
        for i in 6..7 {
            if !self.bar[i].show {
                unsafe { set_rect_empty(&mut self.bar[i].rc); }
                continue;
            }
            let w = (self.bar[i].w as f32 * self.scale) as i32;
            self.bar[i].rc.left = x;
            self.bar[i].rc.right = x + w;
            self.bar[i].rc.top = 3;
            self.bar[i].rc.bottom = bh - 3;
            x += w + 3;
        }
        if self.view_clip {
            for r in 20..=25 { rows[nrows] = r; nrows += 1; }
        } else if self.view_sc {
            for r in 30..=32 { rows[nrows] = r; nrows += 1; }
        } else {
            if self.rec_slot >= 0 { rows[nrows] = 13; nrows += 1; }
            if self.show_settings { rows[nrows] = 10; nrows += 1; }
            if self.show_macros { rows[nrows] = 11; nrows += 1; }
            if self.pin_bar_on && self.star_count() > 0 { rows[nrows] = 14; nrows += 1; }
            if self.sugg_row_on() { rows[nrows] = 12; nrows += 1; }
            if self.compact {
                for r in 0..4 { rows[nrows] = r; nrows += 1; }
            } else if self.show_fn {
                for r in 0..6 { rows[nrows] = r; nrows += 1; }
            } else {
                for r in 1..6 { rows[nrows] = r; nrows += 1; }
            }
        }
        for k in self.keys.iter_mut() {
            let found = rows[..nrows].contains(&k.row);
            if !found {
                unsafe { set_rect_empty(&mut k.rc); }
            }
        }
        let avail = ch - bh - pad * 2 - gap * (nrows as i32 - 1);
        let rh = if nrows > 0 { avail / nrows as i32 } else { 0 };
        let mut y = bh + pad;
        for r in 0..nrows {
            let row = rows[r];
            let mut n = 0;
            let mut tot = 0.0f32;
            for k in self.keys.iter() {
                if k.row == row { tot += k.w; n += 1; }
            }
            let rgap = if row == 14 { 0 } else { gap };
            x = pad;
            let mut ci = 0;
            for k in self.keys.iter_mut() {
                if k.row == row {
                    ci += 1;
                    let mut wpx = (((cw - pad * 2 - rgap * (n as i32 - 1)) as f32)
                        * (k.w / tot)) as i32;
                    if ci == n { wpx = cw - pad - x; }
                    k.rc.left = x;
                    k.rc.top = y;
                    k.rc.right = x + wpx;
                    k.rc.bottom = y + rh;
                    x += wpx + rgap;
                }
            }
            y += rh + rgap;
        }
        if !self.show_fn && !self.compact {
            for k in self.keys.iter_mut() {
                if k.row == 0 {
                    unsafe { set_rect_empty(&mut k.rc); }
                }
            }
        }
    }

    pub fn inv_key(&mut self, idx: i32) {
        if idx >= 0 && (idx as usize) < self.keys.len() {
            let row = self.keys[idx as usize].row;
            if self.visible_row(row) {
                let rc = self.keys[idx as usize].rc;
                unsafe { InvalidateRect(self.hwnd, &rc, FALSE); }
            }
        }
    }

    pub fn inv_mod(&mut self, m: i32) {
        let idxs: Vec<usize> = self.keys.iter().enumerate()
            .filter(|(_, k)| k.kind == Kind::Mod && k.mod_ == m)
            .map(|(i, _)| i)
            .collect();
        for i in idxs { self.inv_key(i as i32); }
    }

    pub fn inv_caps(&mut self) {
        let idxs: Vec<usize> = self.keys.iter().enumerate()
            .filter(|(_, k)| k.kind == Kind::Caps)
            .map(|(i, _)| i)
            .collect();
        for i in idxs { self.inv_key(i as i32); }
    }

    pub fn inv_bar(&mut self, id: i32) {
        for i in 0..7 {
            if self.bar[i].id == id {
                let rc = self.bar[i].rc;
                unsafe { InvalidateRect(self.hwnd, &rc, FALSE); }
            }
        }
    }

    pub fn hit_bar(&self, x: i32, y: i32) -> i32 {
        for i in 0..7 {
            if self.bar[i].show && unsafe { pt_in_rect(&self.bar[i].rc, x, y) } {
                return self.bar[i].id;
            }
        }
        BAR_NONE
    }

    pub fn hit_key(&self, x: i32, y: i32) -> i32 {
        for (i, k) in self.keys.iter().enumerate() {
            if !self.visible_row(k.row) { continue; }
            if unsafe { pt_in_rect(&k.rc, x, y) } { return i as i32; }
        }
        -1
    }

    pub fn do_bar(&mut self, id: i32) {
        match id {
            BAR_X => unsafe { DestroyWindow(self.hwnd); },
            BAR_FN => {
                self.show_fn = !self.show_fn;
                self.resize_for_scale();
            }
            BAR_GEAR => {
                self.show_settings = !self.show_settings;
                if self.show_settings {
                    self.show_macros = false;
                    self.view_clip = false;
                    self.view_sc = false;
                }
                self.resize_for_scale();
            }
            BAR_STAR => {
                self.show_macros = !self.show_macros;
                if self.show_macros {
                    self.show_settings = false;
                    self.view_clip = false;
                    self.view_sc = false;
                }
                self.resize_for_scale();
            }
            BAR_CLIP => {
                self.view_clip = !self.view_clip;
                if self.view_clip {
                    self.show_settings = false;
                    self.show_macros = false;
                    self.view_sc = false;
                } else {
                    self.pin_arm = false;
                }
                self.clip_off = 0;
                self.resize_for_scale();
            }
            BAR_SC => {
                if self.rec_slot >= 0 { self.cancel_record(); }
                self.view_sc = !self.view_sc;
                if self.view_sc {
                    self.show_settings = false;
                    self.show_macros = false;
                    self.view_clip = false;
                    self.pin_arm = false;
                } else {
                    self.sc_del_arm = false;
                    self.star_arm = false;
                }
                self.resize_for_scale();
            }
            BAR_MODE => {
                self.compact = !self.compact;
                if self.compact { self.page = 0; }
                self.pending = -1;
                self.hold_popup = -1;
                unsafe { KillTimer(self.hwnd, TIMER_HOLD); }
                self.build_keys();
                self.set_mode_label();
                self.resize_for_scale();
            }
            _ => {}
        }
        self.save_settings();
    }

    pub fn paint_key(&self, dc: HDC, k: &Key, idx: i32) {
        let mut bg = C_KEY;
        let mut fg = C_TXT;
        unsafe {
            let special = matches!(k.kind,
                Kind::Special | Kind::Caps | Kind::Mod | Kind::Page |
                Kind::Macro | Kind::Set | Kind::Cnav | Kind::Shcap);
            if special { bg = C_SPEC; }
            if k.kind == Kind::Mod {
                let m = k.mod_ as usize;
                if self.mods[m] == M_HELD { bg = C_HELD; fg = rgb(0, 0, 0); }
                else if self.mods[m] == M_LOCKED { bg = C_LOCK; fg = C_TXT; }
                else if self.rec_slot >= 0 && self.rec_step == 0
                    && (self.rec_mods & (1 << k.mod_)) != 0
                {
                    bg = C_LOCK; fg = C_TXT;
                }
            }
            if k.kind == Kind::Caps {
                if (GetKeyState(VK_CAPITAL as i32) & 1) != 0 { bg = C_LOCK; fg = C_TXT; }
            }
            if k.kind == Kind::Shcap {
                if (GetKeyState(VK_CAPITAL as i32) & 1) != 0 { bg = C_LOCK; fg = C_TXT; }
                else if self.mods[MX_SHIFT] == M_LOCKED { bg = C_LOCK; fg = C_TXT; }
                else if self.mods[MX_SHIFT] != M_OFF { bg = C_HELD; fg = rgb(0, 0, 0); }
            }
            if k.kind == Kind::Set {
                let on = match k.vk {
                    SET_PRED => self.predict_on,
                    SET_AUTO => self.autocorrect,
                    SET_SPELL => self.highlight_on,
                    SET_BIGRAM => self.bigram_on,
                    SET_PINBAR => self.pin_bar_on,
                    SET_PINWIN => self.cur_target_pinned(),
                    _ => false,
                };
                if on { bg = C_LOCK; fg = C_TXT; }
            }
            if k.kind == Kind::Cnav && k.vk == CNAV_PIN && self.pin_arm { bg = C_LOCK; fg = C_TXT; }
            if k.kind == Kind::Cnav && k.vk == CNAV_SDEL && self.sc_del_arm { bg = C_LOCK; fg = C_TXT; }
            if idx == self.pressed { bg = C_ACTIVE; }
            if k.kind == Kind::Scut && k.row == 14 { fill_rrr(dc, &k.rc, bg, 4); }
            else { fill_rr(dc, &k.rc, bg); }
            SetBkMode(dc, TRANSPARENT as i32);
            SetTextColor(dc, fg);
            if k.kind == Kind::Clip {
                self.paint_clip_key(dc, k);
                return;
            }
            if k.kind == Kind::Cnav {
                SelectObject(dc, self.f_small as _);
                draw_text(dc, &k.text, &k.rc as *const _ as *mut _, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                return;
            }
            if k.kind == Kind::Scut {
                let s = k.vk as usize;
                if s < MAXSCUT && self.sc[s].used && self.sc[s].name[0] != 0 {
                    let mut disp = [0u16; 12];
                    if self.sc[s].star {
                        disp[0] = '\u{2605}' as u16;
                        for (i, &c) in self.sc[s].name.iter().take(8).enumerate() {
                            if c == 0 { break; }
                            disp[1 + i] = c;
                        }
                    } else {
                        for (i, &c) in self.sc[s].name.iter().enumerate() {
                            if c == 0 { break; }
                            disp[i] = c;
                        }
                    }
                    SelectObject(dc, if k.row == 14 { self.f_key } else { self.f_small } as _);
                    if self.sc_del_arm { SetTextColor(dc, rgb(255, 110, 110)); }
                    draw_text(dc, &disp, &k.rc as *const _ as *mut _, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else {
                    SelectObject(dc, self.f_key as _);
                    SetTextColor(dc, C_DIM);
                    let plus: [u16; 2] = ['+' as u16, 0];
                    draw_text(dc, &plus, &k.rc as *const _ as *mut _, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                return;
            }
            if k.kind == Kind::Info {
                let mut msg = [0u16; 64];
                SelectObject(dc, self.f_small as _);
                if self.rec_step == 0 {
                    let slot = self.rec_slot + 1;
                    let s = format!("\u{25CF} REC {}: tap modifiers+key (SC cancels)", slot);
                    set_wstr(&mut msg, &s);
                    SetTextColor(dc, rgb(255, 110, 110));
                } else {
                    let name = wstr_to_string(&self.rec_name);
                    let s = format!("Name: {} (Enter saves, Esc cancels)", name);
                    set_wstr(&mut msg, &s);
                    SetTextColor(dc, C_TXT);
                }
                draw_text(dc, &msg, &k.rc as *const _ as *mut _, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                return;
            }
            if k.kind == Kind::Sugg {
                if k.vk == 0 && self.highlight_on && self.misspelt && !self.word_buf.is_empty() {
                    SelectObject(dc, self.f_key as _);
                    SetTextColor(dc, rgb(255, 110, 110));
                    draw_text_n(dc, &self.word_buf, self.word_buf.len() as i32,
                        &k.rc as *const _ as *mut _, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    return;
                }
                if (k.vk as usize) < 3 {
                    let s = &self.sugg[k.vk as usize];
                    if !s.is_empty() {
                        SelectObject(dc, self.f_key as _);
                        let su: Vec<u16> = s.encode_utf16().collect();
                        draw_text(dc, &su, &k.rc as *const _ as *mut _, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                }
                return;
            }
            if k.kind == Kind::Char {
                let mut top = k.tag;
                let alpha = {
                    let c = k.lo as u8 as char;
                    c.is_ascii_alphabetic()
                };
                if top == 0 && !alpha { top = k.hi; }
                let mut r = k.rc;
                if top != 0 {
                    SelectObject(dc, self.f_small as _);
                    SetTextColor(dc, if self.mods[MX_SHIFT] != M_OFF { fg } else { C_DIM });
                    let t = [top, 0];
                    draw_text_n(dc, &t, 1, &mut r, DT_TOP | DT_CENTER | DT_SINGLELINE);
                    r.top += (10.0 * self.scale) as i32;
                }
                SelectObject(dc, self.f_key as _);
                SetTextColor(dc, fg);
                draw_text(dc, &k.text, &mut r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                let small = k.kind == Kind::Macro
                    || (k.kind == Kind::Special && (k.vk == VK_PRIOR as u32 || k.vk == VK_NEXT as u32));
                SelectObject(dc, if small { self.f_small } else { self.f_key } as _);
                draw_text(dc, &k.text, &k.rc as *const _ as *mut _, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }

    fn paint_clip_key(&self, dc: HDC, k: &Key) {
        unsafe {
            let hi = self.clip_off + k.vk as usize;
            if hi < self.clip.len() {
                let e = &self.clip[hi];
                if e.is_img && !e.hbmp.is_null() {
                    let mdc = CreateCompatibleDC(dc);
                    let mut th = k.rc.bottom - k.rc.top - 10;
                    if th < 16 { th = 16; }
                    let mut tw = th * e.iw / if e.ih != 0 { e.ih } else { 1 };
                    if tw < 8 { tw = 8; }
                    let ix = k.rc.left + 6;
                    let iy = k.rc.top + (k.rc.bottom - k.rc.top - th) / 2;
                    let fr = RECT { left: ix - 2, top: iy - 2, right: ix + tw + 2, bottom: iy + th + 2 };
                    let gb = CreateSolidBrush(rgb(110, 110, 110));
                    FillRect(dc, &fr, gb);
                    DeleteObject(gb);
                    let so = SelectObject(mdc, e.hbmp as _);
                    SetStretchBltMode(dc, HALFTONE);
                    SetBrushOrgEx(dc, 0, 0, std::ptr::null_mut());
                    StretchBlt(dc, ix, iy, tw, th, mdc, 0, 0, e.iw, e.ih, SRCCOPY);
                    SelectObject(mdc, so);
                    DeleteDC(mdc);
                    let mut dim = [0u16; 32];
                    let s = format!("IMG {}x{}", e.iw, e.ih);
                    set_wstr(&mut dim, &s);
                    SelectObject(dc, self.f_small as _);
                    let mut tr = k.rc;
                    tr.left += 12 + tw;
                    draw_text(dc, &dim, &mut tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                } else if let Some(ref text) = e.text {
                    let n = text.iter().position(|&c| c == 0).unwrap_or(text.len());
                    let mut prev = [0u16; 30];
                    let mut o = 0usize;
                    let mut j = 0usize;
                    while j < n && o < 28 {
                        let mut c = text[j];
                        if c == '\r' as u16 || c == '\n' as u16 || c == '\t' as u16 {
                            c = ' ' as u16;
                        } else if c < 0x20 || (c >= 0x7F && c < 0xA0) {
                            c = ' ' as u16;
                        }
                        if (0xD800..=0xDBFF).contains(&c) && j + 1 < n
                            && (0xDC00..=0xDFFF).contains(&text[j + 1])
                        {
                            if o + 2 > 28 { break; }
                            prev[o] = c;
                            j += 1;
                            o += 1;
                            prev[o] = text[j];
                            o += 1;
                            j += 1;
                            continue;
                        }
                        if (0xD800..=0xDFFF).contains(&c) { j += 1; continue; }
                        prev[o] = c;
                        o += 1;
                        j += 1;
                    }
                    if n > 28 {
                        let el: Vec<u16> = "\u{2026}".encode_utf16().collect();
                        for &c in &el {
                            if o >= 29 { break; }
                            prev[o] = c;
                            o += 1;
                        }
                    }
                    SelectObject(dc, self.f_key as _);
                    draw_text_n(dc, &prev, o as i32, &k.rc as *const _ as *mut _,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }
                if e.pinned {
                    let mut tw = (30.0 * self.scale) as i32;
                    let mut th = (15.0 * self.scale) as i32;
                    if tw < 22 { tw = 22; }
                    if th < 12 { th = 12; }
                    let mut tr = RECT {
                        right: k.rc.right - 4,
                        top: k.rc.top + 3,
                        left: 0, bottom: 0,
                    };
                    tr.left = tr.right - tw;
                    tr.bottom = tr.top + th;
                    if tr.left > k.rc.left + 4 {
                        fill_rr(dc, &tr, C_LOCK);
                        SetBkMode(dc, TRANSPARENT as i32);
                        SetTextColor(dc, C_TXT);
                        SelectObject(dc, self.f_small as _);
                        let pin: [u16; 4] = ['P' as u16, 'I' as u16, 'N' as u16, 0];
                        draw_text(dc, &pin, &mut tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                }
            } else {
                SelectObject(dc, self.f_small as _);
                SetTextColor(dc, C_DIM);
                let dots: [u16; 7] = ['\u{B7}' as u16, ' ' as u16, '\u{B7}' as u16,
                    ' ' as u16, '\u{B7}' as u16, ' ' as u16, 0];
                draw_text(dc, &dots, &k.rc as *const _ as *mut _,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }

    pub fn on_paint(&self) {
        unsafe {
            let mut ps: PAINTSTRUCT = std::mem::zeroed();
            let dc = BeginPaint(self.hwnd, &mut ps);
            let mut cl = zero_rect();
            GetClientRect(self.hwnd, &mut cl);
            let b = CreateSolidBrush(C_BG);
            FillRect(dc, &cl, b);
            DeleteObject(b);
            let bar = RECT { left: 0, top: 0, right: cl.right, bottom: self.bar_h() };
            let b = CreateSolidBrush(C_BAR);
            FillRect(dc, &bar, b);
            DeleteObject(b);
            SetBkMode(dc, TRANSPARENT as i32);
            SelectObject(dc, self.f_bar as _);
            for i in 0..7 {
                if !self.bar[i].show { continue; }
                let bg = if self.bar[i].id == self.down_idx - 1000 { C_ACTIVE } else { C_SPEC };
                fill_rr(dc, &self.bar[i].rc, bg);
                SetTextColor(dc, C_TXT);
                SelectObject(dc, self.f_bar as _);
                draw_text(dc, &self.bar[i].t, &self.bar[i].rc as *const _ as *mut _,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            for i in 0..self.keys.len() {
                let k = self.keys[i];
                if !self.visible_row(k.row) { continue; }
                if is_rect_empty(&k.rc) { continue; }
                self.paint_key(dc, &k, i as i32);
            }
            if self.hold_popup >= 0 && !is_rect_empty(&self.pop_all) {
                for i in 0..2 {
                    let c = if i == 1 { self.pop_alt } else { self.pop_base };
                    let pbg = if i == self.pop_hover { C_ACTIVE }
                        else if i == 1 { C_LOCK } else { C_HELD };
                    let pfg = if i == self.pop_hover || i == 1 { C_TXT } else { rgb(0, 0, 0) };
                    fill_rr(dc, &self.pop_rc[i as usize], pbg);
                    SetBkMode(dc, TRANSPARENT as i32);
                    SetTextColor(dc, pfg);
                    SelectObject(dc, self.f_key as _);
                    let s = [c, 0];
                    draw_text_n(dc, &s, 1, &self.pop_rc[i as usize] as *const _ as *mut _,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
            EndPaint(self.hwnd, &mut ps);
        }
    }
}

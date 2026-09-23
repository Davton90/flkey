use crate::app::*;
use crate::dict::*;
use windows_sys::Win32::Foundation::*;
use windows_sys::Win32::UI::Input::KeyboardAndMouse::*;
use windows_sys::Win32::UI::WindowsAndMessaging::*;
use windows_sys::Win32::System::Threading::*;
use windows_sys::Win32::Graphics::Gdi::*;

impl App {
    pub fn combo_active(&self) -> bool {
        self.mods[MX_CTRL] != M_OFF || self.mods[MX_ALT] != M_OFF || self.mods[MX_WIN] != M_OFF
    }

    pub fn consume_held(&mut self) {
        for i in 0..4 {
            if self.mods[i] == M_HELD {
                unsafe { vk_up([VK_SHIFT as u32, VK_CONTROL as u32, VK_MENU as u32, VK_LWIN as u32][i]); }
                self.mods[i] = M_OFF;
                self.inv_mod(i as i32);
            }
        }
    }

    pub fn shifted_for(&self, lo: u16) -> bool {
        let caps = unsafe { (GetKeyState(VK_CAPITAL as i32) & 1) != 0 };
        let sh = self.mods[MX_SHIFT] != M_OFF;
        let c = lo as u8 as char;
        if c.is_ascii_alphabetic() { return caps != sh; }
        sh
    }

    pub fn ensure_focus(&mut self) {
        unsafe {
            let fg = GetForegroundWindow();
            if fg == self.hwnd {
                if IsWindow(self.last_fg) != 0 {
                    self.force_foreground(self.last_fg);
                    Sleep(30);
                }
                return;
            }
            if !fg.is_null() && !self.is_shell_chrome(fg) && IsWindowVisible(fg) != 0 {
                if !self.startup_fg.is_null() && fg == self.startup_fg
                    && IsWindow(self.last_fg) != 0
                    && self.last_fg != fg
                {
                    self.force_foreground(self.last_fg);
                    Sleep(30);
                    return;
                }
                self.last_fg = fg;
                self.startup_fg = std::ptr::null_mut();
                return;
            }
            if IsWindow(self.last_fg) != 0 {
                self.force_foreground(self.last_fg);
                Sleep(30);
            }
        }
    }

    pub fn force_foreground(&mut self, target: HWND) {
        unsafe {
            if IsWindow(target) == 0 { return; }
            if IsIconic(target) != 0 { ShowWindow(target, SW_RESTORE); }
            let fg = GetForegroundWindow();
            if fg == target { return; }
            let our_thread = GetCurrentThreadId();
            let fg_thread = if !fg.is_null() { GetWindowThreadProcessId(fg, std::ptr::null_mut()) } else { 0 };
            let tgt_thread = GetWindowThreadProcessId(target, std::ptr::null_mut());
            let mut a_fg = false;
            let mut a_tg = false;
            if fg_thread != 0 && fg_thread != our_thread {
                a_fg = AttachThreadInput(our_thread, fg_thread, TRUE) != 0;
            }
            if tgt_thread != 0 && tgt_thread != our_thread && tgt_thread != fg_thread {
                a_tg = AttachThreadInput(our_thread, tgt_thread, TRUE) != 0;
            }
            if fg_thread != 0 && tgt_thread != 0 && fg_thread != tgt_thread {
                AttachThreadInput(fg_thread, tgt_thread, TRUE);
            }
            SetForegroundWindow(target);
            if fg_thread != 0 && tgt_thread != 0 && fg_thread != tgt_thread {
                AttachThreadInput(fg_thread, tgt_thread, FALSE);
            }
            if a_tg { AttachThreadInput(our_thread, tgt_thread, FALSE); }
            if a_fg { AttachThreadInput(our_thread, fg_thread, FALSE); }
        }
    }

    pub fn is_shell_chrome(&self, h: HWND) -> bool {
        unsafe {
            if h.is_null() { return true; }
            let mut cls = [0u16; 64];
            if GetClassNameW(h, cls.as_mut_ptr(), 64) == 0 { return false; }
            if wstr_eq(&cls, "Shell_TrayWnd") || wstr_eq(&cls, "Progman")
                || wstr_eq(&cls, "WorkerW") || wstr_eq(&cls, "FloatKeys")
            {
                return true;
            }
            if h == self.hwnd { return true; }
            let ex = GetWindowLongW(h, GWL_EXSTYLE);
            if ex & WS_EX_NOACTIVATE as i32 != 0 { return true; }
            if wstr_eq(&cls, "Windows.UI.Core.CoreWindow")
                || wstr_eq(&cls, "XamlExplorerHostIslandWindow")
                || wstr_eq(&cls, "DV2ControlHost")
            {
                let mut pid: u32 = 0;
                GetWindowThreadProcessId(h, &mut pid);
                if pid != 0 {
                    let pr = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid);
                    if !pr.is_null() {
                        let mut img = [0u16; 260];
                        let mut n: u32 = 260;
                        let mut shell = false;
                        if QueryFullProcessImageNameW(pr, 0, img.as_mut_ptr(), &mut n) != 0 {
                            shell = wstr_contains_ci(&img, "startmenu")
                                || wstr_contains_ci(&img, "searchhost")
                                || wstr_contains_ci(&img, "shellexperience")
                                || wstr_contains_ci(&img, "textinputhost")
                                || wstr_contains_ci(&img, "searchui");
                        }
                        CloseHandle(pr);
                        return shell;
                    }
                }
                return true;
            }
            false
        }
    }

    pub fn track_foreground(&mut self, fg: HWND) {
        unsafe {
            if fg.is_null() || fg == self.hwnd { return; }
            if IsWindow(fg) == 0 || IsWindowVisible(fg) == 0 { return; }
            if self.is_shell_chrome(fg) { return; }
            if IsIconic(fg) != 0 { return; }
            if !self.startup_fg.is_null() {
                let mut cls = [0u16; 64];
                if GetClassNameW(fg, cls.as_mut_ptr(), 64) != 0
                    && (wstr_eq(&cls, "CabinetWClass") || wstr_eq(&cls, "ExploreWClass"))
                {
                    return;
                }
            }
            self.last_fg = fg;
        }
    }

    pub fn emit_text(&mut self, s: &[u16]) {
        let ss = self.mods[MX_SHIFT];
        let sdn = ss != M_OFF;
        let combo = self.combo_active();
        self.ensure_focus();
        self.auto_active = false;
        let single = s.first().copied().unwrap_or(0);
        let is_single = s.iter().take_while(|&&c| c != 0).count() == 1;
        let commit = is_single && !combo && Self::is_commit_char(single) && self.word_len() >= 3;
        if sdn && !combo { unsafe { vk_up(VK_SHIFT as u32); } }
        if commit { self.auto_correct_now(true); }
        unsafe { type_unicode(s); }
        for &ch in s.iter().take_while(|&&c| c != 0) {
            self.word_char(ch);
        }
        if sdn && !combo {
            if ss == M_LOCKED {
                unsafe { vk_down(VK_SHIFT as u32); }
            } else if self.mods[MX_SHIFT] != M_OFF {
                self.mods[MX_SHIFT] = M_OFF;
                self.inv_mod(MX_SHIFT as i32);
            }
        }
        self.consume_held();
        self.inv_sugg();
    }

    pub fn emit_vk(&mut self, vk: u32) {
        self.ensure_focus();
        if vk == VK_BACK as u32 && self.auto_active {
            let n = self.auto_corr_len + if self.auto_trail { 1 } else { 0 };
            self.auto_active = false;
            for _ in 0..n { unsafe { vk_tap(VK_BACK as u32); } }
            let orig = self.auto_orig.clone();
            unsafe { type_unicode(&orig); }
            for &ch in &orig {
                self.word_char(ch);
            }
            self.consume_held();
            self.inv_sugg();
            return;
        }
        self.auto_active = false;
        if vk == VK_RETURN as u32 && !self.combo_active() && self.word_len() >= 3 {
            self.auto_correct_now(false);
        }
        unsafe { vk_tap(vk); }
        if vk == VK_BACK as u32 {
            self.word_chop();
        } else if vk == VK_RETURN as u32 {
            self.word_commit();
        } else if vk != VK_SHIFT as u32 && vk != VK_CONTROL as u32 && vk != VK_MENU as u32 && vk != VK_LWIN as u32 {
            self.word_clear();
        }
        self.consume_held();
        self.inv_sugg();
    }

    pub fn emit_vk_char(&mut self, vk: u32, ch: u16) {
        if vk == 0 {
            let s = [ch, 0];
            self.emit_text(&s);
            return;
        }
        self.ensure_focus();
        self.auto_active = false;
        if Self::is_commit_char(ch) && self.word_len() >= 3 {
            self.auto_correct_now(true);
        }
        unsafe { vk_tap(vk); }
        self.word_char(ch);
        self.consume_held();
        self.inv_sugg();
    }

    pub fn emit_char_choice(&mut self, lo: u16, ch: u16) {
        if self.combo_active() {
            let vk = char_to_vk(lo);
            if vk != 0 {
                self.emit_vk(vk);
                return;
            }
            let s = [ch, 0];
            self.emit_text(&s);
        } else {
            let vk = if ch == ' ' as u16 { VK_SPACE as u32 } else { char_to_vk(lo) };
            self.emit_vk_char(vk, ch);
        }
    }

    pub fn run_macro(&mut self, mods: i32, vk: u32) {
        self.ensure_focus();
        self.auto_active = false;
        let seq = [1usize, 0, 2, 3];
        for i in 0..4 {
            if mods & (1 << seq[i]) != 0 {
                unsafe { vk_down([VK_SHIFT as u32, VK_CONTROL as u32, VK_MENU as u32, VK_LWIN as u32][seq[i]]); }
            }
        }
        unsafe { Sleep(30); vk_tap(vk); Sleep(30); }
        for i in (0..4).rev() {
            if mods & (1 << seq[i]) != 0 {
                unsafe { vk_up([VK_SHIFT as u32, VK_CONTROL as u32, VK_MENU as u32, VK_LWIN as u32][seq[i]]); }
            }
        }
        self.consume_held();
    }

    pub fn toggle_mod(&mut self, m: usize) {
        let modvk = [VK_SHIFT as u32, VK_CONTROL as u32, VK_MENU as u32, VK_LWIN as u32][m];
        if self.mods[m] == M_OFF {
            self.mods[m] = M_HELD;
            unsafe { vk_down(modvk); }
        } else if self.mods[m] == M_HELD {
            self.mods[m] = M_LOCKED;
        } else {
            self.mods[m] = M_OFF;
            unsafe { vk_up(modvk); }
        }
        self.inv_mod(m as i32);
    }

    pub fn is_repeatable(vk: u32) -> bool {
        matches!(vk as u16,
            VK_BACK | VK_DELETE | VK_SPACE | VK_LEFT | VK_UP | VK_DOWN | VK_RIGHT |
            VK_PRIOR | VK_NEXT)
    }

    pub fn is_double_bksp(&self, now: u32) -> bool {
        self.bksp_last_up != 0 && now.wrapping_sub(self.bksp_last_up) <= BKSP_DBL_MS
    }

    pub fn double_backspace(&mut self) {
        self.ensure_focus();
        self.bksp_last_up = 0;
        let n = self.word_len().min(31);
        if n > 0 {
            for _ in 0..n { unsafe { vk_tap(VK_BACK as u32); } }
        } else {
            if self.mods[MX_CTRL] != M_OFF {
                unsafe { vk_tap(VK_BACK as u32); }
            } else {
                unsafe {
                    vk_down(VK_CONTROL as u32);
                    Sleep(15);
                    vk_tap(VK_BACK as u32);
                    Sleep(15);
                    vk_up(VK_CONTROL as u32);
                }
            }
        }
        self.word_clear();
        self.consume_held();
        self.inv_sugg();
    }

    pub fn shcap_press(&mut self) {
        if self.shcap_armed {
            self.shcap_armed = false;
            unsafe { KillTimer(self.hwnd, TIMER_SHCAP); }
            if self.mods[MX_SHIFT] != M_OFF {
                unsafe { vk_up(VK_SHIFT as u32); }
                self.mods[MX_SHIFT] = M_OFF;
            }
            unsafe { vk_tap(VK_CAPITAL as u32); }
            self.inv_mod(MX_SHIFT as i32);
            self.inv_caps();
            self.shcap_refresh();
            return;
        }
        if self.mods[MX_SHIFT] != M_OFF {
            unsafe { vk_up(VK_SHIFT as u32); }
            self.mods[MX_SHIFT] = M_OFF;
            self.inv_mod(MX_SHIFT as i32);
            self.shcap_refresh();
            return;
        }
        self.mods[MX_SHIFT] = M_HELD;
        unsafe { vk_down(VK_SHIFT as u32); }
        self.inv_mod(MX_SHIFT as i32);
        self.shcap_refresh();
        self.shcap_armed = true;
        unsafe { SetTimer(self.hwnd, TIMER_SHCAP, 350, None); }
    }

    pub fn shcap_refresh(&mut self) {
        let idxs: Vec<usize> = self.keys.iter().enumerate()
            .filter(|(_, k)| k.kind == Kind::Shcap)
            .map(|(i, _)| i)
            .collect();
        for i in idxs { self.inv_key(i as i32); }
    }

    pub fn flush_pending(&mut self) {
        let idx = self.pending;
        self.pending = -1;
        unsafe { KillTimer(self.hwnd, TIMER_HOLD); }
        if idx < 0 || idx as usize >= self.keys.len() { return; }
        let k = self.keys[idx as usize];
        if k.kind == Kind::Char {
            let ch = if self.shifted_for(k.lo) { k.hi } else { k.lo };
            self.emit_char_choice(k.lo, ch);
        }
    }

    pub fn show_popup(&mut self, idx: i32) {
        if idx < 0 || idx as usize >= self.keys.len() { return; }
        let k = self.keys[idx as usize];
        if k.kind != Kind::Char { return; }
        let alt = if k.tag != 0 { k.tag }
            else if k.hi != k.lo { k.hi } else { 0 };
        if alt == 0 { return; }
        let mut cl = zero_rect();
        unsafe { GetClientRect(self.hwnd, &mut cl); }
        let cw = cl.right;
        self.pop_base = k.lo;
        self.pop_alt = alt;
        self.hold_popup = idx;
        self.pop_hover = -1;
        let mut cell_w = k.rc.right - k.rc.left;
        if cell_w < 52 { cell_w = 52; }
        let cell_h = k.rc.bottom - k.rc.top;
        let cx = (k.rc.left + k.rc.right) / 2;
        let mut x0 = cx - cell_w;
        if x0 < 6 { x0 = 6; }
        if x0 + cell_w * 2 > cw - 6 { x0 = cw - 6 - cell_w * 2; }
        let mut y1 = k.rc.top - 4;
        let mut y0 = y1 - cell_h;
        if y0 < self.bar_h() + 2 {
            y0 = k.rc.bottom + 4;
            y1 = y0 + cell_h;
        }
        unsafe {
            set_rect(&mut self.pop_rc[0], x0, y0, x0 + cell_w, y1);
            set_rect(&mut self.pop_rc[1], x0 + cell_w, y0, x0 + cell_w * 2, y1);
            self.pop_all = self.pop_rc[0];
            if self.pop_rc[1].left < self.pop_all.left { self.pop_all.left = self.pop_rc[1].left; }
            if self.pop_rc[1].top < self.pop_all.top { self.pop_all.top = self.pop_rc[1].top; }
            if self.pop_rc[1].right > self.pop_all.right { self.pop_all.right = self.pop_rc[1].right; }
            if self.pop_rc[1].bottom > self.pop_all.bottom { self.pop_all.bottom = self.pop_rc[1].bottom; }
            self.pop_all.left -= 2;
            self.pop_all.top -= 2;
            self.pop_all.right += 2;
            self.pop_all.bottom += 2;
            InvalidateRect(self.hwnd, &self.pop_all, FALSE);
        }
    }

    pub fn resolve_popup(&mut self) {
        let idx = self.hold_popup;
        self.hold_popup = -1;
        self.pending = -1;
        unsafe { KillTimer(self.hwnd, TIMER_HOLD); }
        if idx < 0 || idx as usize >= self.keys.len() { return; }
        let k = self.keys[idx as usize];
        if k.kind != Kind::Char { return; }
        unsafe { InvalidateRect(self.hwnd, &self.pop_all, FALSE); }
        if self.pop_hover == 1 {
            let s = [self.pop_alt, 0];
            self.emit_text(&s);
            return;
        }
        let ch = if self.shifted_for(k.lo) { k.hi } else { k.lo };
        self.emit_char_choice(k.lo, ch);
    }

    pub fn commit_sugg(&mut self, slot: i32) {
        self.auto_active = false;
        if slot == 0 && self.highlight_on && self.misspelt && self.word_len() > 0 {
            let sp = [' ' as u16, 0];
            self.emit_text(&sp);
            return;
        }
        if slot < 0 || slot > 2 || self.sugg[slot as usize].is_empty() { return; }
        let mut w: Vec<u16> = self.sugg[slot as usize].encode_utf16().collect();
        w.push(0);
        let cap = self.word_len() > 0
            && char::from_u32(self.word_buf[0] as u32).map(|c| c.is_ascii_uppercase()).unwrap_or(false);
        if cap {
            if let Some(c) = char::from_u32(w[0] as u32) {
                if c.is_ascii_lowercase() {
                    w[0] = c.to_ascii_uppercase() as u16;
                }
            }
        }
        let n = self.word_len();
        for _ in 0..n {
            self.emit_vk(VK_BACK as u32);
        }
        self.emit_text(&w);
        let sp = [' ' as u16, 0];
        self.emit_text(&sp);
    }

    pub fn do_set_action(&mut self, a: u32) {
        match a {
            SET_SM => {
                if self.scale > 0.75 { self.scale -= 0.1; self.resize_for_scale(); }
            }
            SET_BG => {
                if self.scale < 1.5 { self.scale += 0.1; self.resize_for_scale(); }
            }
            SET_OPDN => {
                self.opacity -= 13;
                if self.opacity < 102 { self.opacity = 102; }
                self.apply_opacity();
            }
            SET_OPUP => {
                self.opacity += 13;
                if self.opacity > 255 { self.opacity = 255; }
                self.apply_opacity();
            }
            SET_DONE => {
                self.show_settings = false;
                self.resize_for_scale();
            }
            SET_LANG => {
                let other = if self.lang == 0 { 1 } else { 0 };
                if dict_avail(self, other) {
                    self.lang = other;
                } else if !dict_avail(self, self.lang) {
                    return;
                }
                self.prev_word.clear();
                self.prev_idx = -1;
                for s in self.sugg.iter_mut() { s.clear(); }
                self.misspelt = false;
                self.word_clear();
                self.build_keys();
                let mut cl = zero_rect();
                unsafe { GetClientRect(self.hwnd, &mut cl); }
                self.compute_layout(cl.right, cl.bottom);
                unsafe { InvalidateRect(self.hwnd, std::ptr::null(), TRUE); }
            }
            SET_PRED => {
                self.predict_on = !self.predict_on;
                for s in self.sugg.iter_mut() { s.clear(); }
                self.misspelt = false;
                self.compute_sugg();
                self.resize_for_scale();
            }
            SET_AUTO => {
                self.autocorrect = !self.autocorrect;
                self.auto_active = false;
            }
            SET_SPELL => {
                self.highlight_on = !self.highlight_on;
                for s in self.sugg.iter_mut() { s.clear(); }
                self.misspelt = false;
                self.compute_sugg();
                self.resize_for_scale();
            }
            SET_PINWIN => self.toggle_pin_window(),
            SET_BIGRAM => {
                self.bigram_on = !self.bigram_on;
                for s in self.sugg.iter_mut() { s.clear(); }
                self.misspelt = false;
                self.compute_sugg();
                self.inv_sugg();
            }
            SET_PINBAR => {
                self.pin_bar_on = !self.pin_bar_on;
                self.resize_for_scale();
            }
            _ => {}
        }
        self.save_settings();
    }

    pub fn is_pinnable(&self, h: HWND) -> bool {
        unsafe {
            if h.is_null() || h == self.hwnd || IsWindow(h) == 0 || IsWindowVisible(h) == 0 {
                return false;
            }
            let mut cls = [0u16; 64];
            GetClassNameW(h, cls.as_mut_ptr(), 64);
            !(wstr_eq(&cls, "Shell_TrayWnd") || wstr_eq(&cls, "Shell_SecondaryTrayWnd")
                || wstr_eq(&cls, "Progman") || wstr_eq(&cls, "WorkerW")
                || wstr_eq(&cls, "FloatKeys"))
        }
    }

    pub fn pin_index(&self, h: HWND) -> i32 {
        self.pinwins[..self.npinwins].iter().position(|&p| p == h)
            .map(|i| i as i32).unwrap_or(-1)
    }

    pub fn pin_target(&self) -> HWND {
        unsafe {
            if IsWindow(self.last_fg) != 0 && self.is_pinnable(self.last_fg) {
                return self.last_fg;
            }
            let fg = GetForegroundWindow();
            if self.is_pinnable(fg) { return fg; }
            std::ptr::null_mut()
        }
    }

    pub fn cur_target_pinned(&self) -> bool {
        unsafe { !self.last_fg.is_null() && IsWindow(self.last_fg) != 0 && self.pin_index(self.last_fg) >= 0 }
    }

    pub fn inv_pin_btn(&mut self) {
        let idxs: Vec<usize> = self.keys.iter().enumerate()
            .filter(|(_, k)| k.kind == Kind::Set && k.vk == SET_PINWIN)
            .map(|(i, _)| i)
            .collect();
        for i in idxs { self.inv_key(i as i32); }
    }

    pub fn toggle_pin_window(&mut self) {
        let t = self.pin_target();
        if t.is_null() { return; }
        let i = self.pin_index(t);
        if i >= 0 {
            unsafe {
                SetWindowPos(t, HWND_NOTOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            let ui = i as usize;
            for k in ui..self.npinwins.saturating_sub(1) {
                self.pinwins[k] = self.pinwins[k + 1];
            }
            self.npinwins = self.npinwins.saturating_sub(1);
        } else {
            if self.npinwins >= MAXPINWIN { return; }
            unsafe {
                if IsIconic(t) != 0 { ShowWindow(t, SW_RESTORE); }
                SetWindowPos(t, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            self.pinwins[self.npinwins] = t;
            self.npinwins += 1;
        }
        self.inv_pin_btn();
    }

    pub fn repair_pins(&mut self) {
        let mut w2 = 0;
        let mut changed = false;
        for i in 0..self.npinwins {
            let ph = self.pinwins[i];
            unsafe {
                if IsWindow(ph) == 0 { changed = true; continue; }
                if GetWindowLongW(ph, GWL_EXSTYLE) & WS_EX_TOPMOST as i32 == 0 {
                    SetWindowPos(ph, HWND_TOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
            self.pinwins[w2] = ph;
            w2 += 1;
        }
        if w2 != self.npinwins {
            self.npinwins = w2;
            changed = true;
        }
        if changed { self.inv_pin_btn(); }
    }

    pub fn unpin_all(&mut self) {
        for i in 0..self.npinwins {
            let ph = self.pinwins[i];
            unsafe {
                if IsWindow(ph) != 0 {
                    SetWindowPos(ph, HWND_NOTOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
        }
        self.npinwins = 0;
    }

    pub fn inv_rec(&mut self) {
        let idxs: Vec<usize> = self.keys.iter().enumerate()
            .filter(|(_, k)| k.kind == Kind::Info)
            .map(|(i, _)| i)
            .collect();
        for i in idxs { self.inv_key(i as i32); }
    }

    pub fn cancel_record(&mut self) {
        self.rec_slot = -1;
        self.rec_step = 0;
        self.rec_mods = 0;
        self.rec_vk = 0;
        self.rec_name = [0; 10];
        for i in 0..4 {
            if self.mods[i] != M_OFF {
                self.mods[i] = M_OFF;
                self.inv_mod(i as i32);
            }
        }
    }

    pub fn start_record(&mut self, s: i32) {
        if s < 0 || s as usize >= MAXSCUT { return; }
        for i in 0..4 {
            if self.mods[i] != M_OFF {
                unsafe { vk_up([VK_SHIFT as u32, VK_CONTROL as u32, VK_MENU as u32, VK_LWIN as u32][i]); }
                self.mods[i] = M_OFF;
                self.inv_mod(i as i32);
            }
        }
        self.rec_slot = s;
        self.rec_step = 0;
        self.rec_mods = 0;
        self.rec_vk = 0;
        self.rec_name = [0; 10];
        self.view_sc = false;
        self.sc_del_arm = false;
        self.star_arm = false;
        self.resize_for_scale();
    }

    pub fn key_name(vk: u32) -> String {
        if (b'A' as u32..=b'Z' as u32).contains(&vk) {
            return (vk as u8 as char).to_string();
        }
        if (b'0' as u32..=b'9' as u32).contains(&vk) {
            return (vk as u8 as char).to_string();
        }
        if vk >= VK_F1 as u32 && vk <= VK_F12 as u32 {
            return format!("F{}", (vk.wrapping_sub(VK_F1 as u32)) + 1);
        }
        match vk as u16 {
            VK_TAB => "Tab".into(),
            VK_RETURN => "Ent".into(),
            VK_ESCAPE => "Esc".into(),
            VK_BACK => "Bksp".into(),
            VK_DELETE => "Del".into(),
            VK_SPACE => "Spc".into(),
            VK_LEFT => "Lt".into(),
            VK_UP => "Up".into(),
            VK_RIGHT => "Rt".into(),
            VK_DOWN => "Dn".into(),
            VK_CAPITAL => "Caps".into(),
            VK_PRIOR => "PgUp".into(),
            VK_NEXT => "PgDn".into(),
            VK_SNAPSHOT => "Prt".into(),
            VK_APPS => "Menu".into(),
            VK_OEM_1 => ";".into(),
            VK_OEM_PLUS => "=".into(),
            VK_OEM_COMMA => ",".into(),
            VK_OEM_MINUS => "-".into(),
            VK_OEM_PERIOD => ".".into(),
            VK_OEM_2 => "/".into(),
            VK_OEM_3 => "`".into(),
            VK_OEM_4 => "[".into(),
            VK_OEM_5 => "\\".into(),
            VK_OEM_6 => "]".into(),
            VK_OEM_7 => "'".into(),
            _ => format!("{:02X}", vk & 0xFF),
        }
    }

    pub fn auto_name(mods: i32, vk: u32) -> String {
        let mut out = String::new();
        if mods & 0x2 != 0 { out.push_str("C+"); }
        if mods & 0x1 != 0 { out.push_str("S+"); }
        if mods & 0x4 != 0 { out.push_str("A+"); }
        if mods & 0x8 != 0 { out.push_str("W+"); }
        out.push_str(&Self::key_name(vk));
        out
    }

    pub fn commit_record(&mut self) {
        let s = self.rec_slot;
        if s < 0 || s as usize >= MAXSCUT || self.rec_vk == 0 { return; }
        let name_raw = wstr_to_string(&self.rec_name);
        let trimmed = name_raw.trim().to_string();
        let nm = if !trimmed.is_empty() {
            trimmed.chars().take(9).collect::<String>()
        } else {
            Self::auto_name(self.rec_mods, self.rec_vk)
        };
        let si = s as usize;
        self.sc[si].used = true;
        self.sc[si].mods = self.rec_mods & 0xF;
        self.sc[si].vk = self.rec_vk;
        set_wstr(&mut self.sc[si].name, &nm);
        self.cancel_record();
        self.view_sc = true;
        self.save_settings();
        self.rebuild_keys();
        self.resize_for_scale();
    }

    pub fn capture_key(&mut self, idx: usize) -> bool {
        let k = self.keys[idx];
        if self.rec_step == 0 {
            if k.kind == Kind::Mod {
                self.rec_mods ^= 1 << k.mod_;
                self.inv_mod(k.mod_);
                self.inv_rec();
                return true;
            }
            if k.kind == Kind::Shcap {
                self.rec_mods ^= 1 << MX_SHIFT;
                self.inv_mod(MX_SHIFT as i32);
                self.shcap_refresh();
                self.inv_rec();
                return true;
            }
            if k.kind == Kind::Char {
                let vk = char_to_vk(k.lo);
                if vk == 0 { return true; }
                self.rec_vk = vk;
            } else if k.kind == Kind::Special {
                self.rec_vk = k.vk;
            } else if k.kind == Kind::Space {
                self.rec_vk = VK_SPACE as u32;
            } else if k.kind == Kind::Caps {
                self.rec_vk = VK_CAPITAL as u32;
            } else {
                return true;
            }
            self.rec_step = 1;
            self.rec_name = [0; 10];
            self.inv_rec();
            return true;
        }
        if k.kind == Kind::Mod {
            let m = k.mod_ as usize;
            self.mods[m] = if self.mods[m] == M_OFF { M_HELD } else { M_OFF };
            self.inv_mod(k.mod_);
            return true;
        }
        if k.kind == Kind::Shcap {
            self.mods[MX_SHIFT] = if self.mods[MX_SHIFT] == M_OFF { M_HELD } else { M_OFF };
            self.inv_mod(MX_SHIFT as i32);
            self.shcap_refresh();
            return true;
        }
        if k.kind == Kind::Char {
            let len = self.rec_name.iter().position(|&c| c == 0).unwrap_or(10);
            if len < 9 {
                self.rec_name[len] = if self.shifted_for(k.lo) { k.hi } else { k.lo };
                self.inv_rec();
            }
            return true;
        }
        if k.kind == Kind::Space {
            let len = self.rec_name.iter().position(|&c| c == 0).unwrap_or(10);
            if len < 9 && len > 0 {
                self.rec_name[len] = ' ' as u16;
                self.inv_rec();
            }
            return true;
        }
        if k.kind == Kind::Special {
            if k.vk == VK_BACK as u32 {
                let len = self.rec_name.iter().position(|&c| c == 0).unwrap_or(0);
                if len > 0 {
                    self.rec_name[len - 1] = 0;
                    self.inv_rec();
                }
            } else if k.vk == VK_RETURN as u32 {
                self.commit_record();
            } else if k.vk == VK_ESCAPE as u32 {
                self.cancel_record();
                self.view_sc = true;
                self.resize_for_scale();
            }
            return true;
        }
        true
    }

    pub fn press_key(&mut self, idx: i32) {
        if idx < 0 || idx as usize >= self.keys.len() { return; }
        if self.rec_slot >= 0 && !self.view_sc && !self.view_clip && self.capture_key(idx as usize) {
            self.pressed = idx;
            unsafe { SetTimer(self.hwnd, TIMER_FLASH, 80, None); }
            self.inv_key(idx);
            return;
        }
        let k = self.keys[idx as usize];
        let is_bksp = k.kind == Kind::Special && k.vk == VK_BACK as u32;
        if !is_bksp {
            self.bksp_last_up = 0;
        }
        if k.kind != Kind::Char && self.pending >= 0 {
            self.flush_pending();
        }
        self.pressed = idx;
        unsafe { SetTimer(self.hwnd, TIMER_FLASH, 80, None); }
        self.inv_key(idx);
        match k.kind {
            Kind::Mod => { self.toggle_mod(k.mod_ as usize); return; }
            Kind::Shcap => { self.shcap_press(); return; }
            Kind::Sugg => { self.commit_sugg(k.vk as i32); return; }
            Kind::Clip => {
                let hi = self.clip_off + k.vk as usize;
                if self.pin_arm {
                    if hi < self.clip.len() {
                        self.clip_toggle_pin(hi);
                        unsafe { InvalidateRect(self.hwnd, std::ptr::null(), FALSE); }
                    }
                    return;
                }
                self.paste_clip(hi);
                return;
            }
            Kind::Cnav => {
                if k.vk == CNAV_UP && self.clip_off > 0 {
                    self.clip_off = self.clip_off.saturating_sub(5);
                } else if k.vk == CNAV_DOWN && self.clip_off + 5 < self.clip.len() {
                    self.clip_off += 5;
                } else if k.vk == CNAV_CLEAR {
                    self.clip_clear_unpinned();
                } else if k.vk == CNAV_PIN {
                    self.pin_arm = !self.pin_arm;
                } else if k.vk == CNAV_SDEL {
                    self.sc_del_arm = !self.sc_del_arm;
                    if self.sc_del_arm { self.star_arm = false; }
                } else if k.vk == CNAV_SSTAR {
                    self.star_arm = !self.star_arm;
                    if self.star_arm { self.sc_del_arm = false; }
                } else if k.vk == CNAV_SBACK {
                    self.view_sc = false;
                    self.sc_del_arm = false;
                    self.star_arm = false;
                    self.resize_for_scale();
                    unsafe { InvalidateRect(self.hwnd, std::ptr::null(), TRUE); }
                    return;
                } else if k.vk == CNAV_BACK {
                    self.view_clip = false;
                    self.pin_arm = false;
                    self.pressed = -1;
                    unsafe { KillTimer(self.hwnd, TIMER_FLASH); }
                    self.resize_for_scale();
                    unsafe { InvalidateRect(self.hwnd, std::ptr::null(), TRUE); }
                    return;
                }
                unsafe { InvalidateRect(self.hwnd, std::ptr::null(), FALSE); }
                return;
            }
            Kind::Macro => { self.run_macro(k.mod_, k.vk); return; }
            Kind::Scut => {
                let s = k.vk as usize;
                if s >= MAXSCUT { return; }
                if self.star_arm {
                    if self.sc[s].used && self.sc[s].vk != 0 {
                        if !self.sc[s].star && self.star_count() >= 3 {
                            // max 3
                        } else {
                            self.sc[s].star = !self.sc[s].star;
                            self.save_settings();
                            self.rebuild_keys();
                            self.resize_for_scale();
                        }
                    }
                    return;
                }
                if self.sc_del_arm {
                    if self.sc[s].used {
                        self.sc[s] = Shortcut::default();
                        self.save_settings();
                        self.rebuild_keys();
                        self.resize_for_scale();
                    }
                    return;
                }
                if !self.sc[s].used || self.sc[s].vk == 0 {
                    self.start_record(s as i32);
                    return;
                }
                let (m, vk) = (self.sc[s].mods, self.sc[s].vk);
                self.run_macro(m, vk);
                return;
            }
            Kind::Info => return,
            Kind::Set => { self.do_set_action(k.vk); return; }
            Kind::Page => {
                self.pressed = -1;
                unsafe { KillTimer(self.hwnd, TIMER_FLASH); }
                self.page = k.vk as i32;
                self.build_keys();
                let mut cl = zero_rect();
                unsafe { GetClientRect(self.hwnd, &mut cl); }
                self.compute_layout(cl.right, cl.bottom);
                unsafe { InvalidateRect(self.hwnd, std::ptr::null(), TRUE); }
                return;
            }
            Kind::Caps => {
                unsafe { vk_tap(VK_CAPITAL as u32); }
                self.inv_caps();
                return;
            }
            Kind::Space => {
                if self.combo_active() {
                    self.repeat_idx = idx;
                    self.repeat_vk = VK_SPACE as u32;
                    self.repeat_first = true;
                    unsafe { SetTimer(self.hwnd, TIMER_REPEAT, 450, None); }
                    self.emit_vk(VK_SPACE as u32);
                } else {
                    self.ensure_focus();
                    self.auto_active = false;
                    if self.word_len() >= 3 { self.auto_correct_now(true); }
                    unsafe { vk_tap(VK_SPACE as u32); }
                    self.word_char(' ' as u16);
                    self.consume_held();
                    self.inv_sugg();
                }
                return;
            }
            Kind::Char => {
                if self.pending >= 0 { self.flush_pending(); }
                self.pending = idx;
                unsafe { SetTimer(self.hwnd, TIMER_HOLD, 400, None); }
                return;
            }
            Kind::Special => {
                if k.vk == VK_BACK as u32 && self.is_double_bksp(unsafe { GetTickCount() }) {
                    self.double_backspace();
                    if Self::is_repeatable(k.vk) {
                        self.repeat_idx = idx;
                        self.repeat_vk = k.vk;
                        self.repeat_first = true;
                        unsafe { SetTimer(self.hwnd, TIMER_REPEAT, 450, None); }
                    }
                    return;
                }
                if Self::is_repeatable(k.vk) {
                    self.repeat_idx = idx;
                    self.repeat_vk = k.vk;
                    self.repeat_first = true;
                    unsafe { SetTimer(self.hwnd, TIMER_REPEAT, 450, None); }
                }
                self.emit_vk(k.vk);
                return;
            }
        }
    }
}

use windows_sys::Win32::System::SystemInformation::GetTickCount;

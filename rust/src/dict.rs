use crate::app::*;

pub static WORDS_ID: &[u8] = include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"), "/../words_id.txt"));
pub static WORDS_EN: &[u8] = include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"), "/../words_en.txt"));
pub static BIGRAM_ID: &[u8] = include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"), "/../bigrams_id.bin"));
pub static BIGRAM_EN: &[u8] = include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"), "/../bigrams_en.bin"));

pub fn parse_dict(data: &[u8]) -> Vec<String> {
    let mut out = Vec::new();
    let mut pos = 0usize;
    while pos < data.len() && out.len() < 60000 {
        let mut end = pos;
        while end < data.len() && data[end] != b'\n' { end += 1; }
        let mut p = pos;
        let mut w = String::new();
        let mut ok = true;
        while p < end && data[p] != b' ' && data[p] != b'\t' && data[p] != b'\r' {
            let rest = &data[p..end];
            match std::str::from_utf8(rest) {
                Ok(st) => {
                    match st.chars().next() {
                        Some(c) => {
                            let c = if c.is_ascii_uppercase() {
                                ((c as u8) + 32) as char
                            } else { c };
                            if (c >= 'a' && c <= 'z') || c == '\'' {
                                if w.chars().count() < MAXWLEN {
                                    w.push(c);
                                    p += c.len_utf8();
                                } else { ok = false; break; }
                            } else { ok = false; break; }
                        }
                        None => { ok = false; break; }
                    }
                }
                Err(_) => { ok = false; break; }
            }
        }
        if ok && !w.is_empty() {
            out.push(w);
        }
        pos = end + 1;
    }
    out
}

pub fn dict_avail(a: &App, lang: usize) -> bool { !a.dict[lang].is_empty() }

pub fn load_bigram(data: &[u8]) -> Vec<BigramRec> {
    if data.is_empty() || data.len() % 4 != 0 { return Vec::new(); }
    let n = data.len() / 4;
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        let prev = u16::from_le_bytes([data[i * 4], data[i * 4 + 1]]);
        let next = u16::from_le_bytes([data[i * 4 + 2], data[i * 4 + 3]]);
        if prev >= 60000 || next >= 60000 {
            return Vec::new();
        }
        out.push(BigramRec { prev, next });
    }
    out
}

pub fn bigram_avail(a: &App, lang: usize) -> bool {
    !a.bigram[lang].is_empty() && dict_avail(a, lang)
}

pub fn bigram_next(a: &App, prev_idx: i32) -> Vec<u32> {
    let mut out = Vec::new();
    if prev_idx < 0 || !bigram_avail(a, a.lang) { return out; }
    let table = &a.bigram[a.lang];
    let mut lo = 0usize;
    let mut hi = table.len();
    while lo < hi {
        let mid = (lo + hi) / 2;
        if (table[mid].prev as i32) < prev_idx { lo = mid + 1; } else { hi = mid; }
    }
    let mut first = lo;
    while first < table.len() && table[first].prev == prev_idx as u16 && out.len() < 8 {
        let nx = table[first].next as u32;
        first += 1;
        if (nx as usize) < a.dict[a.lang].len() {
            out.push(nx);
        }
    }
    out
}

pub fn dict_exact(a: &App, w: &str) -> bool {
    a.dict[a.lang].iter().any(|d| d == w)
}

pub fn edit_dist_cap(a: &str, b: &str, cap: i32) -> i32 {
    let av: Vec<char> = a.chars().collect();
    let bv: Vec<char> = b.chars().collect();
    let n = av.len();
    let m = bv.len();
    if n > 32 || m > 32 { return cap + 1; }
    if (n as i32 - m as i32).abs() > cap { return cap + 1; }
    let mut d = [[0i32; 34]; 34];
    for i in 0..=n { d[i][0] = i as i32; }
    for j in 0..=m { d[0][j] = j as i32; }
    for i in 1..=n {
        let mut best = cap + 1;
        for j in 1..=m {
            let cost = if av[i - 1] == bv[j - 1] { 0 } else { 1 };
            let mut v = d[i - 1][j] + 1;
            let wv = d[i][j - 1] + 1;
            if wv < v { v = wv; }
            let sv = d[i - 1][j - 1] + cost;
            if sv < v { v = sv; }
            if i > 1 && j > 1 && av[i - 1] == bv[j - 2] && av[i - 2] == bv[j - 1] {
                let tv = d[i - 2][j - 2] + 1;
                if tv < v { v = tv; }
            }
            d[i][j] = v;
            if v < best { best = v; }
        }
        if best > cap { return cap + 1; }
    }
    d[n][m]
}

impl App {
    pub fn word_len(&self) -> usize { self.word_buf.len() }

    pub fn sugg_row_has_content(&self) -> bool {
        self.sugg.iter().any(|s| !s.is_empty())
            || (self.highlight_on && self.misspelt && self.word_len() > 0)
    }

    pub fn sugg_row_on(&self) -> bool {
        (self.predict_on || self.highlight_on) && dict_avail(self, self.lang)
            && self.sugg_row_has_content()
    }

    pub fn sync_sugg_row(&mut self) {
        let want = if self.sugg_row_on() { 1 } else { 0 };
        if want == self.sugg_row_on { return; }
        self.sugg_row_on = want;
        if !self.hwnd.is_null() && unsafe { IsWindow(self.hwnd) } != 0 {
            self.resize_for_scale();
        }
    }

    pub fn word_commit(&mut self) {
        if !self.word_buf.is_empty() {
            let low: String = self.word_buf.iter()
                .map(|&c| {
                    let ch = char::from_u32(c as u32).unwrap_or('?');
                    if ch.is_ascii_uppercase() { ch.to_ascii_lowercase() } else { ch }
                })
                .collect();
            self.prev_word = self.word_buf.clone();
            self.prev_idx = -1;
            for (i, d) in self.dict[self.lang].iter().enumerate() {
                if *d == low { self.prev_idx = i as i32; break; }
            }
        }
        self.word_buf.clear();
        self.compute_sugg();
    }

    pub fn word_char(&mut self, ch: u16) {
        let c = ch as u8 as char;
        if c.is_ascii_alphanumeric() || ch == '\'' as u16 {
            if self.word_buf.len() < 31 {
                self.word_buf.push(ch);
            }
            self.compute_sugg();
        } else {
            self.word_commit();
        }
    }

    pub fn word_chop(&mut self) {
        if !self.word_buf.is_empty() { self.word_buf.pop(); }
        self.compute_sugg();
    }

    pub fn word_clear(&mut self) {
        self.word_buf.clear();
        self.compute_sugg();
    }

    pub fn compute_sugg(&mut self) {
        let mut n = 0;
        let mut fresh = [String::new(), String::new(), String::new()];
        if !(self.predict_on || self.highlight_on) || !dict_avail(self, self.lang) {
            for s in self.sugg.iter_mut() { s.clear(); }
            self.misspelt = false;
            self.sync_sugg_row();
            return;
        }
        if self.word_len() == 0 {
            self.misspelt = false;
            if self.predict_on && self.bigram_on && self.prev_idx >= 0 {
                let nx = bigram_next(self, self.prev_idx);
                for idx in nx {
                    if n >= 3 { break; }
                    fresh[n] = self.dict[self.lang][idx as usize].clone();
                    n += 1;
                }
                if n > 0 {
                    self.sugg = fresh.clone();
                }
            }
            self.sync_sugg_row();
            return;
        }
        let pre: String = self.word_buf.iter()
            .take(31)
            .map(|&c| {
                let ch = char::from_u32(c as u32).unwrap_or('?');
                if ch.is_ascii_uppercase() { ch.to_ascii_lowercase() } else { ch }
            })
            .collect();
        if self.predict_on {
            for w in self.dict[self.lang].iter() {
                if n >= 3 { break; }
                if w.starts_with(&pre) {
                    fresh[n] = w.clone();
                    n += 1;
                }
            }
            if self.bigram_on && self.prev_idx >= 0 && n > 1 {
                let nx = bigram_next(self, self.prev_idx);
                let next_words: Vec<&str> = nx.iter()
                    .map(|&i| self.dict[self.lang][i as usize].as_str())
                    .collect();
                let mut tmp = [String::new(), String::new(), String::new()];
                let mut w = 0;
                for j in 0..n {
                    if next_words.contains(&fresh[j].as_str()) {
                        tmp[w] = fresh[j].clone();
                        w += 1;
                    }
                }
                for j in 0..n {
                    if !next_words.contains(&fresh[j].as_str()) {
                        tmp[w] = fresh[j].clone();
                        w += 1;
                    }
                }
                fresh = tmp;
            }
            if n > 0 {
                self.sugg = fresh;
                self.misspelt = false;
            } else if self.word_len() >= 2 {
                self.misspelt = true;
            }
        } else {
            let found = self.dict[self.lang].iter().any(|w| w.starts_with(&pre));
            self.misspelt = !found && self.word_len() >= 2;
        }
        self.sync_sugg_row();
    }

    pub fn inv_sugg(&mut self) {
        if !(self.predict_on || self.highlight_on) || !dict_avail(self, self.lang) { return; }
        let idxs: Vec<usize> = self.keys.iter().enumerate()
            .filter(|(_, k)| k.kind == Kind::Sugg)
            .map(|(i, _)| i)
            .collect();
        for i in idxs { self.inv_key(i as i32); }
    }

    pub fn is_commit_char(c: u16) -> bool {
        if c == '\'' as u16 || c == '_' as u16 { return false; }
        let ch = c as u8 as char;
        !(ch.is_ascii_alphanumeric())
    }

    pub fn auto_correct_now(&mut self, trail: bool) {
        if !self.autocorrect || !dict_avail(self, self.lang) { return; }
        let n = self.word_len();
        if n < 3 || n > 31 { return; }
        let mut low = String::new();
        for (i, &c) in self.word_buf.iter().enumerate() {
            let ch = char::from_u32(c as u32).unwrap_or('?');
            if ch.is_ascii_digit() { return; }
            if ch.is_ascii_uppercase() {
                if i > 0 { return; }
                low.push(ch.to_ascii_lowercase());
            } else {
                low.push(ch);
            }
        }
        if dict_exact(self, &low) { return; }
        let max_d = if n >= 6 { 2 } else { 1 };
        let mut best = 999i32;
        let mut bi: i32 = -1;
        for (i, w) in self.dict[self.lang].iter().enumerate() {
            let wl = w.chars().count() as i32;
            if (wl - n as i32).abs() > max_d { continue; }
            let dd = edit_dist_cap(&low, w, max_d);
            if dd < best {
                best = dd;
                bi = i as i32;
                if best <= 1 { break; }
            }
        }
        if self.bigram_on && self.prev_idx >= 0 {
            let nx = bigram_next(self, self.prev_idx);
            for &idx in &nx {
                let w = &self.dict[self.lang][idx as usize];
                let wl = w.chars().count() as i32;
                if (wl - n as i32).abs() > max_d { continue; }
                let dd = edit_dist_cap(&low, w, max_d);
                if dd < best { best = dd; bi = idx as i32; }
            }
        }
        if bi < 0 || best > max_d { return; }
        let mut out = self.dict[self.lang][bi as usize].clone();
        let first_typed = char::from_u32(self.word_buf[0] as u32).unwrap_or('?');
        if first_typed.is_ascii_uppercase() {
            let mut chars = out.chars();
            if let Some(c) = chars.next() {
                if c.is_ascii_lowercase() {
                    out = c.to_ascii_uppercase().to_string() + chars.as_str();
                }
            }
        }
        self.auto_orig = self.word_buf.clone();
        self.auto_corr_len = out.encode_utf16().count() as i32;
        for _ in 0..n {
            unsafe { crate::app::vk_tap(VK_BACK as u32); }
        }
        let out_u16: Vec<u16> = out.encode_utf16().collect();
        unsafe { crate::app::type_unicode(&out_u16); }
        for &c in &out_u16 {
            self.word_char(c);
        }
        self.auto_trail = trail;
        self.auto_active = true;
        self.inv_sugg();
    }
}

use windows_sys::Win32::UI::Input::KeyboardAndMouse::VK_BACK;
use windows_sys::Win32::UI::WindowsAndMessaging::IsWindow;

use crate::app::*;
use windows_sys::Win32::Foundation::*;
use windows_sys::Win32::System::Registry::*;
use windows_sys::Win32::UI::WindowsAndMessaging::*;

impl App {
    pub fn save_settings(&self) {
        unsafe {
            let mut k: HKEY = std::ptr::null_mut();
            if RegCreateKeyExW(HKEY_CURRENT_USER, reg_path().as_ptr(), 0, std::ptr::null_mut(),
                0, KEY_SET_VALUE, std::ptr::null_mut(), &mut k, std::ptr::null_mut()) != 0
            {
                return;
            }
            let set_dword = |name: &str, v: u32| {
                let n: Vec<u16> = name.encode_utf16().chain(std::iter::once(0)).collect();
                unsafe {
                    RegSetValueExW(k, n.as_ptr(), 0, REG_DWORD, &v as *const _ as *const u8, 4);
                }
            };
            set_dword("Scale", (self.scale * 100.0 + 0.5) as i32 as u32);
            set_dword("Opacity", self.opacity as u32);
            set_dword("Compact", self.compact as u32);
            set_dword("ShowFn", self.show_fn as u32);
            set_dword("Lang", self.lang as u32);
            set_dword("Predict", self.predict_on as u32);
            set_dword("Autocorrect", self.autocorrect as u32);
            set_dword("Highlight", self.highlight_on as u32);
            set_dword("Bigram", self.bigram_on as u32);
            set_dword("PinBar", self.pin_bar_on as u32);
            for i in 0..MAXSCUT {
                let m = format!("SC{}M", i);
                let v = format!("SC{}V", i);
                let n = format!("SC{}N", i);
                let s = format!("SC{}S", i);
                set_dword(&m, (self.sc[i].mods & 0xF) as u32);
                set_dword(&v, self.sc[i].vk);
                let name: Vec<u16> = wstr_to_string(&self.sc[i].name)
                    .encode_utf16()
                    .chain(std::iter::once(0))
                    .collect();
                let nb: Vec<u8> = name.iter().flat_map(|c| c.to_le_bytes()).collect();
                unsafe {
                    let nn: Vec<u16> = n.encode_utf16().chain(std::iter::once(0)).collect();
                    RegSetValueExW(k, nn.as_ptr(), 0, REG_SZ, nb.as_ptr(), nb.len() as u32);
                }
                set_dword(&s, self.sc[i].star as u32);
            }
            if !self.hwnd.is_null() && unsafe { IsWindow(self.hwnd) } != 0 {
                let mut wr: RECT = std::mem::zeroed();
                unsafe { GetWindowRect(self.hwnd, &mut wr); }
                set_dword("X", wr.left as u32);
                set_dword("Y", wr.top as u32);
            }
            RegCloseKey(k);
        }
    }

    pub fn load_settings(&mut self) {
        unsafe {
            let mut k: HKEY = std::ptr::null_mut();
            if RegOpenKeyExW(HKEY_CURRENT_USER, reg_path().as_ptr(), 0,
                KEY_QUERY_VALUE, &mut k) != 0
            {
                return;
            }
            let mut get_dword = |name: &str| -> Option<u32> {
                let n: Vec<u16> = name.encode_utf16().chain(std::iter::once(0)).collect();
                let mut v: u32 = 0;
                let mut sz: u32 = 4;
                let mut ty: u32 = 0;
                if RegQueryValueExW(k, n.as_ptr(), std::ptr::null_mut(), &mut ty,
                    &mut v as *mut _ as *mut u8, &mut sz) == 0
                    && ty == REG_DWORD
                {
                    Some(v)
                } else {
                    None
                }
            };
            if let Some(v) = get_dword("Scale") {
                self.scale = v as i32 as f32 / 100.0;
                if self.scale < 0.75 { self.scale = 0.75; }
                if self.scale > 1.5 { self.scale = 1.5; }
            }
            if let Some(v) = get_dword("Opacity") {
                self.opacity = v as i32;
                if self.opacity < 102 { self.opacity = 102; }
                if self.opacity > 255 { self.opacity = 255; }
            }
            if let Some(v) = get_dword("Compact") { self.compact = v != 0; }
            if let Some(v) = get_dword("ShowFn") { self.show_fn = v != 0; }
            if let Some(v) = get_dword("Lang") { self.lang = if v == 1 { 1 } else { 0 }; }
            if let Some(v) = get_dword("Predict") { self.predict_on = v != 0; }
            if let Some(v) = get_dword("Autocorrect") { self.autocorrect = v != 0; }
            if let Some(v) = get_dword("Highlight") { self.highlight_on = v != 0; }
            if let Some(v) = get_dword("Bigram") { self.bigram_on = v != 0; }
            if let Some(v) = get_dword("PinBar") { self.pin_bar_on = v != 0; }
            for i in 0..MAXSCUT {
                self.sc[i] = Shortcut::default();
                let m = format!("SC{}M", i);
                let v = format!("SC{}V", i);
                let n = format!("SC{}N", i);
                let s = format!("SC{}S", i);
                if let Some(mv) = get_dword(&m) { self.sc[i].mods = (mv & 0xF) as i32; }
                if let Some(vv) = get_dword(&v) {
                    if vv > 0 && vv <= 0xFE { self.sc[i].vk = vv; }
                }
                if self.sc[i].vk == 0 { continue; }
                // load name
                let nn: Vec<u16> = n.encode_utf16().chain(std::iter::once(0)).collect();
                let mut buf = [0u16; 16];
                let mut sz2 = 32u32;
                let mut ty = 0u32;
                if RegQueryValueExW(k, nn.as_ptr(), std::ptr::null_mut(), &mut ty,
                    buf.as_mut_ptr() as *mut u8, &mut sz2) == 0
                    && ty == REG_SZ && sz2 >= 2 && sz2 <= 32
                {
                    buf[9] = 0;
                    set_wstr(&mut self.sc[i].name, &wstr_to_string(&buf));
                } else {
                    let an = Self::auto_name(self.sc[i].mods, self.sc[i].vk);
                    set_wstr(&mut self.sc[i].name, &an);
                }
                self.sc[i].used = true;
                if let Some(sv) = get_dword(&s) {
                    if sv != 0 { self.sc[i].star = true; }
                }
            }
            let mut kept = 0;
            for q in 0..MAXSCUT {
                if self.sc[q].used && self.sc[q].vk != 0 && self.sc[q].star {
                    kept += 1;
                    if kept > 3 { self.sc[q].star = false; }
                }
            }
            if let Some(x) = get_dword("X") {
                self.pos_x = x as i32;
                if let Some(y) = get_dword("Y") {
                    self.pos_y = y as i32;
                    self.has_pos = true;
                }
            }
            RegCloseKey(k);
        }
    }
}

fn reg_path() -> Vec<u16> {
    "Software\\FloatKeys".encode_utf16().chain(std::iter::once(0)).collect()
}

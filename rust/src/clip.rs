use crate::app::*;
use windows_sys::Win32::Foundation::*;
use windows_sys::Win32::Graphics::Gdi::*;
use windows_sys::Win32::System::DataExchange::*;
use windows_sys::Win32::System::Memory::*;
use windows_sys::Win32::UI::WindowsAndMessaging::*;
const CF_UNICODETEXT: u32 = 13;
const CF_BITMAP: u32 = 2;
const CF_DIB: u32 = 8;

impl App {
    pub fn pin_count(&self) -> usize {
        let mut n = 0;
        while n < self.clip.len() && self.clip[n].pinned { n += 1; }
        n
    }

    pub fn clip_img_count(&self) -> usize {
        self.clip.iter().filter(|e| e.is_img).count()
    }

    pub fn clip_free(&mut self, i: usize) {
        if let Some(e) = self.clip.get_mut(i) {
            e.text = None;
            if !e.hbmp.is_null() {
                unsafe { DeleteObject(e.hbmp as _); }
                e.hbmp = std::ptr::null_mut();
            }
            e.is_img = false;
            e.iw = 0;
            e.ih = 0;
            e.pinned = false;
        }
    }

    pub fn clip_clear_all(&mut self) {
        for i in 0..self.clip.len() { self.clip_free(i); }
        self.clip.clear();
        self.clip_off = 0;
    }

    pub fn clip_clear_unpinned(&mut self) {
        self.clip.retain(|e| e.pinned);
        self.clip_off = 0;
    }

    pub fn clip_toggle_pin(&mut self, hist_idx: usize) {
        if hist_idx >= self.clip.len() { return; }
        let mut e = self.clip.remove(hist_idx);
        let pins = self.pin_count();
        e.pinned = !e.pinned;
        let at = pins.min(self.clip.len());
        self.clip.insert(at, e);
        if self.clip_off >= self.clip.len() { self.clip_off = 0; }
    }

    pub fn clip_drop_oldest_img(&mut self) {
        if let Some(i) = self.clip.iter().rposition(|e| e.is_img) {
            self.clip.remove(i);
        }
    }

    pub fn clip_prepend(&mut self, src: ClipEnt) -> bool {
        let pins = self.pin_count();
        for i in 0..self.clip.len() {
            if self.clip[i].is_img != src.is_img { continue; }
            if !src.is_img && self.clip[i].text.is_some() && src.text.is_some()
                && self.clip[i].text == src.text
            {
                if i <= pins { return false; }
                let e = self.clip.remove(i);
                let at = pins.min(self.clip.len());
                self.clip.insert(at, e);
                return true;
            }
        }
        if src.is_img && self.clip_img_count() >= MAXCLIPIMG {
            self.clip_drop_oldest_img();
        }
        let pins = self.pin_count();
        if self.clip.len() >= MAXCLIP {
            self.clip.pop();
        }
        self.clip.insert(pins.min(self.clip.len()), src);
        self.clip_off = 0;
        true
    }

    pub fn bitmap_to_dib(src: HBITMAP, max_dim: i32) -> Option<(HBITMAP, i32, i32)> {
        unsafe {
            if src.is_null() { return None; }
            let mut bm: BITMAP = std::mem::zeroed();
            if GetObjectW(src, std::mem::size_of::<BITMAP>() as i32, &mut bm as *mut _ as _) == 0 {
                return None;
            }
            let (mut w, mut h) = (bm.bmWidth, bm.bmHeight);
            if w <= 0 || h <= 0 { return None; }
            if w > max_dim || h > max_dim {
                if w >= h { h = h * max_dim / w; w = max_dim; }
                else { w = w * max_dim / h; h = max_dim; }
                if w < 1 { w = 1; }
                if h < 1 { h = 1; }
            }
            let mut bi: BITMAPINFO = std::mem::zeroed();
            bi.bmiHeader.biSize = std::mem::size_of::<BITMAPINFOHEADER>() as u32;
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            let sdc = CreateCompatibleDC(std::ptr::null_mut());
            let ddc = CreateCompatibleDC(std::ptr::null_mut());
            let mut dib: HBITMAP = std::ptr::null_mut();
            if !sdc.is_null() && !ddc.is_null() {
                let mut bits: *mut core::ffi::c_void = std::ptr::null_mut();
                dib = CreateDIBSection(ddc, &bi, DIB_RGB_COLORS, &mut bits, std::ptr::null_mut(), 0);
                if !dib.is_null() {
                    let so = SelectObject(sdc, src as _);
                    let dob = SelectObject(ddc, dib as _);
                    SetStretchBltMode(ddc, HALFTONE);
                    SetBrushOrgEx(ddc, 0, 0, std::ptr::null_mut());
                    StretchBlt(ddc, 0, 0, w, h, sdc, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                    SelectObject(sdc, so);
                    SelectObject(ddc, dob);
                }
            }
            if !sdc.is_null() { DeleteDC(sdc); }
            if !ddc.is_null() { DeleteDC(ddc); }
            if dib.is_null() { None } else { Some((dib, w, h)) }
        }
    }

    pub fn bitmap_to_dib_mem(src: HBITMAP) -> HGLOBAL {
        unsafe {
            if src.is_null() { return std::ptr::null_mut(); }
            let mut bm: BITMAP = std::mem::zeroed();
            if GetObjectW(src, std::mem::size_of::<BITMAP>() as i32, &mut bm as *mut _ as _) == 0 {
                return std::ptr::null_mut();
            }
            let row = ((bm.bmWidth * 32 + 31) / 32) * 4;
            let img = row * bm.bmHeight;
            let size = std::mem::size_of::<BITMAPINFOHEADER>() + img as usize;
            let hmem = GlobalAlloc(GMEM_MOVEABLE, size);
            if hmem.is_null() { return std::ptr::null_mut(); }
            let ph = GlobalLock(hmem) as *mut BITMAPINFOHEADER;
            if ph.is_null() {
                GlobalFree(hmem);
                return std::ptr::null_mut();
            }
            std::ptr::write_bytes(ph as *mut u8, 0, size);
            (*ph).biSize = std::mem::size_of::<BITMAPINFOHEADER>() as u32;
            (*ph).biWidth = bm.bmWidth;
            (*ph).biHeight = bm.bmHeight;
            (*ph).biPlanes = 1;
            (*ph).biBitCount = 32;
            (*ph).biCompression = BI_RGB;
            (*ph).biSizeImage = img as u32;
            let dc = CreateCompatibleDC(std::ptr::null_mut());
            GetDIBits(dc, src, 0, bm.bmHeight as u32,
                (ph as *mut u8).add(std::mem::size_of::<BITMAPINFOHEADER>()) as _,
                ph as *mut _ as _, DIB_RGB_COLORS);
            DeleteDC(dc);
            GlobalUnlock(hmem);
            hmem
        }
    }

    pub fn on_clipboard_update(&mut self) {
        if self.own_clip { return; }
        unsafe {
            if OpenClipboard(self.hwnd) == 0 { return; }
            if IsClipboardFormatAvailable(CF_UNICODETEXT) != 0 {
                let d = GetClipboardData(CF_UNICODETEXT);
                if !d.is_null() {
                    let s = GlobalLock(d) as *const u16;
                    if !s.is_null() {
                        let len = (0..4000).take_while(|&i| *s.add(i) != 0).count();
                        if len > 0 {
                            let mut text = vec![0u16; len + 1];
                            std::ptr::copy_nonoverlapping(s, text.as_mut_ptr(), len);
                            let ent = ClipEnt {
                                is_img: false,
                                text: Some(text),
                                hbmp: std::ptr::null_mut(),
                                iw: 0, ih: 0, pinned: false,
                            };
                            let changed = self.clip_prepend(ent);
                            if changed && self.view_clip {
                                InvalidateRect(self.hwnd, std::ptr::null(), FALSE);
                            }
                        }
                        GlobalUnlock(d);
                    }
                }
            } else {
                let d = GetClipboardData(CF_BITMAP);
                let mut src: HBITMAP = std::ptr::null_mut();
                if !d.is_null() { src = d as HBITMAP; }
                let mut dib: HBITMAP = std::ptr::null_mut();
                let mut w = 0;
                let mut h = 0;
                if !src.is_null() {
                    if let Some((b, bw, bh)) = Self::bitmap_to_dib(src, 1280) {
                        dib = b; w = bw; h = bh;
                    }
                }
                if dib.is_null() {
                    let dd = GetClipboardData(CF_DIB);
                    if !dd.is_null() {
                        let ph = GlobalLock(dd) as *const BITMAPINFOHEADER;
                        if !ph.is_null() {
                            if (*ph).biSize >= std::mem::size_of::<BITMAPINFOHEADER>() as u32
                                && (*ph).biWidth > 0 && (*ph).biHeight != 0
                            {
                                let cdc = CreateCompatibleDC(std::ptr::null_mut());
                                let mut bits: *mut core::ffi::c_void = std::ptr::null_mut();
                                let mut bi: BITMAPINFO = std::mem::zeroed();
                                let tw = (*ph).biWidth;
                                let th = if (*ph).biHeight < 0 { -(*ph).biHeight } else { (*ph).biHeight };
                                bi.bmiHeader.biSize = std::mem::size_of::<BITMAPINFOHEADER>() as u32;
                                bi.bmiHeader.biWidth = tw;
                                bi.bmiHeader.biHeight = -th;
                                bi.bmiHeader.biPlanes = 1;
                                bi.bmiHeader.biBitCount = 32;
                                bi.bmiHeader.biCompression = BI_RGB;
                                dib = CreateDIBSection(cdc, &bi, DIB_RGB_COLORS, &mut bits, std::ptr::null_mut(), 0);
                                if !dib.is_null() && !bits.is_null() {
                                    let src_bits = (ph as *const u8).add((*ph).biSize as usize)
                                        .add(((*ph).biClrUsed * 4) as usize);
                                    StretchDIBits(cdc, 0, 0, tw, th, 0, 0, tw, th,
                                        src_bits as *const _, &bi, DIB_RGB_COLORS, SRCCOPY);
                                    w = tw; h = th;
                                } else if !dib.is_null() {
                                    DeleteObject(dib as _);
                                    dib = std::ptr::null_mut();
                                }
                                DeleteDC(cdc);
                            }
                            GlobalUnlock(dd);
                        }
                    }
                }
                if !dib.is_null() {
                    let mut sw = w;
                    let mut sh = h;
                    if w > 1280 || h > 1280 {
                        if let Some((small, bw, bh)) = Self::bitmap_to_dib(dib, 1280) {
                            DeleteObject(dib as _);
                            dib = small;
                            sw = bw; sh = bh;
                        }
                    }
                    if !dib.is_null() {
                        let ent = ClipEnt {
                            is_img: true,
                            text: None,
                            hbmp: dib,
                            iw: sw, ih: sh, pinned: false,
                        };
                        let changed = self.clip_prepend(ent);
                        if changed && self.view_clip {
                            InvalidateRect(self.hwnd, std::ptr::null(), FALSE);
                        } else if !changed {
                            DeleteObject(dib as _);
                        }
                    }
                }
            }
            CloseClipboard();
        }
    }

    pub fn paste_clip(&mut self, hist_idx: usize) {
        if hist_idx >= self.clip.len() { return; }
        self.own_clip = true;
        unsafe {
            if OpenClipboard(self.hwnd) != 0 {
                EmptyClipboard();
                let e = &self.clip[hist_idx];
                if e.is_img && !e.hbmp.is_null() {
                    let hmem = Self::bitmap_to_dib_mem(e.hbmp);
                    if !hmem.is_null() {
                        SetClipboardData(CF_DIB, hmem);
                    }
                } else if let Some(ref text) = e.text {
                    let n = text.len() * 2;
                    let hmem = GlobalAlloc(GMEM_MOVEABLE, n);
                    if !hmem.is_null() {
                        let p = GlobalLock(hmem);
                        if !p.is_null() {
                            std::ptr::copy_nonoverlapping(text.as_ptr(), p as *mut u16, text.len());
                            GlobalUnlock(hmem);
                        }
                        SetClipboardData(CF_UNICODETEXT, hmem);
                    }
                }
                CloseClipboard();
            }
        }
        self.own_clip = false;
        self.run_macro(0x2, 0x56);
    }

    pub fn clip_path() -> Option<std::path::PathBuf> {
        let local = std::env::var_os("LOCALAPPDATA")?;
        let dir = std::path::PathBuf::from(local).join("FloatKeys");
        let _ = std::fs::create_dir_all(&dir);
        Some(dir.join("clip.dat"))
    }

    pub fn save_clip(&self) {
        let Some(path) = Self::clip_path() else { return; };
        let mut buf: Vec<u8> = Vec::new();
        let mut n = 0u32;
        for e in &self.clip {
            if !e.is_img && e.text.is_some() { n += 1; }
        }
        buf.extend_from_slice(&CLIP_MAGIC.to_le_bytes());
        buf.extend_from_slice(&CLIP_VER.to_le_bytes());
        buf.extend_from_slice(&n.to_le_bytes());
        for e in &self.clip {
            if e.is_img { continue; }
            let Some(ref t) = e.text else { continue; };
            let mut cch = (t.iter().position(|&c| c == 0).unwrap_or(t.len())) as u32;
            if cch > 4000 { cch = 4000; }
            let pin = if e.pinned { 1u32 } else { 0 };
            buf.extend_from_slice(&pin.to_le_bytes());
            buf.extend_from_slice(&cch.to_le_bytes());
            for &ch in t.iter().take(cch as usize) {
                buf.extend_from_slice(&ch.to_le_bytes());
            }
        }
        let _ = std::fs::write(path, buf);
    }

    pub fn load_clip(&mut self) {
        let Some(path) = Self::clip_path() else { return; };
        let Ok(data) = std::fs::read(&path) else { return; };
        if data.len() < 12 || data.len() > 4 * 1024 * 1024 { return; }
        let magic = u32::from_le_bytes([data[0], data[1], data[2], data[3]]);
        let ver = u32::from_le_bytes([data[4], data[5], data[6], data[7]]);
        let cnt = u32::from_le_bytes([data[8], data[9], data[10], data[11]]);
        if magic != CLIP_MAGIC || ver != CLIP_VER || cnt as usize > MAXCLIP { return; }
        let mut pos = 12;
        let mut seen_unpinned = false;
        for _ in 0..cnt {
            if self.clip.len() >= MAXCLIP { break; }
            if pos + 8 > data.len() { break; }
            let pin = u32::from_le_bytes(data[pos..pos + 4].try_into().unwrap());
            let cch = u32::from_le_bytes(data[pos + 4..pos + 8].try_into().unwrap());
            pos += 8;
            if cch == 0 || cch > 4000 { break; }
            let byte_len = cch as usize * 2;
            if pos + byte_len > data.len() { break; }
            let mut text = vec![0u16; cch as usize + 1];
            for i in 0..cch as usize {
                text[i] = u16::from_le_bytes([data[pos + i * 2], data[pos + i * 2 + 1]]);
            }
            pos += byte_len;
            let pinned = pin != 0 && !seen_unpinned;
            if pin == 0 { seen_unpinned = true; }
            self.clip.push(ClipEnt {
                is_img: false,
                text: Some(text),
                hbmp: std::ptr::null_mut(),
                iw: 0, ih: 0,
                pinned,
            });
        }
        self.clip_off = 0;
    }
}

# FloatKeys — Floating Virtual Keyboard for Windows

## Word prediction (ID/EN)

A suggestion strip above the keys offers the top-3 completions from a
50,000-word frequency dictionary (Indonesian + English embedded in the
`.exe`, ~1.4MB total — still single-file, no downloads needed). Tap a
suggestion to complete the word + space. `⚙` panel: `ID`/`EN` switches
language, `Pred` toggles predictions. It learns from what you type through
FloatKeys (current word tracking, Backspace-aware). The strip is sticky:
once shown it keeps the last suggestions instead of flashing empty boxes.
`Pred` off = Spell-only mode (no completions, just the red literal).

## Bigram next-word (n-gram)

Di atas kamus unigram, ada model **bigram**: tepat setelah spasi,
strip menawarkan 3 kata lanjutan paling mungkin dari kata barusan
(mis. `saya → tidak/akan/ingin`, `i → have/am/was`), dan saat mengetik
awalan, lanjutan yang cocok konteks naik ke depan. Tabel top-8
lanjutan per kata (EN 60rb pasangan/14,6rb konteks dari frekuensi
Google Books/Norvig; ID 90rb pasangan/16,5rb konteks dari subtitle
TED/Tatoeba/GlobalVoices, berita News-Commentary, Tanzil, dan pelajaran
QED via OPUS) ikut embedded di `.exe` (~600KB).
Panel `⚙`: `Bi` menyalakan/mematikan. Autocorrect juga memilih kandidat
yang nyambung dengan kata sebelumnya bila jaraknya sama baiknya.

## Pin jendela (PinTop built-in)

Tombol `Pin` di panel `⚙` membuat jendela yang sedang diketik (browser,
notepad, ...) selalu di atas — inti cara kerja PinTop, tanpa aplikasi
tambahan. Biru saat jendela aktif sedang di-pin (maks 8 jendela, timer
1 detik menegaskan ulang + membuang yang sudah ditutup, semua dilepas
saat FloatKeys ditutup). Taskbar/desktop tidak bisa di-pin.

## Mengetik di browser / shortcut YouTube

Huruf, angka, simbol, dan spasi dikirim sebagai **penekanan tombol
asli** (bukan UNICODE), sehingga aplikasi web melihat `keydown` dengan
kode tombol yang benar: `f` fullscreen, `k`/`spasi` play-pause, `m`
mute, `j`/`l` mundur/maju di YouTube — panah memang sudah jalan karena
dari dulu sudah tombol asli. (Layout US.)

## Autocorrect + highlight kata salah ketik

Seperti opsi *Autocorrect misspelt words* / *Highlight misspelt words* di
Settings Windows: saat sebuah kata diakhiri dengan spasi/tanda baca/Enter,
kata yang tidak dikenal dan dekat dengan kata kamus (jarak edit kecil,
mis. `teh` → `the`, huruf kapital awal dipertahankan) otomatis diganti.
Satu `Backspace` tepat setelahnya membatalkan koreksi (kembali ke ketikan
asli, gaya HP). Kata yang tidak cocok awalan kamus mana pun tampil
**merah** di slot saran pertama — ketuk untuk mempertahankan ketikanmu.
Panel `⚙`: `Auto` dan `Spell` menyalakan/mematikan keduanya (tersimpan di
registry). Bekerja untuk ketikan lewat FloatKeys, ID maupun EN.

## Custom shortcuts (tombol `SC`)

Halaman sendiri seperti Clip, berisi 10 slot. Ketuk slot bernama untuk
menembakkan kombinasinya sekaligus — tanpa menekan Ctrl/Shift/Alt/Win
satu per satu. Ketuk `+` untuk merekam: tekan modifier + tombolnya di
keyboard (tidak ada yang terkirim selama merekam), lalu ketik nama
pakai keyboard (kosongkan untuk nama otomatis seperti `C+S`), `Enter`
menyimpan, `Esc`/`SC` membatalkan. `Del` + ketuk slot menghapusnya.
Semua tersimpan permanen di registry (`HKCU\Software\FloatKeys`).

## Pin shortcut ke strip sendiri (ikon `★`)

Di halaman SC, nyalakan `★`, lalu ketuk slot untuk menyematkannya —
muncul sebagai **strip tombol tersendiri** (rapat, di bawah strip
prediksi), satu ketuk langsung jalan tanpa membuka halaman SC.
Maksimal 3 (yang ke-4 diabaikan); ketuk lagi untuk melepas. Slot yang
di-pin berlabel `★nama`. Tombol `★` di panel `⚙` menyembunyikan /
menampilkan strip ini (tersimpan di registry).

## Clipboard history (text + images)

`Clip` button opens the clipboard page: the last 25 copies (max 8 images,
auto-downscaled) with thumbnails on a light frame (so dark screenshots
stay visible). It records everything you copy —
including screenshots/images (e.g. Snipping Tool `Win+Shift+S`, copy-image
in browser). Tap an entry to paste it into the focused app. `▲`/`▼`
scroll, `Clear` empties everything except pins, `Keys` returns to the
keyboard. Teks (+pin) tersimpan permanen di
`%LOCALAPPDATA%\FloatKeys\clip.dat` dan dimuat lagi saat dibuka;
gambar hanya sesi berjalan.

## Pinned clipboard items

Tap `Pin` in the clip nav row (it turns blue), then tap any entry to
pin/unpin it — pinned items get a `PIN` tag, always stay on top, survive
`Clear`, and are never auto-evicted. Tap `Pin` again (or leave with
`Keys`) to go back to paste mode.

## Settings are remembered

Size, opacity, layout (Full/Compact), function row and window position are
saved to `HKCU\Software\FloatKeys` on every change and on exit — they
survive restarts and Windows shutdown. Delete that registry key to reset.

Like the Windows touch keyboard, but **with** the missing keys:
`Shift`, `Ctrl`, `Alt`, `Win`, `Tab`, `CapsLock`, `Esc`, `F1–F12`, arrows, `PgUp`/`PgDn`, `Del`, `PrtSc`, menu key.

Two versions, same behavior:

| | `keyboard.c` → `FloatKeys.exe` (use this) | `floating_keyboard.py` (prototype) |
|---|---|---|
| Size | ~200KB single `.exe` | needs Python |
| RAM | ~10MB | ~40-60MB |
| Runtime | none (built into Windows) | Python 3.10+ |
| Windows | 1 window, owner-drawn GDI | tkinter |

## Run (native, recommended)

Just double-click **`FloatKeys.exe`** — no install, no runtime, no admin.

## Rebuild from source

You only need **Zig** (~50MB, no admin):

```bat
winget install -e --id zig.zig
build-zig.bat
```

Or with MSVC (VS Build Tools, C++ workload, from a Developer Prompt):

```bat
build-msvc.bat
```

## Layouts: Full + Compact

- **Full** (default, ~720px): complete PC layout — numbers, QWERTY, function row, arrows.
- **Compact** (~460px, phone-width): 4-row layout with phone-style icon labels
  (`⌫ ⇥ ⏎ ⇧ ⇪`) — letters with small digit hints on top (`Q¹ W² ...`),
  ONE merged `⇪` key: tap once = Shift for next key, tap twice quickly =
  CapsLock on/off. Spare slots hold `Del` (ABC page) and `☰` menu (`&123`). `&123` key switches
  to the numbers/symbols page, `ABC` switches back. Modifiers
  (`Ctrl Win Alt Caps Shift`), `Tab Esc Enter Back Del`, arrows and `Space`
  are all still there, just denser.

## Why this works as a real keyboard

Normal windows steal focus when clicked, so keystrokes would go to the
keyboard itself. This app uses `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` plus
`MA_NOACTIVATE`, so clicks **never activate it** — focus stays in Notepad,
your browser, game, etc. Keys are injected system-wide with `SendInput`.
`WS_EX_LAYERED` gives opacity, `WS_EX_TOPMOST` keeps it floating.

## Modifier behavior (important)

Tap once = **hold for next key only** (light blue).
Tap twice = **lock down** (dark blue). Tap again = release.

Real shortcuts with mouse/touch only:

- `Ctrl` then `C` = Copy, `Ctrl` then `V` = Paste
- `Win` then `L` = Lock PC, `Win` then `E` = Explorer
- `Alt` then `Tab` = switch window (lock `Alt`, tap `Tab`)
- `Shift` + arrows = select text
- `Ctrl` + `Shift` + `Esc` = Task Manager (lock both, tap `Esc`)

`CapsLock` is a real toggle with a live indicator (polls `GetKeyState`).

## Window controls (top bar)

- Drag the empty bar to move (custom drag, never activates)
- `⚙` (left) = settings panel: `A+` / `A-` size, `◐` / `◑` opacity,
  `ID`/`EN` language, `Pred` suggestions, `Bi` bigram next-word,
  `Auto` autocorrect, `Spell` misspelt highlight,
  `Pin` pin window on top, `★` show/hide pinned-shortcut strip, `Done`
- `★` = one-tap shortcut panel: Copy Paste Cut Undo Save All AltTab Shot Lock
- `SC` = custom shortcuts page: 10 user-recorded combos (tap `+` to record)
- `Fn` = show/hide `Esc F1–F12` (Full mode only), `Compact`/`Full` = switch layouts
- `✕` = close (far right). Closing releases held/locked modifiers.
- Hold `Bksp`, `Del`, `Space`, or arrows to auto-repeat. Double-tap
  `Bksp` (within 0.4s) deletes the whole previous word.

## Hold a letter for numbers/symbols

Hold `Q` (or any letter/symbol key) ~0.4s and a `[q|1]` picker pops up —
slide onto `1` and release. No need to open the `&123` page. Quick taps
type normally, so typing rhythm is unchanged.

## Files

- `keyboard.c` — the whole native app (single file, C99 + Win32 only)
- `keyboard.rc` + `app.ico` (`gen_icon.py` regenerates it) — exe icon, all 7 sizes
- `FloatKeys.exe` — built binary, just run it
- `build-zig.bat` / `build-msvc.bat` — one-line builds
- `floating_keyboard.py` — older Python prototype (same layout/logic)
- `start_keyboard.bat` — launcher for the Python version

## Notes

- US layout. Typing uses `KEYEVENTF_UNICODE` (layout-independent);
  `Ctrl`/`Alt`/`Win` combos use virtual-key codes (`VK_*` + `VK_OEM_*`).
- Sticky-modifier state is tracked in-app; `WM_DESTROY` releases everything.

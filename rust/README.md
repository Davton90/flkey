# FloatKeyrusts — Rust port of FloatKeys

Port 1:1 dari `keyboard.c` (Win32 C, ~3200 baris) ke Rust + `windows-sys`. Output: **`FloatKeyrusts.exe`**.

## Build (cepat)

```bat
build-rust.bat
```

Profil release sudah dioptimalkan untuk compile cepat:
- `lto = false` (link jauh lebih cepat)
- `opt-level = 1`
- `codegen-units = 16` (parallel codegen)
- `strip = true`, `debug = false`

Untuk binary final yang lebih dioptimalkan runtime (build lebih lambat), edit `Cargo.toml`:

```toml
opt-level = 3
lto = true          # atau "thin"
codegen-units = 1
```

## Struktur source (modular — edit satu file = recompile cepat)

```
rust/
  Cargo.toml
  build.rs            # embed app.ico → resource ID 1
  .cargo/config.toml  # parallel jobs
  src/
    main.rs           # entry, WndProc, focus restore, WinEvent hook
    app.rs            # struct App, types, SendInput helpers, constants
    layout.rs         # BuildKeys (Full/Compact), ComputeLayout, paint GDI
    state.rs          # emit keys, modifiers, recorder, pin window, settings actions
    dict.rs           # word prediction, bigram, autocorrect, word tracker
    clip.rs           # clipboard history (text + image) + persistence
    settings.rs       # registry HKCU\Software\FloatKeys
```

Data (`words_*.txt`, `bigrams_*.bin`) di-embed via `include_bytes!` — single-file exe, sama seperti versi C (RCDATA).

## Fitur (identik dengan FloatKeys.exe)

- Sticky modifiers (tap=hold, double=lock)
- Word prediction ID/EN + bigram next-word
- Autocorrect + highlight misspelt
- Clipboard history (text + image, pin, persist)
- Custom shortcuts (SC) + pin strip (★)
- Pin window on top (PinTop)
- Full / Compact layout, hold-key picker
- Settings di registry, posisi jendela diingat

## Catatan teknis

- `WS_EX_NOACTIVATE` + `MA_NOACTIVATE` — klik tidak curi fokus
- `SendInput` untuk key injection (VK untuk shortcut, UNICODE untuk simbol)
- Focus restore (`EnsureFocus` / WinEvent hook) — port dari `keyboard.c`
- Class name tetap `FloatKeys` (kompatibel dgn logika shell-chrome check)

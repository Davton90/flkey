/* FloatKeys - floating virtual keyboard (native Win32 C, no runtime)
 *
 * Like the Windows touch keyboard, but WITH Shift/Ctrl/Alt/Win/Tab/Caps/Esc/F-keys.
 * Single HWND, owner-drawn with GDI. ~5MB RAM, ~100KB exe, no dependencies.
 *
 * Build with Zig (recommended, ~50MB toolchain, no admin):
 *   zig cc -O2 -mwindows -o FloatKeys.exe keyboard.c -luser32 -lgdi32 -lshcore
 * Build with MSVC:
 *   cl /O2 keyboard.c user32.lib gdi32.lib shcore.lib /link /SUBSYSTEM:WINDOWS
 */
#include <windows.h>
#include <windowsx.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wininet.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "wininet.lib")
#endif

/* ---------------- SendInput helpers ---------------- */
#define EXTKEY(k) ((k)==VK_LEFT||(k)==VK_UP||(k)==VK_RIGHT||(k)==VK_DOWN|| \
                   (k)==VK_LWIN||(k)==VK_APPS||(k)==VK_DELETE||(k)==VK_SNAPSHOT|| \
                   (k)==VK_HOME||(k)==VK_END||(k)==VK_PRIOR||(k)==VK_NEXT|| \
                   (k)==VK_INSERT||(k)==VK_MEDIA_PLAY_PAUSE|| \
                   (k)==VK_MEDIA_PREV_TRACK||(k)==VK_MEDIA_NEXT_TRACK|| \
                   (k)==VK_VOLUME_MUTE||(k)==VK_VOLUME_DOWN||(k)==VK_VOLUME_UP)

static void vk_down(UINT vk) {
    INPUT in_ = {0};
    in_.type = INPUT_KEYBOARD;
    in_.ki.wVk = (WORD)vk;
    if (EXTKEY(vk)) in_.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &in_, sizeof(INPUT));
}
static void vk_up(UINT vk) {
    INPUT in_ = {0};
    in_.type = INPUT_KEYBOARD;
    in_.ki.wVk = (WORD)vk;
    in_.ki.dwFlags = KEYEVENTF_KEYUP | (EXTKEY(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in_, sizeof(INPUT));
}
static void vk_tap(UINT vk) { vk_down(vk); vk_up(vk); }

static void type_unicode(const wchar_t *s) {
    for (; *s; s++) {
        INPUT dn = {0}, up = {0};
        dn.type = INPUT_KEYBOARD; up.type = INPUT_KEYBOARD;
        dn.ki.wScan = *s; dn.ki.dwFlags = KEYEVENTF_UNICODE;
        up.ki.wScan = *s; up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(1, &dn, sizeof(INPUT));
        SendInput(1, &up, sizeof(INPUT));
    }
}

static UINT char_to_vk(wchar_t lo) {
    if ((lo >= L'a' && lo <= L'z') || (lo >= L'A' && lo <= L'Z'))
        return (UINT)towupper(lo);
    if (lo >= L'0' && lo <= L'9') return (UINT)lo;
    switch (lo) {
        case L';': return VK_OEM_1;    /* ;: */
        case L'=': return VK_OEM_PLUS; /* =+ */
        case L',': return VK_OEM_COMMA;
        case L'-': return VK_OEM_MINUS;
        case L'.': return VK_OEM_PERIOD;
        case L'/': return VK_OEM_2;    /* /? */
        case L'`': return VK_OEM_3;
        case L'[': return VK_OEM_4;
        case L'\\': return VK_OEM_5;
        case L']': return VK_OEM_6;
        case L'\'': return VK_OEM_7;
        default: return 0;
    }
}

/* ---------------- model ---------------- */
typedef enum { K_CHAR, K_SPECIAL, K_MOD, K_CAPS, K_SPACE, K_PAGE, K_MACRO, K_SET,
                 K_SUGG, K_CLIP, K_CNAV, K_SHCAP, K_SCUT, K_INFO, K_CALC } Kind;
/* clip nav actions */
enum { CNAV_UP=1, CNAV_DOWN, CNAV_CLEAR, CNAV_BACK, CNAV_PIN,
       CNAV_SDEL, CNAV_SBACK, CNAV_SSTAR,
       CNAV_TRPASTE, CNAV_TRCLEAR, CNAV_TRGO, CNAV_TRBACK, CNAV_TRLANG,
       CNAV_FIND };
enum { M_OFF = 0, M_HELD = 1, M_LOCKED = 2 };
enum { MX_SHIFT = 0, MX_CTRL = 1, MX_ALT = 2, MX_WIN = 3 };

typedef struct {
    wchar_t text[10];
    wchar_t sub[4];      /* small shifted hint for symbol keys */
    wchar_t tag;         /* small hint above label (compact dual-label keys) */
    Kind kind;
    UINT vk;             /* SPECIAL vk */
    int mod;             /* MOD_* for K_MOD */
    wchar_t lo, hi;      /* K_CHAR pair */
    float w;
    int row;             /* 0=fn 1=num 2=top 3=home 4=shift 5=bottom */
    RECT rc;
} Key;

#define MAXKEYS 192
static Key g_keys[MAXKEYS];
static int g_nkeys = 0;

static HINSTANCE g_hInst;
static HWND g_hwnd;
static float g_scale = 1.0f;
static int g_opacity = 245;             /* 0..255 */
static BOOL g_showFn = TRUE;
static int g_compact = 0;               /* 0=full PC layout, 1=compact phone-style */
static int g_page = 0;                  /* compact page: 0=ABC, 1=&123 symbols */
static int g_showSettings = 0;          /* settings panel row under the bar */
static int g_showMacros = 0;            /* one-tap shortcut panel row */
static int g_posX = 0, g_posY = 0, g_hasPos = 0; /* remembered window position */
/* press-and-hold popup picker (hold Q -> choose 1) */
static int g_pending = -1;              /* K_CHAR waiting for UP (tap) or HOLD (popup) */
static int g_holdPopup = -1;            /* key index with popup currently shown */
static int g_shcapArmed = 0;            /* merged Shift/Caps awaiting 2nd tap */
static wchar_t g_popBase = 0, g_popAlt = 0;
static RECT g_popRc[2], g_popAll;
static int g_popHover = -1;
/* word prediction */
#define MAXDICT 60000
#define MAXWLEN 31
typedef struct { wchar_t w[MAXWLEN+1]; } DictWord;
static DictWord *g_dict[2] = { NULL, NULL }; /* 0=id, 1=en */
static int g_ndict[2] = { 0, 0 };
static int g_lang = 0;                  /* 0=id, 1=en */
static int g_predictOn = 1;
static int g_autocorrect = 1;         /* auto-replace misspelt word on commit */
static int g_highlightOn = 1;         /* show misspelt word in red on strip */
static int g_misspelt = 0;            /* current word has no dict prefix match */
static int g_autoActive = 0;          /* last action was autocorrect: Bksp undoes */
static int g_autoTrail = 0;           /* autocorrect was followed by space/punct */
static int g_autoCorrLen = 0;         /* length of the inserted correction */
static wchar_t g_autoOrig[32];        /* original typing before correction */
static wchar_t g_wordBuf[32];           /* current word prefix typed via this keyboard */
static int g_wordLen = 0;
static wchar_t g_sugg[3][MAXWLEN+1];    /* current top-3 suggestions */
/* bigram next-word model: LE (prev,next) dict-index records, rank order */
typedef struct { unsigned short prev, next; } BigramRec;
static BigramRec *g_bigram[2] = { NULL, NULL };
static int g_nbigram[2] = { 0, 0 };
static int g_bigramOn = 1;
static wchar_t g_prevWord[32];          /* last committed word (context) */
static int g_prevIdx = -1;              /* its dict index in current lang */
/* clipboard history */
#define MAXCLIP 25
#define MAXCLIPIMG 8
typedef struct { int isImg; wchar_t *text; HBITMAP hbmp; int iw, ih; int pinned; } ClipEnt;
static ClipEnt g_clip[MAXCLIP];
static int g_nclip = 0, g_clipOff = 0, g_viewClip = 0, g_ownClip = 0;
static int g_pinArm = 0;   /* Pin mode: tapping a clip entry pins/unpins it */
static int g_findArm = 0;  /* Find typing mode on the keys view */
static wchar_t g_findBuf[32];   /* active filter; empty = show all */
static int g_mods[4] = { M_OFF, M_OFF, M_OFF, M_OFF };
static int g_capsLast = -1;
static const UINT MODVK[4] = { VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN };
static int g_pressed = -1;
static int g_downIdx = -2;              /* key or bar btn held */
static BOOL g_dragging = FALSE;
static POINT g_dragOff;
/* repeat */
static int g_repeatIdx = -1;
static UINT g_repeatVk = 0;
static BOOL g_repeatFirst = FALSE;
/* focus restore: remember last typing target so the first tap after
 * opening the keyboard lands in the browser/search box instead of void.
 * Opening FloatKeys via Explorer/Start/taskbar moves foreground away from
 * the app the user just clicked; WS_EX_NOACTIVATE then preserves that
 * WRONG focus. We track the real target and restore it before SendInput. */
static HWND g_lastFg = NULL;
static HWND g_startupFg = NULL;
static HWINEVENTHOOK g_fgHook = NULL;
/* fonts */
static HFONT g_fKey = NULL, g_fSmall = NULL, g_fBar = NULL;

#define TIMER_CAPS 1
#define TIMER_REPEAT 2
#define TIMER_FLASH 3
#define TIMER_HOLD 4
#define TIMER_SHCAP 5

/* one-tap shortcut macros: label, modifier bits (0=shift,1=ctrl,2=alt,3=win), vk */
typedef struct { const wchar_t *label; int mods; UINT vk; } Macro;
static const Macro g_macros[] = {
    { L"Copy",   0x2, 'C' },
    { L"Paste",  0x2, 'V' },
    { L"Cut",    0x2, 'X' },
    { L"Undo",   0x2, 'Z' },
    { L"Save",   0x2, 'S' },
    { L"All",    0x2, 'A' },
    { L"AltTab", 0x4, VK_TAB },
    { L"Shot",   0x8, VK_SNAPSHOT },
    { L"Lock",   0x8, 'L' },
    { L"Play",   0x0, VK_MEDIA_PLAY_PAUSE },
    { L"Prev",   0x0, VK_MEDIA_PREV_TRACK },
    { L"Next",   0x0, VK_MEDIA_NEXT_TRACK },
    { L"Mute",   0x0, VK_VOLUME_MUTE },
    { L"Vol+",   0x0, VK_VOLUME_UP },
    { L"Vol-",   0x0, VK_VOLUME_DOWN },
    { L"Date",   0x10, 0 },   /* mods 0x10: insert date/time, not a combo */
    { L"Time",   0x10, 1 },
};
#define NMACROS (sizeof(g_macros)/sizeof(g_macros[0]))
/* settings panel actions */
enum { SET_SM=1, SET_BG, SET_OPDN, SET_OPUP, SET_DONE, SET_LANG, SET_PRED,
         SET_AUTO, SET_SPELL, SET_PINWIN, SET_BIGRAM, SET_PINBAR };

/* custom shortcuts page: 10 user-recorded combos (mods bit0=shift,
 * bit1=ctrl, bit2=alt, bit3=win — same as Macro), persistent in registry */
#define MAXSCUT 10
typedef struct { int used; int mods; UINT vk; wchar_t name[10]; int star; } Shortcut;
static Shortcut g_sc[MAXSCUT];
static int g_viewSC = 0;      /* custom-shortcuts page (like g_viewClip) */
static int g_scDelArm = 0;    /* delete mode: tapping a slot clears it */
static int g_recSlot = -1;    /* recording armed for slot, -1 = idle */
static int g_recStep = 0;     /* 0=capture combo, 1=type the name */
static int g_recMods = 0;
static UINT g_recVk = 0;
static wchar_t g_recName[10];

/* bar buttons: X at far right (Windows convention), gear at far left */
enum { BAR_NONE=0, BAR_FN=1, BAR_STAR=2, BAR_MODE=3, BAR_X=4, BAR_GEAR=5, BAR_CLIP=6, BAR_SC=7, BAR_CALC=8, BAR_TR=9 };
typedef struct { int id; wchar_t t[8]; RECT rc; int w; int show; int side; } BarBtn;
static BarBtn g_bar[9] = {
    { BAR_FN,   L"Fn",  {0}, 40, 1, 0 },
    { BAR_STAR, L"\u2605", {0}, 36, 1, 0 },
    { BAR_SC,   L"SC",  {0}, 36, 1, 0 },
    { BAR_CLIP, L"Clip", {0}, 40, 1, 0 },
    { BAR_CALC, L"Calc", {0}, 40, 1, 0 },
    { BAR_TR,   L"Tr",  {0}, 36, 1, 0 },
    { BAR_MODE, L"Compact", {0}, 58, 1, 0 },
    { BAR_X,    L"\u2715", {0}, 40, 1, 0 },
    { BAR_GEAR, L"\u2699", {0}, 36, 1, 1 },
};

static int g_starArm = 0;   /* star mode on SC page: tap toggles pin */
static int g_pinBarOn = 1;  /* pin strip visible (SET_PINBAR toggle, persisted) */

static int StarCount(void) {
    int i, n = 0;
    for (i = 0; i < MAXSCUT; i++)
        n += (g_sc[i].used && g_sc[i].vk && g_sc[i].star) ? 1 : 0;
    return n;
}
/* i-th (0..2) starred slot in ascending order, or -1 */
static int PinSlotByBar(int i) {
    int s, n = 0;
    for (s = 0; s < MAXSCUT; s++)
        if (g_sc[s].used && g_sc[s].vk && g_sc[s].star) {
            if (n == i) return s;
            n++;
        }
    return -1;
}

static void SetModeLabel(void) {
    int i;
    for (i = 0; i < 9; i++) {
        if (g_bar[i].id == BAR_MODE)
            wcscpy(g_bar[i].t, g_compact ? L"Full" : L"Compact");
        if (g_bar[i].id == BAR_FN)
            g_bar[i].show = !g_compact; /* Fn only exists in Full */
    }
}

/* colors */
#define C_BG      RGB(30,30,30)
#define C_BAR     RGB(37,37,38)
#define C_KEY     RGB(45,45,45)
#define C_SPEC    RGB(51,51,56)
#define C_ACTIVE  RGB(74,74,74)
#define C_HELD    RGB(76,194,255)
#define C_LOCK    RGB(0,120,212)
#define C_TXT     RGB(255,255,255)
#define C_DIM     RGB(128,128,128)

static void AddKey(const wchar_t *t, Kind k, UINT vk, int mod,
                   wchar_t lo, wchar_t hi, float w, int row) {
    if (g_nkeys >= MAXKEYS) return;
    Key *c = &g_keys[g_nkeys++];
    wcsncpy(c->text, t, 9); c->text[9] = 0;
    c->sub[0] = 0; c->tag = 0;
    c->kind = k; c->vk = vk; c->mod = mod;
    c->lo = lo; c->hi = hi; c->w = w; c->row = row;
    SetRectEmpty(&c->rc);
}

static void BuildFull(void) {
    int i;
    g_nkeys = 0;
    /* fn row */
    AddKey(L"Esc", K_SPECIAL, VK_ESCAPE, 0, 0,0, 1.2f, 0);
    for (i = 1; i <= 12; i++) {
        wchar_t b[8]; wsprintfW(b, L"F%d", i);
        AddKey(b, K_SPECIAL, (UINT)(VK_F1 + i - 1), 0, 0,0, 1.0f, 0);
    }
    AddKey(L"PrtSc", K_SPECIAL, VK_SNAPSHOT, 0, 0,0, 1.2f, 0);
    AddKey(L"Del", K_SPECIAL, VK_DELETE, 0, 0,0, 1.2f, 0);
    /* number row */
    AddKey(L"`", K_CHAR, 0,0, L'`', L'~', 1.0f, 1);
    {
        const wchar_t *d = L"1234567890", *s = L"!@#$%^&*()";
        for (i = 0; i < 10; i++) {
            wchar_t t[2] = { d[i], 0 };
            AddKey(t, K_CHAR, 0,0, d[i], s[i], 1.0f, 1);
        }
    }
    AddKey(L"-", K_CHAR, 0,0, L'-', L'_', 1.0f, 1);
    AddKey(L"=", K_CHAR, 0,0, L'=', L'+', 1.0f, 1);
    AddKey(L"Back", K_SPECIAL, VK_BACK, 0, 0,0, 2.0f, 1);
    /* top */
    AddKey(L"Tab", K_SPECIAL, VK_TAB, 0, 0,0, 1.6f, 2);
    {
        const wchar_t *r = L"QWERTYUIOP";
        for (i = 0; r[i]; i++) {
            wchar_t t[2] = { r[i], 0 };
            AddKey(t, K_CHAR, 0,0, (wchar_t)towlower(r[i]), r[i], 1.0f, 2);
        }
    }
    AddKey(L"[", K_CHAR, 0,0, L'[', L'{', 1.0f, 2);
    AddKey(L"]", K_CHAR, 0,0, L']', L'}', 1.0f, 2);
    AddKey(L"\\", K_CHAR, 0,0, L'\\', L'|', 1.5f, 2);
    /* home */
    AddKey(L"Caps", K_CAPS, 0,0, 0,0, 1.8f, 3);
    {
        const wchar_t *r = L"ASDFGHJKL";
        for (i = 0; r[i]; i++) {
            wchar_t t[2] = { r[i], 0 };
            AddKey(t, K_CHAR, 0,0, (wchar_t)towlower(r[i]), r[i], 1.0f, 3);
        }
    }
    AddKey(L";", K_CHAR, 0,0, L';', L':', 1.0f, 3);
    AddKey(L"'", K_CHAR, 0,0, L'\'', L'"', 1.0f, 3);
    AddKey(L"Enter", K_SPECIAL, VK_RETURN, 0, 0,0, 2.2f, 3);
    /* shift row */
    AddKey(L"Shift", K_MOD, 0, MX_SHIFT, 0,0, 2.2f, 4);
    {
        const wchar_t *r = L"ZXCVBNM";
        for (i = 0; r[i]; i++) {
            wchar_t t[2] = { r[i], 0 };
            AddKey(t, K_CHAR, 0,0, (wchar_t)towlower(r[i]), r[i], 1.0f, 4);
        }
    }
    AddKey(L",", K_CHAR, 0,0, L',', L'<', 1.0f, 4);
    AddKey(L".", K_CHAR, 0,0, L'.', L'>', 1.0f, 4);
    AddKey(L"/", K_CHAR, 0,0, L'/', L'?', 1.0f, 4);
    AddKey(L"Shift", K_MOD, 0, MX_SHIFT, 0,0, 2.2f, 4);
    AddKey(L"\u25B2", K_SPECIAL, VK_UP, 0, 0,0, 1.0f, 4);
    /* bottom */
    AddKey(L"Ctrl", K_MOD, 0, MX_CTRL, 0,0, 1.5f, 5);
    AddKey(L"Win", K_MOD, 0, MX_WIN, 0,0, 1.5f, 5);
    AddKey(L"Alt", K_MOD, 0, MX_ALT, 0,0, 1.5f, 5);
    AddKey(L"Space", K_SPACE, 0,0, 0,0, 6.0f, 5);
    AddKey(L"Alt", K_MOD, 0, MX_ALT, 0,0, 1.3f, 5);
    AddKey(L"\u2630", K_SPECIAL, VK_APPS, 0, 0,0, 1.3f, 5);
    AddKey(L"Ctrl", K_MOD, 0, MX_CTRL, 0,0, 1.3f, 5);
    AddKey(L"PgUp", K_SPECIAL, VK_PRIOR, 0, 0,0, 1.2f, 5);
    AddKey(L"PgDn", K_SPECIAL, VK_NEXT, 0, 0,0, 1.2f, 5);
    AddKey(L"Home", K_SPECIAL, VK_HOME, 0, 0,0, 1.0f, 5);
    AddKey(L"End", K_SPECIAL, VK_END, 0, 0,0, 1.0f, 5);
    AddKey(L"\u25C0", K_SPECIAL, VK_LEFT, 0, 0,0, 1.0f, 5);
    AddKey(L"\u25BC", K_SPECIAL, VK_DOWN, 0, 0,0, 1.0f, 5);
    AddKey(L"\u25B6", K_SPECIAL, VK_RIGHT, 0, 0,0, 1.0f, 5);
}

/* compact ABC page: phone-style, letters with digit tags, modifiers kept */
static void BuildCompactAbc(void) {
    int i;
    g_nkeys = 0;
    /* R0: Esc + q..p (digit on top) + Back */
    AddKey(L"Esc", K_SPECIAL, VK_ESCAPE, 0, 0,0, 1.2f, 0);
    {
        const wchar_t *q = L"QWERTYUIOP", *dg = L"1234567890";
        for (i = 0; q[i]; i++) {
            wchar_t t[2] = { q[i], 0 };
            AddKey(t, K_CHAR, 0,0, (wchar_t)towlower(q[i]), q[i], 1.0f, 0);
            g_keys[g_nkeys-1].tag = dg[i];
        }
    }
    AddKey(L"⌫", K_SPECIAL, VK_BACK, 0, 0,0, 1.6f, 0);
    /* R1: Tab + a..l + Enter */
    AddKey(L"⇥", K_SPECIAL, VK_TAB, 0, 0,0, 1.4f, 1);
    {
        const wchar_t *r = L"ASDFGHJKL";
        for (i = 0; r[i]; i++) {
            wchar_t t[2] = { r[i], 0 };
            AddKey(t, K_CHAR, 0,0, (wchar_t)towlower(r[i]), r[i], 1.0f, 1);
        }
    }
    AddKey(L"⏎", K_SPECIAL, VK_RETURN, 0, 0,0, 1.8f, 1);
    /* R2: merged Shift/Caps + z..m + , . / + Up */
    AddKey(L"⇪", K_SHCAP, 0,0, 0,0, 2.0f, 2);
    {
        const wchar_t *r = L"ZXCVBNM";
        for (i = 0; r[i]; i++) {
            wchar_t t[2] = { r[i], 0 };
            AddKey(t, K_CHAR, 0,0, (wchar_t)towlower(r[i]), r[i], 1.0f, 2);
        }
    }
    AddKey(L",", K_CHAR, 0,0, L',', L'<', 1.0f, 2);
    AddKey(L".", K_CHAR, 0,0, L'.', L'>', 1.0f, 2);
    AddKey(L"/", K_CHAR, 0,0, L'/', L'?', 1.0f, 2);
    AddKey(L"\u25B2", K_SPECIAL, VK_UP, 0, 0,0, 1.0f, 2);
    /* R3: modifiers + &123 + space + Del + arrows (Del right of Space) */
    AddKey(L"Ctrl", K_MOD, 0, MX_CTRL, 0,0, 1.2f, 3);
    AddKey(L"Win", K_MOD, 0, MX_WIN, 0,0, 1.2f, 3);
    AddKey(L"Alt", K_MOD, 0, MX_ALT, 0,0, 1.2f, 3);
    AddKey(L"&123", K_PAGE, 1,0, 0,0, 1.4f, 3);
    AddKey(L"Space", K_SPACE, 0,0, 0,0, 4.0f, 3);
    AddKey(L"Del", K_SPECIAL, VK_DELETE, 0, 0,0, 1.2f, 3);
    AddKey(L"PgUp", K_SPECIAL, VK_PRIOR, 0, 0,0, 1.0f, 3);
    AddKey(L"PgDn", K_SPECIAL, VK_NEXT, 0, 0,0, 1.0f, 3);
    AddKey(L"Home", K_SPECIAL, VK_HOME, 0, 0,0, 1.0f, 3);
    AddKey(L"End", K_SPECIAL, VK_END, 0, 0,0, 1.0f, 3);
    AddKey(L"\u25C0", K_SPECIAL, VK_LEFT, 0, 0,0, 1.0f, 3);
    AddKey(L"\u25BC", K_SPECIAL, VK_DOWN, 0, 0,0, 1.0f, 3);
    AddKey(L"\u25B6", K_SPECIAL, VK_RIGHT, 0, 0,0, 1.0f, 3);
}

/* compact &123 page: numbers + symbols */
static void BuildCompactSym(void) {
    int i;
    g_nkeys = 0;
    /* R0: number row */
    AddKey(L"`", K_CHAR, 0,0, L'`', L'~', 1.0f, 0);
    {
        const wchar_t *d = L"1234567890", *s = L"!@#$%^&*()";
        for (i = 0; i < 10; i++) {
            wchar_t t[2] = { d[i], 0 };
            AddKey(t, K_CHAR, 0,0, d[i], s[i], 1.0f, 0);
        }
    }
    AddKey(L"-", K_CHAR, 0,0, L'-', L'_', 1.0f, 0);
    AddKey(L"=", K_CHAR, 0,0, L'=', L'+', 1.0f, 0);
    AddKey(L"⌫", K_SPECIAL, VK_BACK, 0, 0,0, 2.0f, 0);
    /* R1: brackets + quotes + Del + Enter */
    AddKey(L"⇥", K_SPECIAL, VK_TAB, 0, 0,0, 1.4f, 1);
    AddKey(L"[", K_CHAR, 0,0, L'[', L'{', 1.0f, 1);
    AddKey(L"]", K_CHAR, 0,0, L']', L'}', 1.0f, 1);
    AddKey(L"\\", K_CHAR, 0,0, L'\\', L'|', 1.0f, 1);
    AddKey(L";", K_CHAR, 0,0, L';', L':', 1.0f, 1);
    AddKey(L"'", K_CHAR, 0,0, L'\'', L'"', 1.0f, 1);
    AddKey(L"Del", K_SPECIAL, VK_DELETE, 0, 0,0, 1.4f, 1);
    AddKey(L"⏎", K_SPECIAL, VK_RETURN, 0, 0,0, 1.8f, 1);
    /* R2: merged Shift/Caps + extra symbols + Up */
    AddKey(L"⇪", K_SHCAP, 0,0, 0,0, 1.8f, 2);
    AddKey(L"<", K_CHAR, 0,0, L'<', L'<', 1.0f, 2);
    AddKey(L">", K_CHAR, 0,0, L'>', L'>', 1.0f, 2);
    AddKey(L"?", K_CHAR, 0,0, L'?', L'?', 1.0f, 2);
    AddKey(L"!", K_CHAR, 0,0, L'!', L'!', 1.0f, 2);
    AddKey(L":", K_CHAR, 0,0, L':', L':', 1.0f, 2);
    AddKey(L"\"", K_CHAR, 0,0, L'"', L'"', 1.0f, 2);
    AddKey(L"\u25B2", K_SPECIAL, VK_UP, 0, 0,0, 1.0f, 2);
    /* R3: modifiers + Menu + ABC + space + arrows (extra key right of Space) */
    AddKey(L"Ctrl", K_MOD, 0, MX_CTRL, 0,0, 1.2f, 3);
    AddKey(L"Win", K_MOD, 0, MX_WIN, 0,0, 1.2f, 3);
    AddKey(L"Alt", K_MOD, 0, MX_ALT, 0,0, 1.2f, 3);
    AddKey(L"ABC", K_PAGE, 0,0, 0,0, 1.4f, 3);
    AddKey(L"Space", K_SPACE, 0,0, 0,0, 4.0f, 3);
    AddKey(L"\u2630", K_SPECIAL, VK_APPS, 0, 0,0, 1.2f, 3);
    AddKey(L"PgUp", K_SPECIAL, VK_PRIOR, 0, 0,0, 1.0f, 3);
    AddKey(L"PgDn", K_SPECIAL, VK_NEXT, 0, 0,0, 1.0f, 3);
    AddKey(L"Home", K_SPECIAL, VK_HOME, 0, 0,0, 1.0f, 3);
    AddKey(L"End", K_SPECIAL, VK_END, 0, 0,0, 1.0f, 3);
    AddKey(L"\u25C0", K_SPECIAL, VK_LEFT, 0, 0,0, 1.0f, 3);
    AddKey(L"\u25BC", K_SPECIAL, VK_DOWN, 0, 0,0, 1.0f, 3);
    AddKey(L"\u25B6", K_SPECIAL, VK_RIGHT, 0, 0,0, 1.0f, 3);
}

/* settings panel (row 10): size + opacity + lang + typing aids, via gear */
static void BuildSettingsRow(void) {
    AddKey(L"A-", K_SET, SET_SM, 0, 0,0, 1.5f, 10);
    AddKey(L"A+", K_SET, SET_BG, 0, 0,0, 1.5f, 10);
    AddKey(L"\u25D0", K_SET, SET_OPDN, 0, 0,0, 1.5f, 10);
    AddKey(L"\u25D1", K_SET, SET_OPUP, 0, 0,0, 1.5f, 10);
    AddKey(g_lang == 0 ? L"ID" : L"EN", K_SET, SET_LANG, 0, 0,0, 1.5f, 10);
    AddKey(L"Pred", K_SET, SET_PRED, 0, 0,0, 1.5f, 10);
    AddKey(L"Auto", K_SET, SET_AUTO, 0, 0,0, 1.5f, 10);
    AddKey(L"Spell", K_SET, SET_SPELL, 0, 0,0, 1.5f, 10);
    AddKey(L"Bi", K_SET, SET_BIGRAM, 0, 0,0, 1.5f, 10);
    AddKey(L"Pin", K_SET, SET_PINWIN, 0, 0,0, 1.5f, 10);
    AddKey(L"\u2605", K_SET, SET_PINBAR, 0, 0,0, 1.5f, 10);
    AddKey(L"Done", K_SET, SET_DONE, 0, 0,0, 1.5f, 10);
}

/* custom shortcuts page (rows 30-32): 10 recordable slots + nav.
 * Tap a named slot to fire it, "+" to record a new one. */
static void BuildSCRows(void) {
    int i;
    for (i = 0; i < MAXSCUT; i++) {
        wchar_t t[10];
        if (g_sc[i].used && g_sc[i].name[0]) wcscpy(t, g_sc[i].name);
        else wcscpy(t, L"+");
        AddKey(t, K_SCUT, (UINT)i, 0, 0,0, 2.0f, i < 5 ? 30 : 31);
    }
    AddKey(L"Del", K_CNAV, CNAV_SDEL, 0, 0,0, 1.5f, 32);
    AddKey(L"\u2605", K_CNAV, CNAV_SSTAR, 0, 0,0, 1.5f, 32);
    AddKey(L"Keys", K_CNAV, CNAV_SBACK, 0, 0,0, 1.5f, 32);
}

/* record banner (row 13): live instructions while capturing on keys view */
static void BuildRecRow(void) {
    AddKey(L"", K_INFO, 0, 0, 0,0, 10.0f, 13);
}

/* calculator page (rows 40-45): display + keys, opened by Calc bar button */
static void BuildCalcRows(void) {
    AddKey(L"", K_INFO, 0, 0, 0,0, 10.0f, 40);
    AddKey(L"C", K_CALC, (UINT)L'C', 0, 0,0, 1.2f, 41);
    AddKey(L"(", K_CALC, (UINT)L'(', 0, 0,0, 1.0f, 41);
    AddKey(L")", K_CALC, (UINT)L')', 0, 0,0, 1.0f, 41);
    AddKey(L"%", K_CALC, (UINT)L'%', 0, 0,0, 1.0f, 41);
    AddKey(L"^", K_CALC, (UINT)L'^', 0, 0,0, 1.0f, 41);
    AddKey(L"\u232B", K_CALC, (UINT)L'B', 0, 0,0, 1.2f, 41);
    AddKey(L"7", K_CALC, (UINT)L'7', 0, 0,0, 1.0f, 42);
    AddKey(L"8", K_CALC, (UINT)L'8', 0, 0,0, 1.0f, 42);
    AddKey(L"9", K_CALC, (UINT)L'9', 0, 0,0, 1.0f, 42);
    AddKey(L"/", K_CALC, (UINT)L'/', 0, 0,0, 1.0f, 42);
    AddKey(L"4", K_CALC, (UINT)L'4', 0, 0,0, 1.0f, 43);
    AddKey(L"5", K_CALC, (UINT)L'5', 0, 0,0, 1.0f, 43);
    AddKey(L"6", K_CALC, (UINT)L'6', 0, 0,0, 1.0f, 43);
    AddKey(L"*", K_CALC, (UINT)L'*', 0, 0,0, 1.0f, 43);
    AddKey(L"1", K_CALC, (UINT)L'1', 0, 0,0, 1.0f, 44);
    AddKey(L"2", K_CALC, (UINT)L'2', 0, 0,0, 1.0f, 44);
    AddKey(L"3", K_CALC, (UINT)L'3', 0, 0,0, 1.0f, 44);
    AddKey(L"-", K_CALC, (UINT)L'-', 0, 0,0, 1.0f, 44);
    AddKey(L"0", K_CALC, (UINT)L'0', 0, 0,0, 1.0f, 45);
    AddKey(L".", K_CALC, (UINT)L'.', 0, 0,0, 1.0f, 45);
    AddKey(L"=", K_CALC, (UINT)L'=', 0, 0,0, 2.0f, 45);
    AddKey(L"+", K_CALC, (UINT)L'+', 0, 0,0, 1.0f, 45);
}

/* pinned-shortcut strip (row 14): starred slots, like the suggestion strip.
 * Tap fires; membership rebuilt on every star change. */
static void BuildPinRow(void) {
    int i;
    for (i = 0; i < 3; i++) {
        int s = PinSlotByBar(i);
        if (s < 0) break;
        AddKey(g_sc[s].name[0] ? g_sc[s].name : L"?", K_SCUT, (UINT)s,
               0, 0,0, 2.0f, 14);
    }
}

/* translate page (rows 50-51): ID input display + Paste/Clear/Go/Keys.
 * Type Indonesian via the keys (tapping the display arms typing), or
 * Paste it from the clipboard, then Go types the English result. */
static void BuildTrRows(void) {
    AddKey(L"", K_INFO, 0, 0, 0,0, 10.0f, 50);
    AddKey(L"Paste", K_CNAV, CNAV_TRPASTE, 0, 0,0, 1.5f, 51);
    AddKey(L"Clear", K_CNAV, CNAV_TRCLEAR, 0, 0,0, 1.2f, 51);
    AddKey(L"\u21C4", K_CNAV, CNAV_TRLANG, 0, 0,0, 1.2f, 51);
    AddKey(L"Go", K_CNAV, CNAV_TRGO, 0, 0,0, 1.5f, 51);
    AddKey(L"Keys", K_CNAV, CNAV_TRBACK, 0, 0,0, 1.5f, 51);
}

/* word suggestions (row 12), labels drawn live from g_sugg */
static void BuildSuggRow(void) {
    int i;
    for (i = 0; i < 3; i++)
        AddKey(L"", K_SUGG, (UINT)i, 0, 0,0, 2.0f, 12);
}

/* one-tap shortcut macros (rows 11+16), opened by the star button.
 * First 9 are app shortcuts; the rest (media, date/time) go on row 16
 * so neither row gets overcrowded. */
static void BuildMacrosRow(void) {
    unsigned i;
    for (i = 0; i < NMACROS; i++)
        AddKey(g_macros[i].label, K_MACRO, g_macros[i].vk, g_macros[i].mods, 0,0, 1.5f, i < 9 ? 11 : 16);
}

/* clipboard page (rows 20-24 entries, 25 nav), opened by Clip button */
static void BuildClipRows(void) {
    int i;
    for (i = 0; i < 5; i++)
        AddKey(L"", K_CLIP, (UINT)i, 0, 0,0, 1.0f, 20 + i);
    AddKey(L"\u25B2", K_CNAV, CNAV_UP, 0, 0,0, 1.2f, 25);
    AddKey(L"\u25BC", K_CNAV, CNAV_DOWN, 0, 0,0, 1.2f, 25);
    AddKey(L"Clear", K_CNAV, CNAV_CLEAR, 0, 0,0, 1.5f, 25);
    AddKey(L"Pin", K_CNAV, CNAV_PIN, 0, 0,0, 1.5f, 25);
    AddKey(L"Find", K_CNAV, CNAV_FIND, 0, 0,0, 1.5f, 25);
    AddKey(L"Keys", K_CNAV, CNAV_BACK, 0, 0,0, 1.5f, 25);
}

static void BuildPanels(void) {
    BuildSettingsRow();
    BuildMacrosRow();
    BuildSuggRow();
    BuildClipRows();
    BuildSCRows();
    BuildRecRow();
    BuildPinRow();
    BuildCalcRows();
    BuildTrRows();
}

static void BuildKeys(void) {
    if (!g_compact) { BuildFull(); BuildPanels(); return; }
    if (g_page == 0) BuildCompactAbc(); else BuildCompactSym();
    BuildPanels();
}

/* ---------------- word prediction (words_XX.txt next to the exe) ---------------- */
static void ExeDir(wchar_t *out, int n) {
    GetModuleFileNameW(NULL, out, n);
    {
        wchar_t *p = wcsrchr(out, L'\\');
        if (p) *(p + 1) = 0;
    }
}

/* parse "word freq" lines (pre-sorted); shared by resource + file loaders */
static int ParseDict(int lang, const char *data, size_t len) {
    size_t pos = 0;
    g_dict[lang] = (DictWord *)calloc(MAXDICT, sizeof(DictWord));
    if (!g_dict[lang]) return 0;
    g_ndict[lang] = 0;
    while (pos < len && g_ndict[lang] < MAXDICT) {
        size_t end = pos;
        while (end < len && data[end] != '\n') end++;
        {
            /* decode first token as UTF-8, keep a-z/apostrophe words only */
            size_t p = pos;
            wchar_t w[MAXWLEN + 1];
            size_t o = 0;
            int ok = 1;
            while (p < end && data[p] != ' ' && data[p] != '\t' && data[p] != '\r') {
                unsigned char c0 = (unsigned char)data[p];
                wchar_t c;
                if (c0 < 0x80) { c = c0; p++; }
                else if ((c0 & 0xE0) == 0xC0 && p + 1 < end) {
                    c = (wchar_t)(((c0 & 0x1F) << 6) | (((unsigned char)data[p+1]) & 0x3F)); p += 2;
                } else if ((c0 & 0xF0) == 0xE0 && p + 2 < end) {
                    c = (wchar_t)(((c0 & 0x0F) << 12) | ((((unsigned char)data[p+1]) & 0x3F) << 6) | (((unsigned char)data[p+2]) & 0x3F)); p += 3;
                } else { ok = 0; break; }
                if (c >= L'A' && c <= L'Z') c += 32;
                if ((c < L'a' || c > L'z') && c != L'\'') { ok = 0; break; }
                if (o < MAXWLEN) w[o++] = c;
                else { ok = 0; break; }
            }
            if (ok && o >= 1) {
                w[o] = 0;
                wcscpy(g_dict[lang][g_ndict[lang]].w, w);
                g_ndict[lang]++;
            }
        }
        pos = end + 1;
    }
    if (g_ndict[lang] == 0) { free(g_dict[lang]); g_dict[lang] = NULL; }
    return g_ndict[lang];
}

/* file format: "word freq" per line, pre-sorted by frequency.
 * Embedded RCDATA first (single-exe), sidecar words_XX.txt fallback. */
static int LoadDict(int lang, const wchar_t *fn) {
    HRSRC hr;
    HGLOBAL hg;
    /* 101 = words_id, 102 = words_en */
    hr = FindResourceW(g_hInst, MAKEINTRESOURCEW(101 + lang), MAKEINTRESOURCEW(10));
    if (hr) {
        DWORD sz = SizeofResource(g_hInst, hr);
        hg = LoadResource(g_hInst, hr);
        if (hg && sz > 0) {
            const char *data = (const char *)LockResource(hg);
            if (data) {
                int n = ParseDict(lang, data, sz);
                if (n > 0) return n;
            }
        }
    }
    {
        wchar_t path[MAX_PATH];
        FILE *f;
        long sz;
        char *buf;
        int n;
        ExeDir(path, MAX_PATH);
        wcsncat(path, fn, MAX_PATH - wcslen(path) - 1);
        f = _wfopen(path, L"rb");
        if (!f) return 0;
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 8 * 1024 * 1024) { fclose(f); return 0; }
        buf = (char *)malloc((size_t)sz);
        if (!buf) { fclose(f); return 0; }
        if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
            free(buf); fclose(f); return 0;
        }
        fclose(f);
        n = ParseDict(lang, buf, (size_t)sz);
        free(buf);
        return n;
    }
}

static int DictAvail(int lang) { return g_dict[lang] && g_ndict[lang] > 0; }

/* bigram table: little-endian (prev,next) uint16 records, sorted by prev
 * with frequency rank order inside each block. Embedded RCDATA first
 * (single-exe), sidecar bigrams_XX.bin fallback. x86/x64 are LE. */
static int LoadBigram(int lang, const wchar_t *fn) {
    HRSRC hr;
    HGLOBAL hg;
    const unsigned char *data = NULL;
    DWORD sz = 0;
    FILE *f = NULL;
    long fsz = 0;
    unsigned char *buf = NULL;
    int n, i;
    hr = FindResourceW(g_hInst, MAKEINTRESOURCEW(103 + lang), MAKEINTRESOURCEW(10));
    if (hr) {
        sz = SizeofResource(g_hInst, hr);
        hg = LoadResource(g_hInst, hr);
        if (hg && sz > 0) data = (const unsigned char *)LockResource(hg);
    }
    if (!data || sz == 0 || (sz % 4) != 0) {
        wchar_t path[MAX_PATH];
        data = NULL;
        ExeDir(path, MAX_PATH);
        wcsncat(path, fn, MAX_PATH - wcslen(path) - 1);
        f = _wfopen(path, L"rb");
        if (!f) return 0;
        fseek(f, 0, SEEK_END);
        fsz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (fsz <= 0 || (fsz % 4) != 0 || fsz > 4 * 1024 * 1024) {
            fclose(f); return 0;
        }
        buf = (unsigned char *)malloc((size_t)fsz);
        if (!buf) { fclose(f); return 0; }
        if (fread(buf, 1, (size_t)fsz, f) != (size_t)fsz) {
            free(buf); fclose(f); return 0;
        }
        fclose(f);
        data = buf;
        sz = (DWORD)fsz;
    }
    n = (int)(sz / 4);
    g_bigram[lang] = (BigramRec *)malloc((size_t)n * sizeof(BigramRec));
    if (!g_bigram[lang]) { if (buf) free(buf); return 0; }
    memcpy(g_bigram[lang], data, (size_t)n * sizeof(BigramRec));
    if (buf) free(buf);
    g_nbigram[lang] = n;
    for (i = 0; i < n; i++)   /* drop corrupt indices, keep table usable */
        if (g_bigram[lang][i].prev >= 60000 || g_bigram[lang][i].next >= 60000) {
            free(g_bigram[lang]);
            g_bigram[lang] = NULL;
            g_nbigram[lang] = 0;
            return 0;
        }
    return n;
}

static int BigramAvail(int lang) {
    return g_bigram[lang] && g_nbigram[lang] > 0 && DictAvail(lang);
}

/* top next-word dict indices for prev (rank order); returns count */
static int BigramNext(int prevIdx, int *out) {
    int lo = 0, hi = g_nbigram[g_lang], mid, first, c = 0;
    if (prevIdx < 0 || !BigramAvail(g_lang)) return 0;
    while (lo < hi) {
        mid = (lo + hi) / 2;
        if (g_bigram[g_lang][mid].prev < (unsigned)prevIdx) lo = mid + 1;
        else hi = mid;
    }
    first = lo;
    while (first < g_nbigram[g_lang] &&
           g_bigram[g_lang][first].prev == (unsigned)prevIdx && c < 8) {
        int nx = g_bigram[g_lang][first++].next;
        if (nx < g_ndict[g_lang]) out[c++] = nx;   /* skip stale indices */
    }
    return c;
}

/* top-3 suggestions: bigram next-words on a fresh word, otherwise prefix
 * matches (Pred only) with bigram continuations boosted. The strip is
 * sticky: sugg slots are never overwritten with emptiness, so once shown
 * the strip stays visible instead of flashing empty boxes. The red
 * misspelt literal is a live overlay on slot 0 (buffer text, needs no
 * slot). Pred off => Spell-only mode (literal only, completions never
 * filled). */
static void SyncSuggRow(void);   /* defined below; strip shows only w/ content */
static void ComputeSugg(void) {
    int n = 0, i, k;
    wchar_t pre[32];
    wchar_t fresh[3][MAXWLEN + 1];
    for (i = 0; i < 3; i++) fresh[i][0] = 0;
    if (!(g_predictOn || g_highlightOn) || !DictAvail(g_lang)) {
        /* mode/data off: strip must empty, never linger */
        for (i = 0; i < 3; i++) g_sugg[i][0] = 0;
        g_misspelt = 0;
        SyncSuggRow();
        return;
    }
    if (g_wordLen == 0) {
        g_misspelt = 0;
        if (g_predictOn && g_bigramOn && g_prevIdx >= 0) {
            int nx[8], nn, j;
            nn = BigramNext(g_prevIdx, nx);
            for (j = 0; j < nn && n < 3; j++)
                wcscpy(fresh[n++], g_dict[g_lang][nx[j]].w);
            if (n > 0)
                for (i = 0; i < 3; i++) wcscpy(g_sugg[i], fresh[i]);
        }
        SyncSuggRow();
        return;
    } else {
        for (i = 0; i < g_wordLen && i < 31; i++) {
            wchar_t c = g_wordBuf[i];
            pre[i] = (c >= L'A' && c <= L'Z') ? c + 32 : c;
        }
        pre[i] = 0;
        if (g_predictOn) {
            for (i = 0; i < g_ndict[g_lang] && n < 3; i++) {
                const wchar_t *w = g_dict[g_lang][i].w;
                for (k = 0; pre[k]; k++)
                    if (w[k] != pre[k]) break;
                if (pre[k]) continue;             /* not a prefix match */
                if (!pre[0] && n >= 3) break;
                wcscpy(fresh[n++], w);
            }
            if (g_bigramOn && g_prevIdx >= 0 && n > 1) {
                /* continuations of the previous word jump to the front */
                int nx[8], nn = BigramNext(g_prevIdx, nx);
                int w = 0, j, m;
                wchar_t tmp[3][MAXWLEN + 1];
                for (j = 0; j < n; j++) {
                    for (m = 0; m < nn; m++)
                        if (!wcscmp(fresh[j], g_dict[g_lang][nx[m]].w)) break;
                    if (m < nn) wcscpy(tmp[w++], fresh[j]);
                }
                for (j = 0; j < n; j++) {
                    for (m = 0; m < nn; m++)
                        if (!wcscmp(fresh[j], g_dict[g_lang][nx[m]].w)) break;
                    if (m >= nn) wcscpy(tmp[w++], fresh[j]);
                }
                for (j = 0; j < n; j++) wcscpy(fresh[j], tmp[j]);
            }
            if (n > 0) {
                for (i = 0; i < 3; i++) wcscpy(g_sugg[i], fresh[i]);
                g_misspelt = 0;
            } else if (g_wordLen >= 2) {
                g_misspelt = 1;   /* live red overlay; sugg slots untouched */
            }
        } else {
            /* Pred off: only detect misspelt for the red literal */
            int found = 0;
            for (i = 0; i < g_ndict[g_lang]; i++) {
                const wchar_t *w = g_dict[g_lang][i].w;
                for (k = 0; pre[k] && w[k] == pre[k]; k++) ;
                if (!pre[k]) { found = 1; break; }
            }
            g_misspelt = (!found && g_wordLen >= 2) ? 1 : 0;
        }
    }
    SyncSuggRow();
}

/* track the word being typed through this keyboard */
static void WordCommit(void) {
    /* word boundary (space/punct/enter): remember context for bigrams */
    if (g_wordLen > 0) {
        int i;
        wchar_t low[32];
        for (i = 0; i < g_wordLen && i < 31; i++) {
            wchar_t c = g_wordBuf[i];
            low[i] = (c >= L'A' && c <= L'Z') ? c + 32 : c;
        }
        low[i] = 0;
        wcsncpy(g_prevWord, g_wordBuf, 31);
        g_prevWord[31] = 0;
        g_prevIdx = -1;
        for (i = 0; i < g_ndict[g_lang]; i++)
            if (!wcscmp(g_dict[g_lang][i].w, low)) { g_prevIdx = i; break; }
    }
    g_wordLen = 0; g_wordBuf[0] = 0;
    ComputeSugg();
}
static void WordChar(wchar_t ch) {
    if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
        (ch >= L'0' && ch <= L'9') || ch == L'\'') {
        if (g_wordLen < 31) g_wordBuf[g_wordLen++] = ch;
        g_wordBuf[g_wordLen] = 0;
    } else {
        WordCommit();   /* space/punct commits */
        return;
    }
    ComputeSugg();
}
static void WordChop(void) {
    if (g_wordLen > 0) { g_wordBuf[--g_wordLen] = 0; }
    ComputeSugg();
}
static void WordClear(void) {
    g_wordLen = 0; g_wordBuf[0] = 0;
    ComputeSugg();
}

/* ---------------- autocorrect (like Windows "Autocorrect misspelt words") -- */
/* Fires when a word is committed with space/punct/Enter: unknown words
 * within a small edit distance of a frequent dictionary word are replaced.
 * One Backspace right after reverts to the original typing (phone-style). */
static void InvSugg(void);   /* forward: defined with the state logic */
static int IsCommitChar(wchar_t c) {
    if (c == L'\'' || c == L'_') return 0;
    if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
        (c >= L'0' && c <= L'9'))
        return 0;
    return 1;
}

static int DictExact(const wchar_t *w) {
    int i;
    for (i = 0; i < g_ndict[g_lang]; i++)
        if (!wcscmp(g_dict[g_lang][i].w, w)) return 1;
    return 0;
}

/* Damerau-Levenshtein (optimal string alignment: adjacent transposition
 * counts as 1, so "teh"->"the" is caught), capped at `cap` for speed. */
static int EditDistCap(const wchar_t *a, const wchar_t *b, int cap) {
    static int d[34][34];
    int n = (int)wcslen(a), m = (int)wcslen(b);
    int i, j, cost, best;
    if (n > 32 || m > 32) return cap + 1;
    if (abs(n - m) > cap) return cap + 1;
    for (i = 0; i <= n; i++) d[i][0] = i;
    for (j = 0; j <= m; j++) d[0][j] = j;
    for (i = 1; i <= n; i++) {
        best = cap + 1;
        for (j = 1; j <= m; j++) {
            cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            {
                int v = d[i - 1][j] + 1;
                int wv = d[i][j - 1] + 1;
                int sv = d[i - 1][j - 1] + cost;
                if (wv < v) v = wv;
                if (sv < v) v = sv;
                if (i > 1 && j > 1 && a[i - 1] == b[j - 2] &&
                    a[i - 2] == b[j - 1]) {
                    int tv = d[i - 2][j - 2] + 1;
                    if (tv < v) v = tv;
                }
                d[i][j] = v;
                if (v < best) best = v;
            }
        }
        if (best > cap) return cap + 1;   /* banded early exit */
    }
    return d[n][m];
}

/* Erase the just-typed word and replace it with the closest dictionary
 * word. `trail` = 1 when called from EmitText (space/punct follows and
 * must be counted by Backspace-undo), 0 for Enter. No-op when the word
 * is fine or no close candidate exists. */
static void AutoCorrectNow(int trail) {
    wchar_t low[32], out[32];
    int n, i, maxD, best = 999, bi = -1;
    if (!g_autocorrect || !DictAvail(g_lang)) return;
    n = g_wordLen;
    if (n < 3 || n > 31) return;
    for (i = 0; i < n; i++) {
        wchar_t c = g_wordBuf[i];
        if (c >= L'0' && c <= L'9') return;              /* alnum: skip */
        if (c >= L'A' && c <= L'Z') {
            if (i > 0) return;                           /* mid-word caps */
            low[i] = c + 32;
        } else low[i] = c;
    }
    low[n] = 0;
    if (DictExact(low)) return;                          /* already right */
    maxD = (n >= 6) ? 2 : 1;
    for (i = 0; i < g_ndict[g_lang]; i++) {
        const wchar_t *w = g_dict[g_lang][i].w;
        int wl = (int)wcslen(w), dd;
        if (abs(wl - n) > maxD) continue;
        dd = EditDistCap(low, w, maxD);
        if (dd < best) {
            best = dd; bi = i;
            if (best <= 1) break;   /* first hit = most frequent: optimal */
        }
    }
    if (g_bigramOn && g_prevIdx >= 0) {
        /* a continuation of the previous word wins on strictly smaller
         * distance (e.g. after "saya", "mau" beats a rarer look-alike) */
        int nx[8], nn = BigramNext(g_prevIdx, nx), m;
        for (m = 0; m < nn; m++) {
            const wchar_t *w = g_dict[g_lang][nx[m]].w;
            int wl = (int)wcslen(w), dd;
            if (abs(wl - n) > maxD) continue;
            dd = EditDistCap(low, w, maxD);
            if (dd < best) { best = dd; bi = nx[m]; }
        }
    }
    if (bi < 0 || best > maxD) return;
    wcscpy(out, g_dict[g_lang][bi].w);
    if (g_wordBuf[0] >= L'A' && g_wordBuf[0] <= L'Z' &&
        out[0] >= L'a' && out[0] <= L'z')
        out[0] -= 32;                                    /* keep capital */
    wcscpy(g_autoOrig, g_wordBuf);
    g_autoOrig[n] = 0;
    g_autoCorrLen = (int)wcslen(out);
    for (i = 0; i < n; i++) vk_tap(VK_BACK);             /* erase original */
    type_unicode(out);
    {
        const wchar_t *p;
        for (p = out; *p; p++) WordChar(*p);
    }
    g_autoTrail = trail;
    g_autoActive = 1;
    InvSugg();
}

/* ---------------- clip persistence (text only) ---------------------- */
/* Text entries (+pins) survive restarts via %LOCALAPPDATA%\FloatKeys\clip.dat.
 * Images are session-only. Format: magic,ver,count, then per entry:
 * pinned(DWORD), cch(DWORD), UTF-16 chars (no NUL). */
#define CLIP_MAGIC 0x464B434C
#define CLIP_VER 1

static void ClipPath(wchar_t *out, int n) {
    DWORD m;
    out[0] = 0;
    m = GetEnvironmentVariableW(L"LOCALAPPDATA", out, (DWORD)n);
    if (!m || m >= (DWORD)n) { out[0] = 0; return; }
    wcsncat(out, L"\\FloatKeys", n - wcslen(out) - 1);
    CreateDirectoryW(out, NULL);
    wcsncat(out, L"\\clip.dat", n - wcslen(out) - 1);
}

static void SaveClip(void) {
    wchar_t path[MAX_PATH];
    FILE *f;
    int i, n = 0;
    DWORD magic = CLIP_MAGIC, ver = CLIP_VER, cnt;
    ClipPath(path, MAX_PATH);
    if (!path[0]) return;
    for (i = 0; i < g_nclip; i++)
        if (!g_clip[i].isImg && g_clip[i].text) n++;
    f = _wfopen(path, L"wb");
    if (!f) return;
    cnt = (DWORD)n;
    fwrite(&magic, 4, 1, f);
    fwrite(&ver, 4, 1, f);
    fwrite(&cnt, 4, 1, f);
    for (i = 0; i < g_nclip; i++) {
        if (g_clip[i].isImg || !g_clip[i].text) continue;
        {
            DWORD cch = (DWORD)wcslen(g_clip[i].text);
            DWORD pin = g_clip[i].pinned ? 1 : 0;
            if (cch > 4000) cch = 4000;
            fwrite(&pin, 4, 1, f);
            fwrite(&cch, 4, 1, f);
            fwrite(g_clip[i].text, sizeof(wchar_t), cch, f);
        }
    }
    fclose(f);
}

static void LoadClip(void) {
    wchar_t path[MAX_PATH];
    FILE *f;
    long fsz;
    DWORD magic, ver, cnt, i;
    int seenUnpinned = 0;
    ClipPath(path, MAX_PATH);
    if (!path[0]) return;
    f = _wfopen(path, L"rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz < 12 || fsz > 4 * 1024 * 1024) { fclose(f); return; }
    if (fread(&magic, 4, 1, f) != 1 || magic != CLIP_MAGIC ||
        fread(&ver, 4, 1, f) != 1 || ver != CLIP_VER ||
        fread(&cnt, 4, 1, f) != 1 || cnt > MAXCLIP) {
        fclose(f); return;
    }
    for (i = 0; i < cnt && g_nclip < MAXCLIP; i++) {
        DWORD pin, cch;
        wchar_t *t;
        if (fread(&pin, 4, 1, f) != 1 || fread(&cch, 4, 1, f) != 1 ||
            cch == 0 || cch > 4000) break;
        t = (wchar_t *)malloc((cch + 1) * sizeof(wchar_t));
        if (!t) break;
        if (fread(t, sizeof(wchar_t), cch, f) != cch) {
            free(t); break;
        }
        t[cch] = 0;
        g_clip[g_nclip].isImg = 0;
        g_clip[g_nclip].text = t;
        g_clip[g_nclip].hbmp = NULL;
        g_clip[g_nclip].iw = g_clip[g_nclip].ih = 0;
        /* enforce pinned-lead invariant against corrupt files */
        if (pin && !seenUnpinned) g_clip[g_nclip].pinned = 1;
        else {
            g_clip[g_nclip].pinned = 0;
            seenUnpinned = 1;
        }
        g_nclip++;
    }
    fclose(f);
    g_clipOff = 0;
}

/* ---------------- clipboard history (text + images) ---------------- */
static void ClipFree(ClipEnt *e) {
    if (e->text) { free(e->text); e->text = NULL; }
    if (e->hbmp) { DeleteObject(e->hbmp); e->hbmp = NULL; }
    e->isImg = 0; e->iw = e->ih = 0; e->pinned = 0;
}
static void ClipClearAll(void) {
    int i;
    for (i = 0; i < g_nclip; i++) ClipFree(&g_clip[i]);
    g_nclip = 0; g_clipOff = 0;
}
/* Clear button: only unpinned entries go, pins survive. */
static void ClipClearUnpinned(void) {
    int i, w = 0;
    for (i = 0; i < g_nclip; i++) {
        if (g_clip[i].pinned) {
            if (w != i) g_clip[w] = g_clip[i];
            w++;
        } else ClipFree(&g_clip[i]);
    }
    for (i = w; i < g_nclip; i++) {
        g_clip[i].text = NULL; g_clip[i].hbmp = NULL;
        g_clip[i].isImg = 0; g_clip[i].pinned = 0;
    }
    g_nclip = w;
    if (g_clipOff >= g_nclip) g_clipOff = 0;
}
/* invariant: pinned entries always lead [0, PinCount) so eviction from
 * the tail and oldest-image drop never touch pins first. */
static int PinCount(void) {
    int n = 0;
    while (n < g_nclip && g_clip[n].pinned) n++;
    return n;
}
/* toggle pin on entry: pinned go to the end of the pin block, unpinned
 * go to the top of the recency block. */
static void ClipTogglePin(int histIdx) {
    ClipEnt e;
    int i, pins;
    if (histIdx < 0 || histIdx >= g_nclip) return;
    e = g_clip[histIdx];
    for (i = histIdx; i < g_nclip - 1; i++) g_clip[i] = g_clip[i + 1];
    g_nclip--;
    pins = 0;
    while (pins < g_nclip && g_clip[pins].pinned) pins++;
    e.pinned = !e.pinned;
    for (i = g_nclip; i > pins; i--) g_clip[i] = g_clip[i - 1];
    g_clip[pins] = e;
    g_nclip++;
    if (g_clipOff >= g_nclip) g_clipOff = 0;
}
static int ClipImgCount(void) {
    int i, n = 0;
    for (i = 0; i < g_nclip; i++) n += g_clip[i].isImg;
    return n;
}
static void ClipDropOldestImg(void) {
    int i;
    for (i = g_nclip - 1; i >= 0; i--)
        if (g_clip[i].isImg) {
            int j;
            ClipFree(&g_clip[i]);
            for (j = i; j < g_nclip - 1; j++) g_clip[j] = g_clip[j + 1];
            g_clip[g_nclip - 1].text = NULL;
            g_clip[g_nclip - 1].hbmp = NULL;
            g_clip[g_nclip - 1].isImg = 0;
            g_clip[g_nclip - 1].pinned = 0;
            g_nclip--;
            return;
        }
}
/* prepend entry (dupes removed); new items land below the pin block;
 * returns 1 if list changed */
static int ClipPrepend(ClipEnt *src) {
    int i, j, pins = PinCount();
    for (i = 0; i < g_nclip; i++) {
        if (g_clip[i].isImg != src->isImg) continue;
        if (!src->isImg && g_clip[i].text && src->text &&
            wcscmp(g_clip[i].text, src->text) == 0) {
            if (i <= pins) return 0;   /* already pinned or on top */
            {
                ClipEnt mv = g_clip[i];
                for (j = i; j > pins; j--) g_clip[j] = g_clip[j - 1];
                g_clip[pins] = mv;
            }
            return 1;
        }
    }
    if (src->isImg && ClipImgCount() >= MAXCLIPIMG) ClipDropOldestImg();
    pins = PinCount();
    if (g_nclip >= MAXCLIP) ClipFree(&g_clip[--g_nclip]);
    for (j = g_nclip; j > pins; j--) g_clip[j] = g_clip[j - 1];
    g_clip[pins] = *src;
    g_nclip++;
    g_clipOff = 0;
    return 1;
}

/* ---------------- clip find/filter ------------------------------ */
/* case-insensitive substring; empty needle matches everything */
static int wcsistr(const wchar_t *hay, const wchar_t *ndl) {
    size_t i, j;
    if (!hay) return 0;
    if (!ndl || !*ndl) return 1;
    for (i = 0; hay[i]; i++) {
        for (j = 0; ndl[j]; j++) {
            wchar_t a = hay[i + j], b = ndl[j];
            if (!a) break;
            if (a >= L'A' && a <= L'Z') a += 32;
            if (b >= L'A' && b <= L'Z') b += 32;
            if (a != b) break;
        }
        if (!ndl[j]) return 1;
    }
    return 0;
}
static int ClipMatch(ClipEnt *e) {
    if (!g_findBuf[0]) return 1;
    if (e->isImg) return wcsistr(L"img", g_findBuf);   /* "img" lists images */
    return wcsistr(e->text ? e->text : L"", g_findBuf);
}
static int ClipMatchCount(void) {
    int i, n = 0;
    for (i = 0; i < g_nclip; i++)
        if (ClipMatch(&g_clip[i])) n++;
    return n;
}
/* visible slot -> history index (filter- and page-aware), or -1 */
static int ClipVisibleIdx(int slot) {
    int i, n = 0, want = g_clipOff + slot;
    for (i = 0; i < g_nclip; i++)
        if (ClipMatch(&g_clip[i]) && n++ == want) return i;
    return -1;
}

/* copy any HBITMAP into a 32bpp DIBSection, downscaled to maxDim */
static HBITMAP BitmapToDib(HBITMAP src, int maxDim, int *pw, int *ph) {
    BITMAP bm;
    HDC sdc, ddc;
    HBITMAP dib = NULL;
    HGDIOBJ so, dob;
    BITMAPINFO bi;
    int w, h;
    if (!src || !GetObjectW(src, sizeof(bm), &bm)) return NULL;
    w = bm.bmWidth; h = bm.bmHeight;
    if (w <= 0 || h <= 0) return NULL;
    if (w > maxDim || h > maxDim) {
        if (w >= h) { h = h * maxDim / w; w = maxDim; }
        else { w = w * maxDim / h; h = maxDim; }
        if (w < 1) w = 1;
        if (h < 1) h = 1;
    }
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    sdc = CreateCompatibleDC(NULL);
    ddc = CreateCompatibleDC(NULL);
    if (sdc && ddc) {
        void *bits = NULL;
        dib = CreateDIBSection(ddc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (dib) {
            so = SelectObject(sdc, src);
            dob = SelectObject(ddc, dib);
            SetStretchBltMode(ddc, HALFTONE);
            SetBrushOrgEx(ddc, 0, 0, NULL);
            StretchBlt(ddc, 0, 0, w, h, sdc, 0, 0,
                       bm.bmWidth, bm.bmHeight, SRCCOPY);
            SelectObject(sdc, so);
            SelectObject(ddc, dob);
        }
    }
    if (sdc) DeleteDC(sdc);
    if (ddc) DeleteDC(ddc);
    if (dib) { *pw = w; *ph = h; }
    return dib;
}

/* HBITMAP -> CF_DIB global (for paste) */
static HGLOBAL BitmapToDibMem(HBITMAP src) {
    BITMAP bm;
    BITMAPINFOHEADER *ph;
    HGLOBAL hmem;
    HDC dc;
    int row, img;
    if (!src || !GetObjectW(src, sizeof(bm), &bm)) return NULL;
    row = ((bm.bmWidth * 32 + 31) / 32) * 4;
    img = row * bm.bmHeight;
    hmem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + img);
    if (!hmem) return NULL;
    ph = (BITMAPINFOHEADER *)GlobalLock(hmem);
    if (!ph) { GlobalFree(hmem); return NULL; }
    ZeroMemory(ph, sizeof(BITMAPINFOHEADER));
    ph->biSize = sizeof(BITMAPINFOHEADER);
    ph->biWidth = bm.bmWidth;
    ph->biHeight = bm.bmHeight;
    ph->biPlanes = 1;
    ph->biBitCount = 32;
    ph->biCompression = BI_RGB;
    ph->biSizeImage = img;
    dc = CreateCompatibleDC(NULL);
    GetDIBits(dc, src, 0, bm.bmHeight, (BYTE *)ph + sizeof(BITMAPINFOHEADER),
              (BITMAPINFO *)ph, DIB_RGB_COLORS);
    DeleteDC(dc);
    GlobalUnlock(hmem);
    return hmem;
}

static void OnClipboardUpdate(void) {
    ClipEnt e;
    if (g_ownClip) return;
    if (!OpenClipboard(g_hwnd)) return;
    ZeroMemory(&e, sizeof(e));
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE d = GetClipboardData(CF_UNICODETEXT);
        if (d) {
            const wchar_t *s = (const wchar_t *)GlobalLock(d);
            if (s && s[0]) {
                size_t n = wcslen(s);
                if (n > 4000) n = 4000;
                e.text = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
                if (e.text) {
                    wcsncpy(e.text, s, n);
                    e.text[n] = 0;
                    e.isImg = 0;
                    /* return 1 = list owns e.text now: must NOT free it */
                    if (ClipPrepend(&e)) {
                        if (g_viewClip)
                            InvalidateRect(g_hwnd, NULL, FALSE);
                    } else if (e.text) {
                        free(e.text);
                    }
                }
            }
            if (s) GlobalUnlock(d);
        }
    } else {
        HANDLE d = GetClipboardData(CF_BITMAP);
        HBITMAP src = NULL, dib = NULL;
        int w = 0, h = 0;
        if (d) src = (HBITMAP)d;
        if (src) dib = BitmapToDib(src, 1280, &w, &h);
        if (!dib) {
            HANDLE dd = GetClipboardData(CF_DIB);
            if (dd) {
                const BITMAPINFOHEADER *ph =
                    (const BITMAPINFOHEADER *)GlobalLock(dd);
                if (ph && ph->biSize >= sizeof(BITMAPINFOHEADER) &&
                    ph->biWidth > 0 && ph->biHeight != 0) {
                    HDC cdc = CreateCompatibleDC(NULL);
                    void *bits = NULL;
                    BITMAPINFO bi;
                    int tw = ph->biWidth;
                    int th = ph->biHeight < 0 ? -ph->biHeight : ph->biHeight;
                    ZeroMemory(&bi, sizeof(bi));
                    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bi.bmiHeader.biWidth = tw;
                    bi.bmiHeader.biHeight = -th;
                    bi.bmiHeader.biPlanes = 1;
                    bi.bmiHeader.biBitCount = 32;
                    bi.bmiHeader.biCompression = BI_RGB;
                    dib = CreateDIBSection(cdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
                    if (dib && bits) {
                        StretchDIBits(cdc, 0, 0, tw, th, 0, 0, tw, th,
                                      (const BYTE *)ph + ph->biSize +
                                      (ph->biClrUsed * 4),
                                      (const BITMAPINFO *)ph, DIB_RGB_COLORS, SRCCOPY);
                        w = tw; h = th;
                    } else if (dib) { DeleteObject(dib); dib = NULL; }
                    DeleteDC(cdc);
                }
                if (ph) GlobalUnlock(dd);
            }
        }
        if (dib) {
            int sw = w, sh = h;
            if (w > 1280 || h > 1280) {
                HBITMAP small = BitmapToDib(dib, 1280, &sw, &sh);
                DeleteObject(dib);
                dib = small;
            }
            if (dib) {
                e.isImg = 1; e.hbmp = dib; e.iw = sw; e.ih = sh;
                /* return 1 = list owns dib now: must NOT delete it */
                if (ClipPrepend(&e)) {
                    if (g_viewClip)
                        InvalidateRect(g_hwnd, NULL, FALSE);
                } else {
                    DeleteObject(dib);
                }
            }
        }
    }
    CloseClipboard();
}

/* paste history entry into the focused app (clipboard + Ctrl+V) */
static void RunMacro(int mods, UINT vk);
static void PasteClip(int histIdx) {
    ClipEnt *e;
    HGLOBAL hmem;
    if (histIdx < 0 || histIdx >= g_nclip) return;
    e = &g_clip[histIdx];
    g_ownClip = 1;
    if (OpenClipboard(g_hwnd)) {
        EmptyClipboard();
        if (e->isImg && e->hbmp) {
            hmem = BitmapToDibMem(e->hbmp);
            if (hmem) SetClipboardData(CF_DIB, hmem);
        } else if (e->text) {
            size_t n = (wcslen(e->text) + 1) * sizeof(wchar_t);
            hmem = GlobalAlloc(GMEM_MOVEABLE, n);
            if (hmem) {
                void *p = GlobalLock(hmem);
                if (p) { memcpy(p, e->text, n); GlobalUnlock(hmem); }
                SetClipboardData(CF_UNICODETEXT, hmem);
            }
        }
        CloseClipboard();
    }
    g_ownClip = 0;
    RunMacro(0x2, 'V');
}

/* ---------------- focus restore (first-tap fix) ---------------- */
/* Why the first tap used to vanish: clicking Search in the browser, then
 * launching/clicking FloatKeys moves Win32 foreground to Explorer / Start /
 * taskbar. WS_EX_NOACTIVATE correctly keeps THAT focus, so SendInput lands
 * outside the browser until the user clicks Search a second time. The
 * built-in touch keyboard avoids this because its launcher never steals
 * focus. Fix: remember the last real typing target and restore it before
 * every injection. */
static BOOL IsShellChrome(HWND h) {
    wchar_t cls[64];
    if (!h) return TRUE;
    if (!GetClassNameW(h, cls, 64)) return FALSE;
    if (!wcscmp(cls, L"Shell_TrayWnd")) return TRUE;  /* taskbar */
    if (!wcscmp(cls, L"Progman")) return TRUE;        /* desktop */
    if (!wcscmp(cls, L"WorkerW")) return TRUE;        /* desktop */
    if (!wcscmp(cls, L"FloatKeys")) return TRUE;      /* ourselves */
    if (h == g_hwnd) return TRUE;
    {
        LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
        if (ex & WS_EX_NOACTIVATE) return TRUE;       /* other overlays */
    }
    /* Start menu / Search host are UWP CoreWindows owned by shell
     * processes; real UWP apps (Settings, etc.) are valid targets. */
    if (!wcscmp(cls, L"Windows.UI.Core.CoreWindow") ||
        !wcscmp(cls, L"XamlExplorerHostIslandWindow") ||
        !wcscmp(cls, L"DV2ControlHost")) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid) {
            HANDLE pr = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                    FALSE, pid);
            if (pr) {
                wchar_t img[MAX_PATH] = { 0 };
                DWORD n = MAX_PATH;
                BOOL shell = FALSE;
                if (QueryFullProcessImageNameW(pr, 0, img, &n)) {
                    wchar_t *p;
                    for (p = img; *p; p++)
                        *p = (wchar_t)towlower(*p);
                    if (wcsstr(img, L"startmenu") ||
                        wcsstr(img, L"searchhost") ||
                        wcsstr(img, L"shellexperience") ||
                        wcsstr(img, L"textinputhost") ||
                        wcsstr(img, L"searchui"))
                        shell = TRUE;
                }
                CloseHandle(pr);
                if (shell) return TRUE;
                return FALSE; /* real UWP app: valid target */
            }
        }
        return TRUE; /* can't query: err on shell side at startup only */
    }
    return FALSE;
}

static void TrackForeground(HWND fg) {
    wchar_t cls[64];
    if (!fg || fg == g_hwnd) return;
    if (!IsWindow(fg) || !IsWindowVisible(fg)) return;
    if (IsShellChrome(fg)) return;  /* launcher chrome: keep old target */
    if (IsIconic(fg)) return;
    /* While the startup launcher (Explorer double-click) is still
     * foreground, ignore Explorer re-activation noise so the browser
     * found beneath it isn't overwritten before the first tap. */
    if (g_startupFg && GetClassNameW(fg, cls, 64) &&
        (!wcscmp(cls, L"CabinetWClass") ||
         !wcscmp(cls, L"ExploreWClass")))
        return;
    g_lastFg = fg;
}

static void CALLBACK FgHookProc(HWINEVENTHOOK hook, DWORD ev, HWND hwnd,
                                LONG idObj, LONG idChild,
                                DWORD tid, DWORD t) {
    (void)hook; (void)tid; (void)t;
    if (ev == EVENT_SYSTEM_FOREGROUND &&
        idObj == OBJID_WINDOW && idChild == CHILDID_SELF)
        TrackForeground(hwnd);
}

/* SetForegroundWindow from a NOACTIVATE background process is blocked by
 * the foreground lock; AttachThreadInput bridges it. */
static void ForceForeground(HWND target) {
    HWND fg;
    DWORD fgThread, tgtThread, ourThread;
    BOOL aFg = FALSE, aTg = FALSE;
    if (!IsWindow(target)) return;
    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
    fg = GetForegroundWindow();
    if (fg == target) return;
    ourThread = GetCurrentThreadId();
    fgThread = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    tgtThread = GetWindowThreadProcessId(target, NULL);
    if (fgThread && fgThread != ourThread)
        aFg = AttachThreadInput(ourThread, fgThread, TRUE);
    if (tgtThread && tgtThread != ourThread && tgtThread != fgThread)
        aTg = AttachThreadInput(ourThread, tgtThread, TRUE);
    if (fgThread && tgtThread && fgThread != tgtThread)
        AttachThreadInput(fgThread, tgtThread, TRUE);
    SetForegroundWindow(target);
    if (fgThread && tgtThread && fgThread != tgtThread)
        AttachThreadInput(fgThread, tgtThread, FALSE);
    if (aTg) AttachThreadInput(ourThread, tgtThread, FALSE);
    if (aFg) AttachThreadInput(ourThread, fgThread, FALSE);
}

/* Called right before every SendInput: if focus sits on our keyboard,
 * nowhere, or shell chrome, jump back to the typing target first.
 * Otherwise the current window IS what the user wants: remember it. */
static void EnsureFocus(void) {
    HWND fg = GetForegroundWindow();
    if (fg == g_hwnd) {
        if (IsWindow(g_lastFg)) {
            ForceForeground(g_lastFg);
            Sleep(30); /* activation is async; let it settle */
        }
        return;
    }
    if (fg && !IsShellChrome(fg) && IsWindowVisible(fg)) {
        /* User intentionally moved to another app.
         * Exception: right after startup the launcher (Explorer double-
         * click / Start) is still foreground; the remembered window
         * beneath it is the real target until focus visibly moves. */
        if (g_startupFg && fg == g_startupFg && IsWindow(g_lastFg) &&
            g_lastFg != fg) {
            ForceForeground(g_lastFg);
            Sleep(30);
            return;
        }
        g_lastFg = fg;
        g_startupFg = NULL; /* focus moved on its own: startup phase over */
        return;
    }
    /* fg is NULL / invisible / shell chrome: restore last target. */
    if (IsWindow(g_lastFg)) {
        ForceForeground(g_lastFg);
        Sleep(30);
    }
}

typedef struct { HWND skip; HWND found; } FindData;
static BOOL CALLBACK FindPrevFg(HWND h, LPARAM p) {
    FindData *d = (FindData *)p;
    RECT r;
    LONG ex;
    wchar_t cls[64];
    if (h == d->skip || h == g_hwnd) return TRUE;
    if (!IsWindowVisible(h)) return TRUE;
    if (IsIconic(h)) return TRUE;
    if (IsShellChrome(h)) return TRUE;
    GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"CabinetWClass") || !wcscmp(cls, L"ExploreWClass"))
        return TRUE; /* launcher Explorer window: look beneath it */
    ex = GetWindowLongW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    GetWindowRect(h, &r);
    if (r.right <= r.left || r.bottom <= r.top) return TRUE;
    d->found = h;
    return FALSE; /* EnumWindows is top-down: first hit wins */
}

/* At startup foreground is the launcher, not the browser search box the
 * user just clicked. Peek beneath it so the very first keytap lands right. */
static void RestoreStartupFocus(void) {
    HWND fg = GetForegroundWindow();
    g_startupFg = fg;
    if (!fg || fg == g_hwnd || IsShellChrome(fg)) {
        FindData d;
        d.skip = fg;
        d.found = NULL;
        EnumWindows(FindPrevFg, (LPARAM)&d);
        if (d.found) {
            g_lastFg = d.found;
            ForceForeground(d.found);
            Sleep(30);
        } else if (fg && !IsShellChrome(fg)) {
            g_lastFg = fg;
        }
        return;
    }
    {
        /* fg is a normal window: usually the launcher Explorer when the
         * user double-clicked the exe. Look beneath it for the app that
         * was focused before (the browser); keep Explorer as fallback. */
        wchar_t cls[64];
        GetClassNameW(fg, cls, 64);
        if (!wcscmp(cls, L"CabinetWClass") ||
            !wcscmp(cls, L"ExploreWClass")) {
            FindData d;
            d.skip = fg;
            d.found = NULL;
            EnumWindows(FindPrevFg, (LPARAM)&d);
            if (d.found) {
                g_lastFg = d.found;
                ForceForeground(d.found);
                Sleep(30);
                return;
            }
        }
        g_lastFg = fg;
        g_startupFg = NULL; /* already on a real target */
    }
}

/* ---------------- pin window (PinTop core, built in) ------------------ */
/* Toggle always-on-top on the current target window (the app you type
 * into). A 1s repair timer re-asserts TOPMOST and drops closed windows.
 * Handles are runtime-only: nothing is persisted, all unpinned on exit. */
#define MAXPINWIN 8
static void InvKey(int idx);   /* forward: defined with the layout code */
static HWND g_pinwins[MAXPINWIN];
static int g_npinwins = 0;
#define TIMER_PIN 6

static int IsPinnable(HWND h) {
    wchar_t cls[64];
    if (!h || h == g_hwnd || !IsWindow(h) || !IsWindowVisible(h)) return 0;
    GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd") ||
        !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
        !wcscmp(cls, L"FloatKeys"))
        return 0;
    return 1;
}
static int PinIndex(HWND h) {
    int i;
    for (i = 0; i < g_npinwins; i++)
        if (g_pinwins[i] == h) return i;
    return -1;
}
/* window the Pin button acts on: last typing target, else foreground */
static HWND PinTarget(void) {
    if (IsWindow(g_lastFg) && IsPinnable(g_lastFg)) return g_lastFg;
    {
        HWND fg = GetForegroundWindow();
        if (IsPinnable(fg)) return fg;
    }
    return NULL;
}
static int CurTargetPinned(void) {
    return (g_lastFg && IsWindow(g_lastFg) && PinIndex(g_lastFg) >= 0);
}
static void InvPinBtn(void) {
    int i;
    for (i = 0; i < g_nkeys; i++)
        if (g_keys[i].kind == K_SET && g_keys[i].vk == SET_PINWIN) InvKey(i);
}
static void TogglePinWindow(void) {
    HWND t = PinTarget();
    int i;
    if (!t) return;
    i = PinIndex(t);
    if (i >= 0) {
        SetWindowPos(t, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        for (; i < g_npinwins - 1; i++) g_pinwins[i] = g_pinwins[i + 1];
        g_npinwins--;
    } else {
        if (g_npinwins >= MAXPINWIN) return;
        if (IsIconic(t)) ShowWindow(t, SW_RESTORE);
        SetWindowPos(t, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        g_pinwins[g_npinwins++] = t;
    }
    InvPinBtn();
}

/* ---------------- state logic ---------------- */
/* repaint helpers (defined in layout section; change-only to avoid flicker) */
static void InvKey(int idx);
static void InvMod(int m);
static void InvCaps(void);
static void InvBar(int id);
static void ComputeLayout(int cw, int ch);
static void BuildKeys(void);
static void ResizeForScale(void);
static void ApplyOpacity(void);
static int BarH(void);
static void SaveSettings(void);
static void InvSugg(void);
static void RunMacro(int mods, UINT vk);
static void ShcapRefresh(void);
static void RebuildKeys(void);
static BOOL ComboActive(void) {
    return g_mods[MX_CTRL] != M_OFF || g_mods[MX_ALT] != M_OFF || g_mods[MX_WIN] != M_OFF;
}
static void ConsumeHeld(void) {
    int i;
    for (i = 0; i < 4; i++)
        if (g_mods[i] == M_HELD) { vk_up(MODVK[i]); g_mods[i] = M_OFF; InvMod(i); }
}
static BOOL ShiftedFor(wchar_t lo) {
    BOOL caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
    BOOL sh = g_mods[MX_SHIFT] != M_OFF;
    if ((lo >= L'a' && lo <= L'z') || (lo >= L'A' && lo <= L'Z'))
        return caps != sh;
    return sh;
}
static void EmitText(const wchar_t *s) {
    int ss = g_mods[MX_SHIFT];
    BOOL sdn = ss != M_OFF, combo = ComboActive();
    const wchar_t *p;
    int commit = 0;
    EnsureFocus();
    g_autoActive = 0;   /* fresh typing cancels a pending autocorrect-undo */
    if (s[0] && !s[1] && !combo && IsCommitChar(s[0]) && g_wordLen >= 3)
        commit = 1;
    if (sdn && !combo) vk_up(VK_SHIFT);
    if (commit) AutoCorrectNow(1);   /* re-arms g_autoActive when it fixes */
    type_unicode(s);
    for (p = s; *p; p++) WordChar(*p);
    if (sdn && !combo) {
        if (ss == M_LOCKED) vk_down(VK_SHIFT);
        else if (g_mods[MX_SHIFT] != M_OFF) { g_mods[MX_SHIFT] = M_OFF; InvMod(MX_SHIFT); }
    }
    ConsumeHeld();
    InvSugg();
}
static void EmitVk(UINT vk) {
    int i;
    EnsureFocus();
    if (vk == VK_BACK && g_autoActive) {
        /* phone-style undo: one Backspace reverts the autocorrection */
        int n = g_autoCorrLen + (g_autoTrail ? 1 : 0);
        g_autoActive = 0;
        for (i = 0; i < n; i++) vk_tap(VK_BACK);
        type_unicode(g_autoOrig);
        {
            const wchar_t *p;
            for (p = g_autoOrig; *p; p++) WordChar(*p);
        }
        ConsumeHeld();
        InvSugg();
        return;
    }
    g_autoActive = 0;
    if (vk == VK_RETURN && !ComboActive() && g_wordLen >= 3)
        AutoCorrectNow(0);   /* autocorrect on Enter (no trailing space) */
    vk_tap(vk);
    if (vk == VK_BACK) WordChop();
    else if (vk == VK_RETURN) WordCommit();   /* enter commits the word too */
    else if (vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU && vk != VK_LWIN)
        WordClear();
    ConsumeHeld();
    InvSugg();
}

/* plain character as a REAL key event (not UNICODE): web apps like YouTube
 * match shortcuts on keydown `code`, which UNICODE (VK_PACKET, keyCode 229)
 * lacks — that's why arrows worked but letters (f/k/m/...) didn't.
 * Physical Shift/Caps state already mirrors the hi/lo choice, so a bare VK
 * tap yields exactly ch. Falls back to UNICODE when unmappable. */
static void EmitVkChar(UINT vk, wchar_t ch) {
    wchar_t s[2];
    if (!vk) {
        s[0] = ch; s[1] = 0;
        EmitText(s);
        return;
    }
    EnsureFocus();
    g_autoActive = 0;
    if (IsCommitChar(ch) && g_wordLen >= 3) AutoCorrectNow(1);
    vk_tap(vk);
    WordChar(ch);
    ConsumeHeld();
    InvSugg();
}

/* emit one character: VK combo when Ctrl/Alt/Win held, else real VK tap */
static void EmitCharChoice(wchar_t lo, wchar_t ch) {
    UINT vk;
    wchar_t s[2];
    if (ComboActive()) {
        vk = char_to_vk(lo);
        if (vk) { EmitVk(vk); return; }
    } else {
        EmitVkChar(ch == L' ' ? VK_SPACE : char_to_vk(lo), ch);
        return;
    }
    s[0] = ch; s[1] = 0;
    EmitText(s);
}

/* one-tap date/time insert (DD/MM/YYYY, HH:MM local) */
static void FormatDateTime(int which, wchar_t *out) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (which == 0)
        wsprintfW(out, L"%02d/%02d/%04d", st.wDay, st.wMonth, st.wYear);
    else
        wsprintfW(out, L"%02d:%02d", st.wHour, st.wMinute);
}
static void InsertDateTime(int which) {
    wchar_t s[16];
    const wchar_t *p;
    FormatDateTime(which, s);
    EnsureFocus();
    g_autoActive = 0;
    type_unicode(s);
    for (p = s; *p; p++) WordChar(*p);
    ConsumeHeld();
    InvSugg();
}
/* one-tap shortcut macro: hold its modifiers, tap key, release.
 * Small sleeps matter: apps read async modifier state when pumping messages,
 * so releasing instantly would look like a bare keypress. */
static void RunMacro(int mods, UINT vk) {
    static const int seq[4] = { 1, 0, 2, 3 }; /* ctrl, shift, alt, win */
    int i;
    EnsureFocus();
    g_autoActive = 0;   /* paste/shortcut output desyncs the word tracker */
    for (i = 0; i < 4; i++)
        if (mods & (1 << seq[i])) vk_down(MODVK[seq[i]]);
    Sleep(30);
    vk_tap(vk);
    Sleep(30);
    for (i = 3; i >= 0; i--)
        if (mods & (1 << seq[i])) vk_up(MODVK[seq[i]]);
    ConsumeHeld();
}

static void DoSetAction(int a) {
    switch (a) {
        case SET_SM:
            if (g_scale > 0.75f) { g_scale -= 0.1f; ResizeForScale(); }
            break;
        case SET_BG:
            if (g_scale < 1.5f) { g_scale += 0.1f; ResizeForScale(); }
            break;
        case SET_OPDN:
            g_opacity -= 13; if (g_opacity < 102) g_opacity = 102;
            ApplyOpacity(); break;
        case SET_OPUP:
            g_opacity += 13; if (g_opacity > 255) g_opacity = 255;
            ApplyOpacity(); break;
        case SET_DONE:
            g_showSettings = 0;
            ResizeForScale();
            break;
        case SET_LANG: {
            RECT cl;
            int other = g_lang == 0 ? 1 : 0;
            if (DictAvail(other)) g_lang = other;
            else if (!DictAvail(g_lang)) break;
            g_prevWord[0] = 0; g_prevIdx = -1;   /* bigram indices are per-lang */
            g_sugg[0][0] = g_sugg[1][0] = g_sugg[2][0] = 0; g_misspelt = 0;
            WordClear();
            BuildKeys();
            GetClientRect(g_hwnd, &cl);
            ComputeLayout(cl.right, cl.bottom);
            InvalidateRect(g_hwnd, NULL, TRUE);
            break;
        }
        case SET_PRED:
            g_predictOn = !g_predictOn;
            g_sugg[0][0] = g_sugg[1][0] = g_sugg[2][0] = 0; g_misspelt = 0;
            ComputeSugg();
            ResizeForScale();
            break;
        case SET_AUTO:
            g_autocorrect = !g_autocorrect;
            g_autoActive = 0;
            break;
        case SET_SPELL:
            g_highlightOn = !g_highlightOn;
            g_sugg[0][0] = g_sugg[1][0] = g_sugg[2][0] = 0; g_misspelt = 0;
            ComputeSugg();
            ResizeForScale();
            break;
        case SET_PINWIN:
            TogglePinWindow();
            break;
        case SET_BIGRAM:
            g_bigramOn = !g_bigramOn;
            g_sugg[0][0] = g_sugg[1][0] = g_sugg[2][0] = 0; g_misspelt = 0;
            ComputeSugg();
            InvSugg();
            break;
        case SET_PINBAR:
            g_pinBarOn = !g_pinBarOn;
            ResizeForScale();
            break;
    }
    SaveSettings();
}

/* tap a suggestion: erase prefix, type word + space.
 * Slot 0 with no completion but a red (misspelt) literal accepts it as-is. */
static void CommitSugg(int slot) {
    wchar_t w[MAXWLEN + 1];
    int i, n, cap = 0;
    g_autoActive = 0;
    if (slot == 0 && g_highlightOn && g_misspelt && g_wordLen > 0) {
        EmitText(L" ");   /* keep my typing, just finish the word */
        return;
    }
    if (slot < 0 || slot > 2 || !g_sugg[slot][0]) return;
    wcscpy(w, g_sugg[slot]);
    if (g_wordLen > 0 && g_wordBuf[0] >= L'A' && g_wordBuf[0] <= L'Z') cap = 1;
    if (cap && w[0] >= L'a' && w[0] <= L'z') w[0] -= 32;
    n = g_wordLen;
    for (i = 0; i < n; i++) EmitVk(VK_BACK);
    EmitText(w);
    EmitText(L" ");
}

static void InvSugg(void) {
    int i;
    if (!(g_predictOn || g_highlightOn) || !DictAvail(g_lang)) return;
    for (i = 0; i < g_nkeys; i++)
        if (g_keys[i].kind == K_SUGG) InvKey(i);
}

/* suggestion strip is only laid out while it shows something, so a bare
 * pin strip is never followed by three empty boxes (symmetric look) */
static int g_suggRowOn = -1;
static int SuggRowHasContent(void) {
    if (g_sugg[0][0] || g_sugg[1][0] || g_sugg[2][0]) return 1;
    if (g_highlightOn && g_misspelt && g_wordLen > 0) return 1;
    return 0;
}
static int SuggRowOn(void) {
    return (g_predictOn || g_highlightOn) && DictAvail(g_lang) &&
           SuggRowHasContent();
}
static void SyncSuggRow(void) {
    int want = SuggRowOn() ? 1 : 0;
    if (want == g_suggRowOn) return;
    g_suggRowOn = want;
    if (g_hwnd && IsWindow(g_hwnd)) ResizeForScale();
}
/* merged Shift+Caps key (compact): 1 tap = Shift one-shot, 2 quick taps = CapsLock */
static void ShcapRefresh(void) {
    int i;
    for (i = 0; i < g_nkeys; i++)
        if (g_keys[i].kind == K_SHCAP) InvKey(i);
}
static void ShcapPress(void) {
    if (g_shcapArmed) {
        g_shcapArmed = 0;
        KillTimer(g_hwnd, TIMER_SHCAP);
        if (g_mods[MX_SHIFT] != M_OFF) {
            vk_up(VK_SHIFT);
            g_mods[MX_SHIFT] = M_OFF;
        }
        vk_tap(VK_CAPITAL);
        InvMod(MX_SHIFT);
        InvCaps();
        ShcapRefresh();
        return;
    }
    if (g_mods[MX_SHIFT] != M_OFF) {
        vk_up(VK_SHIFT);
        g_mods[MX_SHIFT] = M_OFF;
        InvMod(MX_SHIFT);
        ShcapRefresh();
        return;
    }
    g_mods[MX_SHIFT] = M_HELD;
    vk_down(VK_SHIFT);
    InvMod(MX_SHIFT);
    ShcapRefresh();
    g_shcapArmed = 1;
    SetTimer(g_hwnd, TIMER_SHCAP, 350, NULL);
}

/* normal quick-tap of a deferred char key */
static void FlushPending(void) {
    int idx = g_pending;
    g_pending = -1;
    KillTimer(g_hwnd, TIMER_HOLD);
    if (idx < 0 || idx >= g_nkeys) return;
    {
        Key *k = &g_keys[idx];
        if (k->kind == K_CHAR)
            EmitCharChoice(k->lo, ShiftedFor(k->lo) ? k->hi : k->lo);
    }
}

/* hold finished: show [base|alt] picker above the key */
static void ShowPopup(int idx) {
    Key *k;
    wchar_t alt;
    RECT cl;
    int cellW, cellH, cx, x0, y0, y1, cw;
    if (idx < 0 || idx >= g_nkeys) return;
    k = &g_keys[idx];
    if (k->kind != K_CHAR) return;
    alt = k->tag ? k->tag : (k->hi != k->lo ? k->hi : 0);
    if (!alt) return; /* nothing to choose */
    GetClientRect(g_hwnd, &cl);
    cw = cl.right;
    g_popBase = k->lo; g_popAlt = alt;
    g_holdPopup = idx; g_popHover = -1;
    cellW = k->rc.right - k->rc.left;
    if (cellW < 52) cellW = 52;
    cellH = k->rc.bottom - k->rc.top;
    cx = (k->rc.left + k->rc.right) / 2;
    x0 = cx - cellW;
    if (x0 < 6) x0 = 6;
    if (x0 + cellW * 2 > cw - 6) x0 = cw - 6 - cellW * 2;
    y1 = k->rc.top - 4; y0 = y1 - cellH;
    if (y0 < BarH() + 2) { y0 = k->rc.bottom + 4; y1 = y0 + cellH; }
    SetRect(&g_popRc[0], x0, y0, x0 + cellW, y1);
    SetRect(&g_popRc[1], x0 + cellW, y0, x0 + cellW * 2, y1);
    g_popAll = g_popRc[0];
    UnionRect(&g_popAll, &g_popAll, &g_popRc[1]);
    InflateRect(&g_popAll, 2, 2);
    InvalidateRect(g_hwnd, &g_popAll, FALSE);
}

/* finger/mouse released: emit hovered choice (or normal tap) */
static void ResolvePopup(void) {
    int idx = g_holdPopup;
    wchar_t ch, s[2];
    g_holdPopup = -1; g_pending = -1;
    KillTimer(g_hwnd, TIMER_HOLD);
    if (idx < 0 || idx >= g_nkeys) return;
    {
        Key *k = &g_keys[idx];
        if (k->kind != K_CHAR) return;
        InvalidateRect(g_hwnd, &g_popAll, FALSE);
        if (g_popHover == 1) {
            s[0] = g_popAlt; s[1] = 0;
            EmitText(s);
            return;
        }
        ch = ShiftedFor(k->lo) ? k->hi : k->lo;
        EmitCharChoice(k->lo, ch);
    }
}

/* ---------------- auto-translate ID->EN (online) -------------------- */
/* Method (researched): free Google endpoint, no API key, via WinINet
 * (system lib, inherits IE/Edge proxy). Translated in a worker thread
 * so the keyboard never freezes; result is typed at the caret.
 *   GET https://clients5.google.com/translate_a/t?client=dict-chrome-ex
 *       &sl=id&tl=en&q=<url-encoded UTF-8>
 * Response is a JSON array: flat ["text"] or nested [[t,s,...],...];
 * the first string of each element concatenates to the translation. */
#define TR_MAXSRC 200
#define WM_APP_TRDONE (WM_APP + 7)

/* language pairs, cycled by the ⇄ button (all verified live) */
typedef struct { const char *sl, *tl; const wchar_t *tag; } TrPair;
static const TrPair g_trPairs[] = {
    { "id", "en", L"ID>EN" },
    { "en", "id", L"EN>ID" },
    { "id", "ms", L"ID>MS" },
    { "ms", "id", L"MS>ID" },
};
#define NTRPAIR (sizeof(g_trPairs) / sizeof(g_trPairs[0]))
static int g_trPair = 0;

static void InvRec(void);   /* forward: record/translate banner repaint */

static int g_viewTr = 0;        /* translate page (like g_viewClip) */
static int g_trArm = 0;         /* typing Indonesian on keys view */
static int g_trBusy = 0;        /* worker thread in flight */
static int g_trStatus = 0;      /* 0 idle, 1 busy, 2 done, 3 failed */
static wchar_t g_trBuf[TR_MAXSRC + 1];
static wchar_t g_trOut[128];    /* result preview */

/* percent-encode wchar string as UTF-8; returns bytes written (no NUL) */
static int UrlEncode(const wchar_t *src, char *dst, int dstN) {
    char utf8[TR_MAXSRC * 3 + 1];
    int ulen, i, o = 0;
    static const char *hex = "0123456789ABCDEF";
    ulen = WideCharToMultiByte(CP_UTF8, 0, src, -1, utf8, sizeof(utf8),
                               NULL, NULL);
    if (ulen <= 1) return 0;
    ulen--;   /* drop NUL */
    for (i = 0; i < ulen && o + 4 < dstN; i++) {
        unsigned char c = (unsigned char)utf8[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            dst[o++] = (char)c;
        } else {
            dst[o++] = '%';
            dst[o++] = hex[c >> 4];
            dst[o++] = hex[c & 15];
        }
    }
    return o;
}

/* collect translation segments from the JSON array body.
 * Handles flat ["text"] and nested [[t,s,...],...] shapes: a top-level
 * string counts while no sub-array was seen; inside, each sub-array's
 * first string counts (later strings are sources/metadata like "en").
 * Returns 1 when at least one segment was extracted. */
static int TrParseJson(const char *js, size_t len, wchar_t *out, int outN) {
    int depth = 0, elem[8], got = 0, o = 0, i, flat = 1;
    size_t u = 0;
    for (i = 0; i < 8; i++) elem[i] = 0;
    out[0] = 0;
    while (u < len && o + 1 < outN) {
        char c = js[u];
        if (c == '[') {
            if (depth == 1) flat = 0;   /* a nested array: not flat shape */
            if (depth < 7) { depth++; elem[depth] = 0; }
            u++;
        } else if (c == ']') {
            if (depth > 0) depth--;
            u++;
        } else if (c == '"') {
            int isFirst, take = 0;
            u++;   /* opening quote */
            isFirst = (elem[depth] == 0);
            elem[depth]++;
            if (depth == 1 && flat) take = 1;
            else if (depth >= 2 && isFirst) take = 1;
            if (take && o + 1 < outN) {
                /* a translation segment: decode with unescape */
                while (u < len && js[u] != '"' && o + 1 < outN) {
                    if (js[u] == '\\' && u + 1 < len) {
                        char e = js[u + 1];
                        if (e == 'u' && u + 5 < len) {
                            int k, v = 0, ok = 1;
                            for (k = 0; k < 4; k++) {
                                char h = js[u + 2 + k];
                                v <<= 4;
                                if (h >= '0' && h <= '9') v |= h - '0';
                                else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
                                else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
                                else ok = 0;
                            }
                            if (!ok) break;
                            u += 6;
                            if (v >= 0xD800 && v <= 0xDBFF && u + 5 < len &&
                                js[u] == '\\' && js[u + 1] == 'u') {
                                int k2, w = 0, ok2 = 1;   /* surrogate pair */
                                for (k2 = 0; k2 < 4; k2++) {
                                    char h = js[u + 2 + k2];
                                    w <<= 4;
                                    if (h >= '0' && h <= '9') w |= h - '0';
                                    else if (h >= 'a' && h <= 'f') w |= h - 'a' + 10;
                                    else if (h >= 'A' && h <= 'F') w |= h - 'A' + 10;
                                    else ok2 = 0;
                                }
                                if (ok2 && w >= 0xDC00 && w <= 0xDFFF &&
                                    o + 2 < outN) {
                                    out[o++] = (wchar_t)v;
                                    out[o++] = (wchar_t)w;
                                    u += 6;
                                    got = 1;
                                    continue;
                                }
                            }
                            out[o++] = (wchar_t)v;
                            got = 1;
                        } else {
                            if (e == 'n') out[o++] = L'\n';
                            else if (e == 't') out[o++] = L'\t';
                            else if (e == 'r') out[o++] = L'\r';
                            else out[o++] = (wchar_t)e;   /* " \\ / etc. */
                            u += 2;
                            got = 1;
                        }
                    } else {
                        out[o++] = (wchar_t)(unsigned char)js[u++];
                        got = 1;
                    }
                }
                if (u < len && js[u] == '"') u++;
            } else {
                /* skip non-translation string */
                while (u < len && js[u] != '"') {
                    if (js[u] == '\\' && u + 1 < len) u += 2;
                    else u++;
                }
                if (u < len) u++;
            }
        } else u++;
    }
    out[o] = 0;
    return got;
}

typedef struct { wchar_t text[TR_MAXSRC + 1]; char sl[8], tl[8]; } TrJob;

static DWORD WINAPI TrThread(LPVOID param) {
    TrJob *job = (TrJob *)param;
    HINTERNET hs = NULL, hu = NULL;
    char enc[TR_MAXSRC * 9 + 1];
    char url[TR_MAXSRC * 9 + 256];
    char *resp = NULL;
    DWORD cap = 0, len = 0, rd = 0;
    int enclen, ok = 0;
    wchar_t *wout = NULL;
    enclen = UrlEncode(job->text, enc, sizeof(enc));
    enc[enclen] = 0;
    if (!enclen) goto done;
    wsprintfA(url, "https://clients5.google.com/translate_a/t"
                   "?client=dict-chrome-ex&sl=%s&tl=%s&q=%s",
              job->sl, job->tl, enc);
    hs = InternetOpenW(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                       L"AppleWebKit/537.36 (KHTML, like Gecko) "
                       L"Chrome/120.0 Safari/537.36",
                       INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hs) goto done;
    {
        wchar_t wurl[4096];
        int n = MultiByteToWideChar(CP_ACP, 0, url, -1, wurl, 4096);
        if (n <= 0) goto done;
        hu = InternetOpenUrlW(hs, wurl, NULL, 0,
                              INTERNET_FLAG_RELOAD |
                              INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (!hu) goto done;
    }
    cap = 16384;
    resp = (char *)malloc(cap);
    if (!resp) goto done;
    for (;;) {
        if (len + 4096 > cap) break;
        if (!InternetReadFile(hu, resp + len, 4096, &rd) || rd == 0) break;
        len += rd;
    }
    if (len > 0) {
        wout = (wchar_t *)malloc(2001 * sizeof(wchar_t));
        if (wout && TrParseJson(resp, len, wout, 2001) && wout[0])
            ok = 1;
        else if (wout) { free(wout); wout = NULL; }
    }
done:
    if (resp) free(resp);
    if (hu) InternetCloseHandle(hu);
    if (hs) InternetCloseHandle(hs);
    free(job);
    /* wout ownership passes to the UI thread (NULL = failed) */
    PostMessageW(g_hwnd, WM_APP_TRDONE, (WPARAM)ok, (LPARAM)wout);
    return 0;
}

static void InvTr(void) {
    int i;
    for (i = 0; i < g_nkeys; i++)
        if (g_keys[i].row == 50 || g_keys[i].row == 51) InvKey(i);
}

/* two-letter source code of the active pair ("ID", "EN", "MS") */
static void TrSrcTag(wchar_t *out) {
    int p = g_trPair;
    if (p < 0 || p >= (int)NTRPAIR) p = 0;
    out[0] = g_trPairs[p].tag[0];
    out[1] = g_trPairs[p].tag[1];
    out[2] = 0;
}

static void TrGo(void) {
    TrJob *job;
    HANDLE th;
    if (g_trBusy || !g_trBuf[0]) return;
    job = (TrJob *)malloc(sizeof(TrJob));
    if (!job) return;
    wcscpy(job->text, g_trBuf);
    {
        int p = g_trPair;
        if (p < 0 || p >= (int)NTRPAIR) { p = 0; g_trPair = 0; }
        strcpy(job->sl, g_trPairs[p].sl);
        strcpy(job->tl, g_trPairs[p].tl);
    }
    g_trBusy = 1; g_trStatus = 1;
    InvTr();
    th = CreateThread(NULL, 0, TrThread, job, 0, NULL);
    if (!th) { free(job); g_trBusy = 0; g_trStatus = 3; InvTr(); return; }
    CloseHandle(th);
}

/* keystrokes while typing Indonesian on the keys view: swallowed into
 * the translate buffer, nothing is sent. Returns 1 when handled. */
static int TrKey(int idx) {
    Key *k = &g_keys[idx];
    if (k->kind == K_MOD) {
        int m = k->mod;
        g_mods[m] = (g_mods[m] == M_OFF) ? M_HELD : M_OFF;
        InvMod(m);
        return 1;
    }
    if (k->kind == K_SHCAP) {
        g_mods[MX_SHIFT] = (g_mods[MX_SHIFT] == M_OFF) ? M_HELD : M_OFF;
        InvMod(MX_SHIFT); ShcapRefresh();
        return 1;
    }
    if (k->kind == K_CHAR) {
        size_t L = wcslen(g_trBuf);
        if (L < TR_MAXSRC) {
            g_trBuf[L] = ShiftedFor(k->lo) ? k->hi : k->lo;
            g_trBuf[L + 1] = 0;
            g_trStatus = 0;
            InvRec();
        }
        return 1;
    }
    if (k->kind == K_SPACE) {
        size_t L = wcslen(g_trBuf);
        if (L < TR_MAXSRC && L > 0) {
            g_trBuf[L] = L' '; g_trBuf[L + 1] = 0;
            g_trStatus = 0;
            InvRec();
        }
        return 1;
    }
    if (k->kind == K_SPECIAL) {
        if (k->vk == VK_BACK) {
            size_t L = wcslen(g_trBuf);
            if (L) { g_trBuf[L - 1] = 0; g_trStatus = 0; InvRec(); }
        } else if (k->vk == VK_RETURN || k->vk == VK_ESCAPE) {
            int i;
            g_trArm = 0;
            for (i = 0; i < 4; i++)
                if (g_mods[i] != M_OFF) { g_mods[i] = M_OFF; InvMod(i); }
            g_viewTr = 1;
            ResizeForScale();
        }
        return 1;
    }
    return 1;
}

static void ToggleMod(int m) {
    if (g_mods[m] == M_OFF) { g_mods[m] = M_HELD; vk_down(MODVK[m]); }
    else if (g_mods[m] == M_HELD) g_mods[m] = M_LOCKED;
    else { g_mods[m] = M_OFF; vk_up(MODVK[m]); }
    InvMod(m);
}

/* ---------------- built-in calculator (offline) --------------------- */
/* Expression grammar: + - * / % ^, parens, decimals, unary minus.
 * ^ is right-associative. Pure function: fully unit-testable. */
typedef struct { const wchar_t *p; int err; } CParser;

static void c_skip(CParser *c) {
    while (*c->p == L' ' || *c->p == L'\t') c->p++;
}
static double c_expr(CParser *c);
static double c_number(CParser *c) {
    double v = 0, f = 0.1;
    int got = 0;
    c_skip(c);
    while (*c->p >= L'0' && *c->p <= L'9') {
        v = v * 10 + (*c->p - L'0'); c->p++; got = 1;
    }
    if (*c->p == L'.') {
        c->p++;
        while (*c->p >= L'0' && *c->p <= L'9') {
            v += (*c->p - L'0') * f; f *= 0.1; c->p++; got = 1;
        }
    }
    if (!got) c->err = 1;
    return v;
}
static double c_base(CParser *c) {
    double v;
    c_skip(c);
    if (*c->p == L'(') {
        c->p++;
        v = c_expr(c);
        c_skip(c);
        if (*c->p == L')') c->p++;
        else c->err = 1;
        return v;
    }
    if (*c->p == L'-') { c->p++; return -c_base(c); }
    if (*c->p == L'+') { c->p++; return c_base(c); }
    return c_number(c);
}
static double c_factor(CParser *c) {
    double v = c_base(c);
    c_skip(c);
    if (*c->p == L'^') {
        double e;
        c->p++;
        e = c_factor(c);
        v = pow(v, e);
    }
    return v;
}
static double c_term(CParser *c) {
    double v = c_factor(c);
    for (;;) {
        c_skip(c);
        if (*c->p == L'*') { c->p++; v *= c_factor(c); }
        else if (*c->p == L'/') {
            double d;
            c->p++;
            d = c_factor(c);
            if (d == 0) { c->err = 1; return 0; }
            v /= d;
        } else if (*c->p == L'%') {
            double d;
            c->p++;
            d = c_factor(c);
            if (d == 0) { c->err = 1; return 0; }
            v = fmod(v, d);
        } else return v;
    }
}
static double c_expr(CParser *c) {
    double v = c_term(c);
    for (;;) {
        c_skip(c);
        if (*c->p == L'+') { c->p++; v += c_term(c); }
        else if (*c->p == L'-') { c->p++; v -= c_term(c); }
        else return v;
    }
}
static int CalcEval(const wchar_t *s, double *out) {
    CParser c;
    double v;
    if (!s || !*s) return 0;
    c.p = s; c.err = 0;
    v = c_expr(&c);
    c_skip(&c);
    if (c.err || *c.p || v != v || v == HUGE_VAL || v == -HUGE_VAL) return 0;
    *out = v;
    return 1;
}
static void FormatNum(double v, wchar_t *out) {
    if (v == 0) v = 0;   /* normalize -0 */
    swprintf(out, 32, L"%.10g", v);
}

static wchar_t g_calcBuf[64];
static wchar_t g_calcLive[32];
static int g_calcLiveOn = 0, g_calcErr = 0;
static int g_viewCalc = 0;   /* calculator page (like g_viewClip) */

static void InvCalc(void) {
    int i;
    for (i = 0; i < g_nkeys; i++)
        if (g_keys[i].row >= 40 && g_keys[i].row <= 45) InvKey(i);
}
static void CalcLive(void) {
    double v;
    g_calcLiveOn = CalcEval(g_calcBuf, &v);
    if (g_calcLiveOn) FormatNum(v, g_calcLive);
}
/* = key: insert result into the focused app, keep it for chaining */
static void CommitCalc(void) {
    double v;
    if (!CalcEval(g_calcBuf, &v)) {
        g_calcErr = 1; g_calcLiveOn = 0; InvCalc(); return;
    }
    {
        wchar_t res[32];
        const wchar_t *p;
        FormatNum(v, res);
        EnsureFocus();
        type_unicode(res);
        for (p = res; *p; p++) WordChar(*p);
        ConsumeHeld();
        InvSugg();
        wcscpy(g_calcBuf, res);
        g_calcErr = 0; g_calcLiveOn = 0;
        InvCalc();
    }
}
static void CalcKey(wchar_t ch) {
    if (ch == L'C') {
        g_calcBuf[0] = 0; g_calcErr = 0; g_calcLiveOn = 0; InvCalc(); return;
    }
    if (ch == L'B') {
        size_t L = wcslen(g_calcBuf);
        if (L) g_calcBuf[L - 1] = 0;
        g_calcErr = 0; CalcLive(); InvCalc(); return;
    }
    if (ch == L'=') { CommitCalc(); return; }
    if (g_calcErr) { g_calcBuf[0] = 0; g_calcErr = 0; }
    {
        size_t L = wcslen(g_calcBuf);
        if (L < 63) { g_calcBuf[L] = ch; g_calcBuf[L + 1] = 0; }
    }
    CalcLive(); InvCalc();
}

static BOOL IsRepeatable(UINT vk) {
    return vk==VK_BACK||vk==VK_DELETE||vk==VK_SPACE||
           vk==VK_LEFT||vk==VK_UP||vk==VK_DOWN||vk==VK_RIGHT||
           vk==VK_PRIOR||vk==VK_NEXT||vk==VK_HOME||vk==VK_END;
}

/* double-tap Backspace deletes the whole previous word (no need to hold).
 * UP handler stamps g_bkspLastUp; any other key cancels the window. */
#define BKSP_DBL_MS 400
static DWORD g_bkspLastUp = 0;

static int IsDoubleBksp(DWORD now) {
    return (g_bkspLastUp != 0 && now - g_bkspLastUp <= BKSP_DBL_MS);
}

static void DoubleBackspace(void) {
    int n, i;
    EnsureFocus();
    g_bkspLastUp = 0;   /* consume: triple-tap starts a fresh pair */
    n = g_wordLen;
    if (n > 31) n = 31;
    if (n > 0) {
        /* tracker knows the word: erase exactly the remaining chars
         * (first tap already removed one) */
        for (i = 0; i < n; i++) vk_tap(VK_BACK);
    } else {
        /* tracker empty/desynced: standard Ctrl+Backspace deletes prev word */
        if (g_mods[MX_CTRL] != M_OFF) {
            vk_tap(VK_BACK);   /* sticky Ctrl already down: this IS Ctrl+Bksp */
        } else {
            vk_down(VK_CONTROL);
            Sleep(15);
            vk_tap(VK_BACK);
            Sleep(15);
            vk_up(VK_CONTROL);
        }
    }
    WordClear();
    ConsumeHeld();
    InvSugg();
}

/* ---------------- custom shortcut recorder ---------------- */
/* Tap "+" on the SC page -> keyboard view captures the combo (nothing is
 * sent while armed), then you type a name with the keys themselves.
 * Enter saves (auto-name when empty), Esc / SC bar cancels. */
static void InvRec(void) {
    int i;
    for (i = 0; i < g_nkeys; i++)
        if (g_keys[i].kind == K_INFO) InvKey(i);
}

static void CancelRecord(void) {
    int i;
    g_recSlot = -1; g_recStep = 0; g_recMods = 0; g_recVk = 0;
    g_recName[0] = 0;
    /* capture/naming never sent anything, so just clear visual state */
    for (i = 0; i < 4; i++)
        if (g_mods[i] != M_OFF) { g_mods[i] = M_OFF; InvMod(i); }
}

static void StartRecord(int s) {
    int i;
    if (s < 0 || s >= MAXSCUT) return;
    /* release any sticky mods first so capture starts clean */
    for (i = 0; i < 4; i++)
        if (g_mods[i] != M_OFF) { vk_up(MODVK[i]); g_mods[i] = M_OFF; InvMod(i); }
    g_recSlot = s; g_recStep = 0; g_recMods = 0; g_recVk = 0;
    g_recName[0] = 0;
    g_viewSC = 0; g_scDelArm = 0; g_starArm = 0;
    ResizeForScale();   /* banner row appears */
}

static void KeyName(UINT vk, wchar_t *out) {
    if (vk >= 'A' && vk <= 'Z') { out[0] = (wchar_t)vk; out[1] = 0; return; }
    if (vk >= '0' && vk <= '9') { out[0] = (wchar_t)vk; out[1] = 0; return; }
    if (vk >= VK_F1 && vk <= VK_F12) {
        wsprintfW(out, L"F%d", vk - VK_F1 + 1); return;
    }
    switch (vk) {
        case VK_TAB: wcscpy(out, L"Tab"); return;
        case VK_RETURN: wcscpy(out, L"Ent"); return;
        case VK_ESCAPE: wcscpy(out, L"Esc"); return;
        case VK_BACK: wcscpy(out, L"Bksp"); return;
        case VK_DELETE: wcscpy(out, L"Del"); return;
        case VK_SPACE: wcscpy(out, L"Spc"); return;
        case VK_LEFT: wcscpy(out, L"Lt"); return;
        case VK_UP: wcscpy(out, L"Up"); return;
        case VK_RIGHT: wcscpy(out, L"Rt"); return;
        case VK_DOWN: wcscpy(out, L"Dn"); return;
        case VK_CAPITAL: wcscpy(out, L"Caps"); return;
        case VK_PRIOR: wcscpy(out, L"PgUp"); return;
        case VK_NEXT: wcscpy(out, L"PgDn"); return;
        case VK_HOME: wcscpy(out, L"Home"); return;
        case VK_END: wcscpy(out, L"End"); return;
        case VK_MEDIA_PLAY_PAUSE: wcscpy(out, L"Play"); return;
        case VK_MEDIA_PREV_TRACK: wcscpy(out, L"Prev"); return;
        case VK_MEDIA_NEXT_TRACK: wcscpy(out, L"Next"); return;
        case VK_VOLUME_MUTE: wcscpy(out, L"Mute"); return;
        case VK_VOLUME_UP: wcscpy(out, L"Vol+"); return;
        case VK_VOLUME_DOWN: wcscpy(out, L"Vol-"); return;
        case VK_SNAPSHOT: wcscpy(out, L"Prt"); return;
        case VK_APPS: wcscpy(out, L"Menu"); return;
        case VK_OEM_1: wcscpy(out, L";"); return;
        case VK_OEM_PLUS: wcscpy(out, L"="); return;
        case VK_OEM_COMMA: wcscpy(out, L","); return;
        case VK_OEM_MINUS: wcscpy(out, L"-"); return;
        case VK_OEM_PERIOD: wcscpy(out, L"."); return;
        case VK_OEM_2: wcscpy(out, L"/"); return;
        case VK_OEM_3: wcscpy(out, L"`"); return;
        case VK_OEM_4: wcscpy(out, L"["); return;
        case VK_OEM_5: wcscpy(out, L"\\"); return;
        case VK_OEM_6: wcscpy(out, L"]"); return;
        case VK_OEM_7: wcscpy(out, L"'"); return;
        default: wsprintfW(out, L"%02X", vk & 0xFF); return;
    }
}

/* default slot name from the combo: C+S+A+W+Key */
static void AutoName(int mods, UINT vk, wchar_t *out) {
    wchar_t kn[8];
    int p = 0, i;
    if (mods & 0x2) { out[p++] = L'C'; out[p++] = L'+'; }
    if (mods & 0x1) { out[p++] = L'S'; out[p++] = L'+'; }
    if (mods & 0x4) { out[p++] = L'A'; out[p++] = L'+'; }
    if (mods & 0x8) { out[p++] = L'W'; out[p++] = L'+'; }
    KeyName(vk, kn);
    for (i = 0; kn[i] && p < 9; i++) out[p++] = kn[i];
    out[p] = 0;
}

static void CommitRecord(void) {
    int s = g_recSlot;
    wchar_t nm[10];
    int a = 0, b = (int)wcslen(g_recName);
    if (s < 0 || s >= MAXSCUT || !g_recVk) return;
    while (a < b && g_recName[a] == L' ') a++;
    while (b > a && g_recName[b - 1] == L' ') b--;
    if (b > a) {
        if (b - a > 9) b = a + 9;
        wcsncpy(nm, g_recName + a, b - a);
        nm[b - a] = 0;
    } else AutoName(g_recMods, g_recVk, nm);
    g_sc[s].used = 1;
    g_sc[s].mods = g_recMods & 0xF;
    g_sc[s].vk = g_recVk;
    wcsncpy(g_sc[s].name, nm, 9);
    g_sc[s].name[9] = 0;
    CancelRecord();
    g_viewSC = 1;
    SaveSettings();
    RebuildKeys();      /* slot labels changed */
    ResizeForScale();
}

/* swallows the key while recording; 1 = handled, key must not be sent */
static int CaptureKey(int idx) {
    Key *k = &g_keys[idx];
    if (g_recStep == 0) {
        /* step 0: collect modifiers, then one main key */
        if (k->kind == K_MOD) {
            g_recMods ^= (1 << k->mod);
            InvMod(k->mod); InvRec();
            return 1;
        }
        if (k->kind == K_SHCAP) {   /* compact merged shift */
            g_recMods ^= (1 << MX_SHIFT);
            InvMod(MX_SHIFT); ShcapRefresh(); InvRec();
            return 1;
        }
        if (k->kind == K_CHAR) {
            UINT vk = char_to_vk(k->lo);
            if (!vk) return 1;
            g_recVk = vk;
        } else if (k->kind == K_SPECIAL) {
            g_recVk = k->vk;
        } else if (k->kind == K_SPACE) {
            g_recVk = VK_SPACE;
        } else if (k->kind == K_CAPS) {
            g_recVk = VK_CAPITAL;
        } else return 1;   /* anything else: ignore, send nothing */
        g_recStep = 1;
        g_recName[0] = 0;
        InvRec();
        return 1;
    }
    /* step 1: type the name with the keys; nothing is sent */
    if (k->kind == K_MOD) {
        int m = k->mod;
        g_mods[m] = (g_mods[m] == M_OFF) ? M_HELD : M_OFF;
        InvMod(m);
        return 1;
    }
    if (k->kind == K_SHCAP) {
        g_mods[MX_SHIFT] = (g_mods[MX_SHIFT] == M_OFF) ? M_HELD : M_OFF;
        InvMod(MX_SHIFT); ShcapRefresh();
        return 1;
    }
    if (k->kind == K_CHAR) {
        size_t L = wcslen(g_recName);
        if (L < 9) {
            g_recName[L] = ShiftedFor(k->lo) ? k->hi : k->lo;
            g_recName[L + 1] = 0;
            InvRec();
        }
        return 1;
    }
    if (k->kind == K_SPACE) {
        size_t L = wcslen(g_recName);
        if (L < 9 && L > 0) {
            g_recName[L] = L' '; g_recName[L + 1] = 0;
            InvRec();
        }
        return 1;
    }
    if (k->kind == K_SPECIAL) {
        if (k->vk == VK_BACK) {
            size_t L = wcslen(g_recName);
            if (L) { g_recName[L - 1] = 0; InvRec(); }
        } else if (k->vk == VK_RETURN) {
            CommitRecord();
        } else if (k->vk == VK_ESCAPE) {
            CancelRecord();
            g_viewSC = 1;
            ResizeForScale();
        }
        return 1;
    }
    return 1;
}

/* keystrokes while typing a find filter: swallowed, nothing is sent */
static int FindKey(int idx) {
    Key *k = &g_keys[idx];
    if (k->kind == K_MOD) {
        int m = k->mod;
        g_mods[m] = (g_mods[m] == M_OFF) ? M_HELD : M_OFF;
        InvMod(m);
        return 1;
    }
    if (k->kind == K_SHCAP) {
        g_mods[MX_SHIFT] = (g_mods[MX_SHIFT] == M_OFF) ? M_HELD : M_OFF;
        InvMod(MX_SHIFT); ShcapRefresh();
        return 1;
    }
    if (k->kind == K_CHAR) {
        size_t L = wcslen(g_findBuf);
        if (L < 31) {
            g_findBuf[L] = ShiftedFor(k->lo) ? k->hi : k->lo;
            g_findBuf[L + 1] = 0;
            InvRec();
        }
        return 1;
    }
    if (k->kind == K_SPACE) {
        size_t L = wcslen(g_findBuf);
        if (L < 31 && L > 0) {
            g_findBuf[L] = L' '; g_findBuf[L + 1] = 0;
            InvRec();
        }
        return 1;
    }
    if (k->kind == K_SPECIAL) {
        if (k->vk == VK_BACK) {
            size_t L = wcslen(g_findBuf);
            if (L) { g_findBuf[L - 1] = 0; InvRec(); }
        } else if (k->vk == VK_RETURN || k->vk == VK_ESCAPE) {
            int i;
            g_findArm = 0;
            for (i = 0; i < 4; i++)
                if (g_mods[i] != M_OFF) { g_mods[i] = M_OFF; InvMod(i); }
            g_viewClip = 1;
            g_clipOff = 0;
            ResizeForScale();
        }
        return 1;
    }
    return 1;
}

static void PressKey(int idx) {
    Key *k;
    if (idx < 0 || idx >= g_nkeys) return;
    /* shortcut recorder owns every key while armed (nothing is sent) */
    if (g_recSlot >= 0 && !g_viewSC && !g_viewClip && CaptureKey(idx)) {
        g_pressed = idx;
        SetTimer(g_hwnd, TIMER_FLASH, 80, NULL);
        InvKey(idx);
        return;
    }
    /* translate typing mode: keystrokes go to the ID buffer, not the app */
    if (g_trArm && !g_viewTr && !g_viewClip && !g_viewSC && !g_viewCalc &&
        idx >= 0 && idx < g_nkeys && TrKey(idx)) {
        g_pressed = idx;
        SetTimer(g_hwnd, TIMER_FLASH, 80, NULL);
        InvKey(idx);
        return;
    }
    /* find-filter typing mode: same idea, into the clip filter buffer */
    if (g_findArm && !g_viewClip && !g_viewSC && !g_viewCalc && !g_viewTr &&
        idx >= 0 && idx < g_nkeys && FindKey(idx)) {
        g_pressed = idx;
        SetTimer(g_hwnd, TIMER_FLASH, 80, NULL);
        InvKey(idx);
        return;
    }
    k = &g_keys[idx];
    /* any key other than Backspace cancels a pending double-tap window */
    if (!(k->kind == K_SPECIAL && k->vk == VK_BACK)) g_bkspLastUp = 0;
    if (k->kind != K_CHAR && g_pending >= 0) FlushPending();
    g_pressed = idx;
    SetTimer(g_hwnd, TIMER_FLASH, 80, NULL);
    InvKey(idx);
    if (k->kind == K_MOD) { ToggleMod(k->mod); return; }
    if (k->kind == K_SHCAP) { ShcapPress(); return; }
    if (k->kind == K_SUGG) { CommitSugg((int)k->vk); return; }
    if (k->kind == K_CLIP) {
        int hi = ClipVisibleIdx((int)k->vk);
        if (g_pinArm) {   /* Pin mode: tap toggles pin instead of pasting */
            if (hi >= 0) {
                ClipTogglePin(hi);
                InvalidateRect(g_hwnd, NULL, FALSE);
            }
            return;
        }
        PasteClip(hi);
        return;
    }
    if (k->kind == K_CNAV) {
        if (k->vk == CNAV_UP && g_clipOff > 0) { g_clipOff -= 5; if (g_clipOff < 0) g_clipOff = 0; }
        else if (k->vk == CNAV_DOWN && g_clipOff + 5 < ClipMatchCount()) g_clipOff += 5;
        else if (k->vk == CNAV_CLEAR) {
            ClipClearUnpinned();
            if (g_clipOff >= ClipMatchCount()) g_clipOff = 0;
        }
        else if (k->vk == CNAV_PIN) { g_pinArm = !g_pinArm; }
        else if (k->vk == CNAV_FIND) {
            if (g_findBuf[0]) {   /* filter on: tap clears it */
                g_findBuf[0] = 0;
                g_clipOff = 0;
            } else {              /* else type one on the keys view */
                int i;
                for (i = 0; i < 4; i++)
                    if (g_mods[i] != M_OFF) { vk_up(MODVK[i]); g_mods[i] = M_OFF; InvMod(i); }
                g_findArm = 1;
                g_viewClip = 0; g_pinArm = 0;
                ResizeForScale();   /* typing banner appears */
            }
        }
        else if (k->vk == CNAV_SDEL) {
            g_scDelArm = !g_scDelArm;
            if (g_scDelArm) g_starArm = 0;
        }
        else if (k->vk == CNAV_SSTAR) {
            g_starArm = !g_starArm;
            if (g_starArm) g_scDelArm = 0;
        }
        else if (k->vk == CNAV_SBACK) {
            g_viewSC = 0;
            g_scDelArm = 0;
            g_starArm = 0;
            ResizeForScale();
            InvalidateRect(g_hwnd, NULL, TRUE);
            return;
        }
        else if (k->vk == CNAV_TRPASTE) {
            /* load clipboard text as the Indonesian source */
            if (!g_trBusy && OpenClipboard(g_hwnd)) {
                HANDLE d = GetClipboardData(CF_UNICODETEXT);
                if (d) {
                    const wchar_t *s = (const wchar_t *)GlobalLock(d);
                    if (s && s[0]) {
                        wcsncpy(g_trBuf, s, TR_MAXSRC);
                        g_trBuf[TR_MAXSRC] = 0;
                        g_trStatus = 0;
                    }
                    if (s) GlobalUnlock(d);
                }
                CloseClipboard();
            }
        }
        else if (k->vk == CNAV_TRCLEAR) {
            if (!g_trBusy) {
                g_trBuf[0] = 0; g_trStatus = 0; g_trOut[0] = 0;
            }
        }
        else if (k->vk == CNAV_TRGO) { TrGo(); }
        else if (k->vk == CNAV_TRLANG) {
            if (!g_trBusy) {
                g_trPair = (g_trPair + 1) % (int)NTRPAIR;
                g_trStatus = 0;
                SaveSettings();
            }
        }
        else if (k->vk == CNAV_TRBACK) {
            g_viewTr = 0;
            g_trArm = 0;
            ResizeForScale();
            InvalidateRect(g_hwnd, NULL, TRUE);
            return;
        }
        else if (k->vk == CNAV_BACK) {
            g_viewClip = 0;
            g_pinArm = 0;
            g_pressed = -1;
            KillTimer(g_hwnd, TIMER_FLASH);
            ResizeForScale();
            InvalidateRect(g_hwnd, NULL, TRUE);
            return;
        }
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }
    if (k->kind == K_MACRO) {
        if (k->mod & 0x10) { InsertDateTime((int)k->vk); return; }
        RunMacro(k->mod, k->vk);
        return;
    }
    if (k->kind == K_SCUT) {
        int s = (int)k->vk;
        if (s < 0 || s >= MAXSCUT) return;
        if (g_starArm) {   /* star mode: tap pins/unpins to the top bar */
            if (g_sc[s].used && g_sc[s].vk) {
                if (!g_sc[s].star && StarCount() >= 3) {
                    /* strip holds max 3: ignore, stay in star mode */
                } else {
                    g_sc[s].star = !g_sc[s].star;
                    SaveSettings();
                    RebuildKeys();      /* strip membership changed */
                    ResizeForScale();
                }
            }
            return;
        }
        if (g_scDelArm) {   /* delete mode: tap clears the slot */
            if (g_sc[s].used) {
                g_sc[s].used = 0; g_sc[s].mods = 0; g_sc[s].vk = 0;
                g_sc[s].name[0] = 0; g_sc[s].star = 0;
                SaveSettings();
                RebuildKeys();
                ResizeForScale();   /* strip may have shrunk */
            }
            return;
        }
        if (!g_sc[s].used || !g_sc[s].vk) { StartRecord(s); return; }
        RunMacro(g_sc[s].mods, g_sc[s].vk);
        return;
    }
    if (k->kind == K_INFO) {
        /* Tr page display tap: type Indonesian on the keys view */
        if (g_viewTr && !g_trBusy) {
            int i;
            for (i = 0; i < 4; i++)
                if (g_mods[i] != M_OFF) { vk_up(MODVK[i]); g_mods[i] = M_OFF; InvMod(i); }
            g_trArm = 1;
            g_viewTr = 0;
            ResizeForScale();   /* typing banner appears */
        }
        return;   /* record banner: display only */
    }
    if (k->kind == K_CALC) { CalcKey((wchar_t)k->vk); return; }
    if (k->kind == K_SET) { DoSetAction((int)k->vk); return; }
    if (k->kind == K_PAGE) {
        RECT cl;
        g_pressed = -1;
        KillTimer(g_hwnd, TIMER_FLASH);
        g_page = (int)k->vk;
        BuildKeys();
        GetClientRect(g_hwnd, &cl);
        ComputeLayout(cl.right, cl.bottom);
        InvalidateRect(g_hwnd, NULL, TRUE);
        return;
    }
    if (k->kind == K_CAPS) { vk_tap(VK_CAPITAL); InvCaps(); return; }
    if (k->kind == K_SPACE) {
        if (ComboActive()) {
            g_repeatIdx = idx; g_repeatVk = VK_SPACE; g_repeatFirst = TRUE;
            SetTimer(g_hwnd, TIMER_REPEAT, 450, NULL);
            EmitVk(VK_SPACE);
        } else {
            /* real Space key event (proper `code` for web apps) */
            EnsureFocus();
            g_autoActive = 0;
            if (g_wordLen >= 3) AutoCorrectNow(1);
            vk_tap(VK_SPACE);
            WordChar(L' ');
            ConsumeHeld();
            InvSugg();
        }
        return;
    }
    if (k->kind == K_CHAR) {
        /* deferred: quick release = tap, hold = popup picker */
        if (g_pending >= 0) FlushPending();
        g_pending = idx;
        SetTimer(g_hwnd, TIMER_HOLD, 400, NULL);
        return;
    }
    if (k->kind == K_SPECIAL) {
        if (k->vk == VK_BACK && IsDoubleBksp(GetTickCount())) {
            DoubleBackspace();
            if (IsRepeatable(k->vk)) {   /* hold after double keeps deleting */
                g_repeatIdx = idx; g_repeatVk = k->vk; g_repeatFirst = TRUE;
                SetTimer(g_hwnd, TIMER_REPEAT, 450, NULL);
            }
            return;
        }
        if (IsRepeatable(k->vk)) {
            g_repeatIdx = idx; g_repeatVk = k->vk; g_repeatFirst = TRUE;
            SetTimer(g_hwnd, TIMER_REPEAT, 450, NULL);
        }
        EmitVk(k->vk);
        return;
    }
}

/* ---------------- layout & paint ---------------- */
static void RebuildFonts(void) {
    float k = g_scale * (g_compact ? 0.9f : 1.0f); /* slightly smaller keys in compact */
    if (g_fKey) DeleteObject(g_fKey);
    if (g_fSmall) DeleteObject(g_fSmall);
    if (g_fBar) DeleteObject(g_fBar);
    g_fKey = CreateFontW(-(int)(16*k),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    g_fSmall = CreateFontW(-(int)(10*k),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    g_fBar = CreateFontW(-(int)(13*g_scale),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
}

static int BarH(void) { return (int)(30*g_scale); }

static void ComputeLayout(int cw, int ch) {
    int pad = 6, gap = 4, bh = BarH(), hintH = 0;
    int rows[16], nrows = 0, r, i;
    int y, avail, rh, x;
    /* bar buttons: side 0 packs from right (X at far right), side 1 from left */
    x = cw - pad;
    for (i = 7; i >= 0; i--) {
        if (!g_bar[i].show) {
            SetRectEmpty(&g_bar[i].rc);
        } else {
            int w = (int)(g_bar[i].w * g_scale);
            g_bar[i].rc.right = x; g_bar[i].rc.left = x - w;
            g_bar[i].rc.top = 3; g_bar[i].rc.bottom = bh - 3;
            x -= w + 3;
        }
    }
    x = pad;
    for (i = 8; i < 9; i++) {
        if (!g_bar[i].show) { SetRectEmpty(&g_bar[i].rc); continue; }
        int w = (int)(g_bar[i].w * g_scale);
        g_bar[i].rc.left = x; g_bar[i].rc.right = x + w;
        g_bar[i].rc.top = 3; g_bar[i].rc.bottom = bh - 3;
        x += w + 3;
    }
    /* clipboard page replaces everything */
    if (g_viewClip) {
        for (r = 20; r <= 25; r++) rows[nrows++] = r;
    } else if (g_viewSC) {
        for (r = 30; r <= 32; r++) rows[nrows++] = r;
    } else if (g_viewCalc) {
        for (r = 40; r <= 45; r++) rows[nrows++] = r;
    } else if (g_viewTr) {
        for (r = 50; r <= 51; r++) rows[nrows++] = r;
    } else {
    /* record/translate banner first, then panels, pin strip, suggestions, keys */
    if (g_recSlot >= 0 || g_trArm || g_findArm) rows[nrows++] = 13;
    if (g_showSettings) rows[nrows++] = 10;
    if (g_showMacros) rows[nrows++] = 11;
    if (g_showMacros) rows[nrows++] = 16;
    if (g_pinBarOn && StarCount() > 0) rows[nrows++] = 14;
    if (SuggRowOn()) rows[nrows++] = 12;
    if (g_compact) {
        for (r = 0; r < 4; r++) rows[nrows++] = r;
    } else if (g_showFn) {
        for (r = 0; r < 6; r++) rows[nrows++] = r;
    } else {
        for (r = 1; r < 6; r++) rows[nrows++] = r;
    }
    } /* end normal rows */
    /* wipe rects of rows not on screen: stale rects must never hit-test */
    for (i = 0; i < g_nkeys; i++) {
        int found = 0, q;
        for (q = 0; q < nrows; q++)
            if (g_keys[i].row == rows[q]) { found = 1; break; }
        if (!found) SetRectEmpty(&g_keys[i].rc);
    }
    avail = ch - bh - pad*2 - hintH - gap*(nrows-1);
    rh = nrows ? avail / nrows : 0;
    y = bh + pad;
    for (r = 0; r < nrows; r++) {
        int row = rows[r], n = 0, ci;
        int rgap = (row == 14) ? 0 : gap;   /* pin strip: edge-to-edge */
        float tot = 0;
        for (i = 0; i < g_nkeys; i++)
            if (g_keys[i].row == row) { tot += g_keys[i].w; n++; }
        x = pad;
        ci = 0;
        for (i = 0; i < g_nkeys; i++) if (g_keys[i].row == row) {
            int wpx = (int)((cw - pad*2 - rgap*(n-1)) * (g_keys[i].w / tot));
            if (++ci == n) wpx = cw - pad - x; /* absorb rounding */
            g_keys[i].rc.left = x; g_keys[i].rc.top = y;
            g_keys[i].rc.right = x + wpx; g_keys[i].rc.bottom = y + rh;
            x += wpx + rgap;
        }
        y += rh + rgap;
    }
    /* hide fn rects when hidden (Full mode only: in Compact, row 0 is
     * the letter/number row, not the Fn row) */
    if (!g_showFn && !g_compact)
        for (i = 0; i < g_nkeys; i++)
            if (g_keys[i].row == 0) SetRectEmpty(&g_keys[i].rc);
}

static int VisibleRow(int row) {
    /* clip / shortcut views are exclusive: NOTHING else is hittable there */
    if (g_viewClip) return (row >= 20 && row <= 25);
    if (g_viewSC) return (row >= 30 && row <= 32);
    if (g_viewCalc) return (row >= 40 && row <= 45);
    if (g_viewTr) return (row >= 50 && row <= 51);
    if (row == 10) return g_showSettings;
    if (row == 11 || row == 16) return g_showMacros;
    if (row == 12) return SuggRowOn();
    if (row == 13) return (g_recSlot >= 0 || g_trArm || g_findArm);
    if (row == 14) return g_pinBarOn && (StarCount() > 0);
    if (row >= 20) return 0;
    if (g_compact) return 1;
    if (row == 0 && !g_showFn) return 0;
    return 1;
}

/* change-only repaints: never invalidate the whole window on a timer */
static void InvKey(int idx) {
    if (idx >= 0 && idx < g_nkeys && VisibleRow(g_keys[idx].row))
        InvalidateRect(g_hwnd, &g_keys[idx].rc, FALSE);
}
static void InvMod(int m) {
    int i;
    for (i = 0; i < g_nkeys; i++)
        if (g_keys[i].kind == K_MOD && g_keys[i].mod == m) InvKey(i);
}
static void InvCaps(void) {
    int i;
    for (i = 0; i < g_nkeys; i++)
        if (g_keys[i].kind == K_CAPS) InvKey(i);
}
static void InvBar(int id) {
    int i;
    for (i = 0; i < 9; i++)
        if (g_bar[i].id == id) InvalidateRect(g_hwnd, &g_bar[i].rc, FALSE);
}

static void FillRRr(HDC dc, RECT *rc, COLORREF c, int r) {
    HBRUSH b = CreateSolidBrush(c);
    HPEN p = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, rc->left, rc->top, rc->right, rc->bottom, r, r);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);
}
static void FillRR(HDC dc, RECT *rc, COLORREF c) { FillRRr(dc, rc, c, 8); }

static void PaintKey(HDC dc, Key *k, int idx) {
    COLORREF bg = C_KEY, fg = C_TXT;
    BOOL capsOn;
    if (k->kind == K_SPECIAL || k->kind == K_CAPS || k->kind == K_MOD ||
        k->kind == K_PAGE || k->kind == K_MACRO || k->kind == K_SET ||
        k->kind == K_CNAV || k->kind == K_SHCAP) bg = C_SPEC;
    if (k->kind == K_MOD) {
        if (g_mods[k->mod] == M_HELD) { bg = C_HELD; fg = RGB(0,0,0); }
        else if (g_mods[k->mod] == M_LOCKED) { bg = C_LOCK; fg = C_TXT; }
        else if (g_recSlot >= 0 && g_recStep == 0 && (g_recMods & (1 << k->mod))) {
            bg = C_LOCK; fg = C_TXT;   /* recording: captured modifier */
        }
    }
    if (k->kind == K_CAPS) {
        capsOn = (GetKeyState(VK_CAPITAL) & 1) != 0;
        if (capsOn) { bg = C_LOCK; fg = C_TXT; }
    }
    if (k->kind == K_SHCAP) {
        capsOn = (GetKeyState(VK_CAPITAL) & 1) != 0;
        if (capsOn) { bg = C_LOCK; fg = C_TXT; }
        else if (g_mods[MX_SHIFT] == M_LOCKED) { bg = C_LOCK; fg = C_TXT; }
        else if (g_mods[MX_SHIFT] != M_OFF) { bg = C_HELD; fg = RGB(0,0,0); }
    }
    if (k->kind == K_SET && ((k->vk == SET_PRED && g_predictOn) ||
        (k->vk == SET_AUTO && g_autocorrect) ||
        (k->vk == SET_SPELL && g_highlightOn) ||
        (k->vk == SET_BIGRAM && g_bigramOn) ||
        (k->vk == SET_PINBAR && g_pinBarOn) ||
        (k->vk == SET_PINWIN && CurTargetPinned()))) {
        bg = C_LOCK; fg = C_TXT;
    }
    if (k->kind == K_CNAV && k->vk == CNAV_PIN && g_pinArm) { bg = C_LOCK; fg = C_TXT; }
    if (k->kind == K_CNAV && k->vk == CNAV_FIND &&
        (g_findArm || g_findBuf[0])) { bg = C_LOCK; fg = C_TXT; }
    if (k->kind == K_CNAV && k->vk == CNAV_SDEL && g_scDelArm) { bg = C_LOCK; fg = C_TXT; }
    if (idx == g_pressed) bg = C_ACTIVE;
    /* pin strip: slimmer corners to match the tight gap */
    if (k->kind == K_SCUT && k->row == 14) FillRRr(dc, &k->rc, bg, 4);
    else FillRR(dc, &k->rc, bg);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    if (k->kind == K_CLIP) {
        int hi = ClipVisibleIdx((int)k->vk);
        if (hi >= 0) {
            ClipEnt *e = &g_clip[hi];
            if (e->isImg && e->hbmp) {
                HDC mdc = CreateCompatibleDC(dc);
                HGDIOBJ so;
                int th = k->rc.bottom - k->rc.top - 10, tw;
                int ix, iy;
                wchar_t dim[32];
                if (th < 16) th = 16;
                tw = th * e->iw / (e->ih ? e->ih : 1);
                if (tw < 8) tw = 8;
                ix = k->rc.left + 6;
                iy = k->rc.top + (k->rc.bottom - k->rc.top - th) / 2;
                /* light backdrop: dark/transparent shots stay visible */
                {
                    RECT fr;
                    HBRUSH gb = CreateSolidBrush(RGB(110, 110, 110));
                    fr.left = ix - 2; fr.top = iy - 2;
                    fr.right = ix + tw + 2; fr.bottom = iy + th + 2;
                    FillRect(dc, &fr, gb);
                    DeleteObject(gb);
                }
                so = SelectObject(mdc, e->hbmp);
                SetStretchBltMode(dc, HALFTONE);
                SetBrushOrgEx(dc, 0, 0, NULL);
                StretchBlt(dc, ix, iy, tw, th, mdc, 0, 0,
                           e->iw, e->ih, SRCCOPY);
                SelectObject(mdc, so);
                DeleteDC(mdc);
                wsprintfW(dim, L"IMG %dx%d", e->iw, e->ih);
                SelectObject(dc, g_fSmall);
                {
                    RECT tr = k->rc;
                    tr.left += 12 + tw;
                    DrawTextW(dc, dim, -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
                }
            } else if (e->text) {
                wchar_t prev[30];
                int n = (int)wcslen(e->text), j, o = 0;
                for (j = 0; j < n && o < 28; j++) {
                    wchar_t c = e->text[j];
                    if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
                    else if (c < 0x20 || (c >= 0x7F && c < 0xA0)) c = L' ';
                    /* never split a surrogate pair (emoji) in half */
                    if (c >= 0xD800 && c <= 0xDBFF && j + 1 < n &&
                        e->text[j + 1] >= 0xDC00 && e->text[j + 1] <= 0xDFFF) {
                        if (o + 2 > 28) break;
                        prev[o++] = c;
                        prev[o++] = e->text[++j];
                        continue;
                    }
                    if (c >= 0xD800 && c <= 0xDFFF) continue; /* lone: skip */
                    prev[o++] = c;
                }
                prev[o] = 0;
                if (n > 28) wcscat(prev, L"\u2026");
                SelectObject(dc, g_fKey);
                DrawTextW(dc, prev, -1, &k->rc, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            }
        } else {
            SelectObject(dc, g_fSmall);
            SetTextColor(dc, C_DIM);
            DrawTextW(dc, L"\u00B7 \u00B7 \u00B7", -1, &k->rc,
                      DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        {
            /* pinned entries get a small PIN tag at the top-right */
            int hi2 = ClipVisibleIdx((int)k->vk);
            if (hi2 >= 0 && g_clip[hi2].pinned) {
                RECT tr;
                int tw = (int)(30 * g_scale), th = (int)(15 * g_scale);
                if (tw < 22) tw = 22;
                if (th < 12) th = 12;
                tr.right = k->rc.right - 4; tr.left = tr.right - tw;
                tr.top = k->rc.top + 3; tr.bottom = tr.top + th;
                if (tr.left > k->rc.left + 4) {
                    FillRR(dc, &tr, C_LOCK);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, C_TXT);
                    SelectObject(dc, g_fSmall);
                    DrawTextW(dc, L"PIN", -1, &tr,
                              DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                }
            }
        }
        return;
    } else if (k->kind == K_CNAV) {
        SelectObject(dc, g_fSmall);
        if (k->vk == CNAV_TRLANG) {
            /* live pair tag (ID>EN …), tap cycles */
            int p = g_trPair;
            if (p < 0 || p >= (int)NTRPAIR) p = 0;
            DrawTextW(dc, g_trPairs[p].tag, -1, &k->rc,
                      DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        } else DrawTextW(dc, k->text, -1, &k->rc,
                         DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        return;
    } else if (k->kind == K_SCUT) {
        int s = (int)k->vk;
        if (s >= 0 && s < MAXSCUT && g_sc[s].used && g_sc[s].name[0]) {
            wchar_t disp[12];
            if (g_sc[s].star) {
                disp[0] = L'\u2605';
                wcsncpy(disp + 1, g_sc[s].name, 8);
                disp[9] = 0;
            } else {
                wcsncpy(disp, g_sc[s].name, 11);
                disp[11] = 0;
            }
            /* pin strip (row 14): big font like suggestions; SC page keeps small */
            SelectObject(dc, (k->row == 14) ? g_fKey : g_fSmall);
            if (g_scDelArm) SetTextColor(dc, RGB(255, 110, 110));
            DrawTextW(dc, disp, -1, &k->rc,
                      DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        } else {
            SelectObject(dc, g_fKey);
            SetTextColor(dc, C_DIM);
            DrawTextW(dc, L"+", -1, &k->rc,
                      DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        return;
    } else if (k->kind == K_INFO) {
        /* calculator display, translate page/status, or record banner */
        wchar_t msg[128];
        SelectObject(dc, g_fKey);
        SetTextColor(dc, C_TXT);
        if (g_viewCalc) {
            if (g_calcErr) wcscpy(msg, L"Error");
            else if (!g_calcBuf[0]) wcscpy(msg, L"0");
            else if (g_calcLiveOn) wsprintfW(msg, L"%ls = %ls",
                                            g_calcBuf, g_calcLive);
            else wcscpy(msg, g_calcBuf);
            DrawTextW(dc, msg, -1, &k->rc,
                      DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
            return;
        }
        if (g_viewTr) {
            /* ID input / translation status, truncated to fit */
            wchar_t src[48];
            if (g_trStatus == 1) wcscpy(msg, L"\u2026");
            else if (g_trStatus == 3) wcscpy(msg, L"offline?");
            else if (g_trStatus == 2 && g_trOut[0]) {
                wcsncpy(msg, g_trOut, 40); msg[40] = 0;
            } else if (g_trBuf[0]) {
                wchar_t sc[3];
                TrSrcTag(sc);
                wcsncpy(src, g_trBuf, 40); src[40] = 0;
                wsprintfW(msg, L"%ls> %ls", sc, src);
                if (wcslen(g_trBuf) > 40) wcscat(msg, L"\u2026");
            } else wcscpy(msg, L"tap here to type, or Paste");
            DrawTextW(dc, msg, -1, &k->rc,
                      DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            return;
        }
        if (g_trArm) {
            wchar_t tb[48], sc[3];
            TrSrcTag(sc);
            wcsncpy(tb, g_trBuf, 40); tb[40] = 0;
            wsprintfW(msg, L"%ls> %ls (Enter done)", sc, tb);
            DrawTextW(dc, msg, -1, &k->rc,
                      DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            return;
        }
        if (g_findArm) {
            wchar_t fb[48];
            wcsncpy(fb, g_findBuf, 31); fb[31] = 0;
            wsprintfW(msg, L"Find: %ls (Enter done)", fb);
            DrawTextW(dc, msg, -1, &k->rc,
                      DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            return;
        }
        SelectObject(dc, g_fSmall);
        if (g_recStep == 0) {
            wsprintfW(msg, L"\u25CF REC %d: tap modifiers+key (SC cancels)",
                      g_recSlot + 1);
            SetTextColor(dc, RGB(255, 110, 110));
        } else {
            wsprintfW(msg, L"Name: %ls (Enter saves, Esc cancels)", g_recName);
            SetTextColor(dc, C_TXT);
        }
        DrawTextW(dc, msg, -1, &k->rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        return;
    } else if (k->kind == K_SUGG) {
        /* red misspelt literal overlays slot 0 live (buffer, not a slot) */
        if (k->vk == 0 && g_highlightOn && g_misspelt && g_wordLen > 0) {
            SelectObject(dc, g_fKey);
            SetTextColor(dc, RGB(255, 110, 110));
            DrawTextW(dc, g_wordBuf, g_wordLen, &k->rc,
                      DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            return;
        }
        {
            const wchar_t *s = (k->vk < 3) ? g_sugg[k->vk] : L"";
            SelectObject(dc, g_fKey);
            if (s[0]) DrawTextW(dc, s, -1, &k->rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        return;
    } else if (k->kind == K_CHAR) {
        /* dual label: small hint on top (digit tag or shifted symbol), main below */
        wchar_t top = k->tag;
        BOOL alpha = (k->lo>=L'a'&&k->lo<=L'z')||(k->lo>=L'A'&&k->lo<=L'Z');
        RECT r = k->rc;
        if (!top && !alpha) top = k->hi;
        if (top) {
            SelectObject(dc, g_fSmall);
            SetTextColor(dc, g_mods[MX_SHIFT] != M_OFF ? fg : C_DIM);
            DrawTextW(dc, &top, 1, &r, DT_TOP|DT_CENTER|DT_SINGLELINE);
            r.top += (int)(10*g_scale);
        }
        SelectObject(dc, g_fKey);
        SetTextColor(dc, fg);
        DrawTextW(dc, k->text, -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    } else {
        int small = (k->kind == K_MACRO) ||
            (k->kind == K_SPECIAL && (k->vk == VK_PRIOR || k->vk == VK_NEXT ||
             k->vk == VK_HOME || k->vk == VK_END));
        SelectObject(dc, small ? g_fSmall : g_fKey);
        DrawTextW(dc, k->text, -1, &k->rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
}

static void OnPaint(void) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(g_hwnd, &ps);
    RECT cl; int i;
    GetClientRect(g_hwnd, &cl);
    {
        HBRUSH b = CreateSolidBrush(C_BG);
        FillRect(dc, &cl, b);
        DeleteObject(b);
    }
    /* bar */
    {
        RECT bar = {0,0,cl.right,BarH()};
        HBRUSH b = CreateSolidBrush(C_BAR);
        FillRect(dc, &bar, b);
        DeleteObject(b);
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, g_fBar);
        for (i = 0; i < 9; i++) {
            if (!g_bar[i].show) continue;
            FillRR(dc, &g_bar[i].rc, (g_bar[i].id==g_downIdx-1000)?C_ACTIVE:C_SPEC);
            SetTextColor(dc, C_TXT);
            SelectObject(dc, g_fBar);
            DrawTextW(dc, g_bar[i].t, -1, &g_bar[i].rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
    }
    for (i = 0; i < g_nkeys; i++) {
        if (!VisibleRow(g_keys[i].row)) continue;
        if (IsRectEmpty(&g_keys[i].rc)) continue;
        PaintKey(dc, &g_keys[i], i);
    }
    /* hold popup picker [base|alt]: each choice gets its own color so
     * the held character options are easy to tell apart at a glance */
    if (g_holdPopup >= 0 && !IsRectEmpty(&g_popAll)) {
        for (i = 0; i < 2; i++) {
            wchar_t c = i ? g_popAlt : g_popBase;
            COLORREF pbg = (i == g_popHover) ? C_ACTIVE : (i ? C_LOCK : C_HELD);
            COLORREF pfg = (i == g_popHover || i) ? C_TXT : RGB(0, 0, 0);
            FillRR(dc, &g_popRc[i], pbg);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, pfg);
            SelectObject(dc, g_fKey);
            DrawTextW(dc, &c, 1, &g_popRc[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    /* hint removed (cleans up the bottom edge) */
    EndPaint(g_hwnd, &ps);
}

static int HitBar(int x, int y) {
    int i;
    POINT p = {x, y};
    for (i = 0; i < 9; i++)
        if (g_bar[i].show && PtInRect(&g_bar[i].rc, p)) return g_bar[i].id;
    return BAR_NONE;
}
static int HitKey(int x, int y) {
    int i;
    POINT p = {x,y};
    for (i = 0; i < g_nkeys; i++) {
        if (!VisibleRow(g_keys[i].row)) continue;
        if (PtInRect(&g_keys[i].rc, p)) return i;
    }
    return -1;
}

/* ---------------- chrome actions ---------------- */
static int BaseW(void) { return (int)((g_compact ? 460 : 720) * g_scale); }
static int NRows(void) {
    int n;
    if (g_viewClip) return 6;
    if (g_viewSC) return 3;
    if (g_viewCalc) return 6;
    if (g_viewTr) return 2;
    n = g_compact ? 4 : (g_showFn ? 6 : 5);
    if (g_recSlot >= 0 || g_trArm || g_findArm) n += 1;   /* banner above keys */
    if (!g_viewClip && !g_viewSC && g_pinBarOn && StarCount() > 0) n += 1;   /* pin strip */
    if (g_showSettings || g_showMacros) n += 1;
    if (g_showMacros) n += 1;   /* media/date second row */
    if (SuggRowOn()) n += 1;
    return n;
}
static int BaseH(void) {
    return (int)((38 + NRows()*54) * g_scale);
}
static void ApplyOpacity(void) {
    SetLayeredWindowAttributes(g_hwnd, 0, (BYTE)g_opacity, LWA_ALPHA);
}
static void ResizeForScale(void) {
    int w = BaseW();
    int h = BaseH();
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0,0, w,h,
        SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RebuildFonts();
    {
        RECT cl; GetClientRect(g_hwnd,&cl);
        ComputeLayout(cl.right, cl.bottom);
    }
    InvalidateRect(g_hwnd, NULL, TRUE);
}
/* rebuild key labels/rects without resizing (e.g. shortcut names changed) */
static void RebuildKeys(void) {
    RECT cl;
    BuildKeys();
    GetClientRect(g_hwnd, &cl);
    ComputeLayout(cl.right, cl.bottom);
    InvalidateRect(g_hwnd, NULL, TRUE);
}
static void DoBar(int id) {
    switch (id) {
        case BAR_X: DestroyWindow(g_hwnd); break;
        case BAR_FN:
            g_showFn = !g_showFn;
            ResizeForScale();
            break;
        case BAR_GEAR:
            g_showSettings = !g_showSettings;
            if (g_showSettings) { g_showMacros = 0; g_viewClip = 0; g_viewSC = 0; g_viewCalc = 0; g_viewTr = 0; }
            ResizeForScale();
            break;
        case BAR_STAR:
            g_showMacros = !g_showMacros;
            if (g_showMacros) { g_showSettings = 0; g_viewClip = 0; g_viewSC = 0; g_viewCalc = 0; g_viewTr = 0; }
            ResizeForScale();
            break;
        case BAR_CLIP:
            g_viewClip = !g_viewClip;
            if (g_viewClip) { g_showSettings = 0; g_showMacros = 0; g_viewSC = 0; g_viewCalc = 0; g_viewTr = 0; }
            else g_pinArm = 0;
            g_clipOff = 0;
            ResizeForScale();
            break;
        case BAR_SC:
            if (g_recSlot >= 0) CancelRecord();
            g_viewSC = !g_viewSC;
            if (g_viewSC) {
                g_showSettings = 0; g_showMacros = 0; g_viewClip = 0; g_viewCalc = 0; g_viewTr = 0;
                g_pinArm = 0;
            } else { g_scDelArm = 0; g_starArm = 0; }
            ResizeForScale();
            break;
        case BAR_CALC:
            g_viewCalc = !g_viewCalc;
            if (g_viewCalc) {
                g_showSettings = 0; g_showMacros = 0; g_viewClip = 0; g_viewSC = 0; g_viewTr = 0;
                g_pinArm = 0;
            }
            ResizeForScale();
            break;
        case BAR_TR:
            g_viewTr = !g_viewTr;
            if (g_viewTr) {
                g_showSettings = 0; g_showMacros = 0; g_viewClip = 0; g_viewSC = 0; g_viewCalc = 0;
                g_pinArm = 0;
            } else g_trArm = 0;
            ResizeForScale();
            break;
        case BAR_MODE:
            g_compact = !g_compact;
            if (g_compact) g_page = 0;
            g_pending = -1; g_holdPopup = -1; KillTimer(g_hwnd, TIMER_HOLD);
            BuildKeys();
            SetModeLabel();
            ResizeForScale();
            break;
    }
    SaveSettings();
}

/* ---------------- persistent settings (HKCU\Software\FloatKeys) ---------------- */
static void SaveSettings(void) {
    HKEY k;
    DWORD v;
    int i;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\FloatKeys", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    v = (DWORD)(int)(g_scale * 100.0f + 0.5f);
    RegSetValueExW(k, L"Scale", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    v = (DWORD)g_opacity;
    RegSetValueExW(k, L"Opacity", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    v = (DWORD)g_compact;
    RegSetValueExW(k, L"Compact", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    v = (DWORD)(g_showFn ? 1 : 0);
    RegSetValueExW(k, L"ShowFn", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    v = (DWORD)g_lang;
    RegSetValueExW(k, L"Lang", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    v = (DWORD)(g_predictOn ? 1 : 0);
    RegSetValueExW(k, L"Predict", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    v = (DWORD)(g_autocorrect ? 1 : 0);
    RegSetValueExW(k, L"Autocorrect", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    v = (DWORD)(g_highlightOn ? 1 : 0);
    RegSetValueExW(k, L"Highlight", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    v = (DWORD)(g_bigramOn ? 1 : 0);
    RegSetValueExW(k, L"Bigram", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    v = (DWORD)g_trPair;
    RegSetValueExW(k, L"TrPair", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    v = (DWORD)(g_pinBarOn ? 1 : 0);
    RegSetValueExW(k, L"PinBar", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    for (i = 0; i < MAXSCUT; i++) {
        wchar_t nm[16];
        wsprintfW(nm, L"SC%dM", i);
        v = (DWORD)(g_sc[i].mods & 0xF);
        RegSetValueExW(k, nm, 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
        wsprintfW(nm, L"SC%dV", i);
        v = (DWORD)g_sc[i].vk;
        RegSetValueExW(k, nm, 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
        wsprintfW(nm, L"SC%dN", i);
        RegSetValueExW(k, nm, 0, REG_SZ, (const BYTE *)g_sc[i].name,
                       (DWORD)((wcslen(g_sc[i].name) + 1) * sizeof(wchar_t)));
        wsprintfW(nm, L"SC%dS", i);
        v = (DWORD)(g_sc[i].star ? 1 : 0);
        RegSetValueExW(k, nm, 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    }
    if (g_hwnd && IsWindow(g_hwnd)) {
        RECT wr;
        GetWindowRect(g_hwnd, &wr);
        v = (DWORD)wr.left;
        RegSetValueExW(k, L"X", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
        v = (DWORD)wr.top;
        RegSetValueExW(k, L"Y", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    }
    RegCloseKey(k);
}

static void LoadSettings(void) {
    HKEY k;
    DWORD v, sz, type;
    int i;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\FloatKeys", 0,
                      KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return;
    sz = sizeof(v);
    if (RegQueryValueExW(k, L"Scale", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
        && type == REG_DWORD) {
        g_scale = (float)(int)v / 100.0f;
        if (g_scale < 0.75f) g_scale = 0.75f;
        if (g_scale > 1.5f) g_scale = 1.5f;
    }
    sz = sizeof(v);
    if (RegQueryValueExW(k, L"Opacity", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
        && type == REG_DWORD) {
        g_opacity = (int)v;
        if (g_opacity < 102) g_opacity = 102;
        if (g_opacity > 255) g_opacity = 255;
    }
    sz = sizeof(v);
    if (RegQueryValueExW(k, L"Compact", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
        && type == REG_DWORD)
        g_compact = v ? 1 : 0;
    sz = sizeof(v);
    if (RegQueryValueExW(k, L"ShowFn", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
        && type == REG_DWORD)
        g_showFn = v ? TRUE : FALSE;
    sz = sizeof(v);
    if (RegQueryValueExW(k, L"Lang", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
        && type == REG_DWORD)
        g_lang = (v == 1) ? 1 : 0;
    sz = sizeof(v);
    if (RegQueryValueExW(k, L"Predict", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
        && type == REG_DWORD)
        g_predictOn = v ? 1 : 0;
    sz = sizeof(v);
    if (RegQueryValueExW(k, L"Autocorrect", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
        && type == REG_DWORD)
        g_autocorrect = v ? 1 : 0;
    sz = sizeof(v);
    if (RegQueryValueExW(k, L"Highlight", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
        && type == REG_DWORD)
        g_highlightOn = v ? 1 : 0;
    sz = sizeof(v);
    if (RegQueryValueExW(k, L"Bigram", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
        && type == REG_DWORD)
        g_bigramOn = v ? 1 : 0;
    sz = sizeof(v);
    if (RegQueryValueExW(k, L"TrPair", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
        && type == REG_DWORD && (int)v >= 0 && (int)v < (int)NTRPAIR)
        g_trPair = (int)v;
    sz = sizeof(v);
    if (RegQueryValueExW(k, L"PinBar", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
        && type == REG_DWORD)
        g_pinBarOn = v ? 1 : 0;
    for (i = 0; i < MAXSCUT; i++) {
        wchar_t nm[16], buf[16];
        DWORD sz2;
        g_sc[i].used = 0; g_sc[i].mods = 0; g_sc[i].vk = 0;
        g_sc[i].name[0] = 0; g_sc[i].star = 0;
        wsprintfW(nm, L"SC%dM", i);
        sz2 = sizeof(v);
        if (RegQueryValueExW(k, nm, NULL, &type, (BYTE *)&v, &sz2) == ERROR_SUCCESS
            && type == REG_DWORD)
            g_sc[i].mods = (int)v & 0xF;
        wsprintfW(nm, L"SC%dV", i);
        sz2 = sizeof(v);
        if (RegQueryValueExW(k, nm, NULL, &type, (BYTE *)&v, &sz2) == ERROR_SUCCESS
            && type == REG_DWORD && v > 0 && v <= 0xFE)
            g_sc[i].vk = (UINT)v;
        if (!g_sc[i].vk) continue;
        wsprintfW(nm, L"SC%dN", i);
        sz2 = sizeof(buf);
        if (RegQueryValueExW(k, nm, NULL, &type, (BYTE *)buf, &sz2) == ERROR_SUCCESS
            && type == REG_SZ && sz2 >= sizeof(wchar_t) && sz2 <= sizeof(buf)) {
            buf[9] = 0;
            wcscpy(g_sc[i].name, buf);
        } else AutoName(g_sc[i].mods, g_sc[i].vk, g_sc[i].name);
        g_sc[i].used = 1;
        wsprintfW(nm, L"SC%dS", i);
        sz2 = sizeof(v);
        if (RegQueryValueExW(k, nm, NULL, &type, (BYTE *)&v, &sz2) == ERROR_SUCCESS
            && type == REG_DWORD && v)
            g_sc[i].star = 1;
    }
    {   /* bar holds max 3 pins: keep the first three by slot order */
        int kept = 0, q;
        for (q = 0; q < MAXSCUT; q++) {
            if (g_sc[q].used && g_sc[q].vk && g_sc[q].star) {
                if (++kept > 3) g_sc[q].star = 0;
            }
        }
    }
    sz = sizeof(v);
    if (RegQueryValueExW(k, L"X", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
        && type == REG_DWORD) {
        g_posX = (int)v;
        sz = sizeof(v);
        if (RegQueryValueExW(k, L"Y", NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS
            && type == REG_DWORD) {
            g_posY = (int)v;
            g_hasPos = 1;
        }
    }
    RegCloseKey(k);
}

/* ---------------- window proc ---------------- */
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCACTIVATE: return TRUE;
        case WM_CREATE:
            g_capsLast = (GetKeyState(VK_CAPITAL) & 1) != 0;
            SetTimer(h, TIMER_CAPS, 500, NULL);
            SetTimer(h, TIMER_PIN, 1000, NULL);
            AddClipboardFormatListener(h);
            /* track the typing target while we run (out-of-context: no DLL) */
            g_fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
                EVENT_SYSTEM_FOREGROUND, NULL, FgHookProc,
                0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            TrackForeground(GetForegroundWindow());
            return 0;
        case WM_CLIPBOARDUPDATE:
            OnClipboardUpdate();
            return 0;
        case WM_APP_TRDONE: {
            /* worker thread finished: type the English result at the caret */
            int ok = (int)w;
            wchar_t *res = (wchar_t *)l;
            const wchar_t *p;
            g_trBusy = 0;
            if (ok && res && res[0]) {
                g_trStatus = 2;
                wcsncpy(g_trOut, res, 127);
                g_trOut[127] = 0;
                EnsureFocus();
                type_unicode(res);
                for (p = res; *p; p++) WordChar(*p);
                ConsumeHeld();
                InvSugg();
            } else {
                g_trStatus = 3;   /* offline / API error */
            }
            if (res) free(res);
            InvTr();
            return 0;
        }
        case WM_SIZE: {
            int cw = LOWORD(l), ch = HIWORD(l);
            ComputeLayout(cw, ch);
            return 0;
        }
        case WM_PAINT: OnPaint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
            int b = HitBar(x, y);
            SetCapture(h);
            /* bar buttons ride as 1000+id so they never collide with key
             * indices (Full layout has >100 keys: clip/nav sit past 100) */
            if (b != BAR_NONE) { g_downIdx = 1000 + b; InvBar(b); }
            else {
                int k = HitKey(x, y);
                if (k >= 0) { g_downIdx = k; PressKey(k); }
                else if (y < BarH()) {
                    POINT pt; GetCursorPos(&pt);
                    RECT wr; GetWindowRect(h, &wr);
                    g_dragging = TRUE;
                    g_dragOff.x = pt.x - wr.left; g_dragOff.y = pt.y - wr.top;
                    g_downIdx = -3;
                } else g_downIdx = -2;
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (g_dragging) {
                POINT pt; GetCursorPos(&pt);
                SetWindowPos(h, HWND_TOPMOST,
                    pt.x - g_dragOff.x, pt.y - g_dragOff.y, 0,0,
                    SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            } else if (g_holdPopup >= 0) {
                POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
                int hv = PtInRect(&g_popRc[1], pt) ? 1
                       : (PtInRect(&g_popRc[0], pt) ? 0 : -1);
                if (hv != g_popHover) {
                    g_popHover = hv;
                    InvalidateRect(h, &g_popAll, FALSE);
                }
            }
            return 0;
        case WM_LBUTTONUP: {
            KillTimer(h, TIMER_HOLD);
            if (g_holdPopup >= 0) ResolvePopup();
            else if (g_pending >= 0) FlushPending();
            if (g_downIdx >= 0 && g_downIdx < g_nkeys) {
                Key *uk = &g_keys[g_downIdx];
                if (uk->kind == K_SPECIAL && uk->vk == VK_BACK)
                    g_bkspLastUp = GetTickCount();   /* arm double-tap window */
            }
            if (g_downIdx >= 1000) {
                int id = g_downIdx - 1000;
                int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
                g_downIdx = -2;
                InvBar(id);
                if (HitBar(x,y) == id) DoBar(id);
            } else g_downIdx = -2;
            if (g_dragging) SaveSettings();
            g_dragging = FALSE;
            ReleaseCapture();
            KillTimer(h, TIMER_REPEAT);
            g_repeatIdx = -1;
            return 0;
        }
        case WM_TIMER:
            if (w == TIMER_CAPS) {
                /* repaint Caps key ONLY when its real state changed */
                int cur = (GetKeyState(VK_CAPITAL) & 1) != 0;
                if (cur != g_capsLast) { g_capsLast = cur; InvCaps(); ShcapRefresh(); }
            }
            else if (w == TIMER_REPEAT) {
                if (g_repeatIdx >= 0) {
                    vk_tap(g_repeatVk);
                    if (g_repeatFirst) {
                        g_repeatFirst = FALSE;
                        KillTimer(h, TIMER_REPEAT);
                        SetTimer(h, TIMER_REPEAT, 60, NULL);
                    }
                }
            }
            else if (w == TIMER_FLASH) { KillTimer(h, TIMER_FLASH); { int pi = g_pressed; g_pressed = -1; InvKey(pi); } }
            else if (w == TIMER_HOLD) {
                KillTimer(h, TIMER_HOLD);
                if (g_pending >= 0) ShowPopup(g_pending);
            }
            else if (w == TIMER_SHCAP) {
                KillTimer(h, TIMER_SHCAP);
                g_shcapArmed = 0; /* single tap stands as Shift one-shot */
            }
            else if (w == TIMER_PIN) {
                /* PinTop-style repair: re-assert TOPMOST, drop dead windows */
                int i, w2 = 0, changed = 0;
                for (i = 0; i < g_npinwins; i++) {
                    HWND ph = g_pinwins[i];
                    if (!IsWindow(ph)) { changed = 1; continue; }
                    if (!(GetWindowLongW(ph, GWL_EXSTYLE) & WS_EX_TOPMOST))
                        SetWindowPos(ph, HWND_TOPMOST, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    g_pinwins[w2++] = ph;
                }
                if (w2 != g_npinwins) { g_npinwins = w2; changed = 1; }
                if (changed) InvPinBtn();
            }
            return 0;
        case WM_DESTROY: {
            int i;
            SaveSettings();
            SaveClip();
            ClipClearAll();
            if (g_fgHook) { UnhookWinEvent(g_fgHook); g_fgHook = NULL; }
            for (i = 0; i < g_npinwins; i++)
                if (IsWindow(g_pinwins[i]))
                    SetWindowPos(g_pinwins[i], HWND_NOTOPMOST, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            g_npinwins = 0;
            for (i = 0; i < 4; i++)
                if (g_mods[i] != M_OFF) vk_up(MODVK[i]);
            PostQuitMessage(0);
            return 0;
        }
        case WM_ENDSESSION:
            if (w) { SaveSettings(); SaveClip(); }   /* Windows shutdown */
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* single instance: a second launch (taskbar pin clicked twice) just
 * brings the running keyboard forward instead of opening another one.
 * The mutex is auto-released if the process dies; handle kept open. */
static HANDLE g_singleton = NULL;

static void ActivateExisting(HWND h) {
    DWORD fgThread = 0, tgtThread, ourThread = GetCurrentThreadId();
    BOOL aFg = FALSE, aTg = FALSE;
    HWND fg;
    if (!IsWindow(h)) return;
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    fg = GetForegroundWindow();
    if (fg) fgThread = GetWindowThreadProcessId(fg, NULL);
    tgtThread = GetWindowThreadProcessId(h, NULL);
    if (fgThread && fgThread != ourThread)
        aFg = AttachThreadInput(ourThread, fgThread, TRUE);
    if (tgtThread && tgtThread != ourThread && tgtThread != fgThread)
        aTg = AttachThreadInput(ourThread, tgtThread, TRUE);
    if (fgThread && tgtThread && fgThread != tgtThread)
        AttachThreadInput(fgThread, tgtThread, TRUE);
    SetForegroundWindow(h);
    if (fgThread && tgtThread && fgThread != tgtThread)
        AttachThreadInput(fgThread, tgtThread, FALSE);
    if (aTg) AttachThreadInput(ourThread, tgtThread, FALSE);
    if (aFg) AttachThreadInput(ourThread, fgThread, FALSE);
    if (GetForegroundWindow() != h) {
        FLASHWINFO fi;
        fi.cbSize = sizeof(fi);
        fi.hwnd = h;
        fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
        fi.uCount = 3;
        fi.dwTimeout = 0;
        FlashWindowEx(&fi);
    }
}

/* ---------------- entry ---------------- */
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    WNDCLASSEXW wc = {0};
    g_singleton = CreateMutexW(NULL, FALSE, L"FloatKeys_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        /* already running: focus it, don't open a second keyboard */
        HWND h = FindWindowW(L"FloatKeys", NULL);
        if (h) ActivateExisting(h);
        if (g_singleton) CloseHandle(g_singleton);
        g_singleton = NULL;
        return 0;
    }
    /* DPI awareness first */
    {
        HMODULE sh = LoadLibraryW(L"shcore.dll");
        if (sh) {
            FARPROC f = GetProcAddress(sh, "SetProcessDpiAwareness");
            if (f) ((HRESULT(WINAPI*)(int))f)(1);
            FreeLibrary(sh);
        } else SetProcessDPIAware();
    }
    g_hInst = hi;
    LoadSettings();
    LoadDict(0, L"words_id.txt");
    LoadDict(1, L"words_en.txt");
    LoadBigram(0, L"bigrams_id.bin");
    LoadBigram(1, L"bigrams_en.bin");
    if (!DictAvail(g_lang)) g_lang = DictAvail(0) ? 0 : 1;
    if (!DictAvail(0) && !DictAvail(1)) g_predictOn = g_autocorrect = g_highlightOn = 0;
    if (!g_bigram[0] && !g_bigram[1]) g_bigramOn = 0;
    LoadClip();   /* restore persisted text history (images stay session-only) */
    if (g_compact) g_page = 0;
    WordClear();
    BuildKeys();
    SetModeLabel();
    RebuildFonts();
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"FloatKeys";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    /* window/taskbar icon: use embedded exe icon (keyboard.rc ID 1) */
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.cbSize = sizeof(wc);
    wc.hbrBackground = CreateSolidBrush(C_BG);
    RegisterClassExW(&wc);
    {
        int sw = GetSystemMetrics(SM_CXSCREEN), sh2 = GetSystemMetrics(SM_CYSCREEN);
        int w = BaseW(), h2 = BaseH();
        int px = sw - w - 40, py = sh2 - h2 - 80;
        if (g_hasPos) {
            /* keep the remembered position, but clamp onto the virtual screen */
            int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
            int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            px = g_posX; py = g_posY;
            if (px > vx + vw - 80) px = vx + vw - 80;
            if (py > vy + vh - 60) py = vy + vh - 60;
            if (px < vx) px = vx;
            if (py < vy) py = vy;
        }
        g_hwnd = CreateWindowExW(
            WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_COMPOSITED,
            L"FloatKeys", L"FloatKeys",
            WS_POPUP|WS_VISIBLE,
            px, py, w, h2,
            NULL, NULL, hi, NULL);
        if (!g_hwnd) return 1;
        ApplyOpacity();
        {
            RECT cl; GetClientRect(g_hwnd,&cl);
            ComputeLayout(cl.right, cl.bottom);
        }
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(g_hwnd);
        /* First-tap fix: foreground is the launcher right now, not the
         * browser search box. Peek beneath it and hand focus back so the
         * next keytap lands where the user was typing. */
        RestoreStartupFocus();
    }
    {
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}

// ============================================================================
//  鼠标连点器 MouseClicker
//  Copyright (C) 2026  Lin1848624
//  ---------------------------------------------------------------------------
//  本程序是自由软件：你可以依据自由软件基金会发布的 GNU 通用公共许可证
//  （第 3 版或你选择的任何更新版本）条款重新分发和/或修改它。
//
//  本程序基于“有用”的目的分发，但不提供任何担保，甚至不包含适销性或
//  特定用途适用性的默示担保。详见 GNU 通用公共许可证。
//
//  你应该已随本程序收到 GNU 通用公共许可证的副本；若没有，请见
//  <https://www.gnu.org/licenses/>。
//  ---------------------------------------------------------------------------
//  纯 Win32 API + GDI+ 自绘界面实现，不依赖任何第三方库，编译产物为单个 exe。
//
//  功能：
//    · 可调点击间隔（毫秒），内置 10/50/100/200/500/1000 快捷预设
//    · 鼠标左键 / 右键 / 中键
//    · 单击 / 双击 / 三击，可设置按下时长
//    · 固定重复次数 或 一直重复直到手动停止
//    · 跟随当前光标位置 或 点击固定坐标（支持倒计时拾取坐标）
//    · 全局启停热键（默认 F6，可自定义），窗口置顶，开始后自动最小化
//    · 配置自动保存到 exe 同目录的 MouseClicker.ini
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <cstdlib>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")

// 启用 Common Controls v6（现代主题外观）
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ---------------------------------------------------------------------------
//  常量
// ---------------------------------------------------------------------------
enum : int {
    IDC_INTERVAL     = 1001,
    IDC_PRESET_FIRST = 1010,   // 1010 ~ 1015  预设按钮
    IDC_BTN_LEFT     = 1020,
    IDC_BTN_RIGHT    = 1021,
    IDC_BTN_MIDDLE   = 1022,
    IDC_BTN_SINGLE   = 1030,
    IDC_BTN_DOUBLE   = 1031,
    IDC_BTN_TRIPLE   = 1032,
    IDC_HOLD         = 1040,
    IDC_BTN_FIXCOUNT = 1050,
    IDC_BTN_INFINITE = 1051,
    IDC_COUNT        = 1060,
    IDC_BTN_FOLLOW   = 1070,
    IDC_BTN_FIXED    = 1071,
    IDC_EDIT_X       = 1080,
    IDC_EDIT_Y       = 1081,
    IDC_BTN_PICK     = 1090,
    IDC_BTN_HOTKEY   = 1100,
    IDC_BTN_TOPMOST  = 1101,
    IDC_BTN_MINIMIZE = 1102,
    IDC_BTN_START    = 1110,
    IDC_BTN_STOP     = 1111,
    IDC_STATUS       = 1120,
    IDC_LBL_FIRST    = 1200,   // 1200 ~ 1210 静态文字
};

#define WM_APP_WORKER_DONE (WM_APP + 1)
#define TIMER_UI           1
#define TIMER_PICK         2
#define HOTKEY_ID          0xA1

// 客户区逻辑尺寸（96 DPI 基准）
static const int CLIENT_W = 460;
static const int CLIENT_H = 620;

// 双击 / 三击动作内部的连击间隔（毫秒）
static const int MULTI_GAP_MS = 30;
// 坐标拾取倒计时秒数
static const int PICK_SECONDS = 3;

// ---------------------------------------------------------------------------
//  配色
// ---------------------------------------------------------------------------
namespace Col {
const COLORREF BG        = RGB(0x1A, 0x1A, 0x22);   // 窗口背景
const COLORREF EDIT_BG   = RGB(0x2A, 0x2A, 0x38);   // 输入框背景
const COLORREF EDIT_FG   = RGB(0xEC, 0xEC, 0xF4);   // 输入框文字
const COLORREF LINE      = RGB(0x2E, 0x2E, 0x3E);   // 分隔线
const COLORREF TEXT      = RGB(0xE4, 0xE4, 0xEE);   // 正文
const COLORREF TEXT_SUB  = RGB(0x82, 0x82, 0xA0);   // 分组小标题
const COLORREF TEXT_DIM  = RGB(0x9A, 0x9A, 0xB2);   // 次要文字
const COLORREF ACCENT    = RGB(0x3D, 0x7E, 0xFF);   // 主题蓝
const COLORREF ACCENT_HI = RGB(0x5B, 0x93, 0xFF);
const COLORREF ACCENT_LO = RGB(0x2C, 0x63, 0xD6);
const COLORREF DANGER    = RGB(0xDD, 0x4A, 0x4A);   // 停止按钮
const COLORREF DANGER_HI = RGB(0xEE, 0x5C, 0x5C);
const COLORREF DANGER_LO = RGB(0xB8, 0x38, 0x38);
const COLORREF OK        = RGB(0x38, 0xC1, 0x76);   // 状态-运行
const COLORREF WARN      = RGB(0xE0, 0xA8, 0x3C);   // 状态-异常
const COLORREF IDLE_DOT  = RGB(0x6A, 0x6A, 0x86);   // 状态-就绪
const COLORREF BTN_BG    = RGB(0x2A, 0x2A, 0x38);
const COLORREF BTN_HOVER = RGB(0x35, 0x35, 0x47);
const COLORREF BTN_DOWN  = RGB(0x21, 0x21, 0x2D);
const COLORREF BTN_LINE  = RGB(0x3A, 0x3A, 0x4C);
const COLORREF DIS_FG    = RGB(0x55, 0x55, 0x6E);   // 禁用文字
const COLORREF DIS_BG    = RGB(0x23, 0x23, 0x2E);
} // namespace Col

// ---------------------------------------------------------------------------
//  按钮样式
// ---------------------------------------------------------------------------
enum BtnKind {
    BSK_NORMAL,     // 普通按钮
    BSK_PRESET,     // 预设小按钮
    BSK_PRIMARY,    // 主操作（开始）
    BSK_DANGER,     // 停止
    BSK_SEG_OFF,    // 分段控件-未选中
    BSK_SEG_ON,     // 分段控件-选中
    BSK_TOG_OFF,    // 开关-关
    BSK_TOG_ON,     // 开关-开
};

struct BtnData {
    BtnKind kind = BSK_NORMAL;
    bool    hover = false;
};

// ---------------------------------------------------------------------------
//  配置
// ---------------------------------------------------------------------------
struct Config {
    int  intervalMs      = 100;   // 点击间隔（毫秒）
    int  button          = 0;     // 0 左键  1 右键  2 中键
    int  clickType       = 0;     // 0 单击  1 双击  2 三击
    int  holdMs          = 1;     // 按下时长（毫秒），0 表示极速
    bool infinite        = true;  // 是否无限重复
    int  repeatCount     = 100;   // 固定重复次数（动作数）
    bool useFixedPos     = false; // 是否使用固定坐标
    int  posX            = 0;
    int  posY            = 0;
    UINT hotkeyVk        = VK_F6;
    UINT hotkeyMod       = 0;
    bool topmost         = false;
    bool minimizeOnStart = false;
};

// ---------------------------------------------------------------------------
//  全局状态
// ---------------------------------------------------------------------------
static HINSTANCE g_hInst   = nullptr;
static HWND      g_hWnd    = nullptr;
static int       g_dpi     = 96;
static HFONT     g_fontUI  = nullptr;   // 控件字体
static HBRUSH    g_brBg    = nullptr;   // 窗口背景画刷
static HBRUSH    g_brEdit  = nullptr;   // 输入框画刷
static ULONG_PTR g_gdiplusToken = 0;

static Config g_cfg;

static std::atomic<bool>      g_running{false};
static std::atomic<bool>      g_stopReq{false};
static std::atomic<long long> g_clicks{0};      // 已完成的单次点击总数
static std::atomic<long long> g_actions{0};     // 已完成的动作数
static std::thread            g_worker;

static bool g_hotkeyRegistered = false;
static bool g_hotkeyTried      = false;   // 是否已经尝试过注册（用于区分"未注册"与"注册失败"）
static bool g_capturingHotkey  = false;   // 是否正在等待用户按下新热键
static int  g_pickRemain       = 0;       // 坐标拾取剩余秒数

static std::unordered_map<HWND, BtnData> g_btns;

struct UiHandles {
    HWND interval = nullptr;
    HWND preset[6] = {};
    HWND bLeft = nullptr, bRight = nullptr, bMiddle = nullptr;
    HWND bSingle = nullptr, bDouble = nullptr, bTriple = nullptr;
    HWND hold = nullptr;
    HWND bFixCount = nullptr, bInfinite = nullptr;
    HWND count = nullptr;
    HWND bFollow = nullptr, bFixed = nullptr;
    HWND editX = nullptr, editY = nullptr, bPick = nullptr;
    HWND bHotkey = nullptr, bTopmost = nullptr, bMinimize = nullptr;
    HWND bStart = nullptr, bStop = nullptr;
    HWND status = nullptr;
    HWND lbl[12] = {};
};
static UiHandles g_ui;

// ---------------------------------------------------------------------------
//  小工具
// ---------------------------------------------------------------------------
static inline int S(int v) { return MulDiv(v, g_dpi, 96); }   // 逻辑像素 -> 物理像素

static void ResetFonts()
{
    if (g_fontUI) { DeleteObject(g_fontUI); g_fontUI = nullptr; }
    g_fontUI = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                           L"Microsoft YaHei UI");
}

// GDI+ 字体缓存（程序生命周期内不释放，进程退出时由系统回收）
static Gdiplus::Font* UiFont(int px, bool bold)
{
    static std::unordered_map<int, Gdiplus::Font*> cache;
    const int key = px * 2 + (bold ? 1 : 0);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    Gdiplus::Font* f = new Gdiplus::Font(L"Microsoft YaHei UI", (Gdiplus::REAL)px,
                                         bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                                         Gdiplus::UnitPixel);
    if (f->GetLastStatus() != Gdiplus::Ok) {
        delete f;
        f = new Gdiplus::Font(L"Microsoft Sans Serif", (Gdiplus::REAL)px,
                              bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                              Gdiplus::UnitPixel);
    }
    cache[key] = f;
    return f;
}

static inline Gdiplus::Color GC(COLORREF c, BYTE a = 255)
{
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

static void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, float rad)
{
    path.Reset();
    const float d = rad * 2.0f;
    if (d <= 0.0f) { path.AddRectangle(r); return; }
    const float x = r.X, y = r.Y, w = r.Width, h = r.Height;
    if (d > w) rad = w / 2.0f;
    if (d > h) rad = h / 2.0f;
    const float dd = rad * 2.0f;
    path.AddArc(x, y, dd, dd, 180.0f, 90.0f);
    path.AddArc(x + w - dd, y, dd, dd, 270.0f, 90.0f);
    path.AddArc(x + w - dd, y + h - dd, dd, dd, 0.0f, 90.0f);
    path.AddArc(x, y + h - dd, dd, dd, 90.0f, 90.0f);
    path.CloseFigure();
}

// ---------------------------------------------------------------------------
//  控件字体 / 文本读取
// ---------------------------------------------------------------------------
static void ApplyFont(HWND h)
{
    if (h && g_fontUI) SendMessageW(h, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
}

static int GetEditInt(HWND h, int def)
{
    wchar_t buf[64] = {};
    GetWindowTextW(h, buf, 64);
    if (!buf[0]) return def;
    wchar_t* end = nullptr;
    long v = wcstol(buf, &end, 10);
    if (end == buf) return def;              // 完全没有数字
    while (*end == L' ') ++end;
    if (*end != L'\0') return def;           // 含非法字符
    // 保留负值：多显示器环境下副屏坐标可能为负
    if (v < -1000000L)    v = -1000000L;
    if (v > 100000000L)   v = 100000000L;
    return (int)v;
}

static void SetEditInt(HWND h, int v)
{
    wchar_t buf[32] = {};
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", v);
    SetWindowTextW(h, buf);
}

// ---------------------------------------------------------------------------
//  配置文件
// ---------------------------------------------------------------------------
static std::wstring GetIniPath()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    const size_t p = s.find_last_of(L"\\/");
    if (p != std::wstring::npos) s = s.substr(0, p + 1);
    return s + L"MouseClicker.ini";
}

static void LoadConfig()
{
    const std::wstring ini = GetIniPath();
    const wchar_t* sec = L"Settings";
    g_cfg.intervalMs      = GetPrivateProfileIntW(sec, L"Interval",     100, ini.c_str());
    g_cfg.button          = GetPrivateProfileIntW(sec, L"Button",         0, ini.c_str());
    g_cfg.clickType       = GetPrivateProfileIntW(sec, L"ClickType",      0, ini.c_str());
    g_cfg.holdMs          = GetPrivateProfileIntW(sec, L"HoldMs",         1, ini.c_str());
    g_cfg.infinite        = GetPrivateProfileIntW(sec, L"Infinite",       1, ini.c_str()) != 0;
    g_cfg.repeatCount     = GetPrivateProfileIntW(sec, L"RepeatCount",  100, ini.c_str());
    g_cfg.useFixedPos     = GetPrivateProfileIntW(sec, L"UseFixedPos",    0, ini.c_str()) != 0;
    g_cfg.posX            = GetPrivateProfileIntW(sec, L"PosX",           0, ini.c_str());
    g_cfg.posY            = GetPrivateProfileIntW(sec, L"PosY",           0, ini.c_str());
    g_cfg.hotkeyVk        = (UINT)GetPrivateProfileIntW(sec, L"HotkeyVk", VK_F6, ini.c_str());
    g_cfg.hotkeyMod       = (UINT)GetPrivateProfileIntW(sec, L"HotkeyMod",  0, ini.c_str());
    g_cfg.topmost         = GetPrivateProfileIntW(sec, L"Topmost",        0, ini.c_str()) != 0;
    g_cfg.minimizeOnStart = GetPrivateProfileIntW(sec, L"MinimizeOnStart", 0, ini.c_str()) != 0;

    // 合法性收敛
    if (g_cfg.intervalMs < 1)      g_cfg.intervalMs = 1;
    if (g_cfg.intervalMs > 86400000) g_cfg.intervalMs = 86400000;
    if (g_cfg.button < 0 || g_cfg.button > 2)      g_cfg.button = 0;
    if (g_cfg.clickType < 0 || g_cfg.clickType > 2) g_cfg.clickType = 0;
    if (g_cfg.holdMs < 0)   g_cfg.holdMs = 0;
    if (g_cfg.holdMs > 1000) g_cfg.holdMs = 1000;
    if (g_cfg.repeatCount < 1) g_cfg.repeatCount = 1;
    if (g_cfg.hotkeyVk == 0) g_cfg.hotkeyVk = VK_F6;
    g_cfg.hotkeyMod &= (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN);
}

static void SaveConfig()
{
    const std::wstring ini = GetIniPath();
    const wchar_t* sec = L"Settings";
    wchar_t buf[32];

    auto put = [&](const wchar_t* key, int v) {
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", v);
        WritePrivateProfileStringW(sec, key, buf, ini.c_str());
    };
    put(L"Interval",      g_cfg.intervalMs);
    put(L"Button",        g_cfg.button);
    put(L"ClickType",     g_cfg.clickType);
    put(L"HoldMs",        g_cfg.holdMs);
    put(L"Infinite",      g_cfg.infinite ? 1 : 0);
    put(L"RepeatCount",   g_cfg.repeatCount);
    put(L"UseFixedPos",   g_cfg.useFixedPos ? 1 : 0);
    put(L"PosX",          g_cfg.posX);
    put(L"PosY",          g_cfg.posY);
    put(L"HotkeyVk",      (int)g_cfg.hotkeyVk);
    put(L"HotkeyMod",     (int)g_cfg.hotkeyMod);
    put(L"Topmost",       g_cfg.topmost ? 1 : 0);
    put(L"MinimizeOnStart", g_cfg.minimizeOnStart ? 1 : 0);
}

// ---------------------------------------------------------------------------
//  鼠标连点核心
// ---------------------------------------------------------------------------
static void SendOneClick(int button, int holdMs)
{
    DWORD downFlag = MOUSEEVENTF_LEFTDOWN, upFlag = MOUSEEVENTF_LEFTUP;
    switch (button) {
        case 1: downFlag = MOUSEEVENTF_RIGHTDOWN;  upFlag = MOUSEEVENTF_RIGHTUP;  break;
        case 2: downFlag = MOUSEEVENTF_MIDDLEDOWN; upFlag = MOUSEEVENTF_MIDDLEUP; break;
        default: break;
    }
    if (holdMs <= 0) {
        // 极速：一次 SendInput 同时投递按下与抬起，兼容性最好
        INPUT in[2] = {};
        in[0].type = INPUT_MOUSE; in[0].mi.dwFlags = downFlag;
        in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = upFlag;
        SendInput(2, in, sizeof(INPUT));
    } else {
        INPUT d = {};
        d.type = INPUT_MOUSE; d.mi.dwFlags = downFlag;
        SendInput(1, &d, sizeof(INPUT));
        Sleep((DWORD)holdMs);
        INPUT u = {};
        u.type = INPUT_MOUSE; u.mi.dwFlags = upFlag;
        SendInput(1, &u, sizeof(INPUT));
    }
}

// 可中断的高精度等待；返回 false 表示等待期间收到停止请求
static bool PreciseWait(int ms, const std::atomic<bool>& stop)
{
    if (ms <= 0) return !stop.load(std::memory_order_relaxed);
    const auto t0 = std::chrono::steady_clock::now();
    const long long totalUs = (long long)ms * 1000;
    for (;;) {
        if (stop.load(std::memory_order_relaxed)) return false;
        const long long elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - t0).count();
        const long long remain = totalUs - elapsed;
        if (remain <= 0) return true;
        if (remain > 2000) {
            DWORD s = (DWORD)((remain - 1000) / 1000);
            if (s > 25) s = 25;          // 分片睡眠，保证停止响应不超过 ~25ms
            if (s > 0) Sleep(s);
            else       std::this_thread::yield();
        } else {
            std::this_thread::yield();   // 末尾自旋，把误差压到微秒级
        }
    }
}

static void ClickWorker(Config cfg)
{
    timeBeginPeriod(1);                  // 提升系统定时器精度
    const int perAction = cfg.clickType + 1;

    while (!g_stopReq.load(std::memory_order_relaxed)) {
        const auto actionStart = std::chrono::steady_clock::now();

        if (cfg.useFixedPos) {
            SetCursorPos(cfg.posX, cfg.posY);
            if (!PreciseWait(1, g_stopReq)) break;
        }

        bool aborted = false;
        for (int i = 0; i < perAction; ++i) {
            if (g_stopReq.load(std::memory_order_relaxed)) { aborted = true; break; }
            if (i > 0 && !PreciseWait(MULTI_GAP_MS, g_stopReq)) { aborted = true; break; }
            SendOneClick(cfg.button, cfg.holdMs);
            g_clicks.fetch_add(1, std::memory_order_relaxed);
        }
        if (aborted) break;

        g_actions.fetch_add(1, std::memory_order_relaxed);

        if (!cfg.infinite && g_actions.load(std::memory_order_relaxed) >= (long long)cfg.repeatCount)
            break;

        // 间隔按「上一次动作开始 -> 本次动作开始」计算
        const long long usedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - actionStart).count();
        const long long remain = (long long)cfg.intervalMs - usedMs;
        if (remain > 0 && !PreciseWait((int)remain, g_stopReq)) break;
    }

    timeEndPeriod(1);
    g_running.store(false);
    if (g_hWnd) PostMessageW(g_hWnd, WM_APP_WORKER_DONE, 0, 0);
}

// ---------------------------------------------------------------------------
//  热键
// ---------------------------------------------------------------------------
static std::wstring KeyName(UINT vk)
{
    switch (vk) {
        case VK_LBUTTON:  return L"鼠标左键";
        case VK_RBUTTON:  return L"鼠标右键";
        case VK_MBUTTON:  return L"鼠标中键";
        case VK_CANCEL:   return L"Break";
        case VK_BACK:     return L"Backspace";
        case VK_TAB:      return L"Tab";
        case VK_RETURN:   return L"Enter";
        case VK_ESCAPE:   return L"Esc";
        case VK_SPACE:    return L"Space";
        case VK_PRIOR:    return L"PageUp";
        case VK_NEXT:     return L"PageDown";
        case VK_END:      return L"End";
        case VK_HOME:     return L"Home";
        case VK_INSERT:   return L"Insert";
        case VK_DELETE:   return L"Delete";
        case VK_SNAPSHOT: return L"PrintScreen";
        case VK_OEM_3:    return L"`";
        case VK_OEM_MINUS:return L"-";
        case VK_OEM_PLUS: return L"=";
        default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        wchar_t b[8];
        _snwprintf_s(b, _countof(b), _TRUNCATE, L"F%u", vk - VK_F1 + 1);
        return b;
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        return std::wstring(1, (wchar_t)vk);
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        wchar_t b[16];
        _snwprintf_s(b, _countof(b), _TRUNCATE, L"小键盘%u", vk - VK_NUMPAD0);
        return b;
    }
    UINT ch = MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR);
    if (ch >= 32 && ch < 127) return std::wstring(1, (wchar_t)ch);
    wchar_t b[16];
    _snwprintf_s(b, _countof(b), _TRUNCATE, L"按键%u", vk);
    return b;
}

static std::wstring HotkeyText(UINT mod, UINT vk)
{
    std::wstring s;
    if (mod & MOD_CONTROL) s += L"Ctrl+";
    if (mod & MOD_ALT)     s += L"Alt+";
    if (mod & MOD_SHIFT)   s += L"Shift+";
    if (mod & MOD_WIN)     s += L"Win+";
    s += KeyName(vk);
    return s;
}

// 热键必须是功能键，或带有至少一个修饰键，避免吞掉普通按键输入
static bool IsHotkeyAllowed(UINT mod, UINT vk)
{
    if (vk >= VK_F1 && vk <= VK_F24) return true;
    if (mod & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN)) {
        // 修饰键本身不能作为主键
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
            vk == VK_LWIN  || vk == VK_RWIN) return false;
        return true;
    }
    return false;
}

static bool RegisterAppHotkey()
{
    if (g_hotkeyRegistered) {
        UnregisterHotKey(g_hWnd, HOTKEY_ID);
        g_hotkeyRegistered = false;
    }
    g_hotkeyTried = true;
    if (RegisterHotKey(g_hWnd, HOTKEY_ID, g_cfg.hotkeyMod | MOD_NOREPEAT, g_cfg.hotkeyVk)) {
        g_hotkeyRegistered = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  按钮绘制
// ---------------------------------------------------------------------------
static void PaintButton(const DRAWITEMSTRUCT* dis)
{
    auto it = g_btns.find(dis->hwndItem);
    const BtnKind kind = (it != g_btns.end()) ? it->second.kind : BSK_NORMAL;
    const bool hover    = (it != g_btns.end()) && it->second.hover;
    const bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    // 取控件文字
    wchar_t text[128] = {};
    GetWindowTextW(dis->hwndItem, text, _countof(text));

    COLORREF bg = Col::BTN_BG, line = Col::BTN_LINE, fg = Col::TEXT;
    bool drawLine = true;

    switch (kind) {
        case BSK_PRIMARY:
            bg = pressed ? Col::ACCENT_LO : (hover ? Col::ACCENT_HI : Col::ACCENT);
            fg = RGB(0xFF, 0xFF, 0xFF); drawLine = false;
            break;
        case BSK_DANGER:
            bg = pressed ? Col::DANGER_LO : (hover ? Col::DANGER_HI : Col::DANGER);
            fg = RGB(0xFF, 0xFF, 0xFF); drawLine = false;
            break;
        case BSK_SEG_OFF:
            bg = pressed ? Col::BTN_DOWN : (hover ? Col::BTN_HOVER : Col::EDIT_BG);
            fg = Col::TEXT_DIM;
            break;
        case BSK_SEG_ON:
            bg = pressed ? Col::ACCENT_LO : (hover ? Col::ACCENT_HI : Col::ACCENT);
            fg = RGB(0xFF, 0xFF, 0xFF); line = Col::ACCENT; drawLine = false;
            break;
        case BSK_TOG_OFF:
            bg = pressed ? Col::BTN_DOWN : (hover ? Col::BTN_HOVER : Col::BTN_BG);
            fg = Col::TEXT_DIM;
            break;
        case BSK_TOG_ON:
            bg = pressed ? RGB(0x1E, 0x38, 0x60) : (hover ? RGB(0x27, 0x46, 0x74) : RGB(0x22, 0x3E, 0x68));
            line = Col::ACCENT; fg = Col::ACCENT_HI;
            break;
        default: // BSK_NORMAL / BSK_PRESET
            bg = pressed ? Col::BTN_DOWN : (hover ? Col::BTN_HOVER : Col::BTN_BG);
            fg = Col::TEXT;
            break;
    }

    if (disabled) {
        if (kind == BSK_PRIMARY)      { bg = RGB(0x25, 0x36, 0x4E); fg = RGB(0x5C, 0x76, 0x99); }
        else if (kind == BSK_DANGER)  { bg = RGB(0x45, 0x28, 0x2C); fg = RGB(0x8A, 0x60, 0x64); }
        else                          { bg = Col::DIS_BG; fg = Col::DIS_FG; line = RGB(0x2C, 0x2C, 0x3A); }
    }

    Gdiplus::Graphics g(dis->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    const RECT& rc = dis->rcItem;
    Gdiplus::RectF rr((Gdiplus::REAL)rc.left + 0.5f, (Gdiplus::REAL)rc.top + 0.5f,
                      (Gdiplus::REAL)(rc.right - rc.left) - 1.0f,
                      (Gdiplus::REAL)(rc.bottom - rc.top) - 1.0f);

    const float radius = (kind == BSK_PRIMARY || kind == BSK_DANGER) ? (float)S(8) : (float)S(6);

    Gdiplus::GraphicsPath path;
    AddRoundRect(path, rr, radius);

    Gdiplus::SolidBrush brush(GC(bg));
    g.FillPath(&brush, &path);

    if (drawLine) {
        Gdiplus::Pen pen(GC(line), 1.0f);
        g.DrawPath(&pen, &path);
    }

    if (text[0]) {
        int fontPx = S(14);
        bool bold = false;
        if (kind == BSK_PRIMARY || kind == BSK_DANGER) { fontPx = S(16); bold = true; }
        else if (kind == BSK_PRESET) { fontPx = S(13); }

        Gdiplus::Font* font = UiFont(fontPx, bold);
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        Gdiplus::SolidBrush tb(GC(fg));
        Gdiplus::RectF tr((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                          (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));
        g.DrawString(text, -1, font, tr, &fmt, &tb);
    }
}

// 状态行绘制
static void PaintStatus(const DRAWITEMSTRUCT* dis)
{
    const RECT& rc = dis->rcItem;
    Gdiplus::Graphics g(dis->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // 先铺底色：文字长度变化（如坐标位数变少）时不会留下残影
    Gdiplus::SolidBrush bgBrush(GC(Col::BG));
    g.FillRectangle(&bgBrush, (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                    (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));

    const bool running = g_running.load();
    COLORREF dot = Col::IDLE_DOT;
    const wchar_t* stateText = L"就绪";
    if (running) { dot = Col::OK; stateText = L"连点中"; }
    else if (!g_hotkeyRegistered) { dot = Col::WARN; stateText = L"就绪（热键未生效）"; }

    // 左侧状态圆点
    const float cy = (float)(rc.top + rc.bottom) / 2.0f;
    const float r  = (float)S(4);
    Gdiplus::SolidBrush db(GC(dot));
    g.FillEllipse(&db, (Gdiplus::REAL)rc.left, cy - r, r * 2, r * 2);

    Gdiplus::Font* font = UiFont(S(13), false);
    Gdiplus::StringFormat lf;
    lf.SetAlignment(Gdiplus::StringAlignmentNear);
    lf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    lf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    Gdiplus::SolidBrush tb(GC(running ? Col::TEXT : Col::TEXT_DIM));
    Gdiplus::RectF tr((Gdiplus::REAL)rc.left + r * 2 + S(8), (Gdiplus::REAL)rc.top,
                      (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));
    g.DrawString(stateText, -1, font, tr, &lf, &tb);

    // 右侧统计
    wchar_t buf[128];
    const long long clicks = g_clicks.load();
    POINT pt;
    GetCursorPos(&pt);
    if (running || clicks > 0)
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"已点击 %lld 次 · 光标 (%ld, %ld)",
                     clicks, pt.x, pt.y);
    else
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"光标 (%ld, %ld)", pt.x, pt.y);

    Gdiplus::StringFormat rf;
    rf.SetAlignment(Gdiplus::StringAlignmentFar);
    rf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    rf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::SolidBrush tb2(GC(Col::TEXT_DIM));
    Gdiplus::RectF tr2((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                       (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));
    g.DrawString(buf, -1, font, tr2, &rf, &tb2);
}

// ---------------------------------------------------------------------------
//  按钮悬停跟踪
// ---------------------------------------------------------------------------
static LRESULT CALLBACK BtnSubclassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                        UINT_PTR, DWORD_PTR)
{
    switch (msg) {
        case WM_MOUSEMOVE: {
            auto& bd = g_btns[h];
            if (!bd.hover) {
                bd.hover = true;
                InvalidateRect(h, nullptr, FALSE);
                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = h;
                TrackMouseEvent(&tme);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            auto it = g_btns.find(h);
            if (it != g_btns.end() && it->second.hover) {
                it->second.hover = false;
                InvalidateRect(h, nullptr, FALSE);
            }
            break;
        }
        case WM_SETFOCUS:
            // 不让按钮拿走键盘焦点，保证主窗口能收到热键捕获按键
            return 0;
        default:
            break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//  控件创建与布局
// ---------------------------------------------------------------------------
static void PlaceCtl(HWND& h, const wchar_t* cls, const wchar_t* text, DWORD style,
                     DWORD exStyle, int id, int x, int y, int w, int ht, BtnKind kind)
{
    const int px = S(x), py = S(y), pw = S(w), ph = S(ht);
    if (!h) {
        h = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                            px, py, pw, ph, g_hWnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
        if (!h) return;
        if (wcscmp(cls, L"Button") == 0) {
            g_btns[h].kind = kind;
            SetWindowSubclass(h, BtnSubclassProc, 0, 0);
        }
    } else {
        // 仅移动：文字由各自的 Sync* 函数维护，重新布局时不能覆盖
        MoveWindow(h, px, py, pw, ph, TRUE);
    }
    ApplyFont(h);
}

static void PlaceLabel(HWND& h, const wchar_t* text, int x, int y, int w, int ht, int id)
{
    PlaceCtl(h, L"Static", text, SS_LEFT | SS_CENTERIMAGE, 0, id, x, y, w, ht, BSK_NORMAL);
}

static void PlaceBtn(HWND& h, const wchar_t* text, int id, int x, int y, int w, int ht, BtnKind kind)
{
    PlaceCtl(h, L"Button", text, BS_OWNERDRAW | BS_PUSHBUTTON, 0, id, x, y, w, ht, kind);
}

static void PlaceEdit(HWND& h, int id, int x, int y, int w, int ht, const wchar_t* text)
{
    PlaceCtl(h, L"Edit", text, ES_AUTOHSCROLL | ES_CENTER, 0, id, x, y, w, ht, BSK_NORMAL);
    if (h) {
        SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(6), S(6)));
    }
}

static void LayoutControls()
{
    // 标签句柄索引：局部变量，保证重复调用（如 DPI 变化）不会越界
    int li = 0;

    // --- 点击间隔 ---
    PlaceLabel(g_ui.lbl[li++], L"点击间隔",
               20, 14, 200, 20, IDC_LBL_FIRST + 0);
    PlaceEdit(g_ui.interval, IDC_INTERVAL, 20, 38, 90, 32, L"100");
    PlaceLabel(g_ui.lbl[li++], L"毫秒",
               114, 38, 44, 32, IDC_LBL_FIRST + 1);

    static const wchar_t* presetText[6] = { L"10", L"50", L"100", L"200", L"500", L"1s" };
    for (int i = 0; i < 6; ++i) {
        PlaceBtn(g_ui.preset[i], presetText[i], IDC_PRESET_FIRST + i,
                 163 + i * 47, 38, 42, 32, BSK_PRESET);
    }

    // --- 鼠标按键 ---
    PlaceLabel(g_ui.lbl[li++], L"鼠标按键",
               20, 88, 200, 20, IDC_LBL_FIRST + 2);
    PlaceBtn(g_ui.bLeft,   L"左键", IDC_BTN_LEFT,   20, 110, 100, 34, BSK_SEG_OFF);
    PlaceBtn(g_ui.bRight,  L"右键", IDC_BTN_RIGHT, 130, 110, 100, 34, BSK_SEG_OFF);
    PlaceBtn(g_ui.bMiddle, L"中键", IDC_BTN_MIDDLE,240, 110, 100, 34, BSK_SEG_OFF);

    // --- 点击方式 ---
    PlaceLabel(g_ui.lbl[li++], L"点击方式",
               20, 156, 200, 20, IDC_LBL_FIRST + 3);
    PlaceLabel(g_ui.lbl[li++], L"按下时长（毫秒）",
               300, 156, 140, 20, IDC_LBL_FIRST + 4);
    PlaceBtn(g_ui.bSingle, L"单击", IDC_BTN_SINGLE,  20, 178, 80, 34, BSK_SEG_OFF);
    PlaceBtn(g_ui.bDouble, L"双击", IDC_BTN_DOUBLE, 108, 178, 80, 34, BSK_SEG_OFF);
    PlaceBtn(g_ui.bTriple, L"三击", IDC_BTN_TRIPLE, 196, 178, 80, 34, BSK_SEG_OFF);
    PlaceEdit(g_ui.hold, IDC_HOLD, 300, 178, 70, 34, L"1");

    // --- 重复方式 ---
    PlaceLabel(g_ui.lbl[li++], L"重复方式",
               20, 232, 200, 20, IDC_LBL_FIRST + 5);
    PlaceBtn(g_ui.bFixCount, L"固定次数", IDC_BTN_FIXCOUNT,  20, 254, 100, 34, BSK_SEG_OFF);
    PlaceBtn(g_ui.bInfinite, L"一直重复", IDC_BTN_INFINITE, 130, 254, 110, 34, BSK_SEG_OFF);
    PlaceEdit(g_ui.count, IDC_COUNT, 254, 254, 70, 34, L"100");
    PlaceLabel(g_ui.lbl[li++], L"次",
               330, 254, 24, 34, IDC_LBL_FIRST + 6);

    // --- 点击位置 ---
    PlaceLabel(g_ui.lbl[li++], L"点击位置",
               20, 308, 200, 20, IDC_LBL_FIRST + 7);
    PlaceBtn(g_ui.bFollow, L"跟随当前鼠标位置", IDC_BTN_FOLLOW, 20, 330, 150, 34, BSK_SEG_OFF);
    PlaceBtn(g_ui.bFixed,  L"固定坐标",         IDC_BTN_FIXED, 180, 330, 110, 34, BSK_SEG_OFF);
    PlaceLabel(g_ui.lbl[li++], L"X", 20, 372, 16, 32, IDC_LBL_FIRST + 8);
    PlaceEdit(g_ui.editX, IDC_EDIT_X, 40, 372, 70, 32, L"0");
    PlaceLabel(g_ui.lbl[li++], L"Y", 122, 372, 16, 32, IDC_LBL_FIRST + 9);
    PlaceEdit(g_ui.editY, IDC_EDIT_Y, 142, 372, 70, 32, L"0");
    PlaceBtn(g_ui.bPick, L"拾取坐标", IDC_BTN_PICK, 240, 372, 100, 32, BSK_NORMAL);

    // --- 其他设置 ---
    PlaceLabel(g_ui.lbl[li++], L"其他设置",
               20, 418, 200, 20, IDC_LBL_FIRST + 10);
    PlaceBtn(g_ui.bHotkey,   L"", IDC_BTN_HOTKEY,   20, 440, 140, 34, BSK_NORMAL);
    PlaceBtn(g_ui.bTopmost,  L"窗口置顶", IDC_BTN_TOPMOST, 170, 440, 100, 34, BSK_TOG_OFF);
    PlaceBtn(g_ui.bMinimize, L"开始后最小化", IDC_BTN_MINIMIZE, 280, 440, 160, 34, BSK_TOG_OFF);

    // --- 操作按钮 ---
    PlaceBtn(g_ui.bStart, L"开始连点", IDC_BTN_START, 20, 492, 250, 48, BSK_PRIMARY);
    PlaceBtn(g_ui.bStop,  L"停止",     IDC_BTN_STOP, 280, 492, 160, 48, BSK_DANGER);

    // --- 状态行 ---
    PlaceCtl(g_ui.status, L"Static", L"", SS_OWNERDRAW, 0, IDC_STATUS, 20, 566, 420, 22, BSK_NORMAL);
}

// ---------------------------------------------------------------------------
//  界面状态同步
// ---------------------------------------------------------------------------
static void SyncHotkeyButton()
{
    std::wstring t;
    if (g_capturingHotkey) {
        t = L"请按下新热键…";
    } else if (!g_hotkeyRegistered && g_hotkeyTried) {
        t = L"热键被占用(" + HotkeyText(g_cfg.hotkeyMod, g_cfg.hotkeyVk) + L")";
    } else {
        t = L"启停热键: " + HotkeyText(g_cfg.hotkeyMod, g_cfg.hotkeyVk);
    }
    SetWindowTextW(g_ui.bHotkey, t.c_str());
    InvalidateRect(g_ui.bHotkey, nullptr, FALSE);
}

static void SyncToggleButtons()
{
    auto set = [](HWND h, bool on) {
        if (!h) return;
        g_btns[h].kind = on ? BSK_TOG_ON : BSK_TOG_OFF;
        InvalidateRect(h, nullptr, FALSE);
    };
    set(g_ui.bTopmost,  g_cfg.topmost);
    set(g_ui.bMinimize, g_cfg.minimizeOnStart);
}

static void SyncSegButtons()
{
    auto set = [](HWND h, bool on) {
        if (!h) return;
        g_btns[h].kind = on ? BSK_SEG_ON : BSK_SEG_OFF;
        InvalidateRect(h, nullptr, FALSE);
    };
    set(g_ui.bLeft,   g_cfg.button == 0);
    set(g_ui.bRight,  g_cfg.button == 1);
    set(g_ui.bMiddle, g_cfg.button == 2);

    set(g_ui.bSingle, g_cfg.clickType == 0);
    set(g_ui.bDouble, g_cfg.clickType == 1);
    set(g_ui.bTriple, g_cfg.clickType == 2);

    set(g_ui.bFixCount, !g_cfg.infinite);
    set(g_ui.bInfinite, g_cfg.infinite);

    set(g_ui.bFollow, !g_cfg.useFixedPos);
    set(g_ui.bFixed,  g_cfg.useFixedPos);

    EnableWindow(g_ui.count, !g_cfg.infinite);
    EnableWindow(g_ui.editX, g_cfg.useFixedPos);
    EnableWindow(g_ui.editY, g_cfg.useFixedPos);
    EnableWindow(g_ui.bPick, g_cfg.useFixedPos);
}

static void SyncRunButtons()
{
    const bool running = g_running.load();
    EnableWindow(g_ui.bStart, !running);
    EnableWindow(g_ui.bStop, running);
    InvalidateRect(g_ui.bStart, nullptr, FALSE);
    InvalidateRect(g_ui.bStop, nullptr, FALSE);
    InvalidateRect(g_ui.status, nullptr, FALSE);
    if (g_hWnd) {
        // 最小化后也能从任务栏看出当前是否在连点
        SetWindowTextW(g_hWnd, running ? L"鼠标连点器 — 连点中…" : L"鼠标连点器");
    }
}

// 把控件上的当前输入同步进 g_cfg（启动前调用）
static void HarvestUi()
{
    g_cfg.intervalMs  = GetEditInt(g_ui.interval, g_cfg.intervalMs);
    if (g_cfg.intervalMs < 1)        g_cfg.intervalMs = 1;
    if (g_cfg.intervalMs > 86400000) g_cfg.intervalMs = 86400000;   // 上限 24 小时
    g_cfg.holdMs      = GetEditInt(g_ui.hold, g_cfg.holdMs);
    if (g_cfg.holdMs < 0)    g_cfg.holdMs = 0;
    if (g_cfg.holdMs > 1000) g_cfg.holdMs = 1000;
    g_cfg.repeatCount = GetEditInt(g_ui.count, g_cfg.repeatCount);
    if (g_cfg.repeatCount < 1) g_cfg.repeatCount = 1;
    g_cfg.posX        = GetEditInt(g_ui.editX, g_cfg.posX);
    g_cfg.posY        = GetEditInt(g_ui.editY, g_cfg.posY);
}

// ---------------------------------------------------------------------------
//  启停
// ---------------------------------------------------------------------------
static void StartClicking()
{
    if (g_running.load()) return;
    HarvestUi();
    SaveConfig();

    g_clicks.store(0);
    g_actions.store(0);
    g_stopReq.store(false);
    g_running.store(true);

    if (g_worker.joinable()) g_worker.join();
    g_worker = std::thread(ClickWorker, g_cfg);

    SyncRunButtons();
    if (g_cfg.minimizeOnStart) ShowWindow(g_hWnd, SW_MINIMIZE);
}

static void StopClicking()
{
    if (!g_running.load()) return;
    g_stopReq.store(true);
    // 工作线程退出后会 PostMessage(WM_APP_WORKER_DONE)
}

// ---------------------------------------------------------------------------
//  主窗口过程
// ---------------------------------------------------------------------------
static void PaintBackground(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(Col::BG);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    // 分组分隔线
    static const int lineY[] = { 80, 226, 302, 412, 484 };
    HBRUSH lb = CreateSolidBrush(Col::LINE);
    for (int y : lineY) {
        RECT lr = { S(20), S(y), S(440), S(y) + 1 };
        FillRect(mem, &lr, lb);
    }
    DeleteObject(lb);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static void OnCommand(int id, int code)
{
    if (code != BN_CLICKED) return;

    switch (id) {
        case IDC_BTN_LEFT:   g_cfg.button = 0; SyncSegButtons(); break;
        case IDC_BTN_RIGHT:  g_cfg.button = 1; SyncSegButtons(); break;
        case IDC_BTN_MIDDLE: g_cfg.button = 2; SyncSegButtons(); break;

        case IDC_BTN_SINGLE: g_cfg.clickType = 0; SyncSegButtons(); break;
        case IDC_BTN_DOUBLE: g_cfg.clickType = 1; SyncSegButtons(); break;
        case IDC_BTN_TRIPLE: g_cfg.clickType = 2; SyncSegButtons(); break;

        case IDC_BTN_FIXCOUNT: g_cfg.infinite = false; SyncSegButtons(); break;
        case IDC_BTN_INFINITE: g_cfg.infinite = true;  SyncSegButtons(); break;

        case IDC_BTN_FOLLOW: g_cfg.useFixedPos = false; SyncSegButtons(); break;
        case IDC_BTN_FIXED:  g_cfg.useFixedPos = true;  SyncSegButtons(); break;

        case IDC_BTN_TOPMOST:
            g_cfg.topmost = !g_cfg.topmost;
            SetWindowPos(g_hWnd, g_cfg.topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SyncToggleButtons();
            break;

        case IDC_BTN_MINIMIZE:
            g_cfg.minimizeOnStart = !g_cfg.minimizeOnStart;
            SyncToggleButtons();
            break;

        case IDC_BTN_HOTKEY:
            if (!g_running.load()) {
                g_capturingHotkey = true;
                SetFocus(g_hWnd);
                SyncHotkeyButton();
            }
            break;

        case IDC_BTN_PICK:
            if (g_pickRemain == 0) {
                g_pickRemain = PICK_SECONDS;
                SetTimer(g_hWnd, TIMER_PICK, 1000, nullptr);
                SetWindowTextW(g_ui.bPick, L"请移动鼠标…");
                InvalidateRect(g_ui.bPick, nullptr, FALSE);
            }
            break;

        case IDC_BTN_START: StartClicking(); break;
        case IDC_BTN_STOP:  StopClicking();  break;

        default:
            if (id >= IDC_PRESET_FIRST && id < IDC_PRESET_FIRST + 6) {
                static const int presetVal[6] = { 10, 50, 100, 200, 500, 1000 };
                SetEditInt(g_ui.interval, presetVal[id - IDC_PRESET_FIRST]);
            }
            break;
    }
}

static void OnHotkey()
{
    if (g_running.load()) StopClicking();
    else                  StartClicking();
}

static void OnKeyDown(UINT vk)
{
    if (!g_capturingHotkey) return;   // 非捕获状态不拦截任何按键

    if (vk == VK_ESCAPE) {                    // 取消捕获
        g_capturingHotkey = false;
        SyncHotkeyButton();
        return;
    }
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
        vk == VK_LWIN || vk == VK_RWIN ||
        vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL ||
        vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU) {
        return;                                // 只按下修饰键，继续等待主键
    }

    UINT mod = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) mod |= MOD_CONTROL;
    if (GetKeyState(VK_MENU)    & 0x8000) mod |= MOD_ALT;
    if (GetKeyState(VK_SHIFT)   & 0x8000) mod |= MOD_SHIFT;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) mod |= MOD_WIN;

    if (!IsHotkeyAllowed(mod, vk)) {
        g_capturingHotkey = false;
        SetWindowTextW(g_ui.bHotkey, L"热键需为 F1~F12 或含 Ctrl/Alt/Shift");
        InvalidateRect(g_ui.bHotkey, nullptr, FALSE);
        SetTimer(g_hWnd, TIMER_PICK + 10, 1800, nullptr);   // 稍后恢复显示
        return;
    }

    const UINT oldVk = g_cfg.hotkeyVk, oldMod = g_cfg.hotkeyMod;
    g_cfg.hotkeyVk = vk;
    g_cfg.hotkeyMod = mod;
    if (!RegisterAppHotkey()) {
        g_cfg.hotkeyVk = oldVk;
        g_cfg.hotkeyMod = oldMod;
        RegisterAppHotkey();
    }
    g_capturingHotkey = false;
    SyncHotkeyButton();
    SaveConfig();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_CREATE:
            g_hWnd = hwnd;
            g_brBg   = CreateSolidBrush(Col::BG);
            g_brEdit = CreateSolidBrush(Col::EDIT_BG);
            ResetFonts();
            LayoutControls();
            SyncSegButtons();
            SyncToggleButtons();
            SyncHotkeyButton();
            SyncRunButtons();
            SetTimer(hwnd, TIMER_UI, 200, nullptr);
            return 0;

        case WM_PAINT:
            PaintBackground(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            HWND ctl = (HWND)lp;
            // 被禁用的输入框也会走 STATIC 分支，保持它仍是「输入框」的观感
            if (ctl == g_ui.interval || ctl == g_ui.hold || ctl == g_ui.count ||
                ctl == g_ui.editX    || ctl == g_ui.editY) {
                SetTextColor(hdc, Col::DIS_FG);
                SetBkColor(hdc, Col::EDIT_BG);
                return (LRESULT)g_brEdit;
            }
            SetTextColor(hdc, Col::TEXT_SUB);
            SetBkColor(hdc, Col::BG);
            return (LRESULT)g_brBg;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, Col::EDIT_FG);
            SetBkColor(hdc, Col::EDIT_BG);
            return (LRESULT)g_brEdit;
        }

        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, Col::TEXT);
            SetBkColor(hdc, Col::BG);
            return (LRESULT)g_brBg;
        }

        case WM_DRAWITEM: {
            auto* dis = (DRAWITEMSTRUCT*)lp;
            if (dis->CtlID == IDC_STATUS) PaintStatus(dis);
            else                          PaintButton(dis);
            return TRUE;
        }

        case WM_COMMAND:
            OnCommand(LOWORD(wp), HIWORD(wp));
            return 0;

        case WM_HOTKEY:
            if ((int)wp == HOTKEY_ID) OnHotkey();
            return 0;

        case WM_KEYDOWN:
            OnKeyDown((UINT)wp);
            return 0;

        case WM_SYSKEYDOWN:
            // 允许 Alt 组合被捕获
            if (g_capturingHotkey) { OnKeyDown((UINT)wp); return 0; }
            break;

        case WM_TIMER:
            if (wp == TIMER_UI) {
                // 状态行同时显示运行状态、点击计数与实时光标坐标
                InvalidateRect(g_ui.status, nullptr, FALSE);
            } else if (wp == TIMER_PICK) {
                --g_pickRemain;
                if (g_pickRemain > 0) {
                    wchar_t b[32];
                    _snwprintf_s(b, _countof(b), _TRUNCATE, L"%d 秒后拾取…", g_pickRemain);
                    SetWindowTextW(g_ui.bPick, b);
                    InvalidateRect(g_ui.bPick, nullptr, FALSE);
                } else {
                    KillTimer(hwnd, TIMER_PICK);
                    POINT pt = {};
                    GetCursorPos(&pt);
                    g_cfg.posX = pt.x;
                    g_cfg.posY = pt.y;
                    g_cfg.useFixedPos = true;
                    SetEditInt(g_ui.editX, pt.x);
                    SetEditInt(g_ui.editY, pt.y);
                    SetWindowTextW(g_ui.bPick, L"拾取坐标");
                    SyncSegButtons();
                    SaveConfig();
                }
            } else if (wp == TIMER_PICK + 10) {
                KillTimer(hwnd, TIMER_PICK + 10);
                SyncHotkeyButton();
            }
            return 0;

        case WM_APP_WORKER_DONE:
            if (g_worker.joinable()) g_worker.join();
            SyncRunButtons();
            InvalidateRect(g_ui.status, nullptr, TRUE);
            return 0;

        case WM_DPICHANGED: {
            g_dpi = HIWORD(wp);
            RECT* prc = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, prc->left, prc->top,
                         prc->right - prc->left, prc->bottom - prc->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ResetFonts();
            LayoutControls();
            SyncSegButtons();
            SyncToggleButtons();
            SyncHotkeyButton();
            SyncRunButtons();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }

        case WM_CLOSE:
            KillTimer(hwnd, TIMER_UI);
            KillTimer(hwnd, TIMER_PICK);
            g_stopReq.store(true);
            if (g_worker.joinable()) g_worker.join();
            HarvestUi();          // 把界面上尚未提交的改动一并保存
            SaveConfig();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_hotkeyRegistered) {
                UnregisterHotKey(hwnd, HOTKEY_ID);
                g_hotkeyRegistered = false;
            }
            if (g_fontUI) { DeleteObject(g_fontUI); g_fontUI = nullptr; }
            if (g_brBg)   { DeleteObject(g_brBg);   g_brBg = nullptr; }
            if (g_brEdit) { DeleteObject(g_brEdit); g_brEdit = nullptr; }
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//  入口
// ---------------------------------------------------------------------------
static void EnableDpiAwareness()
{
    // PER_MONITOR_AWARE_V2；老系统上静默失败
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *PFN_SetCtx)(HANDLE);
        auto pfn = (PFN_SetCtx)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (pfn && pfn((HANDLE)-4)) return;
    }
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI *PFN_SetAwareness)(int);
        auto pfn = (PFN_SetAwareness)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (pfn) pfn(2);   // PROCESS_PER_MONITOR_DPI_AWARE
        FreeLibrary(shcore);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    EnableDpiAwareness();

    // 单实例
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"MouseClicker_SingleInstance_v1");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(L"MouseClickerWndClass", nullptr);
        if (prev) {
            ShowWindow(prev, SW_RESTORE);
            SetForegroundWindow(prev);
        }
        CloseHandle(hMutex);
        return 0;
    }

    g_hInst = hInstance;

    // 系统 DPI
    {
        HDC hdc = GetDC(nullptr);
        if (hdc) {
            g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(nullptr, hdc);
        }
        if (g_dpi <= 0) g_dpi = 96;
    }

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    Gdiplus::GdiplusStartupInput gsi;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gsi, nullptr) != Gdiplus::Ok)
        return 1;

    LoadConfig();

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, L"IDI_APPICON");
    wc.hIconSm       = LoadIconW(hInstance, L"IDI_APPICON");
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // 背景由 WM_PAINT 绘制
    wc.lpszClassName = L"MouseClickerWndClass";
    if (!RegisterClassExW(&wc)) return 1;

    RECT rc = { 0, 0, MulDiv(CLIENT_W, g_dpi, 96), MulDiv(CLIENT_H, g_dpi, 96) };
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE, 0);
    const int winW = rc.right - rc.left;
    const int winH = rc.bottom - rc.top;
    const int scrW = GetSystemMetrics(SM_CXSCREEN);
    const int scrH = GetSystemMetrics(SM_CYSCREEN);
    const int posX = (scrW - winW) / 2;
    const int posY = (scrH - winH) / 2;

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"鼠标连点器",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        posX < 0 ? 0 : posX, posY < 0 ? 0 : posY, winW, winH,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;

    if (g_cfg.topmost)
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // 应用初始配置到控件
    SetEditInt(g_ui.interval, g_cfg.intervalMs);
    SetEditInt(g_ui.hold,     g_cfg.holdMs);
    SetEditInt(g_ui.count,    g_cfg.repeatCount);
    SetEditInt(g_ui.editX,    g_cfg.posX);
    SetEditInt(g_ui.editY,    g_cfg.posY);
    SyncSegButtons();
    SyncToggleButtons();
    SyncHotkeyButton();

    if (!RegisterAppHotkey()) {
        // 注册失败时先退回到 F6，仍失败则仅提示
        g_cfg.hotkeyVk = VK_F6;
        g_cfg.hotkeyMod = 0;
        RegisterAppHotkey();
    }
    SyncHotkeyButton();

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // 捕获热键期间，无论焦点在哪个子控件上，键盘消息统一交给主窗口处理
        if (g_capturingHotkey && (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)) {
            OnKeyDown((UINT)msg.wParam);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_worker.joinable()) g_worker.join();
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    if (hMutex) CloseHandle(hMutex);
    return 0;
}

// WinLove UI — demo: Bileşenler ekranı (Direct2D + DirectWrite, saf Win32)
// T: tema değiştir · satıra tıkla: işaretle · "Uygula": ilerleme animasyonu
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d2d1_1.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdio>
#include "wl/Painter.h"

using Microsoft::WRL::ComPtr;
using namespace wl;

namespace {

struct Item { const wchar_t* name; const wchar_t* desc; int mb; int risk; bool on; };
// risk: 0 güvenli, 1 dikkat, 2 riskli
std::vector<Item> g_items = {
    { L"Clipchamp", L"Video düzenleyici", 210, 0, true },
    { L"Copilot", L"Görev çubuğundaki asistan", 48, 0, true },
    { L"Haberler", L"Haber akışı", 36, 0, true },
    { L"Hava Durumu", L"Tahmin uygulaması", 32, 0, true },
    { L"Microsoft Teams", L"Kişisel sürüm", 96, 0, true },
    { L"Outlook (yeni)", L"Posta ve takvim", 120, 0, true },
    { L"Xbox", L"Game Pass ve sosyal", 180, 1, false },
    { L"Microsoft Edge", L"Tarayıcı · WebView2 korunur", 520, 1, false },
    { L"OneDrive", L"Bulut eşitleme", 260, 0, false },
    { L"Microsoft Defender", L"Gerçek zamanlı koruma", 410, 2, false },
};

enum HitId { H_NONE = 0, H_THEME, H_MIN, H_MAX, H_CLOSE, H_SEARCH, H_APPLY, H_ROW0 = 100, H_TAB0 = 200, H_MENU0 = 300 };
struct Hit { D2D1_RECT_F r; int id; };

HWND g_hwnd;
ComPtr<ID2D1Factory1> g_d2d;
ComPtr<IDWriteFactory> g_dw;
ComPtr<ID2D1HwndRenderTarget> g_rt;
ComPtr<ID2D1DeviceContext> g_dc;
Painter g_p;
bool g_dark = false;
int g_hover = H_NONE;
float g_progress = -1;          // <0: boşta, 0..1: uygulanıyor
std::vector<Hit> g_hits;
float g_dpi = 96.0f;

bool In(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
State St(int id) { State s; s.hover = (g_hover == id); return s; }
std::wstring Gb(int mb) { wchar_t b[32]; swprintf(b, 32, L"%.2f GB", mb / 1000.0); for (auto& c : b) if (c == L'.') c = L','; return b; }

void CreateTarget() {
    if (g_rt) return;
    RECT rc; GetClientRect(g_hwnd, &rc);
    g_dpi = (float)GetDpiForWindow(g_hwnd);
    D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), g_dpi, g_dpi);
    g_d2d->CreateHwndRenderTarget(rp, D2D1::HwndRenderTargetProperties(g_hwnd, D2D1::SizeU(rc.right, rc.bottom)), &g_rt);
    g_rt.As(&g_dc);
    g_dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
}

void Paint() {
    CreateTarget();
    if (!g_dc) return;
    const Theme& t = g_dark ? DarkTheme() : LightTheme();
    g_hits.clear();
    g_dc->BeginDraw();
    g_p.Begin(g_dc.Get(), t);
    D2D1_SIZE_F sz = g_dc->GetSize();
    const float W = sz.width, H = sz.height;
    g_dc->Clear(t.bg);

    // ---- başlık çubuğu ---------------------------------------------------
    float y = 0, hh = m::TitleBarH;
    g_p.Stroke({ 20, 13, 38, 31 }, t.text, 5, 1.6f);
    g_p.Text(L"WinLove", { 47, 0, 120, hh }, TextStyle::BodySemi, t.text);
    const wchar_t* menus[] = { L"Görüntü", L"Ön Ayar", L"Araçlar" };
    float mx = 138;
    for (int i = 0; i < 3; ++i) {
        float w = g_p.Measure(menus[i], TextStyle::BodySemi) + 2 * m::MenuBtnPadX;
        D2D1_RECT_F r{ mx, 7, mx + w, 7 + m::MenuBtnH };
        g_p.MenuButton(r, menus[i], i == 0, St(H_MENU0 + i));
        g_hits.push_back({ r, H_MENU0 + i }); mx += w + 2;
    }
    float sw = (std::min)(m::SearchMaxW, W - 2 * 380.0f); if (sw < 200) sw = 200;
    D2D1_RECT_F sr{ (W - sw) / 2, 5, (W + sw) / 2, 5 + m::SearchH };
    g_p.SearchBox(sr, L"Bileşen, ayar veya güncelleme ara", L"", L"Ctrl K", St(H_SEARCH));
    g_hits.push_back({ sr, H_SEARCH });
    float cx = W;
    Icon caps[] = { Icon::Close, Icon::Maximize, Icon::Minimize };
    int capIds[] = { H_CLOSE, H_MAX, H_MIN };
    for (int i = 0; i < 3; ++i) {
        D2D1_RECT_F r{ cx - m::CaptionBtnW, 0, cx, hh };
        g_p.CaptionButton(r, caps[i], i == 0, St(capIds[i]));
        g_hits.push_back({ r, capIds[i] }); cx -= m::CaptionBtnW;
    }
    D2D1_RECT_F tr{ cx - m::CaptionBtnW, 0, cx, hh };
    g_p.CaptionButton(tr, g_dark ? Icon::Sun : Icon::Moon, false, St(H_THEME));
    g_hits.push_back({ tr, H_THEME });
    y = hh;

    // ---- görüntü başlığı -------------------------------------------------
    int removed = 0, count = 0;
    for (auto& it : g_items) if (it.on) { removed += it.mb; ++count; }
    const int base = 17820, after = base - removed + 858;
    float px = m::ContentPadX;
    y += m::HeaderTopPad;
    float tw = g_p.Measure(L"Windows 11 Pro", TextStyle::Title);
    g_p.Text(L"Windows 11 Pro", { px, y, px + tw + 2, y + 36 }, TextStyle::Title, t.text);
    g_p.Badge(px + tw + 12, y + 7, L"Bağlı", t.sunk, t.text2);
    g_p.Text(L"24H2 · 26100.1742     x64     tr-TR     D:\\ISO\\Win11_24H2_Turkish_x64.iso",
             { px, y + 40, W / 2, y + 60 }, TextStyle::Small, t.text2);
    std::wstring a = Gb(after), d = L"−" + Gb(base - after);
    float rx = W - px;
    float dw = g_p.Measure(d, TextStyle::BodySemi), aw = g_p.Measure(a, TextStyle::Heading);
    g_p.Text(d, { rx - dw, y + 8, rx, y + 34 }, TextStyle::BodySemi, t.acFg);
    g_p.Text(a, { rx - dw - 10 - aw, y + 6, rx - dw - 10, y + 34 }, TextStyle::Heading, t.text);
    g_p.DrawIcon(Icon::ArrowRight, { rx - dw - aw - 36, y + 13, rx - dw - aw - 22, y + 27 }, t.text3, 2);
    g_p.Text(L"17,82 GB", { rx - dw - aw - 110, y + 6, rx - dw - aw - 44, y + 34 }, TextStyle::Body, t.text3, HAlign::Trailing);
    g_p.Fill({ rx - 180, y + 46, rx, y + 50 }, t.line, 2);
    g_p.Fill({ rx - 180, y + 46, rx - 180 + 180.0f * after / base, y + 50 }, t.ac, 2);
    y += 62 + m::TabsTopPad;

    // ---- sekmeler (grup başlıklı) ----------------------------------------
    struct Tb { const wchar_t* name; int count; };
    struct Grp { const wchar_t* cap; std::vector<Tb> tabs; };
    std::vector<Grp> groups = {
        { L"BAŞLAT", { { L"Görüntü", -1 } } },
        { L"YÜKLE", { { L"Güncellemeler", 2 }, { L"Sürücüler", 3 }, { L"Kayıt Defteri", 0 }, { L"Kurulum Sonrası", 1 } } },
        { L"KALDIR", { { L"Bileşenler", count } } },
        { L"YAPILANDIR", { { L"Özellikler", 0 }, { L"Ayarlar", 8 }, { L"Zamanlanmış Görevler", 0 } } },
        { L"BİTİR", { { L"Uygula", -1 } } },
    };
    float gx = px; int tabId = 0;
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        float x = gx, gStart = gx;
        g_p.GroupCaption({ gx, y, gx + 200, y + m::GroupCaptionH }, groups[gi].cap);
        for (auto& tb : groups[gi].tabs) {
            float w = g_p.TabWidth(tb.name, tb.count);
            D2D1_RECT_F r{ x, y + m::GroupCaptionH, x + w, y + m::GroupCaptionH + m::TabH };
            g_p.Tab(r, tb.name, tb.count, gi == 2, St(H_TAB0 + tabId));
            g_hits.push_back({ r, H_TAB0 + tabId++ });
            x += w + m::TabGap;
        }
        gx = x - m::TabGap + m::GroupGap;
        if (gi + 1 < groups.size()) g_p.VLine(gx, y, y + m::GroupCaptionH + m::TabH - 8, t.line);
        gx += m::GroupGap; (void)gStart;
    }
    y += m::GroupCaptionH + m::TabH;
    g_p.HLine(0, W, y - 1, t.line);

    // ---- gövde: liste + sağ panel ----------------------------------------
    const float statusTop = H - m::StatusBarH;
    float panelL = W - m::SidePanelW;
    g_p.Fill({ panelL, y, W, statusTop }, t.surface);
    g_p.VLine(panelL, y, statusTop, t.line);

    float ly = y + 24, lr = panelL - px;
    g_p.Text(L"UYGULAMALAR VE SİSTEM", { px, ly, lr, ly + 16 }, TextStyle::SectionCap, t.text3);
    ly += 24;
    for (size_t i = 0; i < g_items.size() && ly + m::ListRowH < statusTop; ++i) {
        auto& it = g_items[i];
        D2D1_RECT_F row{ px, ly, lr, ly + m::ListRowH };
        g_p.HLine(px, lr, ly, t.line);
        if (g_hover == H_ROW0 + (int)i) g_p.Fill(row, t.hover);
        float cy = ly + m::ListRowH / 2;
        g_p.Checkbox({ px, cy - 9, px + 18, cy + 9 }, it.on, false, St(H_ROW0 + (int)i));
        g_p.Text(it.name, { px + 36, ly + 9, lr - 200, ly + 29 }, TextStyle::BodyMedium, t.text);
        g_p.Text(it.desc, { px + 36, ly + 29, lr - 200, ly + 47 }, TextStyle::Small, t.text3);
        std::wstring mb = std::to_wstring(it.mb) + L" MB";
        g_p.Text(mb, { lr - 84, ly, lr, ly + m::ListRowH }, TextStyle::Body, t.text2, HAlign::Trailing);
        if (it.risk) g_p.StatusText(lr - 180, row, it.risk == 1 ? L"Dikkat" : L"Riskli",
                                    it.risk == 1 ? t.warn : t.risk, it.risk == 1 ? t.warn : t.risk);
        g_hits.push_back({ row, H_ROW0 + (int)i });
        ly += m::ListRowH;
    }

    // sağ panel: Değişiklikler
    float pp = m::SidePanelPad, pl = panelL + pp, pr = W - pp, py = y + pp;
    g_p.Text(L"Değişiklikler", { pl, py, pr, py + 22 }, TextStyle::Panel, t.text);
    g_p.Text(std::to_wstring(count + 14) + L" işlem", { pl, py, pr, py + 22 }, TextStyle::Small, t.text3, HAlign::Trailing);
    py += 36;
    g_p.Text(L"Kaldırılacak bileşenler", { pl, py, pr, py + 24 }, TextStyle::BodySemi, t.text);
    g_p.Text(std::to_wstring(count), { pl, py, pr, py + 24 }, TextStyle::BodyMedium, t.text3, HAlign::Trailing);
    py += 28;
    for (auto& it : g_items) if (it.on) {
        g_p.Text(it.name, { pl, py, pr - 70, py + m::JournalRowH }, TextStyle::Body, t.text2);
        g_p.Text(std::to_wstring(it.mb) + L" MB", { pl, py, pr, py + m::JournalRowH }, TextStyle::Caption, t.text3, HAlign::Trailing);
        py += m::JournalRowH;
    }
    py += 8; g_p.HLine(pl, pr, py, t.line); py += 10;
    const wchar_t* others[][2] = { { L"Ayarlar", L"8" }, { L"Güncellemeler", L"2" }, { L"Sürücüler", L"3" }, { L"Kurulum Sonrası", L"1" } };
    for (auto& o : others) {
        g_p.Text(o[0], { pl, py, pr, py + 32 }, TextStyle::BodySemi, t.text);
        g_p.Text(o[1], { pl, py, pr, py + 32 }, TextStyle::BodyMedium, t.text3, HAlign::Trailing);
        py += 32;
    }
    float by = statusTop - pp - m::BtnBigH;
    if (g_progress < 0) {
        g_p.HLine(pl, pr, by - 60, t.line);
        g_p.Text(L"Çıktı", { pl, by - 46, pr, by - 26 }, TextStyle::Small, t.text3);
        g_p.Text(L"ESD · ISO oluşturulacak", { pl, by - 46, pr, by - 26 }, TextStyle::Small, t.text, HAlign::Trailing);
        D2D1_RECT_F br{ pl, by, pr, by + m::BtnBigH };
        g_p.Button(br, L"Uygula", ButtonKind::Primary, St(H_APPLY));
        g_hits.push_back({ br, H_APPLY });
    } else {
        int pct = (int)(g_progress * 100);
        g_p.Text(g_progress >= 1 ? L"Tamamlandı" : L"Bileşenler kaldırılıyor", { pl, by - 6, pr, by + 16 }, TextStyle::BodySemi, t.text);
        g_p.Text(std::to_wstring(pct) + L"%", { pl, by - 6, pr, by + 16 }, TextStyle::BodySemi, t.acFg, HAlign::Trailing);
        g_p.Progress({ pl, by + 28, pr, by + 28 + m::ProgressH }, g_progress);
    }

    // ---- durum çubuğu ----------------------------------------------------
    g_p.Fill({ 0, statusTop, W, H }, t.surface);
    g_p.HLine(0, W, statusTop, t.line);
    D2D1_RECT_F sb{ 0, statusTop, W, H };
    g_p.StatusText(m::StatusBarPadX, sb, L"Bağlı · Windows 11 Pro · D:\\WinLove\\mount", t.ac, t.text2);
    g_p.Text(L"DISM 10.0.26100.1     Boş alan D: 182 GB", { W / 2, statusTop, W - m::StatusBarPadX, H }, TextStyle::Caption, t.text2, HAlign::Trailing);

    if (g_dc->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) { g_dc.Reset(); g_rt.Reset(); }
}

D2D1_POINT_2F ToDip(LPARAM lp) { return { GET_X_LPARAM(lp) * 96.0f / g_dpi, GET_Y_LPARAM(lp) * 96.0f / g_dpi }; }

int HitAt(float x, float y) { for (auto& h : g_hits) if (In(h.r, x, y)) return h.id; return H_NONE; }

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCALCSIZE:            // sistem başlık çubuğunu kaldır, kenarlıkları koru
        if (wp) {
            if (IsZoomed(h)) {     // büyütülmüşken ekran dışına taşan çerçeveyi telafi et
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                int f = GetSystemMetricsForDpi(SM_CXFRAME, (UINT)g_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, (UINT)g_dpi);
                p->rgrc[0].left += f; p->rgrc[0].right -= f; p->rgrc[0].top += f; p->rgrc[0].bottom -= f;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &pt);
        RECT rc; GetClientRect(h, &rc);
        const int b = 6;
        if (!IsZoomed(h)) {
            bool L = pt.x < b, R = pt.x >= rc.right - b, T = pt.y < b, B = pt.y >= rc.bottom - b;
            if (T && L) return HTTOPLEFT;
            if (T && R) return HTTOPRIGHT;
            if (B && L) return HTBOTTOMLEFT;
            if (B && R) return HTBOTTOMRIGHT;
            if (L) return HTLEFT;
            if (R) return HTRIGHT;
            if (T) return HTTOP;
            if (B) return HTBOTTOM;
        }
        float x = pt.x * 96.0f / g_dpi, y = pt.y * 96.0f / g_dpi;
        if (y < m::TitleBarH && HitAt(x, y) == H_NONE) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_SIZE:
        if (g_rt) g_rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp); if (g_rt) g_rt->SetDpi(g_dpi, g_dpi);
        auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        auto p = ToDip(lp); int id = HitAt(p.x, p.y);
        if (id != g_hover) { g_hover = id; InvalidateRect(h, nullptr, FALSE); }
        TRACKMOUSEEVENT te{ sizeof(te), TME_LEAVE, h, 0 }; TrackMouseEvent(&te);
        return 0;
    }
    case WM_MOUSELEAVE: g_hover = H_NONE; InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONUP: {
        auto p = ToDip(lp); int id = HitAt(p.x, p.y);
        if (id == H_CLOSE) PostMessage(h, WM_CLOSE, 0, 0);
        else if (id == H_MIN) ShowWindow(h, SW_MINIMIZE);
        else if (id == H_MAX) ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
        else if (id == H_THEME) g_dark = !g_dark;
        else if (id == H_APPLY) { g_progress = 0; SetTimer(h, 1, 30, nullptr); }
        else if (id >= H_ROW0 && id < H_ROW0 + (int)g_items.size()) g_items[id - H_ROW0].on = !g_items[id - H_ROW0].on;
        InvalidateRect(h, nullptr, FALSE); return 0;
    }
    case WM_TIMER:
        g_progress += 0.01f;
        if (g_progress >= 1.0f) { g_progress = 1.0f; KillTimer(h, 1); }
        InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_KEYDOWN:
        if (wp == 'T') { g_dark = !g_dark; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr,
                      reinterpret_cast<void**>(g_d2d.GetAddressOf()));
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(g_dw.GetAddressOf()));
    if (FAILED(g_p.Init(g_d2d.Get(), g_dw.Get()))) return 1;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = WndProc; wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"WinLoveDemo";
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"WinLove", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900, nullptr, nullptr, inst, nullptr);
    g_dpi = (float)GetDpiForWindow(g_hwnd);
    MARGINS mg{ 0, 0, 1, 0 };                       // DWM gölgesi ve köşe yuvarlama
    DwmExtendFrameIntoClientArea(g_hwnd, &mg);
    DWORD cp = 2;                                   // DWMWCP_ROUND (Windows 11)
    DwmSetWindowAttribute(g_hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cp, sizeof(cp));
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(g_hwnd, show);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}

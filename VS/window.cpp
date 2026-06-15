#include <windows.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

static constexpr int IDC_BTN_HAND = 1003;
static constexpr int IDB_BITMAP_TIPS = 101;
static constexpr int IDB_BITMAP_HAND = 102;
static constexpr int IDB_BITMAP_PRESS = 103;
static constexpr int IDB_BITMAP_HP = 104;

enum ZBAND_INFORMATION_UNIVERSAL { ZBAND_SYSTEM_TOOLS = 5 };
typedef BOOL(WINAPI* pfnSetWindowBand)(HWND hWnd, HWND hWndInsertAfter, DWORD dwBand);
typedef LONG(WINAPI* pfnRtlGetVersion)(PRTL_OSVERSIONINFOW);

static HWND hMainWindow = NULL; static HWND hHandButtonWnd = NULL; static HWND hTargetJiyuWnd = NULL;
static HFONT hJiyuFont = NULL; static HBITMAP hBmpHand = NULL; static HBITMAP hBmpPress = NULL; static HBITMAP hBmpHp = NULL;

static bool isEmbeddedMode = false;       // 是否抓到了极域
static bool isDesktopVisibleMode = false; // 当前是否处于大亮摸鱼状态
static bool isMouseHoveringButton = false;
static bool isFirstTimeShowTips = true;
static DWORD GetWindowsMainVersion() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        pfnRtlGetVersion RtlGetVersion = (pfnRtlGetVersion)GetProcAddress(hNtdll, "RtlGetVersion");
        if (RtlGetVersion) {
            OSVERSIONINFOW osInfo = { 0 }; osInfo.dwOSVersionInfoSize = sizeof(osInfo);
            if (RtlGetVersion((PRTL_OSVERSIONINFOW)&osInfo) == 0) return osInfo.dwMajorVersion;
        }
    }
    return 0;
}

static void EnforceAbsoluteTopmost() {
    if (!hMainWindow || !IsWindow(hMainWindow)) return;
    DWORD winVersion = GetWindowsMainVersion(); HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (winVersion >= 10 && hUser32) {
        pfnSetWindowBand SetWindowBand = (pfnSetWindowBand)GetProcAddress(hUser32, "SetWindowBand");
        if (SetWindowBand) {
            SetWindowBand(hMainWindow, NULL, ZBAND_SYSTEM_TOOLS);
            if (hHandButtonWnd && IsWindow(hHandButtonWnd)) SetWindowBand(hHandButtonWnd, NULL, ZBAND_SYSTEM_TOOLS);
        }
    }
    // 💡【核心防遮挡】：无论处于什么状态，主窗口和小手都强制顶在最前方
    SetWindowPos(hMainWindow, HWND_TOPMOST, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (hHandButtonWnd && IsWindow(hHandButtonWnd)) {
        int sw = GetSystemMetrics(SM_CXSCREEN); int sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hHandButtonWnd, HWND_TOPMOST, sw - 48 - 5, sh - 48 - 5, 48, 48, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

static HWND FindAnyJiyuBlackScreen() {
    HWND hJiyu = NULL; wchar_t className[256] = { 0 }; wchar_t windowTitle[256] = { 0 };
    while ((hJiyu = FindWindowExW(NULL, hJiyu, NULL, NULL)) != NULL) {
        if (IsWindowVisible(hJiyu) && hJiyu != hMainWindow) {
            GetClassNameW(hJiyu, className, 256); GetWindowTextW(hJiyu, windowTitle, 256);
            if (wcsncmp(className, L"Afx", 3) == 0 || wcscmp(windowTitle, L"BlackScreen Window") == 0) {
                RECT rc = { 0, 0, 0, 0 }; GetWindowRect(hJiyu, &rc);
                if (rc.right - rc.left >= GetSystemMetrics(SM_CXSCREEN) && rc.bottom - rc.top >= GetSystemMetrics(SM_CYSCREEN)) return hJiyu;
            }
        }
    }
    return NULL;
}
// 💡【核心反制破局：小手子类化点击监听天线】
static LRESULT CALLBACK HandButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    UNREFERENCED_PARAMETER(uIdSubclass); UNREFERENCED_PARAMETER(dwRefData);
    if (msg == WM_LBUTTONDOWN) {
        isDesktopVisibleMode = !isDesktopVisibleMode; // 切换状态
        if (isDesktopVisibleMode) {
            // 💡【摸鱼状态】：主全屏窗口进入 100% 物理透明，露出正常桌面，小手换上浅色 hp.bmp 皮肤
            SetLayeredWindowAttributes(hMainWindow, 0, 0, LWA_ALPHA);
            SendMessage(hwnd, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmpHp);
            isFirstTimeShowTips = false;
        }
        else {
            // 💡【黑幕状态】：主全屏窗口恢复 100% 纯黑不透明，召回假黑幕，小手换回经典蓝手
            SetLayeredWindowAttributes(hMainWindow, 0, 255, LWA_ALPHA);
            SendMessage(hwnd, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmpHand);
        }
        isMouseHoveringButton = false; InvalidateRect(hMainWindow, NULL, TRUE); EnforceAbsoluteTopmost(); return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        hMainWindow = hwnd; int sw = GetSystemMetrics(SM_CXSCREEN); int sh = GetSystemMetrics(SM_CYSCREEN);
        hJiyuFont = CreateFontW(sh / 22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        hBmpHand = LoadBitmap(GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_BITMAP_HAND)); hBmpPress = LoadBitmap(GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_BITMAP_PRESS)); hBmpHp = LoadBitmap(GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_BITMAP_HP));

        // 💡 永远作为子窗口老老实实焊在右下角，绝不脱离父所有权，从根本上杜绝图标失踪！
        hHandButtonWnd = CreateWindowExW(WS_EX_NOACTIVATE, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_BITMAP, sw - 48 - 5, sh - 48 - 5, 48, 48, hwnd, (HMENU)(UINT_PTR)IDC_BTN_HAND, GetModuleHandle(NULL), NULL);
        if (hHandButtonWnd) { SendMessage(hHandButtonWnd, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmpHand); SetWindowSubclass(hHandButtonWnd, HandButtonSubclassProc, 0, 0); }
        EnforceAbsoluteTopmost(); SetTimer(hwnd, 1, 20, NULL); break;
    }
    case WM_TIMER: {
        EnforceAbsoluteTopmost();
        if (!isDesktopVisibleMode && hHandButtonWnd && IsWindow(hHandButtonWnd)) {
            POINT pt = { 0, 0 }; GetCursorPos(&pt); RECT btnRect = { 0, 0, 0, 0 }; GetWindowRect(hHandButtonWnd, &btnRect);
            if (PtInRect(&btnRect, pt)) { if (!isMouseHoveringButton) { isMouseHoveringButton = true; SendMessage(hHandButtonWnd, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmpPress); } }
            else { if (isMouseHoveringButton) { isMouseHoveringButton = false; SendMessage(hHandButtonWnd, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmpHand); } }
        }
        HWND hJiyuBlack = FindAnyJiyuBlackScreen();
        if (hJiyuBlack != NULL) {
            hTargetJiyuWnd = hJiyuBlack; isEmbeddedMode = true;
            // 💡【核心反制】：不改样式、不吸入！直接在后台把极域窗口冷冻压制，让它无法渲染、无法闪烁！
            ShowWindow(hJiyuBlack, SW_MINIMIZE); EnableWindow(hJiyuBlack, FALSE);
        }
        else {
            if (isEmbeddedMode && (hTargetJiyuWnd == NULL || !IsWindow(hTargetJiyuWnd))) {
                // 雷达自愈：极域老师主动退出了黑屏，我们自动解除假黑幕
                isEmbeddedMode = false; isDesktopVisibleMode = false; hTargetJiyuWnd = NULL;
                SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA); // 变透明隐藏
                MessageBoxW(hwnd, L"🎉 安全自愈雷达通知：检测到极域黑屏已被老师解除！", L"自愈雷达", MB_OK | MB_ICONINFORMATION);
            }
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        // 💡 只有在非摸鱼状态（纯黑假幕布）且窗口不是100%透明时，才绘制提示文字和图片
        if (!isDesktopVisibleMode) {
            RECT rect = { 0, 0, 0, 0 }; GetClientRect(hwnd, &rect); SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(255, 235, 0));
            HGDIOBJ hOldFont = SelectObject(hdc, hJiyuFont); DrawTextW(hdc, L"保持安静", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE); SelectObject(hdc, hOldFont);
            if (isFirstTimeShowTips) {
                HDC hMemDC = CreateCompatibleDC(hdc); HBITMAP hTipsBmp = LoadBitmap(GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_BITMAP_TIPS)); HGDIOBJ hOldBmp = SelectObject(hMemDC, hTipsBmp); BITMAP bmpInfo; GetObject(hTipsBmp, sizeof(BITMAP), &bmpInfo);
                int targetWidth = rect.right * 30 / 100; int targetHeight = targetWidth * bmpInfo.bmHeight / bmpInfo.bmWidth; SetStretchBltMode(hdc, COLORONCOLOR); StretchBlt(hdc, rect.right - targetWidth - 10, rect.bottom - targetHeight - 55, targetWidth, targetHeight, hMemDC, 0, 0, bmpInfo.bmWidth, bmpInfo.bmHeight, SRCCOPY);
                SelectObject(hMemDC, hOldBmp); DeleteObject(hTipsBmp); DeleteDC(hMemDC);
            }
        }
        EndPaint(hwnd, &ps); break;
    }
    case WM_CLOSE: { if (hHandButtonWnd && IsWindow(hHandButtonWnd)) { RemoveWindowSubclass(hHandButtonWnd, HandButtonSubclassProc, 0); DestroyWindow(hHandButtonWnd); } DestroyWindow(hwnd); break; }
    case WM_DESTROY: { if (hJiyuFont) DeleteObject(hJiyuFont); if (hBmpHand) DeleteObject(hBmpHand); if (hBmpPress) DeleteObject(hBmpPress); if (hBmpHp) DeleteObject(hBmpHp); PostQuitMessage(0); break; }
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    SetProcessDPIAware(); RegisterHotKey(NULL, 1, MOD_CONTROL, 'Q');
    WNDCLASSEXW wc = { 0 }; wc.cbSize = sizeof(WNDCLASSEXW); wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = WndProc; wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); wc.lpszClassName = L"JiyuUniversalSplitContainerClass";
    if (!RegisterClassExW(&wc)) return 0;

    // 💡【核心破局样式】：使用 WS_EX_LAYERED 开启分层透明特权，初始透明度设为 0（处于隐藏静默盲听状态）
    hMainWindow = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED, L"JiyuUniversalSplitContainerClass", L"", WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, hInstance, NULL);
    if (!hMainWindow) return 0;
    SetLayeredWindowAttributes(hMainWindow, 0, 0, LWA_ALPHA);
    ShowWindow(hMainWindow, SW_SHOW); UpdateWindow(hMainWindow); MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_HOTKEY && msg.wParam == 1) { isDesktopVisibleMode = true; isEmbeddedMode = false; SetLayeredWindowAttributes(hMainWindow, 0, 0, LWA_ALPHA); SendMessage(hHandButtonWnd, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmpHp); }
        TranslateMessage(&msg); DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

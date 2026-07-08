#include <windows.h>
#include <string>
#include "resource.h"

using namespace std;

// ============================================================
// 常量定义
// ============================================================
#define WM_TRAYICON (WM_USER + 100)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_ABOUT 1002
#define ID_TRAY_SHOW_BROADCAST 1003
#define TIMER_SCAN_WINDOWS 1004
#define TIMER_PIN_CONTROL_WINDOW 1005
#define SCAN_INTERVAL 5000  // 5秒

// ============================================================
// 全局变量
// ============================================================
NOTIFYICONDATAW g_nid = { 0 };
HWND g_hMainWnd = NULL;
HWND g_hAboutWnd = NULL;
HWND g_hControlWnd = NULL;
bool g_bBroadcastRestored = false;

// ============================================================
// 查找极域黑屏窗口
// ============================================================
static HWND FindAnyJiyuBlackScreen() {
    HWND hJiyu = NULL;
    wchar_t className[256] = { 0 };
    wchar_t windowTitle[256] = { 0 };

    while ((hJiyu = FindWindowExW(NULL, hJiyu, NULL, NULL)) != NULL) {
        if (IsWindowVisible(hJiyu) && hJiyu != g_hMainWnd && hJiyu != g_hAboutWnd && hJiyu != g_hControlWnd) {
            GetClassNameW(hJiyu, className, 256);
            GetWindowTextW(hJiyu, windowTitle, 256);

            if (wcsncmp(className, L"Afx", 3) == 0 || wcscmp(windowTitle, L"BlackScreen Window") == 0) {
                RECT rc = { 0, 0, 0, 0 };
                GetWindowRect(hJiyu, &rc);
                if (rc.right - rc.left >= GetSystemMetrics(SM_CXSCREEN) &&
                    rc.bottom - rc.top >= GetSystemMetrics(SM_CYSCREEN)) {
                    return hJiyu;
                }
            }
        }
    }
    return NULL;
}

// ============================================================
// 查找屏幕广播窗口
// ============================================================
static HWND FindBroadcastWindow() {
    HWND hWnd = NULL;
    wchar_t className[256] = { 0 };
    wchar_t windowTitle[256] = { 0 };

    while ((hWnd = FindWindowExW(NULL, hWnd, NULL, NULL)) != NULL) {
        if (IsWindowVisible(hWnd) && hWnd != g_hMainWnd && hWnd != g_hAboutWnd && hWnd != g_hControlWnd) {
            GetClassNameW(hWnd, className, 256);
            GetWindowTextW(hWnd, windowTitle, 256);
            if (wcsstr(windowTitle, L"屏幕广播") != NULL ||
                wcsstr(windowTitle, L"广播") != NULL ||
                wcsstr(className, L"Broadcast") != NULL) {
                return hWnd;
            }
        }
    }
    return NULL;
}

// ============================================================
// 关闭目标窗口
// ============================================================
static int CloseTargetWindows() {
    // 如果处于“已恢复”状态，不进行自动扫描
    if (g_bBroadcastRestored) {
        return 0;
    }

    int closedCount = 0;

    // 黑屏窗口：最小化 + 禁用
    HWND hBlack = FindAnyJiyuBlackScreen();
    if (hBlack != NULL) {
        ShowWindow(hBlack, SW_MINIMIZE);
        EnableWindow(hBlack, FALSE);
        closedCount++;
    }

    // 广播窗口：只最小化
    HWND hBroadcast = FindBroadcastWindow();
    if (hBroadcast != NULL) {
        ShowWindow(hBroadcast, SW_MINIMIZE);
        closedCount++;
    }

    return closedCount;
}

// ============================================================
// 控制窗口过程
// ============================================================
static LRESULT CALLBACK ControlWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 创建“再次阻止广播”按钮
        CreateWindowW(L"BUTTON", L"再次阻止广播",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 10, 160, 40,
            hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);

        // 启动定时器刷新置顶状态（20ms）
        SetTimer(hwnd, TIMER_PIN_CONTROL_WINDOW, 20, NULL);
        MessageBoxW(hwnd, L"欢迎使用极域课堂管理软件阻止工具V2.0\n\n程序在系统托盘中运行。", L"欢迎使用", MB_OK);
        break;

    }

    case WM_TIMER: {
        if (wParam == TIMER_PIN_CONTROL_WINDOW) {
            // 始终保持窗口置顶
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        break;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == 1) { // 按钮被点击
            // 重新阻止广播：最小化广播窗口
            HWND hBroadcast = FindBroadcastWindow();
            if (hBroadcast) {
                ShowWindow(hBroadcast, SW_MINIMIZE);
                g_bBroadcastRestored = false;
                // 隐藏控制窗口
                ShowWindow(hwnd, SW_HIDE);
                // 恢复定时器扫描
                SetTimer(g_hMainWnd, TIMER_SCAN_WINDOWS, SCAN_INTERVAL, NULL);
                MessageBoxW(hwnd, L"已重新阻止广播窗口", L"提示", MB_OK | MB_ICONINFORMATION);
            }
            else {
                MessageBoxW(hwnd, L"未找到广播窗口", L"提示", MB_OK | MB_ICONWARNING);
            }
        }
        break;
    }

    case WM_CLOSE: {
        ShowWindow(hwnd, SW_HIDE);
        g_bBroadcastRestored = false;
        // 重新启用定时器扫描
        SetTimer(g_hMainWnd, TIMER_SCAN_WINDOWS, SCAN_INTERVAL, NULL);
        break;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, TIMER_PIN_CONTROL_WINDOW);
        break;
    }

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ============================================================
// 创建控制窗口（紧贴左上角）
// ============================================================
static HWND CreateControlWindow(HINSTANCE hInst) {
    WNDCLASSEXW wcControl = { sizeof(WNDCLASSEXW) };
    wcControl.lpfnWndProc = ControlWndProc;
    wcControl.hInstance = hInst;
    wcControl.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcControl.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcControl.lpszClassName = L"ControlWindowClass";
    RegisterClassExW(&wcControl);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"ControlWindowClass",
        L"广播控制",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        0, 0,          // ⭐ 紧贴左上角
        200, 100,
        NULL, NULL, hInst, NULL
    );
    return hWnd;
}

// ============================================================
// 关于窗口过程
// ============================================================
static LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
        HICON hIcon = LoadIconW(hInst, MAKEINTRESOURCE(IDI_ICON1));
        if (hIcon) {
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);

        HBRUSH hBrush = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(hdc, &rect, hBrush);
        DeleteObject(hBrush);

        HFONT hFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        HFONT oldFont = (HFONT)SelectObject(hdc, hFont);

        SetTextColor(hdc, RGB(0, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        RECT titleRect = { 0, 20, 400, 60 };
        DrawTextW(hdc, L"极域课堂管理软件阻止工具", -1, &titleRect, DT_CENTER | DT_SINGLELINE);

        SelectObject(hdc, oldFont);
        DeleteObject(hFont);

        HFONT hFont2 = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        oldFont = (HFONT)SelectObject(hdc, hFont2);

        SetTextColor(hdc, RGB(80, 80, 80));
        RECT infoRect = { 20, 70, 380, 100 };
        DrawTextW(hdc, L"版本: V2.0", -1, &infoRect, DT_CENTER | DT_SINGLELINE);

        RECT infoRect2 = { 20, 100, 380, 130 };
        DrawTextW(hdc, L"功能: 强制关闭极域黑屏和屏幕广播窗口", -1, &infoRect2, DT_CENTER | DT_SINGLELINE);

        RECT infoRect3 = { 20, 130, 380, 160 };
        DrawTextW(hdc, L"运行模式: 静默后台", -1, &infoRect3, DT_CENTER | DT_SINGLELINE);

        SelectObject(hdc, oldFont);
        DeleteObject(hFont2);

        EndPaint(hwnd, &ps);
        break;
    }

    case WM_CLOSE: {
        ShowWindow(hwnd, SW_HIDE);
        break;
    }

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ============================================================
// 主窗口过程
// ============================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hMainWnd = hwnd;
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);

        // 创建关于窗口
        WNDCLASSEXW wcAbout = { sizeof(WNDCLASSEXW) };
        wcAbout.lpfnWndProc = AboutWndProc;
        wcAbout.hInstance = hInst;
        wcAbout.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcAbout.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcAbout.lpszClassName = L"AboutWindowClass";
        RegisterClassExW(&wcAbout);

        g_hAboutWnd = CreateWindowExW(
            0,
            L"AboutWindowClass",
            L"关于",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 420, 220,
            NULL, NULL, hInst, NULL
        );

        // 创建控制窗口（初始隐藏）
        g_hControlWnd = CreateControlWindow(hInst);
        if (g_hControlWnd) {
            ShowWindow(g_hControlWnd, SW_HIDE);
        }

        // 初始化托盘图标
        ZeroMemory(&g_nid, sizeof(NOTIFYICONDATAW));
        g_nid.cbSize = sizeof(NOTIFYICONDATAW);
        g_nid.hWnd = hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;

        g_nid.hIcon = LoadIconW(hInst, MAKEINTRESOURCE(IDI_ICON1));
        if (!g_nid.hIcon) {
            g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
        }

        wcscpy_s(g_nid.szTip, L"极域课堂管理软件阻止工具V2.0");
        Shell_NotifyIconW(NIM_ADD, &g_nid);

        SetTimer(hwnd, TIMER_SCAN_WINDOWS, SCAN_INTERVAL, NULL);
        PostMessage(hwnd, WM_TIMER, TIMER_SCAN_WINDOWS, 0);
        break;
    }

    case WM_TIMER: {
        if (wParam == TIMER_SCAN_WINDOWS) {
            CloseTargetWindows();
        }
        break;
    }

    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();

            // ⭐ 添加“显示极域屏幕广播窗口”菜单项
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW_BROADCAST, L"显示极域屏幕广播窗口");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_ABOUT, L"关于");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");

            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            PostMessage(hwnd, WM_NULL, 0, 0);
            DestroyMenu(hMenu);
        }
        break;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case ID_TRAY_ABOUT:
            if (g_hAboutWnd) {
                ShowWindow(g_hAboutWnd, SW_SHOW);
                SetForegroundWindow(g_hAboutWnd);
            }
            break;

        case ID_TRAY_SHOW_BROADCAST: {
            // 查找广播窗口
            HWND hBroadcast = FindBroadcastWindow();
            if (!hBroadcast) {
                MessageBoxW(hwnd, L"未找到屏幕广播窗口", L"提示", MB_OK | MB_ICONWARNING);
                break;
            }

            // 恢复广播窗口
            ShowWindow(hBroadcast, SW_RESTORE);
            SetForegroundWindow(hBroadcast);
            g_bBroadcastRestored = true;

            // 暂停自动扫描
            KillTimer(hwnd, TIMER_SCAN_WINDOWS);

            // 显示控制窗口
            if (g_hControlWnd) {
                ShowWindow(g_hControlWnd, SW_SHOW);
                SetForegroundWindow(g_hControlWnd);
            }
            break;
        }

        case ID_TRAY_EXIT:
            KillTimer(hwnd, TIMER_SCAN_WINDOWS);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            break;
        }
        break;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, TIMER_SCAN_WINDOWS);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ============================================================
// WinMain
// ============================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    SetErrorMode(SEM_FAILCRITICALERRORS);

    // 注册主窗口类
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"JiyuWindowCloserClass";

    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    HWND hWnd = CreateWindowExW(
        0,
        L"JiyuWindowCloserClass",
        L"极域课堂管理软件阻止工具V2.0",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
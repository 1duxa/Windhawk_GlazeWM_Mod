// ==WindhawkMod==
// @id              glaze
// @name            GlazeWM Workspace Widget
// @description     Desktop widget showing GlazeWM workspace blocks with window count
// @version         1.0
// @author          Duxa
// @include         explorer.exe
// @compilerOptions -lgdi32 -luser32
// ==/WindhawkMod==

#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <string>
#include <vector>

struct WorkspaceInfo {
    std::wstring name;
    bool hasFocus;
    bool isDisplayed;
    int windowCount;
};
// Globals
HWND hwndOverlay = NULL;
std::vector<WorkspaceInfo> workspaces;
std::thread updateThread;
bool running = true;
DWORD lastUpdateTime = 0;

HWND GetDesktopHostWindow() {
    HWND progman = FindWindow(L"Progman", NULL);
    HWND workerw = NULL;


    SendMessageTimeout(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);

    HWND hwnd = nullptr;
    while ((hwnd = FindWindowEx(NULL, hwnd, L"WorkerW", NULL)) != NULL) {
        HWND shellView = FindWindowEx(hwnd, NULL, L"SHELLDLL_DefView", NULL);
        if (shellView != NULL) {
            workerw = hwnd;
            break;
        }
    }
    return workerw ? workerw : progman;
}

int CountWindowsInWorkspace(const std::wstring& workspaceObj) {
    int count = 0;
    size_t pos = 0;

    size_t childrenStart = workspaceObj.find(L"\"children\"");
    if (childrenStart == std::wstring::npos) return 0;

    size_t arrayStart = workspaceObj.find(L'[', childrenStart);
    if (arrayStart == std::wstring::npos) return 0;

    pos = arrayStart;
    while (pos < workspaceObj.length()) {
        size_t windowType = workspaceObj.find(L"\"type\": \"window\"", pos);
        if (windowType == std::wstring::npos) break;
        count++;
        pos = windowType + 1;
    }
    
    return count;
}

std::vector<WorkspaceInfo> ParseWorkspaceData(const std::wstring& jsonData) {
    std::vector<WorkspaceInfo> result;

    size_t workspacesStart = jsonData.find(L"\"workspaces\"");
    if (workspacesStart == std::wstring::npos) return result;
    
    size_t arrayStart = jsonData.find(L'[', workspacesStart);
    if (arrayStart == std::wstring::npos) return result;

    size_t pos = arrayStart;
    int braceCount = 0;
    size_t objStart = 0;
    
    for (size_t i = arrayStart; i < jsonData.length(); i++) {
        if (jsonData[i] == L'{') {
            if (braceCount == 0) objStart = i;
            braceCount++;
        } else if (jsonData[i] == L'}') {
            braceCount--;
            if (braceCount == 0) {

                std::wstring objContent = jsonData.substr(objStart, i - objStart + 1);
                
                WorkspaceInfo ws;
                ws.hasFocus = false;
                ws.isDisplayed = false;
                ws.windowCount = 0;

                size_t nameStart = objContent.find(L"\"name\"");
                if (nameStart != std::wstring::npos) {
                    size_t colonPos = objContent.find(L':', nameStart);
                    if (colonPos != std::wstring::npos) {
                        size_t quoteStart = objContent.find(L'\"', colonPos);
                        if (quoteStart != std::wstring::npos) {
                            quoteStart++;
                            size_t quoteEnd = objContent.find(L'\"', quoteStart);
                            if (quoteEnd != std::wstring::npos) {
                                ws.name = objContent.substr(quoteStart, quoteEnd - quoteStart);
                            }
                        }
                    }
                }

                size_t focusStart = objContent.find(L"\"hasFocus\"");
                if (focusStart != std::wstring::npos) {
                    size_t colonPos = objContent.find(L':', focusStart);
                    if (colonPos != std::wstring::npos) {
                        if (objContent.find(L"true", colonPos) != std::wstring::npos &&
                            objContent.find(L"true", colonPos) < objContent.find(L"false", colonPos)) {
                            ws.hasFocus = true;
                        }
                    }
                }

                size_t displayStart = objContent.find(L"\"isDisplayed\"");
                if (displayStart != std::wstring::npos) {
                    size_t colonPos = objContent.find(L':', displayStart);
                    if (colonPos != std::wstring::npos) {
                        if (objContent.find(L"true", colonPos) != std::wstring::npos &&
                            objContent.find(L"true", colonPos) < objContent.find(L"false", colonPos)) {
                            ws.isDisplayed = true;
                        }
                    }
                }
                ws.windowCount = CountWindowsInWorkspace(objContent);
                
                if (!ws.name.empty()) {
                    result.push_back(ws);
                }
            }
        }
    }
    
    return result;
}

bool UpdateWorkspaceData() {
    DWORD currentTime = GetTickCount();
    if (currentTime - lastUpdateTime < 5000) {
        return false;
    }
    lastUpdateTime = currentTime;
    STARTUPINFO si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return false;
    }
    
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    
    wchar_t cmdLine[] = L"glazewm query workspaces";
    
    if (!CreateProcess(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return false;
    }
    CloseHandle(hWritePipe);
    WaitForSingleObject(pi.hProcess, 3000);
    char buffer[8192];
    DWORD bytesRead;
    std::string result;
    
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        result += buffer;
    }
    
    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    if (result.empty()) {
        return false;
    }
    
    std::wstring wResult(result.begin(), result.end());
    
    try {
        std::vector<WorkspaceInfo> newWorkspaces = ParseWorkspaceData(wResult);
        if (!newWorkspaces.empty()) {
            workspaces = newWorkspaces;
            return true;
        }
    } catch (...) {
        // :P
    }
    
    return false;
}

void DrawWorkspaceBlocks(HDC hdc, const RECT& clientRect) {
    int blockSize = 40;
    int spacing = 8;
    int startX = 15;
    int startY = 15;
    
    HFONT numberFont = CreateFont(
        18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
    
    HFONT infoFont = CreateFont(
        12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
    
    HFONT oldFont = (HFONT)SelectObject(hdc, numberFont);
    
    for (size_t i = 0; i < workspaces.size() && i < 9; i++) {
        const WorkspaceInfo& ws = workspaces[i];
        
        int x = startX + (int)i * (blockSize + spacing);
        int y = startY;

        COLORREF blockColor;
        COLORREF textColor = RGB(255, 255, 255);
        
        if (ws.hasFocus) {
            blockColor = RGB(0, 150, 0); 
        } else if (ws.isDisplayed) {
            blockColor = RGB(0, 100, 200);
        } else if (ws.windowCount > 0) {
            blockColor = RGB(100, 100, 100);
        } else {
            blockColor = RGB(50, 50, 50); 
        }

        HBRUSH blockBrush = CreateSolidBrush(blockColor);
        RECT blockRect = { x, y, x + blockSize, y + blockSize };
        FillRect(hdc, &blockRect, blockBrush);
        DeleteObject(blockBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
        HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, x, y, x + blockSize, y + blockSize);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(borderPen);
  
        SetTextColor(hdc, textColor);
        SetBkMode(hdc, TRANSPARENT);
        
        RECT textRect = { x, y, x + blockSize, y + blockSize };
        DrawText(hdc, ws.name.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (ws.windowCount > 0) {
            SelectObject(hdc, infoFont);
            std::wstring countText = std::to_wstring(ws.windowCount);
            
            RECT countRect = { x + blockSize - 12, y - 2, x + blockSize + 8, y + 12 };
            HBRUSH countBrush = CreateSolidBrush(RGB(255, 100, 100));
            FillRect(hdc, &countRect, countBrush);
            DeleteObject(countBrush);
            
            SetTextColor(hdc, RGB(255, 255, 255));
            DrawText(hdc, countText.c_str(), -1, &countRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            
            SelectObject(hdc, numberFont);
        }
    }

    SelectObject(hdc, infoFont);
    SetTextColor(hdc, RGB(200, 200, 200));
    
    int legendY = startY + blockSize + 20;
    TextOut(hdc, startX, legendY, L"● Green: Active   ● Blue: Displayed   ● Gray: Has Windows", 57);
    
    SelectObject(hdc, oldFont);
    DeleteObject(numberFont);
    DeleteObject(infoFont);
}

void PaintOverlay(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
    HBRUSH bgBrush = CreateSolidBrush(RGB(20, 20, 20));
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);
    HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(80, 80, 80));
    HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, 0, 0, clientRect.right, clientRect.bottom);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(borderPen);
    DrawWorkspaceBlocks(memDC, clientRect);
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:
        PaintOverlay(hwnd);
        break;
    case WM_DESTROY:
        running = false;
        PostQuitMessage(0);
        break;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void StartUpdater(HWND hwnd) {
    updateThread = std::thread([hwnd]() {
        while (running) {
            if (UpdateWorkspaceData()) {
                InvalidateRect(hwnd, NULL, TRUE);
                UpdateWindow(hwnd);
            }
            Sleep(1000); 
        }
    });
}

void CreateOverlayWindow() {
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"GlazeWMOverlayClass";
    wc.hbrBackground = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    HWND desktop = GetDesktopHostWindow();

    hwndOverlay = CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName,
        L"GlazeWM Workspace Widget",
        WS_POPUP,
        50, 50, 400, 120,
        desktop,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (!hwndOverlay) {
        return;
    }

    SetLayeredWindowAttributes(hwndOverlay, RGB(0, 0, 0), 220, LWA_ALPHA);
    SetWindowPos(hwndOverlay, HWND_BOTTOM, 1450, 800, 400, 120, SWP_SHOWWINDOW);
    workspaces.clear();
    StartUpdater(hwndOverlay);
}
void Wh_ModInit() {
    CreateOverlayWindow();
}

void Wh_ModUninit() {
    running = false;
    
    if (updateThread.joinable()) {
        updateThread.join();
    }
    
    if (hwndOverlay) {
        DestroyWindow(hwndOverlay);
        hwndOverlay = NULL;
    }
}

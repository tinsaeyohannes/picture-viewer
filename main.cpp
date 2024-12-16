#include <windows.h>
#include <gdiplus.h>
#include <memory>
#include <shellapi.h>
#include <commctrl.h>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>

// Link with GDI+ library
#pragma comment(lib, "gdiplus")
#pragma comment(lib, "comctl32.lib")

// Menu IDs
#define ID_FILE_OPEN 1001
#define ID_FILE_SAVE 1002
#define ID_EDIT_ROTATE_LEFT 1003
#define ID_EDIT_ROTATE_RIGHT 1004
#define ID_VIEW_ACTUAL_SIZE 1005
#define ID_VIEW_FIT_TO_WINDOW 1006
#define ID_NAV_PREV 1007
#define ID_NAV_NEXT 1008
#define ID_VIEW_DARK_MODE 1009

// Status bar parts
#define STATUS_PART_DIMENSIONS 0
#define STATUS_PART_ZOOM 1
#define STATUS_PART_FILENAME 2
#define STATUS_PART_FILESIZE 3

// Global variables
std::unique_ptr<Gdiplus::Bitmap> g_pBitmap;
std::unique_ptr<Gdiplus::Bitmap> g_pBufferedBitmap;
float g_zoom = 1.0f;
float g_targetZoom = 1.0f;
float g_rotation = 0.0f;
ULONG_PTR g_gdiplusToken;
bool g_fitToWindow = false;
float g_brightness = 0.0f;
float g_contrast = 1.0f;
HWND g_hwndStatus = NULL;
std::wstring g_currentFile;
std::vector<std::wstring> g_imageFiles;
size_t g_currentImageIndex = 0;
bool g_darkMode = false;
UINT_PTR g_zoomTimerId = 1;
bool g_isZooming = false;
float g_zoomSpeed = 1.1f;

// Function declarations
void LoadImage(HWND hwnd, LPCWSTR filename);
void UpdateBufferedBitmap(HWND hwnd);
void UpdateStatusBar(HWND hwnd);
bool IsImageFile(const std::wstring& filename);
void LoadImageDirectory(const std::wstring& currentFile);
void NavigateImage(HWND hwnd, bool next);
HMENU CreateMainMenu();
float LerpZoom(float current, float target, float t);
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
void StartZoomAnimation(HWND hwnd, float targetZoom);
void ContinuousZoom(HWND hwnd, bool zoomIn);
void StopContinuousZoom(HWND hwnd);
COLORREF GetBackgroundColor();
COLORREF GetTextColor();

COLORREF GetBackgroundColor() {
    return g_darkMode ? RGB(32, 32, 32) : RGB(255, 255, 255);
}

COLORREF GetTextColor() {
    return g_darkMode ? RGB(240, 240, 240) : RGB(0, 0, 0);
}

float LerpZoom(float current, float target, float t) {
    return current + (target - current) * t;
}

void StartZoomAnimation(HWND hwnd, float targetZoom) {
    g_targetZoom = std::max(0.1f, std::min(5.0f, targetZoom));
    SetTimer(hwnd, 1, 16, NULL); // ~60 FPS
}

void ContinuousZoom(HWND hwnd, bool zoomIn) {
    if (!g_isZooming) {
        g_isZooming = true;
        SetTimer(hwnd, g_zoomTimerId, 50, NULL);
    }
    float zoomFactor = zoomIn ? g_zoomSpeed : 1.0f / g_zoomSpeed;
    float newZoom = g_targetZoom * zoomFactor;
    
    if (newZoom >= 0.1f && newZoom <= 5.0f) {
        StartZoomAnimation(hwnd, newZoom);
        UpdateStatusBar(hwnd);
    }
}

void StopContinuousZoom(HWND hwnd) {
    if (g_isZooming) {
        g_isZooming = false;
        KillTimer(hwnd, g_zoomTimerId);
    }
}

std::wstring FormatFileSize(DWORD size) {
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB" };
    int unitIndex = 0;
    double fileSize = static_cast<double>(size);
    
    while (fileSize >= 1024 && unitIndex < 3) {
        fileSize /= 1024;
        unitIndex++;
    }
    
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(unitIndex > 0 ? 1 : 0) << fileSize << L" " << units[unitIndex];
    return ss.str();
}

void UpdateStatusBar(HWND hwnd) {
    if (!g_hwndStatus || !g_pBitmap) return;

    // Update dimensions
    std::wstringstream dimensions;
    dimensions << g_pBitmap->GetWidth() << L" × " << g_pBitmap->GetHeight() << L" px";
    SendMessageW(g_hwndStatus, SB_SETTEXTW, STATUS_PART_DIMENSIONS, (LPARAM)dimensions.str().c_str());

    // Update zoom
    std::wstringstream zoom;
    zoom << std::fixed << std::setprecision(0) << (g_zoom * 100) << L"%";
    SendMessageW(g_hwndStatus, SB_SETTEXTW, STATUS_PART_ZOOM, (LPARAM)zoom.str().c_str());

    // Update filename
    if (!g_currentFile.empty()) {
        size_t lastSlash = g_currentFile.find_last_of(L'\\');
        std::wstring filename = lastSlash != std::wstring::npos ? g_currentFile.substr(lastSlash + 1) : g_currentFile;
        SendMessageW(g_hwndStatus, SB_SETTEXTW, STATUS_PART_FILENAME, (LPARAM)filename.c_str());

        // Update file size
        WIN32_FILE_ATTRIBUTE_DATA fileInfo;
        if (GetFileAttributesExW(g_currentFile.c_str(), GetFileExInfoStandard, &fileInfo)) {
            std::wstring fileSize = FormatFileSize(fileInfo.nFileSizeLow);
            SendMessageW(g_hwndStatus, SB_SETTEXTW, STATUS_PART_FILESIZE, (LPARAM)fileSize.c_str());
        }
    }
}

bool IsImageFile(const std::wstring& filename) {
    std::wstring ext = filename.substr(filename.find_last_of(L'.'));
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == L".jpg" || ext == L".jpeg" || ext == L".png" || ext == L".bmp" || ext == L".gif";
}

void LoadImageDirectory(const std::wstring& currentFile) {
    g_imageFiles.clear();
    g_currentImageIndex = 0;

    // Get directory path
    size_t lastSlash = currentFile.find_last_of(L'\\');
    if (lastSlash == std::wstring::npos) return;
    std::wstring dirPath = currentFile.substr(0, lastSlash + 1);

    // Find all image files in directory
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW((dirPath + L"*.*").c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring filename = dirPath + findData.cFileName;
                if (IsImageFile(filename)) {
                    g_imageFiles.push_back(filename);
                    if (filename == currentFile) {
                        g_currentImageIndex = g_imageFiles.size() - 1;
                    }
                }
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }

    // Sort files by name
    std::sort(g_imageFiles.begin(), g_imageFiles.end());
}

void LoadImage(HWND hwnd, LPCWSTR filename) {
    g_pBitmap.reset(new Gdiplus::Bitmap(filename));
    if (g_pBitmap->GetLastStatus() == Gdiplus::Ok) {
        g_currentFile = filename;
        LoadImageDirectory(filename);

        if (g_fitToWindow) {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            float windowRatio = (float)(clientRect.right - clientRect.left) / (clientRect.bottom - clientRect.top);
            float imageRatio = (float)g_pBitmap->GetWidth() / g_pBitmap->GetHeight();
            
            if (imageRatio > windowRatio) {
                g_zoom = (float)(clientRect.right - clientRect.left) / g_pBitmap->GetWidth();
            } else {
                g_zoom = (float)(clientRect.bottom - clientRect.top) / g_pBitmap->GetHeight();
            }
            g_targetZoom = g_zoom;
        }
        UpdateBufferedBitmap(hwnd);
        UpdateStatusBar(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

void SaveImage(HWND hwnd) {
    if (!g_pBitmap) return;

    OPENFILENAMEW ofn = { 0 };
    WCHAR szFile[260] = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"PNG Files\0*.png\0JPEG Files\0*.jpg\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&ofn)) {
        CLSID encoderClsid;
        GetEncoderClsid(L"image/png", &encoderClsid);
        g_pBitmap->Save(szFile, &encoderClsid, NULL);
    }
}

void UpdateBufferedBitmap(HWND hwnd) {
    if (!g_pBitmap) return;

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;

    g_pBufferedBitmap.reset(new Gdiplus::Bitmap(width, height));
    Gdiplus::Graphics graphics(g_pBufferedBitmap.get());
    
    graphics.Clear(Gdiplus::Color(GetBackgroundColor()));
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    // Calculate centered position
    float scaledWidth = g_pBitmap->GetWidth() * g_zoom;
    float scaledHeight = g_pBitmap->GetHeight() * g_zoom;
    float x = (width - scaledWidth) / 2;
    float y = (height - scaledHeight) / 2;

    // Set up transformation for rotation
    graphics.TranslateTransform(width / 2.0f, height / 2.0f);
    graphics.RotateTransform(g_rotation);
    graphics.TranslateTransform(-width / 2.0f, -height / 2.0f);

    // Set up color matrix for brightness and contrast
    Gdiplus::ColorMatrix colorMatrix = {
        1.0f * g_contrast, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f * g_contrast, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f * g_contrast, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        g_brightness, g_brightness, g_brightness, 0.0f, 1.0f
    };

    Gdiplus::ImageAttributes imageAttr;
    imageAttr.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

    graphics.DrawImage(g_pBitmap.get(), 
        Gdiplus::RectF(x, y, scaledWidth, scaledHeight), 
        0, 0, g_pBitmap->GetWidth(), g_pBitmap->GetHeight(), 
        Gdiplus::UnitPixel, &imageAttr);
}

void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    
    // Create memory DC and bitmap for double buffering
    HDC memDC = CreateCompatibleDC(hdc);
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // Fill background
    HBRUSH hBrush = CreateSolidBrush(GetBackgroundColor());
    FillRect(memDC, &clientRect, hBrush);
    DeleteObject(hBrush);

    if (g_pBufferedBitmap) {
        Gdiplus::Graphics graphics(memDC);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

        // Calculate centered position
        float scaledWidth = g_pBufferedBitmap->GetWidth() * g_zoom;
        float scaledHeight = g_pBufferedBitmap->GetHeight() * g_zoom;
        float x = (width - scaledWidth) / 2;
        float y = (height - scaledHeight) / 2;

        graphics.DrawImage(g_pBufferedBitmap.get(), x, y, scaledWidth, scaledHeight);
    }

    // Copy from memory DC to screen
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    // Clean up
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

void NavigateImage(HWND hwnd, bool next) {
    if (g_imageFiles.empty()) return;

    if (next) {
        g_currentImageIndex = (g_currentImageIndex + 1) % g_imageFiles.size();
    } else {
        g_currentImageIndex = (g_currentImageIndex + g_imageFiles.size() - 1) % g_imageFiles.size();
    }

    LoadImage(hwnd, g_imageFiles[g_currentImageIndex].c_str());
}

HMENU CreateMainMenu() {
    HMENU hMenu = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    HMENU hEditMenu = CreatePopupMenu();
    HMENU hViewMenu = CreatePopupMenu();
    HMENU hNavMenu = CreatePopupMenu();

    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_OPEN, L"&Open\tCtrl+O");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFileMenu, MF_STRING, IDCLOSE, L"E&xit");

    AppendMenuW(hEditMenu, MF_STRING, ID_EDIT_ROTATE_LEFT, L"Rotate &Left\tCtrl+L");
    AppendMenuW(hEditMenu, MF_STRING, ID_EDIT_ROTATE_RIGHT, L"Rotate &Right\tCtrl+R");

    AppendMenuW(hViewMenu, MF_STRING, ID_VIEW_ACTUAL_SIZE, L"&Actual Size\tCtrl+0");
    AppendMenuW(hViewMenu, MF_STRING, ID_VIEW_FIT_TO_WINDOW, L"&Fit to Window\tCtrl+F");
    AppendMenuW(hViewMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hViewMenu, MF_STRING, ID_VIEW_DARK_MODE, L"&Dark Mode\tCtrl+D");

    AppendMenuW(hNavMenu, MF_STRING, ID_NAV_PREV, L"&Previous\tLeft");
    AppendMenuW(hNavMenu, MF_STRING, ID_NAV_NEXT, L"&Next\tRight");

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"&File");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hEditMenu, L"&Edit");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hViewMenu, L"&View");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hNavMenu, L"&Navigate");

    return hMenu;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_CREATE:
        {
            // Enable drag and drop
            DragAcceptFiles(hwnd, TRUE);
            
            // Create status bar with dark mode support
            g_hwndStatus = CreateWindowEx(
                0,
                STATUSCLASSNAME,
                NULL,
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                0, 0, 0, 0,
                hwnd,
                NULL,
                ((LPCREATESTRUCT)lParam)->hInstance,
                NULL);

            // Set status bar parts
            int statusParts[4] = {150, 250, 450, -1};
            SendMessage(g_hwndStatus, SB_SETPARTS, 4, (LPARAM)statusParts);
            
            // Set menu
            SetMenu(hwnd, CreateMainMenu());
            return 0;
        }

        case WM_PAINT:
        {
            OnPaint(hwnd);
            return 0;
        }

        case WM_TIMER:
        {
            if (abs(g_zoom - g_targetZoom) > 0.001f) {
                g_zoom = LerpZoom(g_zoom, g_targetZoom, 0.2f);
                UpdateBufferedBitmap(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            } else {
                KillTimer(hwnd, 1);
            }
            return 0;
        }

        case WM_DROPFILES:
        {
            HDROP hDrop = (HDROP)wParam;
            WCHAR filename[MAX_PATH];
            if (DragQueryFileW(hDrop, 0, filename, MAX_PATH) > 0) {
                LoadImage(hwnd, filename);
            }
            DragFinish(hDrop);
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            float zoomFactor = delta > 0 ? 1.1f : 0.9f;
            float newZoom = g_targetZoom * zoomFactor;
            
            // Limit zoom range
            if (newZoom >= 0.1f && newZoom <= 5.0f) {
                StartZoomAnimation(hwnd, newZoom);
                UpdateStatusBar(hwnd);
            }
            return 0;
        }

        case WM_KEYDOWN:
        {
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                switch (wParam) {
                    case 'O':
                        SendMessage(hwnd, WM_COMMAND, ID_FILE_OPEN, 0);
                        return 0;
                    case 'S':
                        SendMessage(hwnd, WM_COMMAND, ID_FILE_SAVE, 0);
                        return 0;
                    case 'L':
                        SendMessage(hwnd, WM_COMMAND, ID_EDIT_ROTATE_LEFT, 0);
                        return 0;
                    case 'R':
                        SendMessage(hwnd, WM_COMMAND, ID_EDIT_ROTATE_RIGHT, 0);
                        return 0;
                    case 'F':
                        SendMessage(hwnd, WM_COMMAND, ID_VIEW_FIT_TO_WINDOW, 0);
                        return 0;
                    case '0':
                        SendMessage(hwnd, WM_COMMAND, ID_VIEW_ACTUAL_SIZE, 0);
                        return 0;
                    case 'D':
                        SendMessage(hwnd, WM_COMMAND, ID_VIEW_DARK_MODE, 0);
                        return 0;
                }
            } else {
                switch (wParam) {
                    case VK_LEFT:
                        SendMessage(hwnd, WM_COMMAND, ID_NAV_PREV, 0);
                        return 0;
                    case VK_RIGHT:
                        SendMessage(hwnd, WM_COMMAND, ID_NAV_NEXT, 0);
                        return 0;
                    case VK_UP:
                        ContinuousZoom(hwnd, true);
                        return 0;
                    case VK_DOWN:
                        ContinuousZoom(hwnd, false);
                        return 0;
                }
            }
            break;
        }

        case WM_KEYUP:
        {
            switch (wParam) {
                case VK_UP:
                case VK_DOWN:
                    StopContinuousZoom(hwnd);
                    return 0;
            }
            break;
        }

        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case ID_FILE_OPEN:
                {
                    OPENFILENAMEW ofn = { 0 };
                    WCHAR szFile[260] = { 0 };
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = sizeof(szFile);
                    ofn.lpstrFilter = L"Image Files\0*.bmp;*.jpg;*.jpeg;*.png\0All Files\0*.*\0";
                    ofn.nFilterIndex = 1;
                    ofn.lpstrFileTitle = NULL;
                    ofn.nMaxFileTitle = 0;
                    ofn.lpstrInitialDir = NULL;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                    if (GetOpenFileNameW(&ofn)) {
                        LoadImage(hwnd, szFile);
                    }
                    return 0;
                }

                case ID_FILE_SAVE:
                    SaveImage(hwnd);
                    return 0;

                case ID_EDIT_ROTATE_LEFT:
                    g_rotation -= 90.0f;
                    UpdateBufferedBitmap(hwnd);
                    InvalidateRect(hwnd, NULL, TRUE);
                    return 0;

                case ID_EDIT_ROTATE_RIGHT:
                    g_rotation += 90.0f;
                    UpdateBufferedBitmap(hwnd);
                    InvalidateRect(hwnd, NULL, TRUE);
                    return 0;

                case ID_VIEW_ACTUAL_SIZE:
                    g_fitToWindow = false;
                    StartZoomAnimation(hwnd, 1.0f);
                    return 0;

                case ID_VIEW_FIT_TO_WINDOW:
                    g_fitToWindow = true;
                    if (g_pBitmap) {
                        RECT clientRect;
                        GetClientRect(hwnd, &clientRect);
                        float windowRatio = (float)(clientRect.right - clientRect.left) / (clientRect.bottom - clientRect.top);
                        float imageRatio = (float)g_pBitmap->GetWidth() / g_pBitmap->GetHeight();
                        
                        if (imageRatio > windowRatio) {
                            StartZoomAnimation(hwnd, (float)(clientRect.right - clientRect.left) / g_pBitmap->GetWidth());
                        } else {
                            StartZoomAnimation(hwnd, (float)(clientRect.bottom - clientRect.top) / g_pBitmap->GetHeight());
                        }
                    }
                    return 0;

                case ID_NAV_PREV:
                    NavigateImage(hwnd, false);
                    return 0;

                case ID_NAV_NEXT:
                    NavigateImage(hwnd, true);
                    return 0;

                case ID_VIEW_DARK_MODE:
                    g_darkMode = !g_darkMode;
                    CheckMenuItem(GetMenu(hwnd), ID_VIEW_DARK_MODE, 
                                MF_BYCOMMAND | (g_darkMode ? MF_CHECKED : MF_UNCHECKED));
                    
                    // Update status bar colors
                    SendMessage(g_hwndStatus, SB_SETBKCOLOR, 0, (LPARAM)GetBackgroundColor());
                    InvalidateRect(g_hwndStatus, NULL, TRUE);
                    
                    // Redraw main window
                    InvalidateRect(hwnd, NULL, TRUE);
                    return 0;
            }
            break;
        }

        case WM_SIZE:
        {
            // Resize status bar
            SendMessage(g_hwndStatus, WM_SIZE, 0, 0);

            if (g_pBitmap) {
                UpdateBufferedBitmap(hwnd);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;

    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;

    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }

    free(pImageCodecInfo);
    return -1;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPSTR lpCmdLine, int nCmdShow)
{
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

    // Initialize Common Controls
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    const char CLASS_NAME[]  = "Photo Viewer";
    
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    
    RegisterClassEx(&wc);
    
    HWND hwnd = CreateWindowEx(
        WS_EX_ACCEPTFILES,
        CLASS_NAME,
        "Photo Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL,
        NULL,
        hInstance,
        NULL
    );
    
    if (hwnd == NULL)
    {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Shutdown GDI+
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return msg.wParam;
}

#include <windows.h>
#include <gdiplus.h>
#include <memory>
#include <shellapi.h>
#include <commctrl.h>
#include <cmath>

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

// Function declarations
void LoadImage(HWND hwnd, LPCWSTR filename);
void SaveImage(HWND hwnd);
void UpdateBufferedBitmap(HWND hwnd);
void StartZoomAnimation(HWND hwnd, float targetZoom);
float LerpZoom(float current, float target, float t);
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);

// Function to create menu
HMENU CreateMainMenu() {
    HMENU hMenu = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    HMENU hEditMenu = CreatePopupMenu();
    HMENU hViewMenu = CreatePopupMenu();

    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_OPEN, L"&Open\tCtrl+O");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFileMenu, MF_STRING, IDCLOSE, L"E&xit");

    AppendMenuW(hEditMenu, MF_STRING, ID_EDIT_ROTATE_LEFT, L"Rotate &Left\tCtrl+L");
    AppendMenuW(hEditMenu, MF_STRING, ID_EDIT_ROTATE_RIGHT, L"Rotate &Right\tCtrl+R");

    AppendMenuW(hViewMenu, MF_STRING, ID_VIEW_ACTUAL_SIZE, L"&Actual Size\tCtrl+0");
    AppendMenuW(hViewMenu, MF_STRING, ID_VIEW_FIT_TO_WINDOW, L"&Fit to Window\tCtrl+F");

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"&File");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hEditMenu, L"&Edit");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hViewMenu, L"&View");

    return hMenu;
}

void LoadImage(HWND hwnd, LPCWSTR filename) {
    g_pBitmap.reset(new Gdiplus::Bitmap(filename));
    if (g_pBitmap->GetLastStatus() == Gdiplus::Ok) {
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
    
    graphics.Clear(Gdiplus::Color::White);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    // Calculate scaled dimensions
    float scaledWidth = g_pBitmap->GetWidth() * g_zoom;
    float scaledHeight = g_pBitmap->GetHeight() * g_zoom;
    
    // Center the image
    float x = (width - scaledWidth) / 2;
    float y = (height - scaledHeight) / 2;

    // Set up transformation matrix for rotation
    Gdiplus::Matrix matrix;
    matrix.RotateAt(g_rotation, Gdiplus::PointF(x + scaledWidth/2, y + scaledHeight/2));
    graphics.SetTransform(&matrix);

    // Create color matrix for brightness and contrast
    Gdiplus::ColorMatrix colorMatrix = {
        g_contrast, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, g_contrast, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, g_contrast, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        g_brightness, g_brightness, g_brightness, 0.0f, 1.0f
    };

    Gdiplus::ImageAttributes imageAttr;
    imageAttr.SetColorMatrix(&colorMatrix);

    graphics.DrawImage(g_pBitmap.get(), 
        Gdiplus::RectF(x, y, scaledWidth, scaledHeight),
        0, 0, g_pBitmap->GetWidth(), g_pBitmap->GetHeight(),
        Gdiplus::UnitPixel, &imageAttr);
}

void StartZoomAnimation(HWND hwnd, float targetZoom) {
    g_targetZoom = targetZoom;
    SetTimer(hwnd, 1, 16, NULL); // 60 FPS animation
}

float LerpZoom(float current, float target, float t) {
    return current + (target - current) * t;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_CREATE:
        {
            // Enable drag and drop
            DragAcceptFiles(hwnd, TRUE);
            
            // Set menu
            SetMenu(hwnd, CreateMainMenu());
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            if (g_pBufferedBitmap) {
                Gdiplus::Graphics graphics(hdc);
                graphics.DrawImage(g_pBufferedBitmap.get(), 0, 0);
            }
            
            EndPaint(hwnd, &ps);
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
            }
            return 0;
        }

        case WM_KEYDOWN:
        {
            // Handle keyboard shortcuts
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
                }
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
            }
            break;
        }

        case WM_SIZE:
            if (g_pBitmap) {
                UpdateBufferedBitmap(hwnd);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;

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

// Helper function to get encoder CLSID
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

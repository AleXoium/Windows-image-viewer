// d2d_viewer.cpp : Minimal, efficient Windows image viewer (WIC + Direct2D)
// Build (x64 or x86 Dev Cmd Prompt):
//cl /EHsc /O2 d2d_viewer.cpp /DNOMINMAX /DUNICODE /D_UNICODE /link /SUBSYSTEM:WINDOWS d2d1.lib windowscodecs.lib user32.lib gdi32.lib ole32.lib shell32.lib

#define NOMINMAX 1
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <shellapi.h>
#include <sal.h>
#include <string>
#include <algorithm>
#include <strsafe.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")

template<typename T> inline void SafeRelease(T** pp) { if (*pp) { (*pp)->Release(); *pp = nullptr; } }

static void ReportHR(HRESULT hr, const wchar_t* step) {
    wchar_t* sys = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, hr, 0,
        (LPWSTR)&sys, 0, nullptr);
    wchar_t buf[1024];
    StringCchPrintfW(buf, 1024, L"%s failed (0x%08X)\n%s",
        step, (unsigned)hr, sys ? sys : L"");
    if (sys) LocalFree(sys);
    MessageBoxW(nullptr, buf, L"Error", MB_OK | MB_ICONERROR);
    PostQuitMessage((int)hr);
}
#undef HR
#define HR(expr) do { HRESULT __hr = (expr); if (FAILED(__hr)) ReportHR(__hr, L#expr); } while (0)

struct AppState {
    std::wstring path;
    UINT maxDim = 1600;
    ID2D1Factory* d2dFactory = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    ID2D1Bitmap* bmp = nullptr;
    IWICImagingFactory* wic = nullptr;
    UINT bmpW = 0, bmpH = 0;
};

static RECT FitRect(UINT srcW, UINT srcH, UINT dstW, UINT dstH) {
    double sx = (double)dstW / (double)srcW;
    double sy = (double)dstH / (double)srcH;
    double s = std::min(sx, sy);
    UINT w = (UINT)(srcW * s);
    UINT h = (UINT)(srcH * s);
    LONG x = (dstW - (LONG)w) / 2;
    LONG y = (dstH - (LONG)h) / 2;
    RECT r{ x, y, x + (LONG)w, y + (LONG)h };
    return r;
}

static void CreateD2DBitmapFromWicSource(
    ID2D1HwndRenderTarget* rt,
    IWICImagingFactory* wic,
    IWICBitmapSource* src,
    ID2D1Bitmap** outBmp)
{
    *outBmp = nullptr;

    IWICFormatConverter* conv = nullptr;
    HRESULT hr = wic->CreateFormatConverter(&conv);
    if (FAILED(hr)) ReportHR(hr, L"CreateFormatConverter");

    hr = conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0,
        WICBitmapPaletteTypeCustom);

    D2D1_BITMAP_PROPERTIES propsPMA =
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED));

    if (SUCCEEDED(hr)) {
        hr = rt->CreateBitmapFromWicBitmap(conv, &propsPMA, outBmp);
        if (SUCCEEDED(hr)) {
            SafeRelease(&conv);
            return;
        }
    }

    IWICFormatConverter* conv2 = nullptr;
    HR(wic->CreateFormatConverter(&conv2));
    HR(conv2->Initialize(src, GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0,
        WICBitmapPaletteTypeCustom));

    D2D1_BITMAP_PROPERTIES propsIgnore =
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_IGNORE));

    HR(rt->CreateBitmapFromWicBitmap(conv2, &propsIgnore, outBmp));

    SafeRelease(&conv2);
    SafeRelease(&conv);
}

static void CreateDeviceResources(HWND hWnd, AppState* S) {
    if (S->rt) return;

    RECT rc; GetClientRect(hWnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    HR(S->d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hWnd, size),
        &S->rt));

    if (S->bmp) return;

    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;

    HR(S->wic->CreateDecoderFromFilename(
        S->path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &dec));
    HR(dec->GetFrame(0, &frame));

    UINT w, h; HR(frame->GetSize(&w, &h));
    S->bmpW = w; S->bmpH = h;

    UINT tw = w, th = h;
    if (std::max(w, h) > S->maxDim) {
        if (w >= h) { tw = S->maxDim; th = (UINT)((double)h * (double)S->maxDim / (double)w); }
        else { th = S->maxDim; tw = (UINT)((double)w * (double)S->maxDim / (double)h); }
    }

    IWICBitmapSource* source = nullptr;
    if (tw != w || th != h) {
        HR(S->wic->CreateBitmapScaler(&scaler));
        HR(scaler->Initialize(frame, tw, th, WICBitmapInterpolationModeFant));
        source = scaler;
        source->AddRef();
    }
    else {
        source = frame;
        source->AddRef();
    }

    CreateD2DBitmapFromWicSource(S->rt, S->wic, source, &S->bmp);

    SafeRelease(&source);
    SafeRelease(&scaler);
    SafeRelease(&frame);
    SafeRelease(&dec);
}

static void DiscardDeviceResources(AppState* S) {
    SafeRelease(&S->bmp);
    SafeRelease(&S->rt);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* S = reinterpret_cast<AppState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto cs = reinterpret_cast<LPCREATESTRUCTW>(lParam);
        S = (AppState*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)S);

        HR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &S->d2dFactory));
        HR(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&S->wic)));
        return 0;
    }
    case WM_SIZE:
        if (S && S->rt) {
            UINT w = LOWORD(lParam), h = HIWORD(lParam);
            S->rt->Resize(D2D1::SizeU(w, h));
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hWnd, &ps);
        if (S) {
            CreateDeviceResources(hWnd, S);
            if (S->rt) {
                S->rt->BeginDraw();
                S->rt->Clear(D2D1::ColorF(D2D1::ColorF::Black));
                if (S->bmp) {
                    D2D1_SIZE_F sz = S->bmp->GetSize();
                    RECT rc; GetClientRect(hWnd, &rc);
                    RECT fit = FitRect((UINT)sz.width, (UINT)sz.height,
                        rc.right - rc.left, rc.bottom - rc.top);
                    D2D1_RECT_F dst = D2D1::RectF(
                        (FLOAT)fit.left, (FLOAT)fit.top, (FLOAT)fit.right, (FLOAT)fit.bottom);
                    S->rt->DrawBitmap(S->bmp, dst, 1.0f,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                }
                HRESULT hr = S->rt->EndDraw();
                if (hr == D2DERR_RECREATE_TARGET) {
                    DiscardDeviceResources(S);
                }
            }
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        if (S) {
            DiscardDeviceResources(S);
            SafeRelease(&S->wic);
            SafeRelease(&S->d2dFactory);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int nCmdShow) {
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCo)) {
        ReportHR(hrCo, L"CoInitializeEx");
        return 1;
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc < 2) {
        MessageBoxW(nullptr, L"Usage:\n  d2d_viewer.exe <image_path> [--max N]",
            L"Image Viewer", MB_OK | MB_ICONINFORMATION);
        CoUninitialize();
        return 1;
    }

    AppState S{};
    S.path = argv[1];
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == L"--max") {
            UINT v = (UINT)_wtoi(argv[i + 1]);
            if (v > 0) S.maxDim = v;
        }
    }
    LocalFree(argv);

    DWORD attrs = GetFileAttributesW(S.path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, (L"File not found:\n" + S.path).c_str(),
            L"Image Viewer", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"d2d_viewer_cls";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    std::wstring title = L"Viewer - " + S.path;
    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        nullptr, nullptr, hInst, &S);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}

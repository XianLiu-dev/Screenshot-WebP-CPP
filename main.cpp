#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <iostream>
#include "webp/encode.h"

#define _CRT_SECURE_NO_WARNINGS

#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "libwebp.lib")

using namespace Gdiplus;

static std::wstring TimestampFilename() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[128];
    swprintf(buf, _countof(buf), L"screenshot_%04d-%02d-%02d_%02d-%02d-%02d.webp",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

bool SaveScreenshotAsWebP(const std::wstring& filename, int quality = 90, bool lossless = false)
{
    GdiplusStartupInput si;
    ULONG_PTR token;
    if (GdiplusStartup(&token, &si, nullptr) != Ok) {
        std::cerr << "GdiplusStartup failed\n";
        return false;
    }

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hScreenDC = GetDC(nullptr);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBmp = CreateCompatibleBitmap(hScreenDC, vw, vh);
    HGDIOBJ oldObj = SelectObject(hMemDC, hBmp);
    BitBlt(hMemDC, 0, 0, vw, vh, hScreenDC, vx, vy, SRCCOPY);
    SelectObject(hMemDC, oldObj);

    Bitmap srcBmp(hBmp, nullptr);

    BitmapData bd;
    Rect r(0, 0, vw, vh);
    if (srcBmp.LockBits(&r, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok) {
        std::cerr << "LockBits failed\n";
        DeleteObject(hBmp);
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreenDC);
        GdiplusShutdown(token);
        return false;
    }

    uint8_t* pixels = reinterpret_cast<uint8_t*>(bd.Scan0);
    int stride = bd.Stride;
    std::vector<uint8_t> buffer(vw * vh * 4);

    for (int y = 0; y < vh; ++y) {
        memcpy(&buffer[y * vw * 4], pixels + y * stride, vw * 4);
    }

    srcBmp.UnlockBits(&bd);

    uint8_t* webpData = nullptr;
    size_t webpSize = 0;

    WebPConfig config;
    if (!WebPConfigInit(&config)) {
        std::cerr << "WebPConfigInit failed\n";
        DeleteObject(hBmp);
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreenDC);
        GdiplusShutdown(token);
        return false;
    }

    // Set common parameters
    config.method = 1;

    float qf = (float)quality;
    if (lossless) {
        if (!WebPConfigPreset(&config, WEBP_PRESET_DRAWING, qf)) {
            std::cerr << "WebPConfigPreset failed\n";
            DeleteObject(hBmp);
            DeleteDC(hMemDC);
            ReleaseDC(nullptr, hScreenDC);
            GdiplusShutdown(token);
            return false;
        }
        config.lossless = 1;
    }
    else {
        if (!WebPConfigPreset(&config, WEBP_PRESET_DRAWING, qf)) {
            std::cerr << "WebPConfigPreset failed\n";
            DeleteObject(hBmp);
            DeleteDC(hMemDC);
            ReleaseDC(nullptr, hScreenDC);
            GdiplusShutdown(token);
            return false;
        }
        config.quality = qf;
    }

    if (!WebPValidateConfig(&config)) {
        std::cerr << "WebPValidateConfig failed\n";
        DeleteObject(hBmp);
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreenDC);
        GdiplusShutdown(token);
        return false;
    }

    WebPPicture pic;
    if (!WebPPictureInit(&pic)) {
        std::cerr << "WebPPictureInit failed\n";
        DeleteObject(hBmp);
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreenDC);
        GdiplusShutdown(token);
        return false;
    }

    pic.width = vw;
    pic.height = vh;
    pic.use_argb = 1;

    if (!WebPPictureAlloc(&pic)) {
        std::cerr << "WebPPictureAlloc failed\n";
        WebPPictureFree(&pic);
        DeleteObject(hBmp);
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreenDC);
        GdiplusShutdown(token);
        return false;
    }

    uint32_t* argb = pic.argb;
    int argb_stride_pixels = pic.argb_stride;
    for (int y = 0; y < vh; ++y) {
        memcpy(argb + y * argb_stride_pixels, buffer.data() + y * vw * 4, vw * 4);
    }

    WebPMemoryWriter mw;
    WebPMemoryWriterInit(&mw);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &mw;

    if (!WebPEncode(&config, &pic)) {
        std::cerr << "WebPEncode failed: " << pic.error_code << "\n";
        if (mw.mem) WebPFree(mw.mem);
        WebPPictureFree(&pic);
        DeleteObject(hBmp);
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreenDC);
        GdiplusShutdown(token);
        return false;
    }

    webpData = mw.mem;
    webpSize = mw.size;

    if (!webpData || webpSize == 0) {
        std::cerr << "WebP encoding produced no output\n";
        WebPPictureFree(&pic);
        DeleteObject(hBmp);
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreenDC);
        GdiplusShutdown(token);
        return false;
    }

    WebPPictureFree(&pic);

    FILE* f = nullptr;
    _wfopen_s(&f, filename.c_str(), L"wb");
    if (!f) {
        std::cerr << "Cannot open output file\n";
        WebPFree(webpData);
        DeleteObject(hBmp);
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreenDC);
        GdiplusShutdown(token);
        return false;
    }
    fwrite(webpData, 1, webpSize, f);
    fclose(f);

    WebPFree(webpData);
    DeleteObject(hBmp);
    DeleteDC(hMemDC);
    ReleaseDC(nullptr, hScreenDC);
    GdiplusShutdown(token);

    return true;
}

int wmain(int argc, wchar_t* argv[])
{
    std::wstring output = TimestampFilename();
    int quality = 80;  // Updated default to 80 for better balance of size/quality/speed
    bool lossless = false;

    if (argc >= 2) output = argv[1];
    if (argc >= 3) quality = _wtoi(argv[2]);
    for (int i = 1; i < argc; ++i)
        if (_wcsicmp(argv[i], L"-l") == 0) lossless = true;

    std::wcout << L"Output: " << output << L"\nQuality: " << quality
        << L"\nLossless: " << (lossless ? L"yes" : L"no") << L"\n";

    if (SaveScreenshotAsWebP(output, quality, lossless)) {
        std::wcout << L"Saved screenshot successfully.\n";
        return 0;
    }
    else {
        std::wcerr << L"Failed to save screenshot.\n";
        return 1;
    }
}
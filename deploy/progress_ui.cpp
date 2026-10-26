#include "progress_ui.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "core/win7.hpp"

namespace osp::deploy {
namespace {

constexpr const wchar_t* kWindowClass = L"OSPTDeployProgress";
constexpr int kHotkeyId = 0x0F11; // Ctrl+F11 黑屏急救

std::wstring toWide(const std::string& text)
{
    if (text.empty())
    {
        return std::wstring();
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
    {
        return std::wstring();
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string toUtf8(const std::wstring& text)
{
    if (text.empty())
    {
        return std::string();
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr
    );
    if (size <= 0)
    {
        return std::string();
    }
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr
    );
    return utf8;
}

std::string describeMode()
{
    // 供状态行显示当前显示模式（黑屏急救后能确认真的恢复了）
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) == 0)
    {
        return std::string();
    }
    char buffer[64] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%lux%lu %luHz",
        static_cast<unsigned long>(mode.dmPelsWidth),
        static_cast<unsigned long>(mode.dmPelsHeight),
        static_cast<unsigned long>(mode.dmDisplayFrequency)
    );
    return std::string(buffer);
}

std::uint64_t nowMs()
{
    return GetTickCount64();
}

std::string formatDuration(std::uint64_t milliseconds)
{
    const std::uint64_t totalSeconds = milliseconds / 1000;
    char buffer[32] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%02llu:%02llu",
        static_cast<unsigned long long>(totalSeconds / 60),
        static_cast<unsigned long long>(totalSeconds % 60)
    );
    return std::string(buffer);
}

// ---------------------------------------------------------------- gdiplus 扁平 API
//
// 只声明用到的那几个。不链 gdiplus.lib：WinPE 里 gdiplus.dll 不一定在，
// 动态取不到就退回纯色背景 + LoadImage 的 BMP 路径。
struct GdiplusStartupInput
{
    UINT32 GdiplusVersion;
    void* DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
};

using GdiplusStartupFn = int(WINAPI*)(ULONG_PTR*, const GdiplusStartupInput*, void*);
using GdiplusShutdownFn = void(WINAPI*)(ULONG_PTR);
using GdipCreateBitmapFromFileFn = int(WINAPI*)(const wchar_t*, void**);
using GdipGetImageWidthFn = int(WINAPI*)(void*, UINT*);
using GdipGetImageHeightFn = int(WINAPI*)(void*, UINT*);
using GdipCreateFromHDCFn = int(WINAPI*)(HDC, void**);
using GdipDrawImageRectIFn = int(WINAPI*)(void*, void*, int, int, int, int);
using GdipDeleteGraphicsFn = int(WINAPI*)(void*);
using GdipDisposeImageFn = int(WINAPI*)(void*);

struct GdiplusApi
{
    bool available = false;
    ULONG_PTR token = 0;
    GdiplusStartupFn startup = nullptr;
    GdiplusShutdownFn shutdown = nullptr;
    GdipCreateBitmapFromFileFn createBitmapFromFile = nullptr;
    GdipGetImageWidthFn getWidth = nullptr;
    GdipGetImageHeightFn getHeight = nullptr;
    GdipCreateFromHDCFn createFromHdc = nullptr;
    GdipDrawImageRectIFn drawImageRect = nullptr;
    GdipDeleteGraphicsFn deleteGraphics = nullptr;
    GdipDisposeImageFn disposeImage = nullptr;
};

GdiplusApi& gdiplus()
{
    static GdiplusApi api;
    static bool tried = false;
    if (tried)
    {
        return api;
    }
    tried = true;

    HMODULE module = LoadLibraryW(L"gdiplus.dll");
    if (module == nullptr)
    {
        return api;
    }

    api.startup =
        reinterpret_cast<GdiplusStartupFn>(reinterpret_cast<void*>(GetProcAddress(module, "GdiplusStartup")));
    api.shutdown =
        reinterpret_cast<GdiplusShutdownFn>(reinterpret_cast<void*>(GetProcAddress(module, "GdiplusShutdown"))
        );
    api.createBitmapFromFile = reinterpret_cast<GdipCreateBitmapFromFileFn>(
        reinterpret_cast<void*>(GetProcAddress(module, "GdipCreateBitmapFromFile"))
    );
    api.getWidth = reinterpret_cast<GdipGetImageWidthFn>(
        reinterpret_cast<void*>(GetProcAddress(module, "GdipGetImageWidth"))
    );
    api.getHeight = reinterpret_cast<GdipGetImageHeightFn>(
        reinterpret_cast<void*>(GetProcAddress(module, "GdipGetImageHeight"))
    );
    api.createFromHdc = reinterpret_cast<GdipCreateFromHDCFn>(
        reinterpret_cast<void*>(GetProcAddress(module, "GdipCreateFromHDC"))
    );
    api.drawImageRect = reinterpret_cast<GdipDrawImageRectIFn>(
        reinterpret_cast<void*>(GetProcAddress(module, "GdipDrawImageRectI"))
    );
    api.deleteGraphics = reinterpret_cast<GdipDeleteGraphicsFn>(
        reinterpret_cast<void*>(GetProcAddress(module, "GdipDeleteGraphics"))
    );
    api.disposeImage = reinterpret_cast<GdipDisposeImageFn>(
        reinterpret_cast<void*>(GetProcAddress(module, "GdipDisposeImage"))
    );

    if (api.startup == nullptr || api.shutdown == nullptr || api.createBitmapFromFile == nullptr ||
        api.getWidth == nullptr || api.getHeight == nullptr || api.createFromHdc == nullptr ||
        api.drawImageRect == nullptr || api.deleteGraphics == nullptr || api.disposeImage == nullptr)
    {
        return api;
    }

    GdiplusStartupInput input = {};
    input.GdiplusVersion = 1;
    if (api.startup(&api.token, &input, nullptr) != 0)
    {
        return api;
    }
    api.available = true;
    return api;
}

std::vector<std::string> listImages(const std::string& directory)
{
    namespace fs = std::filesystem;
    std::vector<std::string> result;

    std::error_code ec;
    const std::u8string u8dir(reinterpret_cast<const char8_t*>(directory.data()), directory.size());
    const fs::path dir(u8dir);
    if (!fs::is_directory(dir, ec))
    {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
    {
        if (ec || !entry.is_regular_file(ec))
        {
            continue;
        }
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (extension == ".bmp" || extension == ".jpg" || extension == ".jpeg" || extension == ".png")
        {
            const std::u8string u8 = entry.path().u8string();
            result.emplace_back(reinterpret_cast<const char*>(u8.data()), u8.size());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

void fillRect(HDC dc, const RECT& rect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void drawTextLine(HDC dc, int x, int y, const std::wstring& text, COLORREF color, HFONT font)
{
    if (text.empty())
    {
        return;
    }
    HGDIOBJ previousFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
    SelectObject(dc, previousFont);
}

// 在矩形里居中画一行文字（垂直也居中）
void drawTextCentered(HDC dc, const RECT& area, const std::wstring& text, COLORREF color, HFONT font)
{
    if (text.empty())
    {
        return;
    }
    HGDIOBJ previousFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT target = area;
    DrawTextW(
        dc, text.c_str(), static_cast<int>(text.size()), &target, DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );
    SelectObject(dc, previousFont);
}

COLORREF blend(COLORREF a, COLORREF b, int percent)
{
    const int inv = 100 - percent;
    const int r = (GetRValue(a) * inv + GetRValue(b) * percent) / 100;
    const int g = (GetGValue(a) * inv + GetGValue(b) * percent) / 100;
    const int bl = (GetBValue(a) * inv + GetBValue(b) * percent) / 100;
    return RGB(r, g, bl);
}

} // namespace

// ---------------------------------------------------------------- Presentation

bool parseHexColor(const std::string& text, COLORREF& color)
{
    if (text.size() != 7 || text[0] != '#')
    {
        return false;
    }
    for (std::size_t i = 1; i < text.size(); ++i)
    {
        if (std::isxdigit(static_cast<unsigned char>(text[i])) == 0)
        {
            return false;
        }
    }
    const auto component = [&text](std::size_t offset) -> int {
        const std::string part = text.substr(offset, 2);
        return static_cast<int>(std::strtol(part.c_str(), nullptr, 16));
    };
    color = RGB(component(1), component(3), component(5));
    return true;
}

Presentation Presentation::fromPlan(const Plan& plan)
{
    Presentation presentation;
    if (!plan.presentationModule.empty())
    {
        presentation.module = plan.presentationModule;
    }
    if (!plan.presentationBackground.empty())
    {
        presentation.background = plan.presentationBackground;
    }
    presentation.title = plan.presentationTitle;
    presentation.imagePath = plan.presentationImage;
    presentation.switchSeconds = plan.presentationSwitchSeconds > 0 ? plan.presentationSwitchSeconds : 10;
    presentation.resolutionX = plan.presentationResolutionX;
    presentation.resolutionY = plan.presentationResolutionY;
    presentation.hasFontColor = parseHexColor(plan.presentationFontColor, presentation.fontColor);
    return presentation;
}

bool graphicalPageAvailable()
{
    // 服务会话或没有输入桌面时不要试图开窗口
    HWINSTA station = GetProcessWindowStation();
    HDESK desktop = GetThreadDesktop(GetCurrentThreadId());
    if (station == nullptr || desktop == nullptr)
    {
        return false;
    }
    USEROBJECTFLAGS flags = {};
    DWORD needed = 0;
    if (GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof(flags), &needed) == 0)
    {
        return false;
    }
    if ((flags.dwFlags & WSF_VISIBLE) == 0)
    {
        return false;
    }
    return GetSystemMetrics(SM_CXSCREEN) > 0 && GetSystemMetrics(SM_CYSCREEN) > 0;
}

// ---------------------------------------------------------------- 窗口

GraphProgressPage::GraphProgressPage(const Presentation& presentation, int totalTasks)
    : presentation_(presentation), totalTasks_(totalTasks)
{
    startedAtMs_ = nowMs();
    lastSwitchTick_ = startedAtMs_;

    if (!createWindow())
    {
        return;
    }
    applyResolution();
    loadImages();
}

GraphProgressPage::~GraphProgressPage()
{
    if (hotkeyRegistered_)
    {
        UnregisterHotKey(hwnd_, kHotkeyId);
    }
    restoreResolution();

    const GdiplusApi& api = gdiplus();
    if (gdiplusImage_ != nullptr && api.available && api.disposeImage != nullptr)
    {
        api.disposeImage(gdiplusImage_);
        gdiplusImage_ = nullptr;
    }

    if (memoryDc_ != nullptr)
    {
        if (oldBitmap_ != nullptr)
        {
            SelectObject(memoryDc_, oldBitmap_);
        }
        DeleteDC(memoryDc_);
        memoryDc_ = nullptr;
    }
    if (backBuffer_ != nullptr)
    {
        DeleteObject(backBuffer_);
        backBuffer_ = nullptr;
    }
    if (hwnd_ != nullptr)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool GraphProgressPage::createWindow()
{
    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &GraphProgressPage::windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    windowClass.hbrBackground = nullptr;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST,
        kWindowClass,
        L"OpenSysprepTool 部署",
        WS_POPUP,
        0,
        0,
        screenWidth,
        screenHeight,
        nullptr,
        nullptr,
        instance,
        this
    );
    if (hwnd_ == nullptr)
    {
        return false;
    }

    width_ = screenWidth;
    height_ = screenHeight;

    HDC screenDc = GetDC(hwnd_);
    memoryDc_ = CreateCompatibleDC(screenDc);
    backBuffer_ = CreateCompatibleBitmap(screenDc, width_, height_);
    ReleaseDC(hwnd_, screenDc);
    if (memoryDc_ == nullptr || backBuffer_ == nullptr)
    {
        return false;
    }
    oldBitmap_ = SelectObject(memoryDc_, backBuffer_);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    // Ctrl+F11 黑屏急救：分辨率改坏了能一键恢复
    hotkeyRegistered_ = RegisterHotKey(hwnd_, kHotkeyId, MOD_CONTROL, VK_F11) != 0;
    return true;
}

bool GraphProgressPage::applyResolution()
{
    if (presentation_.resolutionX <= 0 || presentation_.resolutionY <= 0)
    {
        return false;
    }

    DEVMODEW current = {};
    current.dmSize = sizeof(current);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &current) != 0)
    {
        savedMode_ = current;
        hasSavedMode_ = true;
        if (static_cast<int>(current.dmPelsWidth) == presentation_.resolutionX &&
            static_cast<int>(current.dmPelsHeight) == presentation_.resolutionY)
        {
            return false; // 已经是目标分辨率，不用改
        }
    }

    DEVMODEW target = current;
    target.dmSize = sizeof(target);
    target.dmPelsWidth = static_cast<DWORD>(presentation_.resolutionX);
    target.dmPelsHeight = static_cast<DWORD>(presentation_.resolutionY);
    target.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

    if (ChangeDisplaySettingsW(&target, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
    {
        return false;
    }
    resolutionChanged_ = true;

    width_ = presentation_.resolutionX;
    height_ = presentation_.resolutionY;
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, width_, height_, SWP_SHOWWINDOW);
    return true;
}

void GraphProgressPage::restoreResolution()
{
    if (!resolutionChanged_ || !hasSavedMode_)
    {
        return;
    }
    DEVMODEW restore = savedMode_;
    restore.dmSize = sizeof(restore);
    ChangeDisplaySettingsW(&restore, 0);
    resolutionChanged_ = false;
}

void GraphProgressPage::loadImages()
{
    if (presentation_.background == "systemDefault" || presentation_.imagePath.empty())
    {
        return;
    }

    if (presentation_.background == "multi")
    {
        imagePaths_ = listImages(presentation_.imagePath);
    }
    else
    {
        imagePaths_.push_back(presentation_.imagePath);
    }
    if (imagePaths_.empty())
    {
        return;
    }

    const GdiplusApi& api = gdiplus();
    if (!api.available)
    {
        return; // 退回纯色背景；BMP 也能在 drawBackground 里用 LoadImage 兜底
    }
    const std::wstring wide = toWide(imagePaths_.front());
    void* image = nullptr;
    if (api.createBitmapFromFile(wide.c_str(), &image) == 0)
    {
        gdiplusImage_ = image;
    }
}

void GraphProgressPage::advanceImageIfNeeded()
{
    if (imagePaths_.size() <= 1 || presentation_.switchSeconds <= 0)
    {
        return;
    }
    const std::uint64_t interval = static_cast<std::uint64_t>(presentation_.switchSeconds) * 1000ull;
    if (nowMs() - lastSwitchTick_ < interval)
    {
        return;
    }
    lastSwitchTick_ = nowMs();
    currentImage_ = (currentImage_ + 1) % imagePaths_.size();

    const GdiplusApi& api = gdiplus();
    if (gdiplusImage_ != nullptr && api.available && api.disposeImage != nullptr)
    {
        api.disposeImage(gdiplusImage_);
        gdiplusImage_ = nullptr;
    }
    if (api.available)
    {
        const std::wstring wide = toWide(imagePaths_[currentImage_]);
        void* image = nullptr;
        if (api.createBitmapFromFile(wide.c_str(), &image) == 0)
        {
            gdiplusImage_ = image;
        }
    }
}

LRESULT CALLBACK GraphProgressPage::windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }

    auto* self = reinterpret_cast<GraphProgressPage*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr)
    {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return self->handleMessage(message, wparam, lparam);
}

LRESULT GraphProgressPage::handleMessage(UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_PAINT: {
        PAINTSTRUCT paintStruct = {};
        HDC dc = BeginPaint(hwnd_, &paintStruct);
        if (memoryDc_ != nullptr)
        {
            paint(memoryDc_);
            BitBlt(dc, 0, 0, width_, height_, memoryDc_, 0, 0, SRCCOPY);
        }
        EndPaint(hwnd_, &paintStruct);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // 全部在 WM_PAINT 里双缓冲绘制，不擦背景避免闪烁
    case WM_HOTKEY:
        if (wparam == kHotkeyId)
        {
            // 黑屏急救：回到驱动默认显示模式
            ChangeDisplaySettingsW(nullptr, 0);
            resolutionChanged_ = false;
            statusLine_ =
                "已恢复默认显示模式（Ctrl+F11 急救）" + (describeMode().empty() ? "" : "：" + describeMode());
            InvalidateRect(hwnd_, nullptr, FALSE);
            UpdateWindow(hwnd_);
            return 0;
        }
        break;
    case WM_CLOSE:
        // 部署过程中不允许关窗，只有执行结束才会销毁
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void GraphProgressPage::pump()
{
    if (hwnd_ == nullptr)
    {
        return;
    }
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    advanceImageIfNeeded();

    const std::uint64_t elapsed = nowMs() - startedAtMs_;
    if (elapsed % 1000 < 60)
    {
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
    }
}

bool GraphProgressPage::saveToBmp(const std::string& utf8Path, std::string& error)
{
    error.clear();
    if (memoryDc_ == nullptr || width_ <= 0 || height_ <= 0)
    {
        error = "还没有可保存的画面";
        return false;
    }

    paint(memoryDc_); // 先确保后备缓冲是最新的

    const int rowBytes = ((width_ * 3 + 3) / 4) * 4;
    const int imageBytes = rowBytes * height_;
    const int headerBytes = 14 + 40;
    const int fileBytes = headerBytes + imageBytes;

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(imageBytes), 0);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width_;
    info.bmiHeader.biHeight = height_; // 正值 = 自下而上，BMP 就是这个顺序
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    info.bmiHeader.biCompression = BI_RGB;
    if (GetDIBits(
            memoryDc_, backBuffer_, 0, static_cast<UINT>(height_), pixels.data(), &info, DIB_RGB_COLORS
        ) == 0)
    {
        error = "GetDIBits 失败，GetLastError=" + std::to_string(GetLastError());
        return false;
    }

    std::vector<std::uint8_t> header(static_cast<std::size_t>(headerBytes), 0);
    const auto put16 = [&header](std::size_t offset, std::uint16_t value) {
        header[offset] = static_cast<std::uint8_t>(value & 0xFF);
        header[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    };
    const auto put32 = [&header](std::size_t offset, std::uint32_t value) {
        for (int i = 0; i < 4; ++i)
        {
            header[offset + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
        }
    };

    header[0] = 'B';
    header[1] = 'M';
    put32(2, static_cast<std::uint32_t>(fileBytes));
    put32(10, static_cast<std::uint32_t>(headerBytes));
    put32(14, 40);
    put32(18, static_cast<std::uint32_t>(width_));
    put32(22, static_cast<std::uint32_t>(height_));
    put16(26, 1);
    put16(28, 24);
    put32(34, static_cast<std::uint32_t>(imageBytes));

    std::ofstream out(osp::win7::pathFromUtf8(utf8Path), std::ios::binary | std::ios::trunc);
    if (!out)
    {
        error = "打不开写入目标：" + utf8Path;
        return false;
    }
    out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    out.close();
    if (!out)
    {
        error = "写入失败：" + utf8Path;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- 绘制

void GraphProgressPage::drawBackground(HDC dc)
{
    RECT full = {0, 0, width_, height_};
    fillRect(dc, full, RGB(24, 32, 44));

    if (gdiplusImage_ != nullptr)
    {
        const GdiplusApi& api = gdiplus();
        void* graphics = nullptr;
        if (api.available && api.createFromHdc(dc, &graphics) == 0 && graphics != nullptr)
        {
            api.drawImageRect(graphics, gdiplusImage_, 0, 0, width_, height_);
            api.deleteGraphics(graphics);
        }
        // GDI 没有真正的 alpha 合成：在文字区域压一条实色暗带，保证可读性
        RECT band = {0, height_ - 240, width_, height_};
        fillRect(dc, band, RGB(18, 24, 32));
        return;
    }

    if (!imagePaths_.empty())
    {
        // gdiplus 不可用时至少支持 BMP
        const std::wstring wide = toWide(imagePaths_[currentImage_]);
        HBITMAP bitmap =
            static_cast<HBITMAP>(LoadImageW(nullptr, wide.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE));
        if (bitmap != nullptr)
        {
            HDC source = CreateCompatibleDC(dc);
            HGDIOBJ previous = SelectObject(source, bitmap);
            SetStretchBltMode(dc, HALFTONE);
            StretchBlt(dc, 0, 0, width_, height_, source, 0, 0, width_, height_, SRCCOPY);
            SelectObject(source, previous);
            DeleteDC(source);
            DeleteObject(bitmap);
        }
    }
}

void GraphProgressPage::drawTitle(HDC dc)
{
    const COLORREF color = presentation_.hasFontColor ? presentation_.fontColor : RGB(240, 240, 240);
    HFONT titleFont = CreateFontW(
        -MulDiv(32, GetDeviceCaps(dc, LOGPIXELSY), 72),
        0,
        0,
        0,
        FW_SEMIBOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        L"Microsoft YaHei"
    );

    // 没配标题时给一个默认的：空着会让上半屏看起来像没画出来
    const std::string title = presentation_.title.empty() ? "正在部署系统，请稍候…" : presentation_.title;
    const std::wstring wide = toWide(title);

    // 标题水平居中，垂直放在上方 1/6 处
    RECT area = {80, height_ / 6, width_ - 80, height_ / 6 + 60};
    HGDIOBJ previousFont = SelectObject(dc, titleFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, wide.c_str(), static_cast<int>(wide.size()), &area, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, previousFont);
    DeleteObject(titleFont);
}

void GraphProgressPage::paintGrid(HDC dc, const RECT& area, int percent)
{
    // 四格进度：SC 的「四格进度」模块。
    // 四格代表 0–25–50–75–100% 的完成度，不是「一个阶段一格」——
    // 旁边补一个百分比，免得 5 个阶段配 4 格让人误读。
    const int gap = 12;
    const int cellWidth = (area.right - area.left - gap * 3) / 4;
    const int filled = (percent * 4 + 99) / 100;

    for (int i = 0; i < 4; ++i)
    {
        RECT cell = {
            area.left + i * (cellWidth + gap),
            area.top,
            area.left + i * (cellWidth + gap) + cellWidth,
            area.bottom,
        };
        fillRect(dc, cell, i < filled ? RGB(58, 150, 240) : RGB(52, 62, 78));
    }

    HFONT font = CreateFontW(
        -MulDiv(18, GetDeviceCaps(dc, LOGPIXELSY), 72),
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        L"Microsoft YaHei"
    );
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%d%%", percent);
    RECT textArea = {area.right + 16, area.top, area.right + 96, area.bottom};
    drawTextCentered(dc, textArea, toWide(buffer), RGB(230, 236, 245), font);
    DeleteObject(font);
}

void GraphProgressPage::paintTrueTrans(HDC dc, const RECT& area, int percent)
{
    // Aero 风格：浅色底槽 + 渐变高光 + 右侧百分比
    RECT track = area;
    fillRect(dc, track, RGB(46, 56, 72));

    RECT fillArea = track;
    fillArea.right = track.left + (track.right - track.left) * percent / 100;
    if (fillArea.right > fillArea.left)
    {
        fillRect(dc, fillArea, RGB(86, 178, 255));
        RECT highlight = fillArea;
        highlight.bottom = fillArea.top + (fillArea.bottom - fillArea.top) / 2;
        fillRect(dc, highlight, RGB(150, 210, 255));
    }

    HFONT font = CreateFontW(
        -MulDiv(18, GetDeviceCaps(dc, LOGPIXELSY), 72),
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        L"Microsoft YaHei"
    );
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%d%%", percent);
    RECT textArea = area;
    textArea.left = area.right + 16;
    textArea.right = textArea.left + 80;
    drawTextCentered(dc, textArea, toWide(buffer), RGB(230, 236, 245), font);
    DeleteObject(font);
}

void GraphProgressPage::paintColorBar(HDC dc, const RECT& area, int percent)
{
    // 多彩进度条：按进度分段换色
    RECT track = area;
    fillRect(dc, track, RGB(40, 48, 62));

    RECT fillArea = track;
    fillArea.right = track.left + (track.right - track.left) * percent / 100;
    if (fillArea.right > fillArea.left)
    {
        const COLORREF from = percent < 50 ? RGB(255, 176, 32) : RGB(64, 196, 120);
        const COLORREF to = percent < 50 ? RGB(255, 96, 64) : RGB(120, 232, 168);
        const int width = fillArea.right - fillArea.left;
        for (int x = 0; x < width; ++x)
        {
            RECT line = {fillArea.left + x, fillArea.top, fillArea.left + x + 1, fillArea.bottom};
            fillRect(dc, line, blend(from, to, width == 0 ? 0 : x * 100 / width));
        }
    }
}

void GraphProgressPage::paintImageModule(HDC dc, const RECT& area, int percent)
{
    // 图片控件：背景图已经铺满，这里只画一条细进度线与百分比
    RECT line = area;
    line.bottom = area.top + 6;
    fillRect(dc, line, RGB(30, 38, 50));

    RECT fillArea = line;
    fillArea.right = line.left + (line.right - line.left) * percent / 100;
    if (fillArea.right > fillArea.left)
    {
        fillRect(dc, fillArea, RGB(120, 220, 160));
    }
}

void GraphProgressPage::paint(HDC dc)
{
    drawBackground(dc);
    drawTitle(dc);

    const int percent = totalTasks_ <= 0 ? 0 : std::min(100, doneTasks_ * 100 / totalTasks_);

    // 进度条放在屏幕中偏下（55% 处），文字块紧跟其后 —— 与 SC 的部署页观感一致，
    // 而不是把所有内容都堆在屏幕最底部。
    const int barTop = height_ * 55 / 100;
    RECT progressArea = {80, barTop, width_ - 160, barTop + 30};
    switch (presentation_.module == "trueTrans"      ? 1
                : presentation_.module == "colorBar" ? 2
                : presentation_.module == "image"    ? 3
                                                     : 0)
    {
    case 1:
        paintTrueTrans(dc, progressArea, percent);
        break;
    case 2:
        paintColorBar(dc, progressArea, percent);
        break;
    case 3:
        paintImageModule(dc, progressArea, percent);
        break;
    default:
        paintGrid(dc, progressArea, percent);
        break;
    }

    HFONT bodyFont = CreateFontW(
        -MulDiv(16, GetDeviceCaps(dc, LOGPIXELSY), 72),
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        L"Microsoft YaHei"
    );
    HFONT monoFont = CreateFontW(
        -MulDiv(14, GetDeviceCaps(dc, LOGPIXELSY), 72),
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        FIXED_PITCH,
        L"Consolas"
    );

    const COLORREF color = presentation_.hasFontColor ? presentation_.fontColor : RGB(226, 232, 240);

    int y = barTop + 56;
    if (!phaseLabel_.empty())
    {
        drawTextLine(
            dc,
            80,
            y,
            toWide(
                "阶段 " + std::to_string(phaseOrdinal_) + "/" + std::to_string(phaseTotal_) + "  " +
                phaseLabel_
            ),
            color,
            bodyFont
        );
        y += 30;
    }
    if (!taskText_.empty())
    {
        drawTextLine(dc, 80, y, toWide(taskText_), blend(color, RGB(24, 32, 44), 30), bodyFont);
        y += 28;
    }
    if (!statusLine_.empty())
    {
        drawTextLine(dc, 80, y, toWide(statusLine_), RGB(120, 220, 160), bodyFont);
        y += 28;
    }

    for (std::size_t i = 0; i < recent_.size() && i < 3; ++i)
    {
        drawTextLine(dc, 80, y, toWide(recent_[recent_.size() - 1 - i]), RGB(150, 162, 178), monoFont);
        y += 22;
    }

    // 预览模式下计划里的 taskCount 可能与实际画的条数不同，显示时夹到 [0, total]
    const int shownDone = totalTasks_ > 0 ? std::min(doneTasks_, totalTasks_) : doneTasks_;
    char stats[160] = {};
    std::snprintf(
        stats,
        sizeof(stats),
        "完成 %d / %d    失败 %d    已用 %s",
        shownDone,
        totalTasks_,
        failedTasks_,
        formatDuration(nowMs() - startedAtMs_).c_str()
    );
    RECT statsArea = {80, height_ - 60, width_ - 80, height_ - 24};
    drawTextLine(dc, statsArea.left, statsArea.top, toWide(stats), RGB(180, 190, 205), monoFont);

    if (hotkeyRegistered_)
    {
        RECT hintArea = {width_ - 420, height_ - 60, width_ - 80, height_ - 24};
        drawTextCentered(dc, hintArea, L"显示异常按 Ctrl+F11 恢复", RGB(130, 142, 158), monoFont);
    }

    DeleteObject(bodyFont);
    DeleteObject(monoFont);
}

// ---------------------------------------------------------------- Sink

void GraphProgressPage::phaseStart(const Phase& phase, int ordinal, int total)
{
    phaseLabel_ = phase.label.empty() ? phase.id : phase.label;
    phaseOrdinal_ = ordinal;
    phaseTotal_ = total;
    taskText_.clear();
    statusLine_.clear();
    recent_.push_back("[阶段] " + phaseLabel_);
    pump();
    if (hwnd_ != nullptr)
    {
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
    }
}

void GraphProgressPage::taskStart(const Phase& phase, const Task& task, int ordinal, int total)
{
    (void)phase;
    taskText_ = "任务 " + std::to_string(ordinal) + "/" + std::to_string(total) + "  " + task.commandLine;
    pump();
    if (hwnd_ != nullptr)
    {
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
    }
}

void GraphProgressPage::taskFinish(const Phase& phase, const Task& task, const TaskOutcome& outcome)
{
    (void)phase;
    (void)task;
    ++doneTasks_;
    if (!outcome.ok)
    {
        ++failedTasks_;
        recent_.push_back("[失败] " + outcome.message);
    }
    if (recent_.size() > 12)
    {
        recent_.erase(recent_.begin());
    }
    pump();
    if (hwnd_ != nullptr)
    {
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
    }
}

void GraphProgressPage::phaseFinish(const Phase& phase, int okCount, int failedCount)
{
    recent_.push_back(
        "[完成] " + phase.label + " 成功 " + std::to_string(okCount) + " 失败 " + std::to_string(failedCount)
    );
    pump();
}

} // namespace osp::deploy

#include "popups.hpp"

#include <cstdlib>
#include <string>

#include <windows.h>

namespace osp::ui {
namespace {

// ---- DFM 坐标（TSBOX.dfm，逐字） ----
// 页签容器 Sbpage 在 (7,8)；下面两组控件坐标相对页签内容区，
// 所以绝对位置 = (7,8) + 相对坐标。
constexpr int kPageX = 7;
constexpr int kPageY = 8;
constexpr int kRadioSilentX = 5;
constexpr int kRadioSilentY = 36;
constexpr int kRadioSilentW = 126;
constexpr int kRadioSilentH = 17;
constexpr int kRadioShowX = 147;
constexpr int kRadioShowY = 36;
constexpr int kRadioShowW = 134;
constexpr int kRadioShowH = 17;
constexpr int kButtonX = 184;
constexpr int kButtonY = 81;
constexpr int kButtonW = 73;
constexpr int kButtonH = 27;

constexpr int kIdcRadioSilent = 4001;
constexpr int kIdcRadioShow = 4002;
constexpr int kIdcOk = 4003;

// 文案以语言文件为准：DFM 里页签 2 的两个单选按钮 Caption 写的是「静默重启」，
// 那是 SC 自己的复制粘贴错误，运行时被 LANGS_SCCHS_2052.bin 覆盖成「静默创建」。
const wchar_t* radioSilentText(ChoiceKind kind)
{
    return kind == ChoiceKind::Restart ? L"静默重启[无界面]" : L"静默创建[无界面]";
}

const wchar_t* radioShowText(ChoiceKind kind)
{
    return kind == ChoiceKind::Restart ? L"倒计时重启[有界面]" : L"倒计时创建[有界面]";
}

// 弹窗状态由调用方持有：窗口销毁后还要读结果，不能在窗口过程里 new/delete
struct DialogState
{
    ChoiceKind kind = ChoiceKind::Restart;
    bool silent = true;
    bool confirmed = false;
    bool closed = false;
    HWND silentRadio = nullptr;
    HWND showRadio = nullptr;
    HWND okButton = nullptr;
};

LRESULT CALLBACK choiceProc(HWND wnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<DialogState*>(::GetWindowLongPtrW(wnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<DialogState*>(create->lpCreateParams);
        ::SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (state == nullptr)
        {
            return -1;
        }

        state->silentRadio = ::CreateWindowExW(
            0,
            L"BUTTON",
            radioSilentText(state->kind),
            WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON | WS_TABSTOP,
            kPageX + kRadioSilentX,
            kPageY + kRadioSilentY,
            kRadioSilentW,
            kRadioSilentH,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcRadioSilent)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );
        state->showRadio = ::CreateWindowExW(
            0,
            L"BUTTON",
            radioShowText(state->kind),
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP,
            kPageX + kRadioShowX,
            kPageY + kRadioShowY,
            kRadioShowW,
            kRadioShowH,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcRadioShow)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );
        state->okButton = ::CreateWindowExW(
            0,
            L"BUTTON",
            L"确定(&K)",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            kPageX + kButtonX,
            kPageY + kButtonY,
            kButtonW,
            kButtonH,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcOk)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );

        ::SendMessageW(state->silentRadio, BM_SETCHECK, BST_CHECKED, 0);
        ::SetFocus(state->okButton);
        return 0;
    }
    case WM_COMMAND: {
        if (state == nullptr)
        {
            return 0;
        }
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == kIdcOk && code == BN_CLICKED)
        {
            state->silent = ::SendMessageW(state->silentRadio, BM_GETCHECK, 0, 0) == BST_CHECKED;
            state->confirmed = true;
            ::DestroyWindow(wnd);
            return 0;
        }
        if ((id == kIdcRadioSilent || id == kIdcRadioShow) && code == BN_CLICKED)
        {
            state->silent = id == kIdcRadioSilent;
        }
        return 0;
    }
    case WM_CLOSE:
        if (state != nullptr)
        {
            state->confirmed = false;
        }
        ::DestroyWindow(wnd);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            ::SendMessageW(wnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (state != nullptr)
        {
            state->closed = true;
            ::SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
        }
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(wnd, message, wParam, lParam);
}

bool ensureClass()
{
    static bool registered = false;
    if (registered)
    {
        return true;
    }
    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = choiceProc;
    cls.hInstance = ::GetModuleHandleW(nullptr);
    cls.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    cls.lpszClassName = L"OSPTChoiceBox";
    if (::RegisterClassExW(&cls) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }
    registered = true;
    return true;
}

HWND createDialog(void* ownerHwnd, DialogState* state, bool visible, bool withOwner)
{
    if (!ensureClass())
    {
        return nullptr;
    }
    RECT rect = {0, 0, kChoiceClientWidth, kChoiceClientHeight};
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | (visible ? WS_VISIBLE : 0);
    ::AdjustWindowRectEx(&rect, style, FALSE, WS_EX_DLGMODALFRAME);
    return ::CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"OSPTChoiceBox",
        L"OpenSysprepTool",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        withOwner ? static_cast<HWND>(ownerHwnd) : nullptr,
        nullptr,
        ::GetModuleHandleW(nullptr),
        state
    );
}

void centerOn(HWND dialog, HWND ownerHwnd)
{
    RECT target = {0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    if (ownerHwnd != nullptr && ::IsWindow(ownerHwnd) != FALSE)
    {
        ::GetWindowRect(ownerHwnd, &target);
    }
    RECT window = {};
    ::GetWindowRect(dialog, &window);
    const int width = window.right - window.left;
    const int height = window.bottom - window.top;
    const int x = target.left + ((target.right - target.left) - width) / 2;
    const int y = target.top + ((target.bottom - target.top) - height) / 2;
    ::SetWindowPos(dialog, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

} // namespace

ChoiceResult showChoiceDialog(void* ownerHwnd, ChoiceKind kind, bool silentDefault)
{
    ChoiceResult result;
    DialogState state;
    state.kind = kind;
    state.silent = silentDefault;

    HWND dialog = createDialog(ownerHwnd, &state, true, true);
    if (dialog == nullptr)
    {
        return result;
    }

    // 默认选中项显式摆位：AUTORADIOBUTTON 只在用户点击时互斥，
    // 程序设置勾选状态必须自己清掉另一个
    ::SendMessageW(state.silentRadio, BM_SETCHECK, silentDefault ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessageW(state.showRadio, BM_SETCHECK, silentDefault ? BST_UNCHECKED : BST_CHECKED, 0);

    centerOn(dialog, static_cast<HWND>(ownerHwnd));
    ::EnableWindow(static_cast<HWND>(ownerHwnd), FALSE);

    MSG message = {};
    while (!state.closed && ::GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (::IsDialogMessageW(dialog, &message) == FALSE)
        {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }

    ::EnableWindow(static_cast<HWND>(ownerHwnd), TRUE);
    if (::IsWindow(static_cast<HWND>(ownerHwnd)) != FALSE)
    {
        ::SetActiveWindow(static_cast<HWND>(ownerHwnd));
    }

    result.confirmed = state.confirmed;
    result.silent = state.silent;
    return result;
}

bool smokeChoiceDialog(void* ownerHwnd, ChoiceKind kind, std::string& detail)
{
    DialogState state;
    state.kind = kind;

    HWND dialog = createDialog(ownerHwnd, &state, false, false); // 不上屏
    if (dialog == nullptr)
    {
        detail = "创建 TSBOX 弹窗失败，GetLastError=" + std::to_string(::GetLastError());
        return false;
    }

    RECT client = {};
    ::GetClientRect(dialog, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width != kChoiceClientWidth || height != kChoiceClientHeight)
    {
        detail = "客户区 " + std::to_string(width) + "x" + std::to_string(height) + "，期望 300x136";
        ::DestroyWindow(dialog);
        return false;
    }
    if (state.silentRadio == nullptr || state.showRadio == nullptr || state.okButton == nullptr)
    {
        detail = "弹窗控件缺失（单选或确定按钮没建起来）";
        ::DestroyWindow(dialog);
        return false;
    }

    RECT ok = {};
    ::GetWindowRect(state.okButton, &ok);
    ::MapWindowPoints(nullptr, dialog, reinterpret_cast<LPPOINT>(&ok), 2);
    const int expectedX = kPageX + kButtonX;
    const int expectedY = kPageY + kButtonY;
    if (std::abs(ok.left - expectedX) > 1 || std::abs(ok.top - expectedY) > 1)
    {
        detail = "确定按钮在 " + std::to_string(ok.left) + "," + std::to_string(ok.top) + "，期望 " +
            std::to_string(expectedX) + "," + std::to_string(expectedY);
        ::DestroyWindow(dialog);
        return false;
    }

    // 选「倒计时」再点确定：结果必须带回 confirmed=true / silent=false
    ::SendMessageW(state.silentRadio, BM_SETCHECK, BST_UNCHECKED, 0);
    ::SendMessageW(state.showRadio, BM_SETCHECK, BST_CHECKED, 0);
    ::SendMessageW(state.okButton, BM_CLICK, 0, 0);
    if (!state.confirmed || state.silent)
    {
        detail = std::string("点确定后结果不对：confirmed=") + (state.confirmed ? "1" : "0") +
            " silent=" + (state.silent ? "1" : "0");
        return false;
    }

    detail = "客户区 " + std::to_string(width) + "x" + std::to_string(height) + "，确定按钮 @" +
        std::to_string(ok.left) + "," + std::to_string(ok.top) + "，选择=倒计时";
    return true;
}

} // namespace osp::ui

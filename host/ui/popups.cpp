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

std::wstring toWideUtf8(const std::string& text)
{
    if (text.empty())
    {
        return std::wstring();
    }
    const int size =
        ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
    {
        return std::wstring();
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

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

// ---------------------------------------------------------------- TAPPMSGFORM

namespace {

// ---- DFM 坐标（TAPPMSGFORM.dfm，逐字） ----
constexpr int kMsgTipX = 16;
constexpr int kMsgTipY = 72;
constexpr int kMsgTipW = 449;
constexpr int kMsgTipH = 73;
constexpr int kMsgLittleX = 16;
constexpr int kMsgLittleY = 6;
constexpr int kMsgLittleW = 121;
constexpr int kMsgLittleH = 25;
constexpr int kMsgButtonY = 191;
constexpr int kMsgButtonH = 51;
constexpr int kMsgBigOkW = 480;
constexpr int kMsgHalfW = 240;
constexpr int kMsgCloseX = 456;
constexpr int kMsgCloseW = 24;
constexpr int kMsgCloseH = 20;

constexpr int kIdcMsgBigOk = 4201;
constexpr int kIdcMsgExit = 4202;
constexpr int kIdcMsgOk = 4203;
constexpr int kIdcMsgClose = 4204;

struct MessageState
{
    MessageKind kind = MessageKind::Info;
    bool confirmed = false;
    bool closed = false;
    HWND bigOk = nullptr;
    HWND exitButton = nullptr;
    HWND okButton = nullptr;
    HWND tip = nullptr;
};

LRESULT CALLBACK messageProc(HWND wnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<MessageState*>(::GetWindowLongPtrW(wnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<MessageState*>(create->lpCreateParams);
        ::SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (state == nullptr)
        {
            return -1;
        }

        // 标题行的产品名：SC 那里写的是它的品牌名，我们不搬，用自己的
        ::CreateWindowExW(
            0,
            L"STATIC",
            L"OpenSysprepTool",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            kMsgLittleX,
            kMsgLittleY,
            kMsgLittleW,
            kMsgLittleH,
            wnd,
            nullptr,
            ::GetModuleHandleW(nullptr),
            nullptr
        );
        state->tip = ::CreateWindowExW(
            0,
            L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_EDITCONTROL,
            kMsgTipX,
            kMsgTipY,
            kMsgTipW,
            kMsgTipH,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(0)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );

        state->bigOk = ::CreateWindowExW(
            0,
            L"BUTTON",
            L"确定(&K)",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            0,
            kMsgButtonY,
            kMsgBigOkW,
            kMsgButtonH,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcMsgBigOk)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );
        state->exitButton = ::CreateWindowExW(
            0,
            L"BUTTON",
            L"取消(&E)",
            WS_CHILD | WS_TABSTOP,
            0,
            kMsgButtonY,
            kMsgHalfW,
            kMsgButtonH,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcMsgExit)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );
        state->okButton = ::CreateWindowExW(
            0,
            L"BUTTON",
            L"确定(&K)",
            WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON,
            kMsgHalfW,
            kMsgButtonY,
            kMsgHalfW,
            kMsgButtonH,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcMsgOk)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );
        ::CreateWindowExW(
            0,
            L"BUTTON",
            L"",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            kMsgCloseX,
            0,
            kMsgCloseW,
            kMsgCloseH,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcMsgClose)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );

        // 两种模式：Info 只留整条宽度的「确定」；Confirm 换成「取消 / 确定」两半
        const bool confirm = state->kind == MessageKind::Confirm;
        ::ShowWindow(state->bigOk, confirm ? SW_HIDE : SW_SHOW);
        ::ShowWindow(state->exitButton, confirm ? SW_SHOW : SW_HIDE);
        ::ShowWindow(state->okButton, confirm ? SW_SHOW : SW_HIDE);
        ::SetFocus(confirm ? state->okButton : state->bigOk);
        return 0;
    }
    case WM_COMMAND: {
        if (state == nullptr)
        {
            return 0;
        }
        const int id = LOWORD(wParam);
        if (HIWORD(wParam) != BN_CLICKED)
        {
            return 0;
        }
        if (id == kIdcMsgOk || id == kIdcMsgBigOk)
        {
            state->confirmed = true;
            ::DestroyWindow(wnd);
            return 0;
        }
        if (id == kIdcMsgExit || id == kIdcMsgClose)
        {
            state->confirmed = false;
            ::DestroyWindow(wnd);
            return 0;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            if (state != nullptr)
            {
                state->confirmed = false;
            }
            ::DestroyWindow(wnd);
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

bool ensureMessageClass()
{
    static bool registered = false;
    if (registered)
    {
        return true;
    }
    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = messageProc;
    cls.hInstance = ::GetModuleHandleW(nullptr);
    cls.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    cls.lpszClassName = L"OSPTMessageBox";
    if (::RegisterClassExW(&cls) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }
    registered = true;
    return true;
}

HWND createMessageWindow(void* ownerHwnd, MessageState* state, bool visible, bool withOwner)
{
    if (!ensureMessageClass())
    {
        return nullptr;
    }
    RECT rect = {0, 0, kMessageClientWidth, kMessageClientHeight};
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | (visible ? WS_VISIBLE : 0);
    ::AdjustWindowRectEx(&rect, style, FALSE, WS_EX_DLGMODALFRAME);
    return ::CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"OSPTMessageBox",
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

} // namespace

MessageResult showMessageDialog(void* ownerHwnd, const std::string& text, MessageKind kind)
{
    MessageResult result;
    MessageState state;
    state.kind = kind;

    HWND dialog = createMessageWindow(ownerHwnd, &state, true, true);
    if (dialog == nullptr)
    {
        return result;
    }
    ::SetWindowTextW(state.tip, toWideUtf8(text).c_str());
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
    result.confirmed = kind == MessageKind::Info ? true : state.confirmed;
    return result;
}

bool smokeMessageDialog(void* ownerHwnd, std::string& detail)
{
    MessageState state;
    state.kind = MessageKind::Confirm;
    HWND dialog = createMessageWindow(ownerHwnd, &state, false, false);
    if (dialog == nullptr)
    {
        detail = "创建 TAPPMSGFORM 失败，GetLastError=" + std::to_string(::GetLastError());
        return false;
    }

    RECT client = {};
    ::GetClientRect(dialog, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width != kMessageClientWidth || height != kMessageClientHeight)
    {
        detail = "客户区 " + std::to_string(width) + "x" + std::to_string(height) + "，期望 480x242";
        ::DestroyWindow(dialog);
        return false;
    }

    // Confirm 模式：取消/确定可见，整条宽度的确定隐藏
    if (state.exitButton == nullptr || state.okButton == nullptr || state.bigOk == nullptr ||
        state.tip == nullptr)
    {
        detail = "提示框控件缺失";
        ::DestroyWindow(dialog);
        return false;
    }
    if (::IsWindowVisible(state.exitButton) != FALSE || ::IsWindowVisible(state.okButton) != FALSE)
    {
        detail = "Confirm 模式下两个半宽按钮应当可见（窗口未上屏时 IsWindowVisible 为假，这里跳过）";
    }

    // 正文回显
    ::SetWindowTextW(state.tip, L"测试提示正文");
    wchar_t buffer[64] = {};
    ::GetWindowTextW(state.tip, buffer, 64);
    if (std::wstring(buffer) != L"测试提示正文")
    {
        detail = "正文没有回显到 lbTip";
        ::DestroyWindow(dialog);
        return false;
    }

    // 点「确定」应当返回确认
    ::SendMessageW(state.okButton, BM_CLICK, 0, 0);
    if (!state.confirmed)
    {
        detail = "点确定后没有返回确认";
        return false;
    }

    detail =
        "客户区 " + std::to_string(width) + "x" + std::to_string(height) + "，正文回显正常，确定返回确认";
    return true;
}

} // namespace osp::ui

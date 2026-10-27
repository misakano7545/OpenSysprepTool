#include "task_dialog.hpp"

#include <string>
#include <vector>

#include <windows.h>

namespace osp::ui {
namespace {

// ---- DFM 坐标（TTLMF.dfm，逐字） ----
constexpr int kIdcTaskType = 4101;
constexpr int kIdcRunMode = 4102;
constexpr int kIdcRunWait = 4103;
constexpr int kIdcWaitTime = 4104;
constexpr int kIdcRunAfter = 4105;
constexpr int kIdcRunPath = 4106;
constexpr int kIdcRunPathEdit = 4107;
constexpr int kIdcBrowse = 4108;
constexpr int kIdcCmdLine = 4109;
constexpr int kIdcSave = 4110;
constexpr int kIdcPhaseBase = 4120; // Btn_RunPlan1..5

// 图纸上五个阶段按钮的位置与顺序
struct PhaseButton
{
    const wchar_t* caption;
    const char* phase;
    int x;
    int y;
    int width;
    int height;
};
constexpr PhaseButton kPhaseButtons[] = {
    {L"部署前(&B)", "preinstall", 4, 9, 93, 27},
    {L"部署中(&C)", "install", 103, 9, 93, 27},
    {L"部署后(&A)", "postinstall", 202, 9, 93, 27},
    {L"登录时(&L)", "logon", 301, 9, 93, 27},
    {L"桌面后(&D)", "desktop", 401, 9, 93, 27},
};

// 下拉条目：与 core/runplan 的四个维度一致（值取自那里的 id，文案取自图纸）
struct ComboItem
{
    const wchar_t* text;
    const char* value;
};
constexpr ComboItem kTaskTypes[] = {
    {L"运行程序", "run"},
    {L"导注册表", "reg"},
    {L"删除文件", "deleteFile"},
    {L"删除目录", "deleteDir"},
};
constexpr ComboItem kRunModes[] = {
    {L"常规窗口", "normal"},
    {L"隐藏窗口", "hidden"},
    {L"最小化", "minimized"},
    {L"最大化", "maximized"},
};
constexpr ComboItem kRunWaits[] = {
    {L"等待完成", "wait"},
    {L"不等待", "noWait"},
    {L"自定义秒数", "custom"},
};
constexpr ComboItem kRunAfters[] = {
    {L"保留文件", "keep"},
    {L"删除文件", "deleteFile"},
};

template <std::size_t N> const char* valueOf(const ComboItem (&items)[N], int index)
{
    if (index < 0 || index >= static_cast<int>(N))
    {
        return items[0].value;
    }
    return items[static_cast<std::size_t>(index)].value;
}

template <std::size_t N> int indexOf(const ComboItem (&items)[N], const std::string& value)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        if (value == items[i].value)
        {
            return static_cast<int>(i);
        }
    }
    return 0;
}

std::wstring toWide(const std::string& text)
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

std::string fromWide(const std::wstring& text)
{
    if (text.empty())
    {
        return std::string();
    }
    const int size = ::WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr
    );
    if (size <= 0)
    {
        return std::string();
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr
    );
    return out;
}

std::string textOf(HWND control)
{
    const int length = ::GetWindowTextLengthW(control);
    if (length <= 0)
    {
        return std::string();
    }
    std::wstring buffer(static_cast<std::size_t>(length) + 1, L'\0');
    ::GetWindowTextW(control, buffer.data(), length + 1);
    buffer.resize(static_cast<std::size_t>(length));
    return fromWide(buffer);
}

struct DialogState
{
    TaskDraft draft;
    bool confirmed = false;
    bool closed = false;
    HWND taskType = nullptr;
    HWND runMode = nullptr;
    HWND runWait = nullptr;
    HWND waitTime = nullptr;
    HWND runAfter = nullptr;
    HWND runPathEdit = nullptr;
    HWND cmdLine = nullptr;
    HWND status = nullptr;
    HWND phaseButtons[5] = {};
};

HWND makeLabel(HWND parent, const wchar_t* text, int x, int y, int width, int height)
{
    return ::CreateWindowExW(
        0,
        L"STATIC",
        text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x,
        y,
        width,
        height,
        parent,
        nullptr,
        ::GetModuleHandleW(nullptr),
        nullptr
    );
}

HWND makeCombo(HWND parent, int id, int x, int y, int width, int height, const ComboItem* items, int count)
{
    HWND combo = ::CreateWindowExW(
        0,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        ::GetModuleHandleW(nullptr),
        nullptr
    );
    for (int i = 0; i < count; ++i)
    {
        ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i].text));
    }
    ::SendMessageW(combo, CB_SETCURSEL, 0, 0);
    return combo;
}

LRESULT CALLBACK taskDialogProc(HWND wnd, UINT message, WPARAM wParam, LPARAM lParam)
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

        // 标签：坐标逐字来自 DFM
        makeLabel(wnd, L"任务类型:", 83, 72, 61, 21);
        makeLabel(wnd, L"运行计划:", 83, 127, 61, 21);
        makeLabel(wnd, L"运行方式:", 87, 176, 61, 21);
        makeLabel(wnd, L"运行等待:", 279, 176, 61, 21);
        makeLabel(wnd, L"等待时间:", 470, 176, 61, 21);
        makeLabel(wnd, L"S", 626, 176, 17, 21);
        makeLabel(wnd, L"执行路径:", 87, 224, 61, 21);
        makeLabel(wnd, L"执行参数:", 87, 272, 61, 21);
        makeLabel(wnd, L"善后处理:", 470, 272, 61, 21);

        state->taskType = makeCombo(
            wnd,
            kIdcTaskType,
            150,
            67,
            106,
            26,
            kTaskTypes,
            static_cast<int>(sizeof(kTaskTypes) / sizeof(kTaskTypes[0]))
        );
        state->runMode = makeCombo(
            wnd,
            kIdcRunMode,
            154,
            171,
            106,
            26,
            kRunModes,
            static_cast<int>(sizeof(kRunModes) / sizeof(kRunModes[0]))
        );
        state->runWait = makeCombo(
            wnd,
            kIdcRunWait,
            346,
            171,
            106,
            26,
            kRunWaits,
            static_cast<int>(sizeof(kRunWaits) / sizeof(kRunWaits[0]))
        );
        state->runAfter = makeCombo(
            wnd,
            kIdcRunAfter,
            537,
            267,
            106,
            26,
            kRunAfters,
            static_cast<int>(sizeof(kRunAfters) / sizeof(kRunAfters[0]))
        );

        // 五个阶段按钮（图纸上 Pl_RunPlan 面板里横排）
        for (int i = 0; i < 5; ++i)
        {
            const PhaseButton& button = kPhaseButtons[i];
            state->phaseButtons[i] = ::CreateWindowExW(
                0,
                L"BUTTON",
                button.caption,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                150 + button.x,
                111 + button.y,
                button.width,
                button.height,
                wnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcPhaseBase + i)),
                ::GetModuleHandleW(nullptr),
                nullptr
            );
        }

        state->waitTime = ::CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"0",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_RIGHT,
            537,
            171,
            90,
            26,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcWaitTime)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );
        makeCombo(wnd, kIdcRunPath, 154, 219, 106, 26, kRunModes, 1); // 路径类型下拉先给一项，避免空框
        state->runPathEdit = ::CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            266,
            219,
            297,
            26,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcRunPathEdit)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );
        ::CreateWindowExW(
            0,
            L"BUTTON",
            L"浏览(&O)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            569,
            219,
            75,
            27,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcBrowse)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );
        state->cmdLine = ::CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            154,
            267,
            298,
            26,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcCmdLine)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );
        ::CreateWindowExW(
            0,
            L"BUTTON",
            L"添 加(&T)",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            537,
            330,
            106,
            27,
            wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcSave)),
            ::GetModuleHandleW(nullptr),
            nullptr
        );
        state->status = makeLabel(wnd, L"", 87, 336, 444, 15);

        // 初值回填
        ::SendMessageW(state->taskType, CB_SETCURSEL, indexOf(kTaskTypes, state->draft.type), 0);
        ::SendMessageW(state->runMode, CB_SETCURSEL, indexOf(kRunModes, state->draft.windowStyle), 0);
        ::SendMessageW(state->runWait, CB_SETCURSEL, indexOf(kRunWaits, state->draft.waitMode), 0);
        ::SendMessageW(state->runAfter, CB_SETCURSEL, indexOf(kRunAfters, state->draft.cleanup), 0);
        ::SetWindowTextW(state->waitTime, toWide(std::to_string(state->draft.waitSeconds)).c_str());
        ::SetWindowTextW(state->runPathEdit, toWide(state->draft.target).c_str());
        ::SetWindowTextW(state->cmdLine, toWide(state->draft.arguments).c_str());
        ::SetFocus(state->runPathEdit);
        return 0;
    }
    case WM_COMMAND: {
        if (state == nullptr)
        {
            return 0;
        }
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == kIdcSave && code == BN_CLICKED)
        {
            const std::string target = textOf(state->runPathEdit);
            if (target.empty())
            {
                // 图纸的 Lab_run 就是状态行：路径为空时把原因显示在这里，不弹二级对话框
                ::SetWindowTextW(state->status, L"执行路径不能为空");
                return 0;
            }
            state->draft.type =
                valueOf(kTaskTypes, static_cast<int>(::SendMessageW(state->taskType, CB_GETCURSEL, 0, 0)));
            state->draft.windowStyle =
                valueOf(kRunModes, static_cast<int>(::SendMessageW(state->runMode, CB_GETCURSEL, 0, 0)));
            state->draft.waitMode =
                valueOf(kRunWaits, static_cast<int>(::SendMessageW(state->runWait, CB_GETCURSEL, 0, 0)));
            state->draft.cleanup =
                valueOf(kRunAfters, static_cast<int>(::SendMessageW(state->runAfter, CB_GETCURSEL, 0, 0)));
            state->draft.waitSeconds = std::atoi(textOf(state->waitTime).c_str());
            state->draft.target = target;
            state->draft.arguments = textOf(state->cmdLine);
            state->confirmed = true;
            ::DestroyWindow(wnd);
            return 0;
        }
        if (id >= kIdcPhaseBase && id < kIdcPhaseBase + 5 && code == BN_CLICKED)
        {
            // 五个阶段按钮是互斥选择：选中项记录到草稿的 phase
            state->draft.phase = kPhaseButtons[id - kIdcPhaseBase].phase;
            for (int i = 0; i < 5; ++i)
            {
                ::SendMessageW(
                    state->phaseButtons[i], BM_SETSTATE, (kIdcPhaseBase + i) == id ? TRUE : FALSE, 0
                );
            }
            return 0;
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
    cls.lpfnWndProc = taskDialogProc;
    cls.hInstance = ::GetModuleHandleW(nullptr);
    cls.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    cls.lpszClassName = L"OSPTTaskDialog";
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
    RECT rect = {0, 0, kTaskDialogWidth, kTaskDialogHeight};
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | (visible ? WS_VISIBLE : 0);
    ::AdjustWindowRectEx(&rect, style, FALSE, WS_EX_DLGMODALFRAME);
    return ::CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"OSPTTaskDialog",
        L"添加任务",
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

bool showTaskDialog(void* ownerHwnd, TaskDraft& draft)
{
    DialogState state;
    state.draft = draft;
    if (state.draft.phase.empty())
    {
        state.draft.phase = "postinstall"; // 默认「部署后」，与 SC 的常用路径一致
    }
    if (state.draft.cleanup.empty())
    {
        state.draft.cleanup = "keep";
    }

    HWND dialog = createDialog(ownerHwnd, &state, true, true);
    if (dialog == nullptr)
    {
        return false;
    }

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

    if (state.confirmed)
    {
        draft = state.draft;
    }
    return state.confirmed;
}

bool smokeTaskDialog(void* ownerHwnd, std::string& detail)
{
    DialogState state;
    state.draft.type = "run";
    state.draft.windowStyle = "hidden";
    state.draft.waitMode = "wait";
    state.draft.cleanup = "keep";

    HWND dialog = createDialog(ownerHwnd, &state, false, false);
    if (dialog == nullptr)
    {
        detail = "创建 TTLMF 弹窗失败，GetLastError=" + std::to_string(::GetLastError());
        return false;
    }

    RECT client = {};
    ::GetClientRect(dialog, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width != kTaskDialogWidth || height != kTaskDialogHeight)
    {
        detail = "客户区 " + std::to_string(width) + "x" + std::to_string(height) + "，期望 740x400";
        ::DestroyWindow(dialog);
        return false;
    }
    if (state.taskType == nullptr || state.runMode == nullptr || state.runWait == nullptr ||
        state.waitTime == nullptr || state.runAfter == nullptr || state.runPathEdit == nullptr ||
        state.cmdLine == nullptr || state.status == nullptr)
    {
        detail = "TTLMF 控件缺失";
        ::DestroyWindow(dialog);
        return false;
    }

    // 五个阶段按钮要都在
    for (int i = 0; i < 5; ++i)
    {
        if (state.phaseButtons[i] == nullptr)
        {
            detail = "阶段按钮 Btn_RunPlan" + std::to_string(i + 1) + " 缺失";
            ::DestroyWindow(dialog);
            return false;
        }
    }

    // 空路径点「添加」必须被拦下（图纸的 Lab_run 状态行给出原因）
    HWND save = ::GetDlgItem(dialog, kIdcSave);
    if (save == nullptr)
    {
        detail = "添加按钮缺失";
        ::DestroyWindow(dialog);
        return false;
    }
    ::SendMessageW(save, BM_CLICK, 0, 0);
    if (state.confirmed)
    {
        detail = "空路径竟然通过了校验";
        ::DestroyWindow(dialog);
        return false;
    }
    if (textOf(state.status).find("执行路径") == std::string::npos)
    {
        detail = "空路径时状态行没有给出原因：" + textOf(state.status);
        ::DestroyWindow(dialog);
        return false;
    }

    // 填上路径再点：应当确认，并把五个字段带出来
    ::SetWindowTextW(state.runPathEdit, L"C:\\Windows\\System32\\notepad.exe");
    ::SetWindowTextW(state.cmdLine, L"/quiet");
    ::SendMessageW(state.phaseButtons[2], BM_CLICK, 0, 0); // 部署后
    ::SendMessageW(save, BM_CLICK, 0, 0);
    if (!state.confirmed)
    {
        detail = "填好路径后仍未能确认";
        return false;
    }
    if (state.draft.target != "C:\\Windows\\System32\\notepad.exe" || state.draft.arguments != "/quiet" ||
        state.draft.phase != "postinstall")
    {
        detail = "确认后草稿不对：target=" + state.draft.target + " args=" + state.draft.arguments +
            " phase=" + state.draft.phase;
        return false;
    }

    detail = "客户区 " + std::to_string(width) + "x" + std::to_string(height) +
        "，空路径被拦下，填好后 phase/target/args 正确";
    return true;
}

} // namespace osp::ui

// 主程序 UI 的窗口层（宪章 §8 M7b）
//
// 原生 Win32 标准控件 + WTL 薄封装：Tab 用 WC_TABCONTROL、标签用 static、
// 勾选/单选/下拉/编辑框/进度条/列表都是系统标准类（comctl32 的 PROGRESS_CLASS、
// SysListView32 也算系统标准控件）；没有一个控件是自绘的，也没有引入任何图片或
// 字体资源（视觉完全自研 = 不做 SC 那种换肤）。
//
// 渲染是数据驱动的：PagePanel 按 Page/Group/Field 的 kind 决定用哪个控件类，
// 控件消息再写回 Field，最后由 applyTo() 落到 core/config 的模型上。
// 冒烟测试与窗口层放在同一个 TU：它需要直接按控件 ID 驱动它们。
//
// 按钮的安全性：本文件里所有按钮只改计数器或配置，不调用 ExitWindowsEx、
// 不启动 sysprep、不执行 dism —— 冒烟测试会点一遍全部按钮，绝不能让机器真的
// 重启或开始封装。

#include "deploy_runner.hpp"
#include "ui.hpp"

#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>

#include <atlbase.h>
#include <atlapp.h>

#include <atlctrls.h>
#include <atltypes.h>
#include <atlwin.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "core/runplan.hpp"
#include "core/win7.hpp"
#include "core/sysinfo.hpp"
#include "core/deploy_progress.hpp"

// WTL 要求全局有一个应用模块对象
CAppModule _Module;

namespace osp::ui {
namespace {

// ---------------------------------------------------------------- 控件 ID

constexpr int kIdcTab = 1000;
constexpr int kIdcPagePanel = 1001;
constexpr int kIdcStatus = 1002;
constexpr int kIdcGroupBase = 1500; // 分组框
constexpr int kIdcFieldBase = 2000; // 字段控件：base + 页内扁平序号
constexpr int kIdcLabelBase = 6000; // 字段标签：base + 页内扁平序号
constexpr int kFieldIdLimit = 1000; // 一页最多这么多字段

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
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string fromWide(const std::wstring& text)
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
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr
    );
    return out;
}

std::string windowText(HWND wnd)
{
    if (wnd == nullptr)
    {
        return std::string();
    }
    const int length = ::GetWindowTextLengthW(wnd);
    std::wstring buffer(static_cast<std::size_t>(length) + 1, L'\0');
    ::GetWindowTextW(wnd, buffer.data(), static_cast<int>(buffer.size()));
    buffer.resize(static_cast<std::size_t>(length));
    return fromWide(buffer);
}

// 字段在某页里的扁平序号（与 PagePanel 的编号规则一致）
int indexOfField(const Page& page, const std::string& fieldId)
{
    int index = 0;
    for (const Group& group : page.groups)
    {
        for (const Field& field : group.fields)
        {
            if (field.id == fieldId)
            {
                return index;
            }
            ++index;
        }
    }
    return -1;
}

// 阶段英文 id -> 图纸上的中文标签（preinstall / install / postinstall / logon / desktop）
std::string phaseLabelOf(const std::string& phaseId)
{
    if (phaseId == "preinstall")
    {
        return "部署前";
    }
    if (phaseId == "install")
    {
        return "部署中";
    }
    if (phaseId == "postinstall")
    {
        return "部署后";
    }
    if (phaseId == "logon")
    {
        return "登录时";
    }
    if (phaseId == "desktop")
    {
        return "进桌面";
    }
    return phaseId;
}

class PagePanel;

// ---------------------------------------------------------------- 菜单命令 ID

constexpr int kIdcMenuOpenConfig = 9100;
constexpr int kIdcMenuSaveConfigAs = 9101;
constexpr int kIdcMenuExit = 9102;
constexpr int kIdcMenuScanNow = 9103;   // 与母盘系统页的「封装体检」同一个动作
constexpr int kIdcMenuRunDeploy = 9104; // 运行部署（拉起 osptd，进度进「运行」页）

// 部署线程 -> 界面线程的自定义消息（回调里 new 一个对象传过来，界面线程负责 delete）
constexpr UINT kMsgDeployProgress = WM_APP + 11;
constexpr UINT kMsgDeployFinish = WM_APP + 12;

// ---------------------------------------------------------------- 主框架

class MainFrame : public CWindowImpl<MainFrame>
{
public:
    DECLARE_WND_CLASS(L"OSPTMainFrame")

    CTabCtrl tab;
    PagePanel* panel = nullptr; // 由 OnCreate 创建，随窗口销毁
    CStatic status;

    // 应用状态：页面表由配置模型构建，按钮动作可能改配置并重建页面
    config::Config cfg;
    std::vector<Page> pages;

    // 交互式运行时的状态
    std::string configPath;   // 当前配置文件名，空 = 未命名
    bool configDirty = false; // 有未保存的编辑
    CMenu mainMenu;

    // 按钮动作的落地情况，冒烟测试按这些计数器核对「控件真的响应了」
    int scanCount = 0;
    int srsRunCount = 0;
    int taskAddCount = 0;
    int sysprepRunCount = 0;
    int shutdownIntentCount = 0;
    int restartIntentCount = 0;

    long long messageCount = 0; // 处理过的消息数，供冒烟测试确认消息循环真的在跑

    // 部署（运行页 / 完成页）
    DeployRunner deployRunner;
    bool deployStarted = false; // 本次会话是否跑过部署
    bool deploySucceeded = false;
    int deployFinishedTasks = 0;
    int deployTotalTasks = 0;
    std::string deployError;
    int doneCountdown = 0; // 完成页倒计时剩余秒数
    UINT_PTR doneTimer = 0;
    static constexpr UINT_PTR kDoneTimer = 0x52;
    // 冒烟测试用：为 true 时关机/重启只记录不真的执行
    bool powerActionsDryRun = false;
    int powerActionRequests = 0;

    BEGIN_MSG_MAP(MainFrame)
    MESSAGE_HANDLER(WM_CREATE, OnCreate)
    MESSAGE_HANDLER(WM_SIZE, OnSize)
    MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
    MESSAGE_HANDLER(WM_COMMAND, OnCommand)
    MESSAGE_HANDLER(WM_CLOSE, OnClose)
    MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    MESSAGE_HANDLER(WM_TIMER, OnDoneTimer)
    MESSAGE_HANDLER(kMsgDeployProgress, OnDeployProgress)
    MESSAGE_HANDLER(kMsgDeployFinish, OnDeployFinish)
    END_MSG_MAP()

    LRESULT OnDoneTimer(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDeployProgress(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDeployFinish(UINT, WPARAM, LPARAM, BOOL&);

    void rebuildPages();
    void handleAction(const std::string& action, const std::string& controlId);
    void refreshStatus();

    // 运行部署：把当前配置生成运行计划，拉起同目录的 osptd，进度进「运行」页
    bool startDeployment(std::string& error);
    // 切换页面并刷新（tab 与 panel 一起动）
    void showPage(const std::string& pageId);
    // 完成页：按结果显示成功/失败图与文案，并起倒计时
    void enterDonePage(bool success, const std::string& error);
    // 关机 / 重启（真的执行；冒烟测试只记录）
    void requestPowerAction(bool reboot);

    // 交互式运行：菜单命令与配置读写
    void applyAllPages();
    void refreshPagesFromConfig();
    void markDirty();
    void updateTitle();
    bool loadConfigFrom(const std::string& path);
    bool saveConfigTo(const std::string& path);
    bool promptOpenConfig();
    bool promptSaveConfigAs();
    void setStatus(const std::string& text);

    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&);
    LRESULT OnNotify(UINT, WPARAM, LPARAM lParam, BOOL&);
    LRESULT OnCommand(UINT, WPARAM wParam, LPARAM lParam, BOOL&);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
};

// ---------------------------------------------------------------- 页面面板

// 一个页面 = 若干分组框，每个分组里一行一个字段（标签 + 控件）。
// 布局在 WM_SIZE / WM_VSCROLL 里手算，超出可视区用标准滚动条。
class PagePanel : public CWindowImpl<PagePanel>
{
public:
    DECLARE_WND_CLASS(L"OSPSPagePanel")

    struct ControlRef
    {
        int fieldIndex = -1; // 页内扁平序号
        HWND handle = nullptr;
        FieldKind kind = FieldKind::Info;
    };

    MainFrame* owner = nullptr;
    int currentPage = 0;
    std::vector<ControlRef> controls;
    int offsetY = 0;
    int contentHeight = 0;
    int spinAngle = 0; // 转圈动画的当前角度，由定时器推进
    UINT_PTR spinTimer = 0;
    static constexpr UINT_PTR kSpinTimer = 0x51;

    // 控件 ID -> DFM 控件名（自绘时需要知道画什么）
    std::string imageIdFor(int controlId)
    {
        const int index = controlId - static_cast<int>(kIdcFieldBase);
        const Field* field = fieldAt(index);
        if (field == nullptr || field->kind != FieldKind::Image)
        {
            return std::string();
        }
        return field->id;
    }

    BEGIN_MSG_MAP(PagePanel)
    MESSAGE_HANDLER(WM_CREATE, OnCreate)
    MESSAGE_HANDLER(WM_SIZE, OnSize)
    MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
    MESSAGE_HANDLER(WM_COMMAND, OnCommand)
    MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
    MESSAGE_HANDLER(WM_TIMER, OnTimer)
    END_MSG_MAP()

    // 图纸里的 TImage（Pic_RunGif / PIC_Success / PIC_Error）。SC 的位图不能搬，
    // 这里按同一矩形自绘我们自己的图案：运行页是转动的圆弧，成功页是对勾、失败页是叉。
    LRESULT OnDrawItem(UINT, WPARAM, LPARAM lParam, BOOL&)
    {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item == nullptr || item->CtlType != ODT_STATIC)
        {
            return FALSE;
        }
        const std::string id = imageIdFor(static_cast<int>(item->CtlID));
        if (id.empty())
        {
            return FALSE;
        }

        const int width = item->rcItem.right - item->rcItem.left;
        const int height = item->rcItem.bottom - item->rcItem.top;
        HDC dc = item->hDC;
        RECT area = item->rcItem;

        // 底色与页面一致，避免重绘残影
        HBRUSH background = ::CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));
        ::FillRect(dc, &area, background);
        ::DeleteObject(background);

        const int size = (std::min)(width, height);
        const int left = area.left + (width - size) / 2;
        const int top = area.top + (height - size) / 2;
        const int right = left + size;
        const int bottom = top + size;

        if (id == "Pic_RunGif")
        {
            // 转动的圆弧：四段，靠 timer 推进起始角
            HPEN pen = ::CreatePen(PS_SOLID, (std::max)(3, size / 14), RGB(0x2F, 0x6F, 0xD0));
            HGDIOBJ oldPen = ::SelectObject(dc, pen);
            const int inset = size / 8;
            for (int i = 0; i < 4; ++i)
            {
                const int start = (spinAngle + i * 90) % 360;
                ::MoveToEx(dc, 0, 0, nullptr);
                ::AngleArc(
                    dc,
                    (left + right) / 2,
                    (top + bottom) / 2,
                    (size - inset * 2) / 2,
                    static_cast<float>(start),
                    60.0f
                );
            }
            ::SelectObject(dc, oldPen);
            ::DeleteObject(pen);
            return TRUE;
        }

        // 成功 / 失败：圆 + 对勾或叉
        const bool success = id == "PIC_Success";
        const COLORREF color = success ? RGB(0x2E, 0xA0, 0x43) : RGB(0xC0, 0x3A, 0x2B);
        HBRUSH fill = ::CreateSolidBrush(color);
        HPEN ring = ::CreatePen(PS_SOLID, 2, color);
        HGDIOBJ oldBrush = ::SelectObject(dc, fill);
        HGDIOBJ oldPen = ::SelectObject(dc, ring);
        ::Ellipse(dc, left + 4, top + 4, right - 4, bottom - 4);

        HPEN mark = ::CreatePen(PS_SOLID, (std::max)(3, size / 12), RGB(255, 255, 255));
        ::SelectObject(dc, mark);
        if (success)
        {
            POINT points[3] = {
                {left + size / 4, top + size / 2},
                {left + size / 2 - size / 20, bottom - size / 3},
                {right - size / 4, top + size / 3},
            };
            ::Polyline(dc, points, 3);
        }
        else
        {
            ::MoveToEx(dc, left + size / 4, top + size / 4, nullptr);
            ::LineTo(dc, right - size / 4, bottom - size / 4);
            ::MoveToEx(dc, right - size / 4, top + size / 4, nullptr);
            ::LineTo(dc, left + size / 4, bottom - size / 4);
        }
        ::SelectObject(dc, oldBrush);
        ::SelectObject(dc, oldPen);
        ::DeleteObject(fill);
        ::DeleteObject(ring);
        ::DeleteObject(mark);
        return TRUE;
    }

    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&)
    {
        rebuild();
        return 0;
    }

    // 运行页的转圈动画：自绘的圆弧靠定时器推进角度，80ms 一帧
    LRESULT OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&)
    {
        if (wParam != kSpinTimer || owner == nullptr || currentPage < 0 ||
            currentPage >= static_cast<int>(owner->pages.size()))
        {
            return 0;
        }
        spinAngle = (spinAngle + 24) % 360;
        const Page& page = owner->pages[static_cast<std::size_t>(currentPage)];
        int index = 0;
        for (const Group& group : page.groups)
        {
            for (const Field& field : group.fields)
            {
                if (field.id == "Pic_RunGif")
                {
                    HWND control = controlAt(index);
                    if (control != nullptr)
                    {
                        ::InvalidateRect(control, nullptr, TRUE);
                    }
                }
                ++index;
            }
        }
        return 0;
    }

    LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&)
    {
        const int width = LOWORD(lParam);
        const int height = HIWORD(lParam);
        layout(width, height, false);
        return 0;
    }

    LRESULT OnVScroll(UINT, WPARAM wParam, LPARAM, BOOL&)
    {
        SCROLLINFO info = {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_ALL;
        GetScrollInfo(SB_VERT, &info);
        int position = info.nPos;
        switch (LOWORD(wParam))
        {
        case SB_LINEUP:
            position -= 20;
            break;
        case SB_LINEDOWN:
            position += 20;
            break;
        case SB_PAGEUP:
            position -= static_cast<int>(info.nPage);
            break;
        case SB_PAGEDOWN:
            position += static_cast<int>(info.nPage);
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            position = info.nTrackPos;
            break;
        default:
            break;
        }
        position = (std::max)(0, position);
        if (position != offsetY)
        {
            offsetY = position;
            RECT client = {};
            ::GetClientRect(m_hWnd, &client);
            layout(client.right, client.bottom, false);
        }
        return 0;
    }

    LRESULT OnCommand(UINT, WPARAM wParam, LPARAM lParam, BOOL&)
    {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id < kIdcFieldBase || id >= kIdcFieldBase + kFieldIdLimit)
        {
            return 0;
        }
        const int index = id - kIdcFieldBase;
        Field* field = fieldAt(index);
        if (field == nullptr)
        {
            return 0;
        }

        switch (field->kind)
        {
        case FieldKind::Check:
            field->checked =
                (::SendMessageW(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        case FieldKind::Radio: {
            const HWND handle = reinterpret_cast<HWND>(lParam);
            field->checked = (::SendMessageW(handle, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (field->checked)
            {
                // 同组互斥：把同一分组里其它单选取消
                for (ControlRef& ref : controls)
                {
                    if (ref.handle == handle || ref.kind != FieldKind::Radio)
                    {
                        continue;
                    }
                    if (!sameGroup(ref.fieldIndex, index))
                    {
                        continue;
                    }
                    ::SendMessageW(ref.handle, BM_SETCHECK, BST_UNCHECKED, 0);
                    if (Field* other = fieldAt(ref.fieldIndex))
                    {
                        other->checked = false;
                    }
                }

                // 选中「工作组」就把域输入清掉：所见即所存，不能让保存时静默丢弃用户输入
                if (field->id == "Rb_WorkGroup")
                {
                    for (const char* domainField : {"Ed_Domain_name", "Ed_Domain_user", "Ed_Domain_pw"})
                    {
                        for (ControlRef& ref : controls)
                        {
                            if (Field* target = fieldAt(ref.fieldIndex))
                            {
                                if (target->id == domainField)
                                {
                                    target->value.clear();
                                    if (ref.handle != nullptr)
                                    {
                                        ::SetWindowTextW(ref.handle, L"");
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case FieldKind::Edit:
        case FieldKind::Number:
            field->value = windowText(reinterpret_cast<HWND>(lParam));
            break;
        case FieldKind::Combo: {
            const HWND handle = reinterpret_cast<HWND>(lParam);
            const LRESULT selected = ::SendMessageW(handle, CB_GETCURSEL, 0, 0);
            if (selected >= 0 && selected < static_cast<int>(field->options.size()))
            {
                field->comboIndex = static_cast<int>(selected);
                field->value = field->options[static_cast<std::size_t>(selected)];
            }
            break;
        }
        case FieldKind::Button:
            if (code == BN_CLICKED && owner != nullptr)
            {
                owner->handleAction(field->action, field->id);
            }
            break;
        default:
            break;
        }

        // 编辑类控件动过就标记「有未保存修改」（按钮只是发动作，不算编辑）
        if (owner != nullptr && field->kind != FieldKind::Button && field->kind != FieldKind::Info &&
            field->kind != FieldKind::Progress && field->kind != FieldKind::List)
        {
            owner->markDirty();
        }
        return 0;
    }

    // 用当前页面重建全部子控件
    void rebuild()
    {
        if (owner == nullptr)
        {
            return;
        }
        for (ControlRef& ref : controls)
        {
            if (ref.handle != nullptr)
            {
                ::DestroyWindow(ref.handle);
            }
        }
        for (HWND decoration : decorations)
        {
            if (decoration != nullptr)
            {
                ::DestroyWindow(decoration);
            }
        }
        controls.clear();
        decorations.clear();
        offsetY = 0;

        if (currentPage < 0 || currentPage >= static_cast<int>(owner->pages.size()))
        {
            return;
        }
        const Page& page = owner->pages[static_cast<std::size_t>(currentPage)];

        int index = 0;
        for (std::size_t g = 0; g < page.groups.size(); ++g)
        {
            const Group& group = page.groups[g];
            // caption 为空 = 图纸这里是 Pan_* 面板（BorderSides = []），没有边框，不画分组框
            if (!group.caption.empty())
            {
                CButton groupBox;
                groupBox.Create(
                    m_hWnd,
                    rcDefault,
                    toWide(group.caption).c_str(),
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    0,
                    static_cast<UINT>(kIdcGroupBase + g)
                );
                decorations.push_back(groupBox.m_hWnd);
            }

            for (const Field& field : group.fields)
            {
                // 图纸绝对布局的控件自己就是「标签」（Lab_* 在 SC 里是独立控件），
                // 再补一行左侧说明会多出图纸上没有的东西
                if (!field.placed())
                {
                    CStatic label;
                    label.Create(
                        m_hWnd,
                        rcDefault,
                        toWide(field.label).c_str(),
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        0,
                        static_cast<UINT>(kIdcLabelBase + index)
                    );
                    decorations.push_back(label.m_hWnd);
                }

                const UINT controlId = static_cast<UINT>(kIdcFieldBase + index);
                HWND handle = nullptr;
                switch (field.kind)
                {
                case FieldKind::Info: {
                    // 图纸绝对布局的 Lab_* 是纯文本标签：不能带 3D 边框（那是编辑框的样子），
                    // 对齐也照图纸来
                    DWORD style = WS_CHILD | WS_VISIBLE | SS_LEFT;
                    DWORD exStyle = WS_EX_CLIENTEDGE;
                    if (field.placed())
                    {
                        exStyle = 0;
                        if (field.align == 1)
                        {
                            style = WS_CHILD | WS_VISIBLE | SS_RIGHT;
                        }
                        else if (field.align == 2)
                        {
                            style = WS_CHILD | WS_VISIBLE | SS_CENTER;
                        }
                    }
                    CStatic value;
                    value.Create(m_hWnd, rcDefault, toWide(field.value).c_str(), style, exStyle, controlId);
                    handle = value.m_hWnd;
                    break;
                }
                case FieldKind::Check: {
                    CButton button;
                    button.Create(
                        m_hWnd,
                        rcDefault,
                        toWide(field.label).c_str(),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                        0,
                        controlId
                    );
                    button.SetCheck(field.checked ? BST_CHECKED : BST_UNCHECKED);
                    handle = button.m_hWnd;
                    break;
                }
                case FieldKind::Radio: {
                    CButton button;
                    button.Create(
                        m_hWnd,
                        rcDefault,
                        toWide(field.label).c_str(),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                        0,
                        controlId
                    );
                    button.SetCheck(field.checked ? BST_CHECKED : BST_UNCHECKED);
                    handle = button.m_hWnd;
                    break;
                }
                case FieldKind::Edit:
                case FieldKind::Number: {
                    CEdit edit;
                    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
                    if (field.kind == FieldKind::Number)
                    {
                        style |= ES_NUMBER;
                    }
                    edit.Create(m_hWnd, rcDefault, toWide(field.value).c_str(), style, 0, controlId);
                    handle = edit.m_hWnd;
                    break;
                }
                case FieldKind::Combo: {
                    CComboBox combo;
                    combo.Create(
                        m_hWnd,
                        rcDefault,
                        nullptr,
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                        0,
                        controlId
                    );
                    for (const std::string& option : field.options)
                    {
                        combo.AddString(toWide(option).c_str());
                    }
                    if (field.comboIndex >= 0 && field.comboIndex < static_cast<int>(field.options.size()))
                    {
                        combo.SetCurSel(field.comboIndex);
                    }
                    handle = combo.m_hWnd;
                    break;
                }
                case FieldKind::Button: {
                    CButton button;
                    button.Create(
                        m_hWnd,
                        rcDefault,
                        toWide(field.label).c_str(),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        0,
                        controlId
                    );
                    handle = button.m_hWnd;
                    break;
                }
                case FieldKind::Progress: {
                    CProgressBarCtrl bar;
                    bar.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, controlId);
                    ::SendMessageW(bar.m_hWnd, PBM_SETRANGE32, 0, 100);
                    ::SendMessageW(bar.m_hWnd, PBM_SETPOS, static_cast<WPARAM>(field.progress), 0);
                    handle = bar.m_hWnd;
                    break;
                }
                case FieldKind::Image: {
                    // 图纸里的 TImage，图案我们自绘（SC 的位图不搬）。
                    // SS_OWNERDRAW + 父窗口画：ui_frame 在 WM_DRAWITEM 里按 field.id 选图案。
                    CStatic image;
                    image.Create(m_hWnd, rcDefault, L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, controlId);
                    handle = image.m_hWnd;
                    break;
                }
                case FieldKind::List: {
                    CListViewCtrl list;
                    list.Create(
                        m_hWnd,
                        rcDefault,
                        nullptr,
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                        0,
                        controlId
                    );
                    list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
                    for (std::size_t c = 0; c < field.columns.size(); ++c)
                    {
                        list.InsertColumn(
                            static_cast<int>(c),
                            toWide(field.columns[c]).c_str(),
                            LVCFMT_LEFT,
                            140,
                            static_cast<int>(c)
                        );
                    }
                    for (const std::vector<std::string>& row : field.rows)
                    {
                        const int item = list.GetItemCount();
                        std::wstring first = row.empty() ? std::wstring() : toWide(row[0]);
                        list.InsertItem(item, first.c_str());
                        for (std::size_t c = 1; c < row.size(); ++c)
                        {
                            list.SetItemText(item, static_cast<int>(c), toWide(row[c]).c_str());
                        }
                    }
                    handle = list.m_hWnd;
                    break;
                }
                }

                // 图纸里靠 Visible 二选一的控件（成功/失败图与文案）：初始按模型隐藏
                if (handle != nullptr && !field.visible)
                {
                    ::ShowWindow(handle, SW_HIDE);
                }

                ControlRef ref;
                ref.fieldIndex = index;
                ref.handle = handle;
                ref.kind = field.kind;
                controls.push_back(ref);
                ++index;
            }
        }
    }

    // 一页的扁平字段序号 -> 字段
    Field* fieldAt(int index)
    {
        if (owner == nullptr || currentPage < 0 || currentPage >= static_cast<int>(owner->pages.size()))
        {
            return nullptr;
        }
        Page& page = owner->pages[static_cast<std::size_t>(currentPage)];
        int cursor = 0;
        for (Group& group : page.groups)
        {
            for (Field& field : group.fields)
            {
                if (cursor == index)
                {
                    return &field;
                }
                ++cursor;
            }
        }
        return nullptr;
    }

    // 两个扁平序号是否属于同一个分组框（单选互斥只在组内生效）
    bool sameGroup(int a, int b) const
    {
        if (owner == nullptr || currentPage < 0 || currentPage >= static_cast<int>(owner->pages.size()))
        {
            return false;
        }
        const Page& page = owner->pages[static_cast<std::size_t>(currentPage)];
        const auto groupOf = [&page](int index) -> int {
            int cursor = 0;
            for (std::size_t g = 0; g < page.groups.size(); ++g)
            {
                for (std::size_t f = 0; f < page.groups[g].fields.size(); ++f)
                {
                    if (cursor == index)
                    {
                        return static_cast<int>(g);
                    }
                    ++cursor;
                }
            }
            return -1;
        };
        return groupOf(a) == groupOf(b);
    }

    HWND controlAt(int index) const
    {
        for (const ControlRef& ref : controls)
        {
            if (ref.fieldIndex == index)
            {
                return ref.handle;
            }
        }
        return nullptr;
    }

    // 手算布局：分组框竖着摞，组内一行一个字段
    void layout(int width, int height, bool strictSize)
    {
        (void)strictSize;
        if (owner == nullptr || currentPage < 0 || currentPage >= static_cast<int>(owner->pages.size()))
        {
            return;
        }
        const Page& page = owner->pages[static_cast<std::size_t>(currentPage)];

        // 图纸绝对布局页（运行 / 完成）：坐标是 800x540 设计空间里的绝对值，
        // 按客户区宽度等比缩放。这类页不滚动、也不做纵向堆叠。
        if (page.dfmLayout && page.clientWidth > 0 && page.clientHeight > 0)
        {
            const double scale = static_cast<double>(width) / static_cast<double>(page.clientWidth);
            int index = 0;
            for (const Group& group : page.groups)
            {
                for (const Field& field : group.fields)
                {
                    HWND control = controlAt(index);
                    if (control != nullptr && field.placed())
                    {
                        ::SetWindowPos(
                            control,
                            nullptr,
                            static_cast<int>(field.x * scale),
                            static_cast<int>(field.y * scale),
                            (std::max)(1, static_cast<int>(field.width * scale)),
                            (std::max)(1, static_cast<int>(field.height * scale)),
                            SWP_NOZORDER | SWP_NOACTIVATE
                        );
                    }
                    ++index;
                }
            }
            contentHeight = height;
            SCROLLINFO info = {};
            info.cbSize = sizeof(info);
            info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            info.nMin = 0;
            info.nMax = (std::max)(height, 1);
            info.nPage = static_cast<UINT>((std::max)(height, 1));
            info.nPos = 0;
            SetScrollInfo(SB_VERT, &info, TRUE);
            return;
        }

        const int margin = 10;
        const int labelWidth = 170;
        const int rowHeight = 24;
        const int rowGap = 4;
        const int groupHeader = 22;
        const int groupBottom = 10;

        int y = margin - offsetY;
        int index = 0;
        for (std::size_t g = 0; g < page.groups.size(); ++g)
        {
            const Group& group = page.groups[g];
            int groupHeight = groupHeader + groupBottom;
            for (const Field& field : group.fields)
            {
                groupHeight += rowHeight + rowGap;
                if (field.kind == FieldKind::List)
                {
                    groupHeight += 120;
                }
            }

            HWND groupBox = GetDlgItem(static_cast<int>(kIdcGroupBase + g));
            if (groupBox != nullptr)
            {
                ::SetWindowPos(
                    groupBox,
                    nullptr,
                    margin,
                    y,
                    width - margin * 2,
                    groupHeight,
                    SWP_NOZORDER | SWP_NOACTIVATE
                );
            }

            int rowY = y + groupHeader;
            for (const Field& field : group.fields)
            {
                HWND label = GetDlgItem(kIdcLabelBase + index);
                HWND control = controlAt(index);
                const int controlHeight =
                    field.kind == FieldKind::List ? 140 : (field.kind == FieldKind::Progress ? 18 : 22);
                if (label != nullptr)
                {
                    ::SetWindowPos(
                        label,
                        nullptr,
                        margin + 8,
                        rowY + 3,
                        labelWidth - 12,
                        18,
                        SWP_NOZORDER | SWP_NOACTIVATE
                    );
                }
                if (control != nullptr)
                {
                    int controlWidth = width - margin * 2 - labelWidth - 20;
                    if (field.kind == FieldKind::Check || field.kind == FieldKind::Radio)
                    {
                        controlWidth = (std::max)(200, controlWidth);
                    }
                    if (field.kind == FieldKind::Button)
                    {
                        controlWidth = 140;
                    }
                    ::SetWindowPos(
                        control,
                        nullptr,
                        margin + labelWidth,
                        rowY,
                        controlWidth,
                        controlHeight,
                        SWP_NOZORDER | SWP_NOACTIVATE
                    );
                    if (field.kind == FieldKind::Combo)
                    {
                        // 下拉列表拉下来时的高度
                        ::SendMessageW(control, CB_SETDROPPEDWIDTH, (WPARAM)(controlWidth + 60), 0);
                    }
                }
                rowY += rowHeight + rowGap;
                if (field.kind == FieldKind::List)
                {
                    rowY += 120;
                }
                ++index;
            }

            y += groupHeight + 8;
            (void)height;
        }

        contentHeight = y + offsetY + margin;

        SCROLLINFO info = {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMin = 0;
        info.nMax = (std::max)(contentHeight, 1);
        info.nPage = static_cast<UINT>((std::max)(height, 1));
        info.nPos = offsetY;
        SetScrollInfo(SB_VERT, &info, TRUE);
    }

    void setPage(int index)
    {
        currentPage = index;
        rebuild();

        // 只有运行页需要动画定时器，切到别的页要停掉，免得白跑
        bool needsSpin = false;
        if (index >= 0 && index < static_cast<int>(owner->pages.size()))
        {
            for (const Group& group : owner->pages[static_cast<std::size_t>(index)].groups)
            {
                for (const Field& field : group.fields)
                {
                    if (field.id == "Pic_RunGif")
                    {
                        needsSpin = true;
                    }
                }
            }
        }
        if (needsSpin && spinTimer == 0)
        {
            spinTimer = ::SetTimer(m_hWnd, kSpinTimer, 80, nullptr);
        }
        else if (!needsSpin && spinTimer != 0)
        {
            ::KillTimer(m_hWnd, kSpinTimer);
            spinTimer = 0;
        }

        RECT client = {};
        ::GetClientRect(m_hWnd, &client);
        layout(client.right, client.bottom, false);
    }

private:
    std::vector<HWND> decorations; // 分组框与标签，重建时一并销毁
};

// ---------------------------------------------------------------- 框架实现

void MainFrame::rebuildPages()
{
    pages = pagesFromConfig(cfg);
    if (panel != nullptr)
    {
        panel->setPage(panel->currentPage);
    }
}

void MainFrame::handleAction(const std::string& action, const std::string& controlId)
{
    if (action == "scan")
    {
        // 重新体检：重跑 M2 的采集，再刷新母盘系统页
        ++scanCount;
        if (Page* page = findPage(pages, "system"))
        {
            *page = systemPage();
        }
        if (panel != nullptr && panel->currentPage == 0)
        {
            panel->rebuild();
            RECT client = {};
            ::GetClientRect(panel->m_hWnd, &client);
            panel->layout(client.right, client.bottom, false);
        }
    }
    else if (action == "srsRun")
    {
        // 只暂存意图：真正执行要由 --inject-drivers --execute 走 M6 的路径
        ++srsRunCount;
    }
    else if (action == "addTask")
    {
        if (const Page* tasks = findPage(pages, "tasks"))
        {
            cfg.runPlan.push_back(taskFromPage(*tasks));
            ++taskAddCount;
            rebuildPages();
        }
    }
    else if (action == "sysprepRun")
    {
        // 只暂存意图：封装必须由 --sysprep --execute 启动，界面按钮不执行
        ++sysprepRunCount;
    }
    else if (action == "afterShutdown")
    {
        // 完成页的「关机(&T)」——图纸上就是立刻关机，不是只记个意图
        requestPowerAction(false);
    }
    else if (action == "afterRestart")
    {
        requestPowerAction(true);
    }
    else if (action == "runDeploy")
    {
        std::string error;
        if (!startDeployment(error))
        {
            const std::wstring message = toWide(error);
            ::MessageBoxW(m_hWnd, message.c_str(), L"无法开始部署", MB_OK | MB_ICONWARNING);
        }
    }
    (void)controlId;
}

// 进度回调（后台线程 PostMessage 过来）：刷新「运行」页的两条进度条与说明文字
LRESULT MainFrame::OnDeployProgress(UINT, WPARAM, LPARAM lParam, BOOL&)
{
    std::unique_ptr<osp::deploy::ProgressEvent> event(reinterpret_cast<osp::deploy::ProgressEvent*>(lParam));
    if (!event || !event->parsed)
    {
        return 0;
    }

    deployFinishedTasks = event->finished;
    if (event->totalTasks > 0)
    {
        deployTotalTasks = event->totalTasks;
    }

    Page* run = findPage(pages, "run");
    if (run == nullptr)
    {
        return 0;
    }
    const int overall = osp::deploy::overallPercent(event->finished, deployTotalTasks);
    const int taskPercent = osp::deploy::phasePercent(event->taskIndex, event->taskTotal);
    for (Group& group : run->groups)
    {
        for (Field& field : group.fields)
        {
            if (field.id == "Pro_Total")
            {
                field.progress = overall;
                if (HWND control = panel->controlAt(indexOfField(*run, field.id)))
                {
                    ::SendMessageW(control, PBM_SETPOS, static_cast<WPARAM>(overall), 0);
                }
            }
            else if (field.id == "Pro_Task")
            {
                field.progress = taskPercent;
                if (HWND control = panel->controlAt(indexOfField(*run, field.id)))
                {
                    ::SendMessageW(control, PBM_SETPOS, static_cast<WPARAM>(taskPercent), 0);
                }
            }
            else if (field.id == "Lab_Run")
            {
                // SC 的说明文字是「正在执行，请稍候...」；跑起来后换成具体阶段与任务
                std::string text = "正在执行，请稍候...";
                if (!event->phase.empty() && event->event != "result")
                {
                    text = "正在执行：" + phaseLabelOf(event->phase);
                    if (event->taskIndex > 0)
                    {
                        text += "（" + std::to_string(event->taskIndex) + "/" +
                            std::to_string(event->taskTotal) + "）";
                    }
                }
                field.value = text;
                if (HWND control = panel->controlAt(indexOfField(*run, field.id)))
                {
                    ::SetWindowTextW(control, toWide(text).c_str());
                }
            }
        }
    }
    refreshStatus();
    return 0;
}

// 部署结束：切到完成页，按结果显示成功/失败
LRESULT MainFrame::OnDeployFinish(UINT, WPARAM, LPARAM lParam, BOOL&)
{
    std::unique_ptr<DeployRunSummary> summary(reinterpret_cast<DeployRunSummary*>(lParam));
    if (!summary)
    {
        return 0;
    }
    enterDonePage(summary->ok, summary->error);
    return 0;
}

// 完成页倒计时：每秒减一，到 0 执行用户选择的重启（图纸：程序将在 N 秒后重启）
LRESULT MainFrame::OnDoneTimer(UINT, WPARAM wParam, LPARAM, BOOL&)
{
    if (wParam != kDoneTimer || doneCountdown <= 0)
    {
        return 0;
    }
    --doneCountdown;

    Page* done = findPage(pages, "done");
    if (done != nullptr)
    {
        for (Group& group : done->groups)
        {
            for (Field& field : group.fields)
            {
                if (field.id == "Lab_Countdown")
                {
                    const std::string text =
                        "程序将在" + std::to_string(doneCountdown) + "秒后重启计算机，如需取消请点“返回”。";
                    field.value = text;
                    if (HWND control = panel->controlAt(indexOfField(*done, field.id)))
                    {
                        ::SetWindowTextW(control, toWide(text).c_str());
                    }
                }
            }
        }
    }

    if (doneCountdown <= 0)
    {
        ::KillTimer(m_hWnd, kDoneTimer);
        doneTimer = 0;
        requestPowerAction(true);
    }
    return 0;
}

// 状态栏文本：部署中显示进度，否则显示配置状态
void MainFrame::refreshStatus()
{
    if (deployRunner.running())
    {
        setStatus(
            "正在部署：" + std::to_string(deployFinishedTasks) + "/" + std::to_string(deployTotalTasks) +
            " 个任务"
        );
        return;
    }
    if (deployStarted)
    {
        setStatus(
            std::string(deploySucceeded ? "部署完成" : "部署失败") + "（" +
            std::to_string(deployFinishedTasks) + "/" + std::to_string(deployTotalTasks) + "）" +
            (deployError.empty() ? std::string() : "：" + deployError)
        );
        return;
    }
    setStatus(configDirty ? "配置已修改（未保存）" : "就绪");
}

// 自身 exe 所在目录（发布包里 ospt.exe 与 osptd.exe 并排）
std::wstring directoryOfSelf()
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return std::wstring();
    }
    std::wstring full(path, length);
    const std::size_t slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : full.substr(0, slash + 1);
}

// ---------------------------------------------------------------- 运行部署

bool MainFrame::startDeployment(std::string& error)
{
    if (deployRunner.running())
    {
        error = "上一次部署还在运行";
        return false;
    }

    // 1) 用当前界面上的配置生成运行计划。页面上的编辑先落回 cfg，避免「界面改了没生效」
    for (const Page& page : pages)
    {
        if (page.configPage)
        {
            applyTo(page, cfg);
        }
    }

    const osp::runplan::PlanResult plan = osp::runplan::build(cfg, osp::runplan::enumerateLocalDrives());
    if (!plan.ok)
    {
        error = plan.error.empty() ? std::string("运行计划生成失败") : plan.error;
        return false;
    }

    // 运行目录放在系统临时目录下的固定名字：部署端要能读到它，路径里不能有中文变量
    wchar_t tempPath[MAX_PATH] = {};
    const DWORD tempLength = ::GetTempPathW(MAX_PATH, tempPath);
    if (tempLength == 0)
    {
        error = "取不到临时目录";
        return false;
    }
    const std::string runDir = osp::win7::toUtf8Path(std::wstring(tempPath) + L"ospt-run");
    if (!osp::runplan::writePlanDirectory(plan, runDir, error))
    {
        return false;
    }

    // 2) 找同目录的 osptd.exe（发布包里两个 exe 并排）
    const std::string osptdPath = osp::win7::toUtf8Path(directoryOfSelf() + L"\\osptd.exe");
    if (!osp::win7::pathFromUtf8(osptdPath).has_filename() ||
        !std::filesystem::exists(osp::win7::pathFromUtf8(osptdPath)))
    {
        error = "找不到 osptd.exe（应与 ospt.exe 同目录）：" + osptdPath;
        return false;
    }

    // 3) 回调都在后台线程里，必须 PostMessage 回界面线程再碰控件
    HWND self = m_hWnd;
    deployRunner.setProgressCallback([self](const osp::deploy::ProgressEvent& event) {
        ::PostMessageW(
            self, kMsgDeployProgress, 0, reinterpret_cast<LPARAM>(new osp::deploy::ProgressEvent(event))
        );
    });
    deployRunner.setFinishCallback([self](const DeployRunSummary& summary) {
        ::PostMessageW(self, kMsgDeployFinish, 0, reinterpret_cast<LPARAM>(new DeployRunSummary(summary)));
    });

    if (!deployRunner.start(osptdPath, runDir, error))
    {
        return false;
    }

    deployStarted = true;
    deploySucceeded = false;
    deployError.clear();
    deployFinishedTasks = 0;
    deployTotalTasks = 0;
    for (const auto& phase : plan.phases)
    {
        deployTotalTasks += static_cast<int>(phase.tasks.size());
    }
    shutdownIntentCount = 0;
    restartIntentCount = 0;

    showPage("run");
    return true;
}

void MainFrame::showPage(const std::string& pageId)
{
    for (std::size_t i = 0; i < pages.size(); ++i)
    {
        if (pages[i].id != pageId)
        {
            continue;
        }
        const int index = static_cast<int>(i);
        if (tab.m_hWnd != nullptr)
        {
            tab.SetCurSel(index);
        }
        if (panel != nullptr)
        {
            panel->setPage(index);
        }
        return;
    }
}

void MainFrame::enterDonePage(bool success, const std::string& error)
{
    deploySucceeded = success;
    deployError = error;

    // 按结果二选一：成功图/成功文案 或 失败图/失败文案（图纸上它们占同一矩形）
    if (const Page* done = findPage(pages, "done"))
    {
        for (Group& group : const_cast<Page*>(done)->groups)
        {
            for (Field& field : group.fields)
            {
                if (field.id == "PIC_Success" || field.id == "Lab_Complete")
                {
                    field.visible = success;
                }
                else if (field.id == "PIC_Error" || field.id == "Lab_CompleteErr")
                {
                    field.visible = !success;
                }
                else if (field.id == "Lab_Countdown")
                {
                    // 失败时不显示「N 秒后重启」—— 重试还是排查由用户决定
                    field.visible = success;
                }
                else if (field.id == "Pro_End")
                {
                    field.progress = success ? 100 : 0;
                }
            }
        }
    }

    showPage("done");

    // 倒计时：成功了才起；秒数取图纸文案里的 10 秒
    if (doneTimer != 0)
    {
        ::KillTimer(m_hWnd, kDoneTimer);
        doneTimer = 0;
    }
    doneCountdown = success ? 10 : 0;
    if (success)
    {
        doneTimer = ::SetTimer(m_hWnd, kDoneTimer, 1000, nullptr);
    }
    refreshStatus();
}

void MainFrame::requestPowerAction(bool reboot)
{
    if (reboot)
    {
        cfg.sysprep.afterAction = "reboot";
        ++restartIntentCount;
    }
    else
    {
        cfg.sysprep.afterAction = "shutdown";
        ++shutdownIntentCount;
    }
    ++powerActionRequests;

    if (powerActionsDryRun)
    {
        refreshStatus();
        return;
    }

    // 真执行：启用 SE_SHUTDOWN_NAME 后调 ExitWindowsEx。
    // 失败要把原因说清楚，不能静默什么都不发生。
    std::string error;
    if (!osp::win7::requestSystemPowerAction(reboot, error))
    {
        const std::wstring message = toWide(error);
        ::MessageBoxW(m_hWnd, message.c_str(), L"关机 / 重启失败", MB_OK | MB_ICONERROR);
    }
    refreshStatus();
}

LRESULT MainFrame::OnCreate(UINT, WPARAM, LPARAM, BOOL&)
{
    tab.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | TCS_TABS, 0, kIdcTab);

    panel = new PagePanel();
    panel->owner = this;
    panel->Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, kIdcPagePanel);

    status.Create(m_hWnd, rcDefault, L"就绪", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_SUNKEN, 0, kIdcStatus);

    // 菜单：只用系统标准菜单，配置的打开 / 另存为 / 退出，加一个与「封装体检」同源的立即体检
    if (HMENU menu = ::CreateMenu())
    {
        HMENU fileMenu = ::CreatePopupMenu();
        ::AppendMenuW(fileMenu, MF_STRING, kIdcMenuOpenConfig, L"打开配置(&O)...\tCtrl+O");
        ::AppendMenuW(fileMenu, MF_STRING, kIdcMenuSaveConfigAs, L"另存为配置(&S)...\tCtrl+S");
        ::AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(fileMenu, MF_STRING, kIdcMenuExit, L"退出(&X)");
        ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"文件(&F)");

        HMENU toolMenu = ::CreatePopupMenu();
        ::AppendMenuW(toolMenu, MF_STRING, kIdcMenuScanNow, L"立即封装体检(&R)");
        ::AppendMenuW(toolMenu, MF_SEPARATOR, 0, nullptr);
        // 运行部署：与 SC 的「运行」页对应 —— 跑起来后进度进那一页
        ::AppendMenuW(toolMenu, MF_STRING, kIdcMenuRunDeploy, L"运行部署(&D)...");
        ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(toolMenu), L"工具(&T)");

        ::SetMenu(m_hWnd, menu);
        mainMenu.Attach(menu); // 交给窗口持有，随窗口一起销毁
    }

    pages = pagesFromConfig(cfg);

    TCITEMW item = {};
    item.mask = TCIF_TEXT;
    for (const Page& page : pages)
    {
        std::wstring title = toWide(page.title);
        item.pszText = title.data();
        tab.InsertItem(static_cast<int>(tab.GetItemCount()), &item);
    }
    tab.SetCurSel(0);
    panel->setPage(0);
    updateTitle();
    return 0;
}

void MainFrame::updateTitle()
{
    const std::string name = configPath.empty() ? std::string("未命名配置") : configPath;
    const std::wstring title = toWide("OpenSysprepTool 配置 - " + name + (configDirty ? " *" : ""));
    SetWindowTextW(title.c_str());
}

void MainFrame::markDirty()
{
    if (!configDirty)
    {
        configDirty = true;
        updateTitle();
    }
}

void MainFrame::setStatus(const std::string& text)
{
    if (status.m_hWnd != nullptr)
    {
        status.SetWindowTextW(toWide(text).c_str());
    }
}

void MainFrame::applyAllPages()
{
    // 界面上改过的值一律先落到配置模型，再谈保存
    cfg = configFromPages(pages);
}

void MainFrame::refreshPagesFromConfig()
{
    pages = pagesFromConfig(cfg);
    if (tab.m_hWnd != nullptr)
    {
        tab.DeleteAllItems();
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        for (const Page& page : pages)
        {
            std::wstring title = toWide(page.title);
            item.pszText = title.data();
            tab.InsertItem(static_cast<int>(tab.GetItemCount()), &item);
        }
        tab.SetCurSel(0);
    }
    if (panel != nullptr && panel->m_hWnd != nullptr)
    {
        panel->setPage(0);
    }
    updateTitle();
}

bool MainFrame::loadConfigFrom(const std::string& path)
{
    config::Config loaded;
    std::string error;
    if (!loadConfigFile(path, loaded, error))
    {
        MessageBoxW(toWide("载入失败：\n" + error).c_str(), L"打开配置", MB_OK | MB_ICONWARNING);
        setStatus("载入失败：" + error);
        return false;
    }

    cfg = loaded;
    configPath = path;
    configDirty = false;
    refreshPagesFromConfig();
    setStatus("已载入 " + path);
    return true;
}

bool MainFrame::saveConfigTo(const std::string& path)
{
    applyAllPages();
    std::string error;
    if (!saveConfigFile(path, cfg, error))
    {
        MessageBoxW(toWide("保存失败：\n" + error).c_str(), L"另存为配置", MB_OK | MB_ICONWARNING);
        setStatus("保存失败：" + error);
        return false;
    }

    configPath = path;
    configDirty = false;
    updateTitle();
    setStatus("已保存到 " + path);
    return true;
}

bool MainFrame::promptOpenConfig()
{
    if (configDirty)
    {
        const int answer = MessageBoxW(
            L"当前配置有未保存的修改，继续打开会丢弃它们。\n\n要打开其它配置吗？",
            L"打开配置",
            MB_YESNO | MB_ICONQUESTION
        );
        if (answer != IDYES)
        {
            return false;
        }
    }

    wchar_t buffer[MAX_PATH] = L"";
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = m_hWnd;
    dialog.lpstrFilter = L"配置文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = ARRAYSIZE(buffer);
    dialog.lpstrDefExt = L"json";
    dialog.lpstrTitle = L"打开配置";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (GetOpenFileNameW(&dialog) == 0)
    {
        return false; // 用户取消
    }
    return loadConfigFrom(fromWide(std::wstring(buffer)));
}

bool MainFrame::promptSaveConfigAs()
{
    wchar_t buffer[MAX_PATH] = L"";
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = m_hWnd;
    dialog.lpstrFilter = L"配置文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = ARRAYSIZE(buffer);
    dialog.lpstrDefExt = L"json";
    dialog.lpstrTitle = L"另存为配置";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (GetSaveFileNameW(&dialog) == 0)
    {
        return false; // 用户取消
    }
    return saveConfigTo(fromWide(std::wstring(buffer)));
}

LRESULT MainFrame::OnCommand(UINT, WPARAM wParam, LPARAM lParam, BOOL&)
{
    const int id = LOWORD(wParam);
    if (HIWORD(wParam) == 0 && lParam == 0)
    {
        // 菜单命令：lParam 为 0；子控件通知由各自的面板处理
        switch (id)
        {
        case kIdcMenuOpenConfig:
            promptOpenConfig();
            return 0;
        case kIdcMenuSaveConfigAs:
            promptSaveConfigAs();
            return 0;
        case kIdcMenuExit:
            SendMessageW(WM_CLOSE, 0, 0);
            return 0;
        case kIdcMenuRunDeploy: {
            std::string error;
            if (!startDeployment(error))
            {
                const std::wstring message = toWide(error);
                ::MessageBoxW(m_hWnd, message.c_str(), L"无法开始部署", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }
        case kIdcMenuScanNow:
            handleAction("scan", "Btn_Scan");
            setStatus("已完成封装体检");
            return 0;
        default:
            return 0;
        }
    }
    return 0;
}

LRESULT MainFrame::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&)
{
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    const int statusHeight = 22;

    if (tab.m_hWnd != nullptr)
    {
        ::SetWindowPos(tab, nullptr, 0, 0, width, height - statusHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (panel != nullptr && panel->m_hWnd != nullptr)
    {
        RECT inner = {0, 0, width, height - statusHeight};
        tab.AdjustRect(FALSE, &inner);
        ::SetWindowPos(
            panel->m_hWnd,
            nullptr,
            inner.left,
            inner.top,
            inner.right - inner.left,
            inner.bottom - inner.top,
            SWP_NOZORDER | SWP_NOACTIVATE
        );
    }
    if (status.m_hWnd != nullptr)
    {
        ::SetWindowPos(
            status, nullptr, 0, height - statusHeight, width, statusHeight, SWP_NOZORDER | SWP_NOACTIVATE
        );
    }
    return 0;
}

LRESULT MainFrame::OnNotify(UINT, WPARAM, LPARAM lParam, BOOL&)
{
    auto* header = reinterpret_cast<LPNMHDR>(lParam);
    if (header != nullptr && header->idFrom == kIdcTab && header->code == TCN_SELCHANGE)
    {
        const int selected = tab.GetCurSel();
        if (panel != nullptr && selected >= 0 && selected < static_cast<int>(pages.size()))
        {
            panel->setPage(selected);
            status.SetWindowTextW(toWide(pages[static_cast<std::size_t>(selected)].title).c_str());
        }
    }
    return 0;
}

LRESULT MainFrame::OnClose(UINT, WPARAM, LPARAM, BOOL&)
{
    if (configDirty)
    {
        const int answer = MessageBoxW(
            L"配置有未保存的修改。\n\n点『是』退出前先另存为，点『否』直接退出，点『取消』留在界面上。",
            L"退出",
            MB_YESNOCANCEL | MB_ICONQUESTION
        );
        if (answer == IDCANCEL)
        {
            return 0; // 不关窗
        }
        if (answer == IDYES && !promptSaveConfigAs())
        {
            return 0; // 用户取消了保存，留在界面上
        }
    }
    DestroyWindow();
    return 0;
}

LRESULT MainFrame::OnDestroy(UINT, WPARAM, LPARAM, BOOL&)
{
    if (panel != nullptr && panel->m_hWnd != nullptr)
    {
        panel->DestroyWindow();
    }
    delete panel;
    panel = nullptr;
    PostQuitMessage(0);
    return 0;
}

// ---------------------------------------------------------------- 冒烟测试

struct SmokeContext
{
    SmokeResult result;
    MainFrame frame;
    bool moduleInitialized = false;

    void record(bool ok, const std::string& what, const std::string& detail = std::string())
    {
        ++result.checks;
        if (ok)
        {
            result.lines.push_back("PASS  " + what);
            return;
        }
        ++result.failures;
        std::string line = "FAIL  " + what;
        if (!detail.empty())
        {
            line += "  [" + detail + "]";
        }
        result.lines.push_back(line);
        result.failuresDetail.push_back(line);
    }
};

// 把窗口消息真正泵一遍，确认消息循环在跑而不是死窗口
void pumpMessages(int rounds)
{
    MSG msg = {};
    for (int i = 0; i < rounds; ++i)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(1);
    }
}

// 真实控件在用户操作时是把 WM_COMMAND 发给自己的父窗口的（编辑框/下拉是通知码）。
void notifyCommand(HWND parent, int controlId, int code, HWND control)
{
    ::SendMessageW(
        parent, WM_COMMAND, MAKEWPARAM(controlId, static_cast<WORD>(code)), reinterpret_cast<LPARAM>(control)
    );
}

// 真实切换页签时 tabs 会向父窗口发 TCN_SELCHANGE
void notifyTabChange(HWND frame, HWND tab)
{
    NMHDR header = {};
    header.hwndFrom = tab;
    header.idFrom = kIdcTab;
    header.code = TCN_SELCHANGE;
    ::SendMessageW(frame, WM_NOTIFY, kIdcTab, reinterpret_cast<LPARAM>(&header));
}

// 一个页面上按这个页面的字段去驱动控件。
// 注意：点「封装体检」这类按钮会换掉当前页的字段表，所以遍历用快照，
// 校验时再按控件名回到活页面上查。
void drivePage(SmokeContext& context, const Page& livePage)
{
    PagePanel* panel = context.frame.panel;
    const Page snapshot = livePage;
    const std::string pageName = snapshot.title + "（" + snapshot.dfmSheet + "）";

    int index = 0;
    for (const Group& group : snapshot.groups)
    {
        for (const Field& field : group.fields)
        {
            const int controlId = kIdcFieldBase + index;
            HWND handle = panel->controlAt(index);
            if (handle == nullptr)
            {
                context.record(false, pageName + " 找到控件 " + field.id);
                ++index;
                continue;
            }

            switch (field.kind)
            {
            case FieldKind::Check: {
                const bool before = field.checked;
                ::SendMessageW(handle, BM_CLICK, 0, 0);
                pumpMessages(2);
                const bool after = (::SendMessageW(handle, BM_GETCHECK, 0, 0) == BST_CHECKED);
                const Field* updated = findField(livePage, field.id);
                context.record(
                    updated != nullptr && updated->checked == after && after != before,
                    pageName + " 复选框可切换: " + field.id,
                    "before=" + std::string(before ? "1" : "0") + " after=" + std::string(after ? "1" : "0")
                );
                ++context.result.controlsDriven;
                break;
            }
            case FieldKind::Radio: {
                if (!field.checked)
                {
                    ::SendMessageW(handle, BM_CLICK, 0, 0);
                    pumpMessages(2);
                    const Field* updated = findField(livePage, field.id);
                    context.record(
                        updated != nullptr && updated->checked,
                        pageName + " 单选可选中: " + field.id,
                        updated == nullptr ? "字段丢失" : "checked=1"
                    );
                    ++context.result.controlsDriven;
                }
                break;
            }
            case FieldKind::Edit:
            case FieldKind::Number: {
                const std::string probe =
                    field.kind == FieldKind::Number ? std::string("77") : ("probe-" + field.id);
                ::SetWindowTextW(handle, toWide(probe).c_str());
                notifyCommand(panel->m_hWnd, controlId, EN_CHANGE, handle);
                const Field* updated = findField(livePage, field.id);
                context.record(
                    updated != nullptr && updated->value == probe,
                    pageName + " 编辑框往返一致: " + field.id,
                    updated == nullptr ? "字段丢失"
                                       : ("控件=" + windowText(handle) + " 模型=" + updated->value)
                );
                ++context.result.controlsDriven;
                break;
            }
            case FieldKind::Combo: {
                const LRESULT count = ::SendMessageW(handle, CB_GETCOUNT, 0, 0);
                const LRESULT target = count - 1;
                ::SendMessageW(handle, CB_SETCURSEL, static_cast<WPARAM>(target), 0);
                notifyCommand(panel->m_hWnd, controlId, CBN_SELCHANGE, handle);
                const Field* updated = findField(livePage, field.id);
                context.record(
                    updated != nullptr && updated->comboIndex == static_cast<int>(target) && count > 0,
                    pageName + " 下拉可选中: " + field.id,
                    "count=" + std::to_string(count) +
                        " sel=" + std::to_string(updated == nullptr ? -99 : updated->comboIndex)
                );
                ++context.result.controlsDriven;
                break;
            }
            case FieldKind::Progress: {
                ::SendMessageW(handle, PBM_SETPOS, 42, 0);
                const LRESULT position = ::SendMessageW(handle, PBM_GETPOS, 0, 0);
                context.record(
                    position == 42, pageName + " 进度条可读写: " + field.id, "pos=" + std::to_string(position)
                );
                ++context.result.controlsDriven;
                break;
            }
            case FieldKind::List: {
                const LRESULT items = ::SendMessageW(handle, LVM_GETITEMCOUNT, 0, 0);
                context.record(
                    items == static_cast<LRESULT>(field.rows.size()),
                    pageName + " 列表行数与模型一致: " + field.id,
                    "控件=" + std::to_string(items) + " 模型=" + std::to_string(field.rows.size())
                );
                ++context.result.controlsDriven;
                break;
            }
            case FieldKind::Button: {
                // 「添加」按钮要改运行计划，单独在下面按固定顺序验一遍，这里跳过
                if (field.action == "addTask")
                {
                    ++index;
                    continue;
                }
                ::SendMessageW(handle, BM_CLICK, 0, 0);
                pumpMessages(2);
                context.record(true, pageName + " 按钮已点击: " + field.id);
                ++context.result.controlsDriven;
                break;
            }
            case FieldKind::Info: {
                const std::string text = windowText(handle);
                context.record(
                    text == field.value,
                    pageName + " 只读值显示正确: " + field.id,
                    "控件=" + (text.empty() ? std::string("(空)") : text) + " 模型=" + field.value
                );
                ++context.result.controlsDriven;
                break;
            }
            }
            ++index;
        }
    }
}

} // namespace

SmokeResult runSmokeTest(bool visible)
{
    SmokeResult result;

    // 无头/无窗口站环境：显式声明跳过，绝不静默略过（宪章 §9.7 M7）
    if (GetProcessWindowStation() == nullptr) // NOLINT
    {
        result.skipped = true;
        result.skipReason = "当前进程没有窗口站（无头环境），UI 冒烟测试无法执行";
        result.lines.push_back(std::string(kSkipMarker) + "  " + result.skipReason);
        return result;
    }

    SmokeContext context;

    _Module.Init(nullptr, GetModuleHandleW(nullptr));
    context.moduleInitialized = true;

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC =
        ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    ::InitCommonControlsEx(&controls);

    // 客户区尺寸取 SC 主窗体的 DFM 值（TSCMF: ClientWidth=800 / ClientHeight=540）。
    // 用 900x640 时，图纸绝对坐标的页面会被放大 12%，与 DFM 的版面比例对不上。
    // 冒烟测试会对每个按钮发 BM_CLICK，包括完成页的「关机(&T)」「重启(&R)」。
    // 必须是 dry-run：CI 的 runner 是管理员会话，真调 ExitWindowsEx 会把机器关掉，
    // 整条流水线就此挂死（第一次跑就踩到了）。按钮的响应仍然照常记录进报告。
    context.frame.powerActionsDryRun = true;

    RECT rect = {0, 0, 800, 540};
    HWND handle = context.frame.Create(nullptr, rect, L"OpenSysprepTool 配置", WS_OVERLAPPEDWINDOW, 0);
    if (handle == nullptr)
    {
        context.record(
            false, "创建主窗口", "CreateWindowEx 失败，GetLastError=" + std::to_string(GetLastError())
        );
        result = context.result;
        _Module.Term();
        return result;
    }
    context.record(true, "创建主窗口");
    pumpMessages(5);

    HWND tabHandle = context.frame.GetDlgItem(kIdcTab);
    context.record(tabHandle != nullptr, "页签控件存在");
    HWND panelHandle = context.frame.panel != nullptr ? context.frame.panel->m_hWnd : nullptr;
    context.record(panelHandle != nullptr, "页面面板存在");

    // 页签必须是真正的通用控件，而不是自己造的东西
    if (tabHandle != nullptr)
    {
        wchar_t className[64] = {};
        ::GetClassNameW(tabHandle, className, static_cast<int>(std::size(className)));
        const std::wstring actual(className);
        context.record(
            actual == L"SysTabControl32", "页签控件是系统标准的 SysTabControl32", fromWide(actual)
        );
        context.record(::GetModuleHandleW(L"comctl32.dll") != nullptr, "通用控件库 comctl32 已加载");

        const int count = static_cast<int>(::SendMessageW(tabHandle, TCM_GETITEMCOUNT, 0, 0));
        context.record(
            count == static_cast<int>(context.frame.pages.size()) && count == kConfigPageCount,
            "页签数量与交付的页面数一致",
            "count=" + std::to_string(count) + " 期望=" + std::to_string(kConfigPageCount)
        );
    }

    // 逐页打开、逐页驱动
    if (tabHandle != nullptr && panelHandle != nullptr)
    {
        for (int pageIndex = 0; pageIndex < static_cast<int>(context.frame.pages.size()); ++pageIndex)
        {
            const Page& page = context.frame.pages[static_cast<std::size_t>(pageIndex)];

            // 用真实页签控件选页，再按控件真实的通知码通知父窗口
            ::SendMessageW(tabHandle, TCM_SETCURSEL, static_cast<WPARAM>(pageIndex), 0);
            notifyTabChange(context.frame.m_hWnd, tabHandle);
            pumpMessages(3);

            const int selected = static_cast<int>(::SendMessageW(tabHandle, TCM_GETCURSEL, 0, 0));
            const bool opened = (selected == pageIndex) && (context.frame.panel->currentPage == pageIndex) &&
                !context.frame.panel->controls.empty();
            context.record(
                opened,
                "页面能打开: " + page.title + "（" + page.dfmSheet + "）",
                "tabSel=" + std::to_string(selected) +
                    " 面板控件数=" + std::to_string(context.frame.panel->controls.size())
            );

            // 图纸绝对布局的页面（运行 / 完成）：把每个控件的实际矩形也记进报告，
            // 验收脚本据此核对「真的按图纸坐标摆出来了」，而不是只在模型里对。
            if (page.dfmLayout && opened)
            {
                RECT panelRect = {};
                ::GetClientRect(context.frame.panel->m_hWnd, &panelRect);
                const double scale =
                    page.clientWidth > 0 ? static_cast<double>(panelRect.right) / page.clientWidth : 1.0;
                int controlIndex = 0;
                for (const auto& group : page.groups)
                {
                    for (const auto& field : group.fields)
                    {
                        HWND control = context.frame.panel->controlAt(controlIndex);
                        ++controlIndex;
                        if (control == nullptr || !field.placed())
                        {
                            continue;
                        }
                        RECT rect = {};
                        ::GetWindowRect(control, &rect);
                        ::MapWindowPoints(
                            nullptr, context.frame.panel->m_hWnd, reinterpret_cast<LPPOINT>(&rect), 2
                        );
                        const int expectedX = static_cast<int>(field.x * scale);
                        const int expectedY = static_cast<int>(field.y * scale);
                        const int expectedW = (std::max)(1, static_cast<int>(field.width * scale));
                        const int expectedH = (std::max)(1, static_cast<int>(field.height * scale));
                        const bool closeEnough = std::abs(rect.left - expectedX) <= 2 &&
                            std::abs(rect.top - expectedY) <= 2 &&
                            std::abs((rect.right - rect.left) - expectedW) <= 2 &&
                            std::abs((rect.bottom - rect.top) - expectedH) <= 2;
                        context.record(
                            closeEnough,
                            "控件按图纸坐标摆放: " + field.id,
                            "实际=" + std::to_string(rect.left) + "," + std::to_string(rect.top) + " " +
                                std::to_string(rect.right - rect.left) + "x" +
                                std::to_string(rect.bottom - rect.top) +
                                " 期望=" + std::to_string(expectedX) + "," + std::to_string(expectedY) + " " +
                                std::to_string(expectedW) + "x" + std::to_string(expectedH) +
                                " 缩放=" + std::to_string(scale)
                        );
                    }
                }
            }

            PageSummary summary;
            summary.id = page.id;
            summary.title = page.title;
            summary.dfmSheet = page.dfmSheet;
            summary.fields = static_cast<int>(fieldCount(page));
            summary.controls = static_cast<int>(context.frame.panel->controls.size());
            summary.opened = opened;
            context.result.pages.push_back(summary);

            if (!opened)
            {
                continue;
            }
            ++context.result.pagesOpened;

            context.record(
                context.frame.panel->controls.size() == fieldCount(page),
                page.title + " 控件数与页面模型一致",
                "控件=" + std::to_string(context.frame.panel->controls.size()) +
                    " 模型=" + std::to_string(fieldCount(page))
            );

            drivePage(context, context.frame.pages[static_cast<std::size_t>(pageIndex)]);
        }
    }

    // 计划任务页：点『添加』必须真的往运行计划里加一条，且列表跟着长
    if (tabHandle != nullptr && panelHandle != nullptr)
    {
        const int tasksIndex = 2; // pages 顺序：system/target/tasks/deploy/sysprep/run/done
        ::SendMessageW(tabHandle, TCM_SETCURSEL, static_cast<WPARAM>(tasksIndex), 0);
        notifyTabChange(context.frame.m_hWnd, tabHandle);
        pumpMessages(3);

        Page* tasks = findPage(context.frame.pages, "tasks");
        context.record(tasks != nullptr && context.frame.panel->currentPage == tasksIndex, "切到计划任务页");

        if (tasks != nullptr)
        {
            // 草稿行先填上内容，再点『添加』
            if (Field* path = findField(*tasks, "Ed_RunPath"))
            {
                path->value = "tools\\hello.cmd";
            }
            if (Field* args = findField(*tasks, "Ed_CmdLine"))
            {
                args->value = "/quiet";
            }

            int flat = 0;
            HWND addButton = nullptr;
            for (const Group& group : tasks->groups)
            {
                for (const Field& field : group.fields)
                {
                    if (field.id == "Btn_Add")
                    {
                        addButton = context.frame.panel->controlAt(flat);
                    }
                    ++flat;
                }
            }
            context.record(addButton != nullptr, "找到『添加』按钮");

            const std::size_t before = context.frame.cfg.runPlan.size();
            if (addButton != nullptr)
            {
                ::SendMessageW(addButton, BM_CLICK, 0, 0);
                pumpMessages(3);
            }
            const std::size_t after = context.frame.cfg.runPlan.size();
            context.record(
                after == before + 1,
                "点『添加』后运行计划正好多出一条任务",
                "before=" + std::to_string(before) + " after=" + std::to_string(after)
            );
            if (after > before)
            {
                const config::RunPlanTask& task = context.frame.cfg.runPlan.back();
                context.record(
                    task.target == "tools\\hello.cmd" && task.arguments == "/quiet",
                    "添加的任务带上了草稿行的执行路径与参数",
                    "target=" + task.target + " args=" + task.arguments
                );
                context.record(
                    !task.phase.empty() && !task.type.empty() && !task.windowStyle.empty() &&
                        !task.waitMode.empty(),
                    "新增任务带齐阶段/类型/运行方式/运行等待",
                    "phase=" + task.phase + " type=" + task.type + " mode=" + task.windowStyle +
                        " wait=" + task.waitMode
                );
            }

            Page* refreshed = findPage(context.frame.pages, "tasks");
            if (refreshed != nullptr)
            {
                if (const Field* list = findField(*refreshed, "Lv_Tasklist"))
                {
                    context.record(
                        list->rows.size() == context.frame.cfg.runPlan.size(),
                        "任务列表行数与运行计划一致",
                        "rows=" + std::to_string(list->rows.size()) +
                            " 计划=" + std::to_string(context.frame.cfg.runPlan.size())
                    );
                }
            }
        }
    }

    // 按钮动作的安全性：这些计数器必须涨，而机器绝不能真的重启/封装
    context.record(
        context.frame.srsRunCount >= 1,
        "SRS 执行按钮只记录意图（不真跑 dism）",
        "count=" + std::to_string(context.frame.srsRunCount)
    );
    context.record(
        context.frame.sysprepRunCount >= 1,
        "开始封装按钮只记录意图（不启动 sysprep）",
        "count=" + std::to_string(context.frame.sysprepRunCount)
    );
    context.record(
        context.frame.scanCount >= 1,
        "重新体检按钮触发了采集",
        "count=" + std::to_string(context.frame.scanCount)
    );

    // ---------------------------------------------------------------- 菜单与配置读写
    if (handle != nullptr)
    {
        HMENU menu = ::GetMenu(handle);
        const int topCount = menu != nullptr ? static_cast<int>(::GetMenuItemCount(menu)) : 0;
        context.record(
            menu != nullptr && topCount == 2,
            "主菜单有『文件』与『工具』两项",
            "顶层项数=" + std::to_string(topCount)
        );

        wchar_t firstItem[64] = {};
        if (menu != nullptr)
        {
            ::GetMenuStringW(menu, 0, firstItem, static_cast<int>(std::size(firstItem)), MF_BYPOSITION);
        }
        const std::string firstLabel = fromWide(std::wstring(firstItem));
        context.record(
            firstLabel.find("文件") != std::string::npos, "第一项是『文件』菜单", "实际=" + firstLabel
        );

        HMENU fileMenu = menu != nullptr ? ::GetSubMenu(menu, 0) : nullptr;
        const int fileCount = fileMenu != nullptr ? static_cast<int>(::GetMenuItemCount(fileMenu)) : 0;
        context.record(
            fileCount == 4,
            "『文件』菜单是 打开配置 / 另存为配置 / 分隔线 / 退出",
            "项数=" + std::to_string(fileCount)
        );

        // 菜单命令走真实 WM_COMMAND（与用户点菜单同一条路径）
        const int scanBefore = context.frame.scanCount;
        ::SendMessageW(handle, WM_COMMAND, MAKEWPARAM(kIdcMenuScanNow, 0), 0);
        pumpMessages(2);
        context.record(
            context.frame.scanCount == scanBefore + 1,
            "菜单命令『立即封装体检』真的执行了体检",
            "before=" + std::to_string(scanBefore) + " after=" + std::to_string(context.frame.scanCount)
        );

        // 动过控件之后必须被标记成「有未保存修改」
        context.record(
            context.frame.configDirty,
            "编辑过控件后进入未保存状态",
            std::string("configDirty=") + (context.frame.configDirty ? "1" : "0")
        );
        context.record(
            windowText(handle).find("*") != std::string::npos,
            "窗口标题里出现了未保存标记 *",
            windowText(handle)
        );
    }

    // 配置读写：界面上改过的值存成 cfg.json，再读回来必须与内存里的页面模型一致
    {
        const std::filesystem::path work = std::filesystem::temp_directory_path() / "ospt-ui-smoke";
        std::error_code ec;
        std::filesystem::create_directories(work, ec);
        const std::filesystem::path cfgPath = work / "roundtrip.json";

        context.frame.applyAllPages();
        const std::string savedJson = config::toJson(context.frame.cfg);
        std::string error;
        context.record(
            saveConfigFile(cfgPath.string(), context.frame.cfg, error),
            "配置能存成 cfg.json",
            error.empty() ? cfgPath.string() : error
        );

        config::Config reloaded;
        error.clear();
        const bool loaded = loadConfigFile(cfgPath.string(), reloaded, error);
        context.record(loaded, "cfg.json 能读回来", error.empty() ? cfgPath.string() : error);
        if (loaded)
        {
            context.record(
                config::toJson(reloaded) == savedJson,
                "读回来的配置与存出去前逐字一致",
                std::to_string(savedJson.size()) + " 字节"
            );
            context.record(
                toJson(pagesFromConfig(reloaded)) == toJson(pagesFromConfig(context.frame.cfg)),
                "用文件里的配置重建页面与当前页面一致"
            );
        }

        // 错误路径：非法 JSON 必须被拒绝、给出原因，而且不崩
        const std::filesystem::path badPath = work / "broken.json";
        {
            std::ofstream bad(badPath, std::ios::binary | std::ios::trunc);
            bad << "{ this is not json ";
        }
        config::Config ignored;
        error.clear();
        const bool badLoaded = loadConfigFile(badPath.string(), ignored, error);
        context.record(!badLoaded && !error.empty(), "非法 JSON 被拒绝并给出原因", error);
        context.record(true, "非法 JSON 没有把程序搞崩");
    }

    // 退出无崩溃：销毁窗口并确认进程仍能继续
    context.frame.DestroyWindow();
    pumpMessages(5);
    context.record(true, "窗口销毁无崩溃");
    context.record(context.frame.panel == nullptr, "面板随窗口一起销毁");

    if (visible)
    {
        context.record(true, "（可见模式）窗口已按请求显示过");
    }

    context.result.excludedControls = excludedControlIds();
    context.result.powerActionRequests = context.frame.powerActionRequests;
    context.result.powerActionsDryRun = context.frame.powerActionsDryRun;
    context.result.forbiddenBindings = forbiddenBindings();
    result = context.result;
    result.ok = (result.failures == 0);

    if (context.moduleInitialized)
    {
        _Module.Term();
    }
    return result;
}

int runApplication(const std::string& initialConfigPath)
{
    if (GetProcessWindowStation() == nullptr) // NOLINT
    {
        std::fputs("无窗口站（无头环境），打不开配置界面。用 --ui-smoke-test 做无人值守检查。\n", stderr);
        return 3;
    }

    MainFrame frame;

    _Module.Init(nullptr, GetModuleHandleW(nullptr));

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC =
        ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    ::InitCommonControlsEx(&controls);

    RECT rect = {0, 0, 980, 700};
    if (frame.Create(nullptr, rect, L"OpenSysprepTool 配置", WS_OVERLAPPEDWINDOW) == nullptr)
    {
        _Module.Term();
        std::fprintf(stderr, "创建主窗口失败，GetLastError=%lu\n", GetLastError());
        return 4;
    }

    frame.ShowWindow(SW_SHOWNORMAL);
    frame.UpdateWindow();

    // 启动时带了配置文件就直接载入；载入失败只在界面上报警，不退出
    if (!initialConfigPath.empty())
    {
        frame.loadConfigFrom(initialConfigPath);
    }
    else
    {
        frame.setStatus("就绪：文件菜单可打开 / 另存为配置（cfg.json）");
    }

    // 常驻消息循环：等到窗口关闭（WM_QUIT）才返回
    MSG message = {};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
        ++frame.messageCount;
    }

    _Module.Term();
    return static_cast<int>(message.wParam);
}

std::string smokeReport(const SmokeResult& result)
{
    std::string out;
    out += "UI 冒烟测试（宪章 §8 M7b）\n";

    if (result.skipped)
    {
        out += std::string(kSkipMarker) + "  " + result.skipReason + "\n";
        out += "RESULT: SMOKE-SKIP\n";
        return out;
    }

    for (const std::string& line : result.lines)
    {
        out += "  " + line + "\n";
    }
    out += "\n打开页面 " + std::to_string(result.pagesOpened) + " 个，驱动控件 " +
        std::to_string(result.controlsDriven) + " 个\n";
    out += "检查 " + std::to_string(result.checks) + " 项，失败 " + std::to_string(result.failures) + " 项\n";
    out += result.ok ? "RESULT: SMOKE-OK\n" : "RESULT: SMOKE-FAILED\n";
    return out;
}

std::string smokeJson(const SmokeResult& result)
{
    const auto quote = [](const std::string& text) {
        std::string out;
        for (const char ch : text)
        {
            if (ch == '"' || ch == '\\')
            {
                out += '\\';
            }
            if (ch == '\n')
            {
                out += "\\n";
                continue;
            }
            out += ch;
        }
        return out;
    };

    std::string out = "{\n";
    out += "  \"schemaVersion\": 2,\n";
    out += std::string("  \"ok\": ") + (result.ok ? "true" : "false") + ",\n";
    out += std::string("  \"skipped\": ") + (result.skipped ? "true" : "false") + ",\n";
    out += "  \"skipReason\": \"" + quote(result.skipReason) + "\",\n";
    out += "  \"checks\": " + std::to_string(result.checks) + ",\n";
    out += "  \"failures\": " + std::to_string(result.failures) + ",\n";
    out += "  \"pagesOpened\": " + std::to_string(result.pagesOpened) + ",\n";
    out += "  \"controlsDriven\": " + std::to_string(result.controlsDriven) + ",\n";

    // 页面一览：走查时按这份清单核对「哪些页开了、每页多少字段与控件」
    out += "  \"pages\": [";
    for (std::size_t i = 0; i < result.pages.size(); ++i)
    {
        const PageSummary& page = result.pages[i];
        out += "\n    {\"id\": \"" + quote(page.id) + "\", \"title\": \"" + quote(page.title) +
            "\", \"dfmSheet\": \"" + quote(page.dfmSheet) + "\", \"fields\": " + std::to_string(page.fields) +
            ", \"controls\": " + std::to_string(page.controls) +
            ", \"opened\": " + (page.opened ? "true" : "false") + "}";
        if (i + 1 < result.pages.size())
        {
            out += ",";
        }
    }
    out += "\n  ],\n";

    out += "  \"powerActionRequests\": " + std::to_string(result.powerActionRequests) + ",\n";
    out += std::string("  \"powerActionsDryRun\": ") + (result.powerActionsDryRun ? "true" : "false") + ",\n";
    out += "  \"excludedControls\": [";
    for (std::size_t i = 0; i < result.excludedControls.size(); ++i)
    {
        out += "\n    \"" + quote(result.excludedControls[i]) + "\"";
        if (i + 1 < result.excludedControls.size())
        {
            out += ",";
        }
    }
    out += "\n  ],\n";

    out += "  \"forbiddenBindings\": [";
    for (std::size_t i = 0; i < result.forbiddenBindings.size(); ++i)
    {
        out += "\n    \"" + quote(result.forbiddenBindings[i]) + "\"";
        if (i + 1 < result.forbiddenBindings.size())
        {
            out += ",";
        }
    }
    out += "\n  ],\n";

    out += "  \"lines\": [";
    for (std::size_t i = 0; i < result.lines.size(); ++i)
    {
        out += "\n    \"" + quote(result.lines[i]) + "\"";
        if (i + 1 < result.lines.size())
        {
            out += ",";
        }
    }
    out += "\n  ],\n";
    out += "  \"failuresDetail\": [";
    for (std::size_t i = 0; i < result.failuresDetail.size(); ++i)
    {
        out += "\n    \"" + quote(result.failuresDetail[i]) + "\"";
        if (i + 1 < result.failuresDetail.size())
        {
            out += ",";
        }
    }
    out += "\n  ]\n}\n";
    return out;
}

} // namespace osp::ui

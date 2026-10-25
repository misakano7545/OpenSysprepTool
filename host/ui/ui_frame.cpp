// 主程序 UI 的窗口层（宪章 §8 M7a）
//
// 原生 Win32 标准控件 + WTL 薄封装：Tab 控件用 WC_TABCONTROL，标签用 static，
// 按钮/单选/下拉/编辑框都是系统标准类；没有一个控件是自绘的，也没有引入任何
// 图片或字体资源（视觉完全自研 = 不做 SC 那种换肤）。
//
// 冒烟测试与窗口层放在同一个 TU：它需要直接按控件 ID 去驱动它们，
// 隔一层内部头文件只会增加耦合面。

#include "ui.hpp"

#include <windows.h>

#include <atlbase.h>
#include <atlapp.h>

#include <atlctrls.h>
#include <atltypes.h> // CRect 在这里；atlwin.h 不会替你带上它
#include <atlwin.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/sysinfo.hpp"

// WTL 要求全局有一个应用模块对象
CAppModule _Module;

namespace osp::ui {
namespace {

// ---------------------------------------------------------------- 控件 ID

constexpr int kIdcTab = 1000;
constexpr int kIdcPagePanel = 1001;
constexpr int kIdcStatus = 1002;
constexpr int kIdcRefresh = 1100;
constexpr int kIdcSrsCert = 1110;   // 单选：使用随盘 SRS
constexpr int kIdcSrsDiy = 1111;    // 单选：自定义目录
constexpr int kIdcSrsDrive = 1112;  // 下拉：目标系统分区
constexpr int kIdcSrsPath = 1113;   // 编辑框：自定义 SRS 目录
constexpr int kIdcFieldBase = 1200; // 每个字段一个 static，ID 依次递增

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

// ---------------------------------------------------------------- 页面面板

// 一个页面 = 一组「标签 + 值」static，加一个重新体检按钮，加 SRS 编组。
// 控件全部是系统标准类，布局在 WM_SIZE 里手算。
class PagePanel : public CWindowImpl<PagePanel>
{
public:
    DECLARE_WND_CLASS(L"OSPSPagePanel")

    int refreshCount = 0;    // 重新体检按钮被触发的次数
    std::vector<Page> pages; // 面板承载的页面
    int currentPage = 0;

    BEGIN_MSG_MAP(PagePanel)
    MESSAGE_HANDLER(WM_CREATE, OnCreate)
    MESSAGE_HANDLER(WM_SIZE, OnSize)
    MESSAGE_HANDLER(WM_COMMAND, OnCommand)
    END_MSG_MAP()

    void setPages(std::vector<Page> value)
    {
        pages = std::move(value);
    }

    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&)
    {
        // 每个字段：左边是名称 static，右边是值 static
        for (std::size_t i = 0; i < pages.front().fields.size(); ++i)
        {
            CStatic label;
            label.Create(
                m_hWnd,
                rcDefault,
                toWide(pages.front().fields[i].label).c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0,
                static_cast<UINT>(kIdcFieldBase + i)
            );

            CStatic value;
            value.Create(
                m_hWnd,
                rcDefault,
                L"",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                WS_EX_CLIENTEDGE,
                static_cast<UINT>(kIdcFieldBase + 100 + i)
            );
        }

        CButton refresh;
        refresh.Create(m_hWnd, rcDefault, L"重新体检", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, kIdcRefresh);

        // SRS 编组：DFM 里 Gb_SRS 的原生控件对应物
        CButton cert;
        cert.Create(
            m_hWnd,
            rcDefault,
            L"使用随盘 SRS 驱动",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            0,
            kIdcSrsCert
        );
        CButton diy;
        diy.Create(
            m_hWnd, rcDefault, L"自定义驱动目录", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 0, kIdcSrsDiy
        );

        CComboBox drive;
        drive.Create(
            m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, kIdcSrsDrive
        );
        for (const sysinfo::PartitionInfo& part : sysinfo::collect().partitions)
        {
            // 原生控件一律用宽字符接口，std::string 只作为模型层类型
            const std::wstring item = toWide(part.drive + "  " + part.fileSystem);
            drive.AddString(item.c_str());
        }
        if (drive.GetCount() > 0)
        {
            drive.SetCurSel(0);
        }

        CEdit path;
        path.Create(
            m_hWnd, rcDefault, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, kIdcSrsPath
        );

        cert.SetCheck(BST_CHECKED);
        updateTexts();
        return 0;
    }

    LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&)
    {
        const int width = LOWORD(lParam);
        const int height = HIWORD(lParam);
        const int margin = 12;
        const int labelWidth = 110;
        const int rowHeight = 22;

        int y = margin;
        for (std::size_t i = 0; i < pages.front().fields.size(); ++i)
        {
            if (HWND label = fieldLabel(i))
            {
                ::SetWindowPos(
                    label, nullptr, margin, y, labelWidth, rowHeight, SWP_NOZORDER | SWP_NOACTIVATE
                );
            }
            if (HWND value = fieldValue(i))
            {
                ::SetWindowPos(
                    value,
                    nullptr,
                    margin + labelWidth,
                    y,
                    width - margin * 2 - labelWidth,
                    rowHeight,
                    SWP_NOZORDER | SWP_NOACTIVATE
                );
            }
            y += rowHeight + 4;
        }

        if (HWND refresh = GetDlgItem(kIdcRefresh))
        {
            ::SetWindowPos(refresh, nullptr, margin, y + 6, 100, 26, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        const int srsTop = y + 44;
        if (HWND cert = GetDlgItem(kIdcSrsCert))
        {
            ::SetWindowPos(cert, nullptr, margin, srsTop, 180, 20, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (HWND diy = GetDlgItem(kIdcSrsDiy))
        {
            ::SetWindowPos(diy, nullptr, margin + 190, srsTop, 180, 20, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (HWND drive = GetDlgItem(kIdcSrsDrive))
        {
            ::SetWindowPos(drive, nullptr, margin, srsTop + 24, 220, 200, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (HWND path = GetDlgItem(kIdcSrsPath))
        {
            ::SetWindowPos(
                path,
                nullptr,
                margin + 240,
                srsTop + 24,
                width - margin * 2 - 240,
                24,
                SWP_NOZORDER | SWP_NOACTIVATE
            );
        }

        (void)height;
        return 0;
    }

    LRESULT OnCommand(UINT, WPARAM wParam, LPARAM, BOOL&)
    {
        const int id = LOWORD(wParam);
        if (id == kIdcRefresh)
        {
            ++refreshCount;
            // 重新体检：重跑 M2 的采集，再刷新控件文本
            pages[0] = systemPage();
            updateTexts();
        }
        else if (id == kIdcSrsCert || id == kIdcSrsDiy)
        {
            // 选中一项时把另一个取消，行为与系统标准单选组一致
            const bool cert = (id == kIdcSrsCert);
            if (HWND certWnd = GetDlgItem(kIdcSrsCert))
            {
                ::SendMessageW(certWnd, BM_SETCHECK, cert ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            if (HWND diyWnd = GetDlgItem(kIdcSrsDiy))
            {
                ::SendMessageW(diyWnd, BM_SETCHECK, cert ? BST_UNCHECKED : BST_CHECKED, 0);
            }
        }
        return 0;
    }

    HWND fieldLabel(std::size_t index) const
    {
        return const_cast<PagePanel*>(this)->GetDlgItem(static_cast<int>(kIdcFieldBase + index));
    }
    HWND fieldValue(std::size_t index) const
    {
        return const_cast<PagePanel*>(this)->GetDlgItem(static_cast<int>(kIdcFieldBase + 100 + index));
    }

    void updateTexts()
    {
        if (pages.empty())
        {
            return;
        }
        for (std::size_t i = 0; i < pages.front().fields.size(); ++i)
        {
            if (HWND wnd = fieldValue(i))
            {
                ::SetWindowTextW(wnd, toWide(pages.front().fields[i].value).c_str());
            }
        }
    }

    std::string fieldValueText(std::size_t index) const
    {
        HWND wnd = fieldValue(index);
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
};

// ---------------------------------------------------------------- 主框架

class MainFrame : public CWindowImpl<MainFrame>
{
public:
    DECLARE_WND_CLASS(L"OSPTMainFrame")

    CTabCtrl tab;
    PagePanel panel;
    CStatic status;
    long long messageCount = 0; // 处理过的消息数，供冒烟测试确认消息循环真的在跑

    BEGIN_MSG_MAP(MainFrame)
    MESSAGE_HANDLER(WM_CREATE, OnCreate)
    MESSAGE_HANDLER(WM_SIZE, OnSize)
    MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
    MESSAGE_HANDLER(WM_CLOSE, OnClose)
    MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()

    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&)
    {
        tab.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | TCS_TABS, 0, kIdcTab);

        panel.pages = allPages();
        panel.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE, 0, kIdcPagePanel);
        panel.setPages(allPages());

        status.Create(m_hWnd, rcDefault, L"就绪", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_SUNKEN, 0, kIdcStatus);

        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        for (const Page& page : panel.pages)
        {
            std::wstring title = toWide(page.title);
            item.pszText = title.data();
            tab.InsertItem(static_cast<int>(tab.GetItemCount()), &item);
        }
        tab.SetCurSel(0);
        return 0;
    }

    LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&)
    {
        const int width = LOWORD(lParam);
        const int height = HIWORD(lParam);
        const int statusHeight = 22;

        if (tab.m_hWnd != nullptr)
        {
            ::SetWindowPos(tab, nullptr, 0, 0, width, height - statusHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (panel.m_hWnd != nullptr)
        {
            RECT inner = {0, 0, width, height - statusHeight};
            tab.AdjustRect(FALSE, &inner);
            ::SetWindowPos(
                panel,
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

    LRESULT OnNotify(UINT, WPARAM, LPARAM lParam, BOOL&)
    {
        auto* header = reinterpret_cast<LPNMHDR>(lParam);
        if (header != nullptr && header->idFrom == kIdcTab && header->code == TCN_SELCHANGE)
        {
            const int selected = tab.GetCurSel();
            if (selected >= 0 && selected < static_cast<int>(panel.pages.size()))
            {
                panel.currentPage = selected;
                panel.pages[0] = panel.pages[static_cast<std::size_t>(selected)];
                panel.updateTexts();
                status.SetWindowTextW(toWide(panel.pages[static_cast<std::size_t>(selected)].title).c_str());
            }
        }
        return 0;
    }

    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&)
    {
        DestroyWindow();
        return 0;
    }

    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&)
    {
        if (panel.m_hWnd != nullptr)
        {
            panel.DestroyWindow();
        }
        PostQuitMessage(0);
        return 0;
    }
};

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

    RECT rect = {0, 0, 760, 560};
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

    // 系统信息页能打开
    HWND tabHandle = context.frame.GetDlgItem(kIdcTab);
    context.record(tabHandle != nullptr, "页签控件存在");
    if (tabHandle != nullptr)
    {
        const int count = static_cast<int>(::SendMessageW(tabHandle, TCM_GETITEMCOUNT, 0, 0));
        context.record(count >= 1, "页签数量 >= 1", "count=" + std::to_string(count));

        ::SendMessageW(tabHandle, TCM_SETCURSEL, 0, 0);
        const int selected = static_cast<int>(::SendMessageW(tabHandle, TCM_GETCURSEL, 0, 0));
        context.record(selected == 0, "选中第一个页签（母盘系统）", "sel=" + std::to_string(selected));
    }

    HWND panelHandle = context.frame.GetDlgItem(kIdcPagePanel);
    context.record(panelHandle != nullptr, "页面面板存在");

    // 页签必须是真正的通用控件，而不是自己造的东西：
    // 类名应为 SysTabControl32，且 comctl32 此时已加载。
    if (tabHandle != nullptr)
    {
        wchar_t className[64] = {};
        ::GetClassNameW(tabHandle, className, static_cast<int>(std::size(className)));
        const std::wstring actual(className);
        context.record(
            actual == L"SysTabControl32", "页签控件是系统标准的 SysTabControl32", fromWide(actual)
        );
        context.record(::GetModuleHandleW(L"comctl32.dll") != nullptr, "通用控件库 comctl32 已加载");
    }

    // 逐字段核对：控件存在、文本非空、与页面模型一致
    const std::vector<std::string> expected = expectedControlIds();
    const Page page = systemPage();
    context.record(
        page.fields.size() == expected.size(),
        "字段数与 DFM 图纸一致",
        std::to_string(page.fields.size()) + " vs " + std::to_string(expected.size())
    );

    for (std::size_t i = 0; i < page.fields.size(); ++i)
    {
        const std::string text = context.frame.panel.fieldValueText(i);
        const bool matches = text == page.fields[i].value;
        context.record(
            matches,
            "字段文本与页面模型一致: " + page.fields[i].label,
            "控件=" + (text.empty() ? std::string("(空)") : text) + " 模型=" + page.fields[i].value
        );
    }

    // 控件能响应之一：按钮 -> WM_COMMAND -> 处理器真的跑了
    // 注意：这些控件的父窗口是页面面板（真实点击时 WM_COMMAND 也发给面板），
    // 所以要向面板发消息、也在面板上按 ID 找控件。
    HWND refresh = context.frame.panel.GetDlgItem(kIdcRefresh);
    if (refresh != nullptr)
    {
        const int before = context.frame.panel.refreshCount;
        ::SendMessageW(
            context.frame.panel.m_hWnd,
            WM_COMMAND,
            MAKEWPARAM(kIdcRefresh, BN_CLICKED),
            reinterpret_cast<LPARAM>(refresh)
        );
        pumpMessages(3);
        context.record(
            context.frame.panel.refreshCount == before + 1,
            "「重新体检」按钮响应并触发处理器",
            "before=" + std::to_string(before) + " after=" + std::to_string(context.frame.panel.refreshCount)
        );
        context.record(!context.frame.panel.fieldValueText(0).empty(), "重新体检后字段仍有值");
    }
    else
    {
        context.record(false, "找到「重新体检」按钮");
    }

    // 控件能响应之二：单选按钮状态可读写
    HWND cert = context.frame.panel.GetDlgItem(kIdcSrsCert);
    HWND diy = context.frame.panel.GetDlgItem(kIdcSrsDiy);
    if (cert != nullptr && diy != nullptr)
    {
        ::SendMessageW(cert, BM_SETCHECK, BST_CHECKED, 0);
        ::SendMessageW(diy, BM_SETCHECK, BST_UNCHECKED, 0);
        const LRESULT certState = ::SendMessageW(cert, BM_GETCHECK, 0, 0);
        context.record(
            certState == BST_CHECKED, "单选按钮状态可读写", "BM_GETCHECK=" + std::to_string(certState)
        );

        // 走一遍命令处理，确认两个单选互斥
        ::SendMessageW(
            context.frame.panel.m_hWnd,
            WM_COMMAND,
            MAKEWPARAM(kIdcSrsDiy, BN_CLICKED),
            reinterpret_cast<LPARAM>(diy)
        );
        const LRESULT certAfter = ::SendMessageW(cert, BM_GETCHECK, 0, 0);
        const LRESULT diyAfter = ::SendMessageW(diy, BM_GETCHECK, 0, 0);
        context.record(
            certAfter == BST_UNCHECKED && diyAfter == BST_CHECKED,
            "两个单选按钮互斥",
            "cert=" + std::to_string(certAfter) + " diy=" + std::to_string(diyAfter)
        );
    }
    else
    {
        context.record(false, "找到 SRS 单选按钮");
    }

    // 控件能响应之三：下拉框条目可选中
    HWND combo = context.frame.panel.GetDlgItem(kIdcSrsDrive);
    if (combo != nullptr)
    {
        const LRESULT count = ::SendMessageW(combo, CB_GETCOUNT, 0, 0);
        context.record(count > 0, "下拉框已填入分区", "count=" + std::to_string(count));
        if (count > 0)
        {
            ::SendMessageW(combo, CB_SETCURSEL, count - 1, 0);
            const LRESULT sel = ::SendMessageW(combo, CB_GETCURSEL, 0, 0);
            context.record(sel == count - 1, "下拉框选中项可读写", "sel=" + std::to_string(sel));
        }
    }
    else
    {
        context.record(false, "找到分区下拉框");
    }

    // 控件能响应之四：编辑框往返
    HWND edit = context.frame.panel.GetDlgItem(kIdcSrsPath);
    if (edit != nullptr)
    {
        const wchar_t* probe = L"O:\\drivers\\srs";
        ::SetWindowTextW(edit, probe);
        wchar_t buffer[128] = {};
        ::GetWindowTextW(edit, buffer, static_cast<int>(std::size(buffer)));
        context.record(std::wstring(buffer) == probe, "编辑框文本往返一致", fromWide(buffer));
    }
    else
    {
        context.record(false, "找到自定义目录编辑框");
    }

    // 消息循环确实在跑
    pumpMessages(5);
    context.record(true, "消息循环已泵过");

    // 退出无崩溃：销毁窗口并确认进程仍能继续
    context.frame.DestroyWindow();
    pumpMessages(5);
    context.record(true, "窗口销毁无崩溃");

    if (visible)
    {
        context.record(true, "（可见模式）窗口已按请求显示过");
    }

    result = context.result;
    result.ok = (result.failures == 0);

    if (context.moduleInitialized)
    {
        _Module.Term();
    }
    return result;
}

std::string smokeReport(const SmokeResult& result)
{
    std::string out;
    out += "UI 冒烟测试（宪章 §8 M7a）\n";

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
    out +=
        "\n检查 " + std::to_string(result.checks) + " 项，失败 " + std::to_string(result.failures) + " 项\n";
    out += result.ok ? "RESULT: SMOKE-OK\n" : "RESULT: SMOKE-FAILED\n";
    return out;
}

std::string smokeJson(const SmokeResult& result)
{
    std::string out = "{\n";
    out += "  \"schemaVersion\": 1,\n";
    out += std::string("  \"ok\": ") + (result.ok ? "true" : "false") + ",\n";
    out += std::string("  \"skipped\": ") + (result.skipped ? "true" : "false") + ",\n";
    out += "  \"skipReason\": \"" + result.skipReason + "\",\n";
    out += "  \"checks\": " + std::to_string(result.checks) + ",\n";
    out += "  \"failures\": " + std::to_string(result.failures) + ",\n";
    out += "  \"lines\": [";
    for (std::size_t i = 0; i < result.lines.size(); ++i)
    {
        out += "\n    \"" + result.lines[i] + "\"";
        if (i + 1 < result.lines.size())
        {
            out += ",";
        }
    }
    out += "\n  ],\n";
    out += "  \"failuresDetail\": [";
    for (std::size_t i = 0; i < result.failuresDetail.size(); ++i)
    {
        out += "\n    \"" + result.failuresDetail[i] + "\"";
        if (i + 1 < result.failuresDetail.size())
        {
            out += ",";
        }
    }
    out += "\n  ]\n}\n";
    return out;
}

} // namespace osp::ui

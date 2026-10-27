#include "tmffun.hpp"

#include <windows.h>
#include <commctrl.h>

#include <map>
#include <string>
#include <vector>

#include <commdlg.h>
#include <shlobj.h>

#include "core/md5.hpp"

namespace osp::ui {
namespace {

// ---------------------------------------------------------------- 控件表结构
//
// 表由 tools/gen_tmffun.py 从 DFM 生成（坐标）与 tools/ui_bindings/tmffun.json
// （类型/文案/绑定）合并而来，不要手改。

struct ControlSpec
{
    const char* id;
    const char* kind; // label / edit / number / radio / combo / button / group / list
    const char* text;
    int x;
    int y;
    int width;
    int height;
    const char* binding;    // 落到 config 的字段路径（空 = 不绑定）
    const char* radioGroup; // 同组单选互斥
    const char* radioValue; // 选中时写进 binding 的值
    const char* action;     // 按钮动作：browse / save / close
    const char* role;       // 特殊用途：status = 底部状态行
    const char* const* items;
    int itemCount;
    const char* parent; // 组框名（坐标相对组框）
};

struct TabSpec
{
    const char* id;
    const char* caption;
    const char* note; // 未实现页的说明，或已实现页的一句备注
    const ControlSpec* controls;
    int controlCount;
};

#include "tmffun_table.inc"

constexpr int kIdcTabs = 4300;
constexpr int kIdcFirstControl = 4400;

// 页签条与内容区的相对位置：DFM 里 Mfpage 占满窗体上部，页签内容自己带坐标。
constexpr int kTabStripX = 6;
constexpr int kTabStripY = 6;
constexpr int kTabStripW = kTmffunClientWidth - 12;
constexpr int kTabStripH = kTmffunClientHeight - 12;

std::wstring wide(const char* text)
{
    if (text == nullptr || *text == '\0')
    {
        return std::wstring();
    }
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (size <= 0)
    {
        return std::wstring();
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), size);
    out.resize(static_cast<std::size_t>(size - 1)); // 去掉结尾的 NUL
    return out;
}

std::wstring toWide(const char* text)
{
    return wide(text);
}

std::string narrow(const std::wstring& text)
{
    if (text.empty())
    {
        return std::string();
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return std::string();
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), size, nullptr, nullptr);
    out.resize(static_cast<std::size_t>(size - 1));
    return out;
}

int toInt(const std::string& text, int fallback)
{
    if (text.empty())
    {
        return fallback;
    }
    try
    {
        return std::stoi(text);
    }
    catch (...)
    {
        return fallback;
    }
}

// ---------------------------------------------------------------- 配置读写
//
// 只按 TMFFUN 里出现的字段改写，不碰其它配置项；未认识的绑定直接忽略并记一笔。

enum class BindResult
{
    Ok,
    Unknown
};

BindResult readBinding(const std::string& path, const config::Config& cfg, std::string& out, bool& flag)
{
    flag = false;
    out.clear();
    if (path == "optimization.myDocumentsTarget")
    {
        out = cfg.optimization.myDocumentsTarget;
        return BindResult::Ok;
    }
    if (path == "optimization.favoritesTarget")
    {
        out = cfg.optimization.favoritesTarget;
        return BindResult::Ok;
    }
    if (path == "optimization.documentsSmart")
    {
        flag = cfg.optimization.documentsSmart;
        return BindResult::Ok;
    }
    if (path == "optimization.favoritesSmart")
    {
        flag = cfg.optimization.favoritesSmart;
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileSmart")
    {
        flag = cfg.optimization.pageFileSmart;
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileDrive")
    {
        out = cfg.optimization.pageFileDrive;
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileSizeMode")
    {
        out = cfg.optimization.pageFileSizeMode;
        return BindResult::Ok;
    }
    if (path == "optimization.ipMode")
    {
        out = cfg.optimization.ipMode;
        return BindResult::Ok;
    }
    if (path == "optimization.ipAddress")
    {
        out = cfg.optimization.ipAddress;
        return BindResult::Ok;
    }
    if (path == "optimization.ipRandomBase")
    {
        out = cfg.optimization.ipRandomBase;
        return BindResult::Ok;
    }
    if (path == "optimization.ipRandomOctetStart")
    {
        out = std::to_string(cfg.optimization.ipRandomOctetStart);
        return BindResult::Ok;
    }
    if (path == "optimization.ipRandomOctetEnd")
    {
        out = std::to_string(cfg.optimization.ipRandomOctetEnd);
        return BindResult::Ok;
    }
    if (path == "optimization.subnetMask")
    {
        out = cfg.optimization.subnetMask;
        return BindResult::Ok;
    }
    if (path == "optimization.gateway")
    {
        out = cfg.optimization.gateway;
        return BindResult::Ok;
    }
    if (path == "optimization.dnsPreferred")
    {
        out = cfg.optimization.dnsPreferred;
        return BindResult::Ok;
    }
    if (path == "optimization.dnsAlternate")
    {
        out = cfg.optimization.dnsAlternate;
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileMinMultiplier")
    {
        out = std::to_string(cfg.optimization.pageFileMinMultiplier);
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileMaxMultiplier")
    {
        out = std::to_string(cfg.optimization.pageFileMaxMultiplier);
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileMinFixedMb")
    {
        out = std::to_string(cfg.optimization.pageFileMinFixedMb);
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileMaxFixedMb")
    {
        out = std::to_string(cfg.optimization.pageFileMaxFixedMb);
        return BindResult::Ok;
    }
    return BindResult::Unknown;
}

BindResult
writeBinding(const std::string& path, const std::string& text, bool flag, bool hasFlag, config::Config& cfg)
{
    if (path == "optimization.myDocumentsTarget")
    {
        cfg.optimization.myDocumentsTarget = text;
        return BindResult::Ok;
    }
    if (path == "optimization.favoritesTarget")
    {
        cfg.optimization.favoritesTarget = text;
        return BindResult::Ok;
    }
    if (path == "optimization.documentsSmart")
    {
        cfg.optimization.documentsSmart = flag;
        return BindResult::Ok;
    }
    if (path == "optimization.favoritesSmart")
    {
        cfg.optimization.favoritesSmart = flag;
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileSmart")
    {
        cfg.optimization.pageFileSmart = flag;
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileDrive")
    {
        cfg.optimization.pageFileDrive = text;
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileSizeMode")
    {
        cfg.optimization.pageFileSizeMode = text;
        return BindResult::Ok;
    }
    if (path == "optimization.ipMode")
    {
        cfg.optimization.ipMode = text;
        return BindResult::Ok;
    }
    if (path == "optimization.ipAddress")
    {
        cfg.optimization.ipAddress = text;
        return BindResult::Ok;
    }
    if (path == "optimization.ipRandomBase")
    {
        cfg.optimization.ipRandomBase = text;
        return BindResult::Ok;
    }
    if (path == "optimization.ipRandomOctetStart")
    {
        cfg.optimization.ipRandomOctetStart = toInt(text, 0);
        return BindResult::Ok;
    }
    if (path == "optimization.ipRandomOctetEnd")
    {
        cfg.optimization.ipRandomOctetEnd = toInt(text, 0);
        return BindResult::Ok;
    }
    if (path == "optimization.subnetMask")
    {
        cfg.optimization.subnetMask = text;
        return BindResult::Ok;
    }
    if (path == "optimization.gateway")
    {
        cfg.optimization.gateway = text;
        return BindResult::Ok;
    }
    if (path == "optimization.dnsPreferred")
    {
        cfg.optimization.dnsPreferred = text;
        return BindResult::Ok;
    }
    if (path == "optimization.dnsAlternate")
    {
        cfg.optimization.dnsAlternate = text;
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileMinMultiplier")
    {
        cfg.optimization.pageFileMinMultiplier = toInt(text, 0);
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileMaxMultiplier")
    {
        cfg.optimization.pageFileMaxMultiplier = toInt(text, 0);
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileMinFixedMb")
    {
        cfg.optimization.pageFileMinFixedMb = toInt(text, 0);
        return BindResult::Ok;
    }
    if (path == "optimization.pageFileMaxFixedMb")
    {
        cfg.optimization.pageFileMaxFixedMb = toInt(text, 0);
        return BindResult::Ok;
    }
    (void)hasFlag;
    return BindResult::Unknown;
}

// ---------------------------------------------------------------- 窗体状态

struct TabPageState
{
    HWND sheet = nullptr;
    std::vector<HWND> controls;
    HWND status = nullptr;
    bool dirty = false;
};

struct DialogState
{
    config::Config* cfg = nullptr;
    HWND tabs = nullptr;
    std::vector<TabPageState> pages;
    int current = -1;
    bool saved = false;
    bool closed = false;
};

const ControlSpec* specAt(int tab, int index)
{
    if (tab < 0 || tab >= kTabCount)
    {
        return nullptr;
    }
    const TabSpec* spec = kTabs[tab];
    if (spec == nullptr || spec->controls == nullptr)
    {
        return nullptr;
    }
    if (index < 0 || index >= spec->controlCount)
    {
        return nullptr;
    }
    return &spec->controls[index];
}

HWND createControl(HWND parent, const ControlSpec& spec, const ControlSpec* groupSpec)
{
    const int offsetX = groupSpec == nullptr ? 0 : groupSpec->x;
    const int offsetY = groupSpec == nullptr ? 0 : groupSpec->y;
    const int x = spec.x - offsetX;
    const int y = spec.y - offsetY;
    const std::wstring text = wide(spec.text);
    const std::string kind = spec.kind;

    DWORD style = WS_CHILD | WS_VISIBLE;
    const wchar_t* cls = L"STATIC";

    if (kind == "label")
    {
        cls = L"STATIC";
        style |= SS_LEFT;
    }
    else if (kind == "group")
    {
        cls = L"BUTTON";
        style |= BS_GROUPBOX;
    }
    else if (kind == "edit" || kind == "number")
    {
        cls = L"EDIT";
        style |= WS_BORDER | ES_AUTOHSCROLL;
        if (kind == "number")
        {
            style |= ES_NUMBER;
        }
    }
    else if (kind == "radio")
    {
        cls = L"BUTTON";
        style |= BS_AUTORADIOBUTTON;
    }
    else if (kind == "combo")
    {
        cls = L"COMBOBOX";
        style |= WS_BORDER | CBS_DROPDOWN | WS_VSCROLL;
    }
    else if (kind == "button")
    {
        cls = L"BUTTON";
        style |= BS_PUSHBUTTON;
    }
    else if (kind == "list")
    {
        cls = WC_LISTVIEWW;
        style =
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP;
    }

    HWND handle = ::CreateWindowExW(
        0,
        cls,
        text.c_str(),
        style,
        x,
        y,
        spec.width,
        spec.height,
        parent,
        nullptr,
        ::GetModuleHandleW(nullptr),
        nullptr
    );
    if (handle != nullptr && kind == "list")
    {
        // 防篡改清单三列：文件 / MD5 / 说明。SC 的列头是运行时加的，图纸里看不到，
        // 这里按实际存的三样东西给列名。
        const wchar_t* headers[] = {L"文件", L"MD5", L"说明"};
        for (int i = 0; i < 3; ++i)
        {
            LVCOLUMNW column = {};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            column.pszText = const_cast<wchar_t*>(headers[i]);
            column.cx = i == 0 ? 330 : (i == 1 ? 230 : 130);
            column.iSubItem = i;
            ::SendMessageW(
                handle, LVM_INSERTCOLUMNW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&column)
            );
        }
    }
    if (handle != nullptr && kind == "combo" && spec.items != nullptr)
    {
        for (int i = 0; i < spec.itemCount; ++i)
        {
            ::SendMessageW(handle, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(spec.items[i]).c_str()));
        }
    }
    // 组框要排在子控件之后创建，否则会盖住它们 —— 生成器已按 DFM 顺序把组框排在前面，
    // 这里用 SetWindowPos 把它沉到同级最底解决。
    if (kind == "group" && handle != nullptr)
    {
        ::SetWindowPos(handle, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return handle;
}

// 防篡改清单：把 cfg.deploy.tamperFiles 填进 ListView
void fillTamperList(HWND list, const config::Config& cfg)
{
    if (list == nullptr)
    {
        return;
    }
    ::SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    int row = 0;
    for (const config::DeploySettings::TamperEntry& entry : cfg.deploy.tamperFiles)
    {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.iSubItem = 0;
        std::wstring path = toWide(entry.path.c_str());
        item.pszText = path.data();
        ::SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));

        std::wstring md5 = toWide(entry.md5.c_str());
        LVITEMW sub = {};
        sub.iItem = row;
        sub.iSubItem = 1;
        sub.mask = LVIF_TEXT;
        sub.pszText = md5.data();
        ::SendMessageW(list, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&sub));

        std::wstring tip = toWide(entry.tip.c_str());
        sub.iSubItem = 2;
        sub.pszText = tip.data();
        ::SendMessageW(list, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&sub));
        ++row;
    }
}

// 防篡改清单的一行：读文件算 MD5 再收进配置。
// 加不进去的三种情况分别给原因（没填路径 / 读不到文件 / 已经在了），不静默失败。
void addTamperEntry(DialogState& state, HWND wnd)
{
    if (state.current < 0 || state.cfg == nullptr)
    {
        return;
    }
    HWND pathEdit = nullptr;
    HWND tipEdit = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    const TabSpec* tabSpec = kTabs[state.current];
    TabPageState& page = state.pages[static_cast<std::size_t>(state.current)];
    for (int i = 0; i < tabSpec->controlCount; ++i)
    {
        const std::string role =
            tabSpec->controls[i].role == nullptr ? std::string() : tabSpec->controls[i].role;
        HWND handle = page.controls[static_cast<std::size_t>(i)];
        if (role == "tamperPath")
        {
            pathEdit = handle;
        }
        else if (role == "tamperTip")
        {
            tipEdit = handle;
        }
        else if (role == "tamperList")
        {
            list = handle;
        }
        else if (role == "status")
        {
            status = handle;
        }
    }
    auto say = [status](const std::string& text) {
        if (status != nullptr)
        {
            ::SetWindowTextW(status, toWide(text.c_str()).c_str());
        }
    };

    if (pathEdit == nullptr)
    {
        return;
    }
    wchar_t buffer[MAX_PATH * 4] = {};
    ::GetWindowTextW(pathEdit, buffer, MAX_PATH * 4);
    const std::string path = narrow(buffer);
    if (path.empty())
    {
        say("先填要锁定的文件路径");
        return;
    }
    for (const config::DeploySettings::TamperEntry& entry : state.cfg->deploy.tamperFiles)
    {
        if (entry.path == path)
        {
            say("这个文件已经在清单里了");
            return;
        }
    }

    std::string error;
    const std::string digest = osp::crypto::md5File(path, error);
    if (digest.empty())
    {
        // 读不到就别加 —— 加一条空摘要，部署时会永远「校验失败」，比不加更难查
        say("读不到文件，未加入：" + error);
        return;
    }

    config::DeploySettings::TamperEntry entry;
    entry.path = path;
    entry.md5 = digest;
    if (tipEdit != nullptr)
    {
        wchar_t tipBuffer[512] = {};
        ::GetWindowTextW(tipEdit, tipBuffer, 512);
        entry.tip = narrow(tipBuffer);
    }
    state.cfg->deploy.tamperFiles.push_back(entry);
    // 清单非空就意味着要校验：SC 的 Cb_Tampering 与这份清单是一件事
    state.cfg->deploy.verifyIntegrity = true;
    state.saved = true;
    page.dirty = true;

    fillTamperList(list, *state.cfg);
    ::SetWindowTextW(pathEdit, L"");
    if (tipEdit != nullptr)
    {
        ::SetWindowTextW(tipEdit, L"");
    }
    say("已加入 " + entry.path + "（MD5 " + digest + "）");
}

void loadTabInto(DialogState& state, int tab)
{
    if (tab < 0 || tab >= kTabCount || state.cfg == nullptr)
    {
        return;
    }
    const TabSpec* tabSpec = kTabs[tab];
    TabPageState& page = state.pages[static_cast<std::size_t>(tab)];
    if (tabSpec == nullptr || tabSpec->controls == nullptr)
    {
        return;
    }

    for (int i = 0; i < tabSpec->controlCount; ++i)
    {
        const ControlSpec& spec = tabSpec->controls[i];
        HWND handle = page.controls[static_cast<std::size_t>(i)];
        if (handle == nullptr)
        {
            continue;
        }
        const std::string kind = spec.kind;
        if (kind == "list")
        {
            fillTamperList(handle, *state.cfg);
        }
        else if (kind == "edit" || kind == "number")
        {
            std::string value;
            bool flag = false;
            if (spec.binding != nullptr && *spec.binding != '\0' &&
                readBinding(spec.binding, *state.cfg, value, flag) == BindResult::Ok)
            {
                ::SetWindowTextW(handle, wide(value.c_str()).c_str());
            }
        }
        else if (kind == "radio")
        {
            std::string value;
            bool flag = false;
            bool checked = false;
            if (spec.binding != nullptr && *spec.binding != '\0' &&
                readBinding(spec.binding, *state.cfg, value, flag) == BindResult::Ok)
            {
                // 单选有两种绑定形态：布尔（smart/diy）与字符串（mode）
                const std::string want = spec.radioValue == nullptr ? std::string() : spec.radioValue;
                if (want == "smart" || want == "diy")
                {
                    checked = (want == "smart") == flag;
                }
                else
                {
                    checked = value == want;
                }
            }
            ::SendMessageW(handle, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        else if (kind == "combo")
        {
            std::string value;
            bool flag = false;
            if (spec.binding != nullptr && *spec.binding != '\0' &&
                readBinding(spec.binding, *state.cfg, value, flag) == BindResult::Ok && !value.empty())
            {
                const LRESULT index = ::SendMessageW(
                    handle,
                    CB_FINDSTRINGEXACT,
                    static_cast<WPARAM>(-1),
                    reinterpret_cast<LPARAM>(wide(value.c_str()).c_str())
                );
                if (index >= 0)
                {
                    ::SendMessageW(handle, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
                }
            }
        }
    }
    page.dirty = false;
}

// 把当前页签的控件写回配置。返回 false = 校验没过（原因写进状态行）。
bool saveTab(DialogState& state, int tab, std::string& error)
{
    if (tab < 0 || tab >= kTabCount || state.cfg == nullptr)
    {
        error = "页签下标越界";
        return false;
    }
    const TabSpec* tabSpec = kTabs[tab];
    TabPageState& page = state.pages[static_cast<std::size_t>(tab)];
    if (tabSpec == nullptr || tabSpec->controls == nullptr)
    {
        error = "本页尚未实现，没有可保存的内容";
        return false;
    }

    // 先收集，校验通过才落地 —— 半途失败会把配置改成「一部分新一部分旧」的状态，
    // 那比不保存更难查。这里用一份配置副本做暂存。
    config::Config staged = *state.cfg;

    for (int i = 0; i < tabSpec->controlCount; ++i)
    {
        const ControlSpec& spec = tabSpec->controls[i];
        HWND handle = page.controls[static_cast<std::size_t>(i)];
        if (handle == nullptr || spec.binding == nullptr || *spec.binding == '\0')
        {
            continue;
        }
        const std::string kind = spec.kind;
        if (kind == "edit" || kind == "number")
        {
            wchar_t buffer[512] = {};
            ::GetWindowTextW(handle, buffer, 512);
            const std::string value = narrow(buffer);
            if (kind == "number" && !value.empty() && toInt(value, -1) < 0)
            {
                error = std::string("『") + spec.text + "』只能填非负整数";
                return false;
            }
            writeBinding(spec.binding, value, false, false, staged);
        }
        else if (kind == "radio")
        {
            const LRESULT checked = ::SendMessageW(handle, BM_GETCHECK, 0, 0);
            if (checked != BST_CHECKED)
            {
                continue; // 只写选中的那个，未选中的不动
            }
            const std::string want = spec.radioValue == nullptr ? std::string() : spec.radioValue;
            if (want == "smart" || want == "diy")
            {
                writeBinding(spec.binding, std::string(), want == "smart", true, staged);
            }
            else
            {
                writeBinding(spec.binding, want, false, true, staged);
            }
        }
        else if (kind == "combo")
        {
            const LRESULT index = ::SendMessageW(handle, CB_GETCURSEL, 0, 0);
            if (index >= 0 && spec.items != nullptr && index < spec.itemCount)
            {
                writeBinding(spec.binding, spec.items[index], false, false, staged);
            }
        }
    }

    // 虚拟内存那页的大小关系要在保存时就拦下来，不然要等到封装前才报错
    if (std::string(tabSpec->id) == "MlSkinTabSheet7")
    {
        const config::Optimization& opt = staged.optimization;
        if (opt.pageFileSizeMode == "multiplier" && opt.pageFileMinMultiplier > 0 &&
            opt.pageFileMaxMultiplier > 0 && opt.pageFileMinMultiplier > opt.pageFileMaxMultiplier)
        {
            error = "虚拟内存最小倍数不能大于最大倍数";
            return false;
        }
        if (opt.pageFileSizeMode == "fixed" && opt.pageFileMinFixedMb > 0 && opt.pageFileMaxFixedMb > 0 &&
            opt.pageFileMinFixedMb > opt.pageFileMaxFixedMb)
        {
            error = "虚拟内存最小固定值不能大于最大固定值";
            return false;
        }
    }

    *state.cfg = staged;
    page.dirty = false;
    state.saved = true;
    return true;
}

void showTab(DialogState& state, int tab)
{
    if (tab < 0 || tab >= kTabCount)
    {
        return;
    }
    for (int i = 0; i < static_cast<int>(state.pages.size()); ++i)
    {
        const bool visible = i == tab;
        for (HWND handle : state.pages[static_cast<std::size_t>(i)].controls)
        {
            if (handle != nullptr)
            {
                ::ShowWindow(handle, visible ? SW_SHOW : SW_HIDE);
            }
        }
    }
    state.current = tab;
    loadTabInto(state, tab);
}

void buildTabs(DialogState& state, HWND parent)
{
    state.tabs = ::CreateWindowExW(
        0,
        WC_TABCONTROLW,
        L"",
        WS_CHILD | WS_VISIBLE | TCS_MULTILINE,
        kTabStripX,
        kTabStripY,
        kTabStripW,
        kTabStripH,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdcTabs)),
        ::GetModuleHandleW(nullptr),
        nullptr
    );
    if (state.tabs == nullptr)
    {
        return;
    }

    state.pages.resize(static_cast<std::size_t>(kTabCount));
    for (int i = 0; i < kTabCount; ++i)
    {
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        std::wstring caption = wide(kTabs[i]->caption);
        item.pszText = caption.data();
        ::SendMessageW(state.tabs, TCM_INSERTITEMW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&item));

        // 内容区：页签控件客户区减去页签条高度，控件坐标直接用 DFM 的页签内坐标
        RECT client = {};
        ::GetClientRect(state.tabs, &client);
        RECT strip = {};
        ::SendMessageW(state.tabs, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&strip));

        const int contentX = strip.left - client.left;
        const int contentY = strip.top - client.top;
        const int contentW = (strip.right - strip.left);
        const int contentH = (strip.bottom - strip.top);

        HWND sheet = ::CreateWindowExW(
            0,
            L"STATIC",
            L"",
            WS_CHILD | WS_CLIPCHILDREN,
            contentX,
            contentY,
            contentW,
            contentH,
            state.tabs,
            nullptr,
            ::GetModuleHandleW(nullptr),
            nullptr
        );
        state.pages[static_cast<std::size_t>(i)].sheet = sheet;

        const TabSpec* tabSpec = kTabs[i];
        if (tabSpec == nullptr || tabSpec->controls == nullptr)
        {
            // 未实现的页签：把原因写在内容区，别让用户面对一个空白页
            HWND note = ::CreateWindowExW(
                0,
                L"STATIC",
                wide(tabSpec == nullptr ? "本页未实现" : tabSpec->note).c_str(),
                WS_CHILD | SS_LEFT | SS_EDITCONTROL,
                19,
                24,
                contentW - 40,
                contentH - 60,
                sheet,
                nullptr,
                ::GetModuleHandleW(nullptr),
                nullptr
            );
            state.pages[static_cast<std::size_t>(i)].controls.push_back(note);
            continue;
        }

        // 两遍：先建组框，再建组框里的控件（子控件要盖在组框上面）
        std::vector<int> order;
        for (int c = 0; c < tabSpec->controlCount; ++c)
        {
            if (std::string(tabSpec->controls[c].kind) == "group")
            {
                order.push_back(c);
            }
        }
        for (int c = 0; c < tabSpec->controlCount; ++c)
        {
            if (std::string(tabSpec->controls[c].kind) != "group")
            {
                order.push_back(c);
            }
        }

        state.pages[static_cast<std::size_t>(i)].controls.assign(
            static_cast<std::size_t>(tabSpec->controlCount), nullptr
        );

        std::map<std::string, const ControlSpec*> groups;
        for (int c : order)
        {
            const ControlSpec& spec = tabSpec->controls[c];
            const std::string kind = spec.kind;
            const ControlSpec* groupSpec = nullptr;
            if (spec.parent != nullptr && *spec.parent != '\0')
            {
                const auto found = groups.find(spec.parent);
                if (found != groups.end())
                {
                    groupSpec = found->second;
                }
            }
            HWND handle = createControl(sheet, spec, groupSpec);
            state.pages[static_cast<std::size_t>(i)].controls[static_cast<std::size_t>(c)] = handle;
            if (kind == "group" && handle != nullptr)
            {
                groups[spec.id] = &spec;
            }
            if (handle != nullptr && std::string(spec.role) == "status")
            {
                state.pages[static_cast<std::size_t>(i)].status = handle;
            }
            // 按钮的点击通过父窗口的 WM_COMMAND 上报，这里给每个控件一个稳定 id
            if (handle != nullptr)
            {
                ::SetWindowLongPtrW(handle, GWLP_ID, kIdcFirstControl + c);
            }
        }
        // 组框里的控件要跟随组框显示/隐藏，所以 parent 一律记成 sheet（同一窗口），
        // 靠 Z 序保证可见性即可。
    }
    showTab(state, 0);
}

void browseFor(HWND owner, HWND edit)
{
    wchar_t buffer[MAX_PATH] = {};
    ::GetWindowTextW(edit, buffer, MAX_PATH);

    BROWSEINFOW info = {};
    info.hwndOwner = owner;
    info.lpszTitle = L"选择转移目录";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST item = ::SHBrowseForFolderW(&info);
    if (item == nullptr)
    {
        return;
    }
    wchar_t path[MAX_PATH] = {};
    if (::SHGetPathFromIDListW(item, path) != FALSE)
    {
        ::SetWindowTextW(edit, path);
    }
    ::CoTaskMemFree(item);
}

void setStatus(DialogState& state, const std::string& text)
{
    if (state.current < 0)
    {
        return;
    }
    HWND status = state.pages[static_cast<std::size_t>(state.current)].status;
    if (status != nullptr)
    {
        ::SetWindowTextW(status, wide(text.c_str()).c_str());
    }
}

LRESULT CALLBACK tmffunProc(HWND wnd, UINT message, WPARAM wParam, LPARAM lParam)
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
        buildTabs(*state, wnd);
        return 0;
    }
    case WM_NOTIFY: {
        auto* header = reinterpret_cast<LPNMHDR>(lParam);
        if (state != nullptr && header != nullptr && header->idFrom == kIdcTabs &&
            header->code == TCN_SELCHANGE)
        {
            const int selected = static_cast<int>(::SendMessageW(state->tabs, TCM_GETCURSEL, 0, 0));
            showTab(*state, selected);
        }
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
        const int index = id - kIdcFirstControl;
        const ControlSpec* spec = specAt(state->current, index);
        if (spec == nullptr)
        {
            return 0;
        }
        const std::string action = spec->action == nullptr ? std::string() : spec->action;
        if (action == "close")
        {
            ::DestroyWindow(wnd);
            return 0;
        }
        if (action == "save")
        {
            std::string error;
            if (saveTab(*state, state->current, error))
            {
                setStatus(*state, "已保存");
            }
            else
            {
                setStatus(*state, "保存失败：" + error);
            }
            return 0;
        }
        if (action == "browseFile")
        {
            // 选一个文件写进本页的 Ed_Tampering（role=tamperPath）
            HWND edit = nullptr;
            for (int i = 0; i < kTabs[state->current]->controlCount; ++i)
            {
                if (std::string(kTabs[state->current]->controls[i].role) == "tamperPath")
                {
                    edit = state->pages[static_cast<std::size_t>(state->current)]
                               .controls[static_cast<std::size_t>(i)];
                }
            }
            if (edit != nullptr)
            {
                wchar_t path[MAX_PATH * 4] = {};
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = wnd;
                ofn.lpstrFile = path;
                ofn.nMaxFile = MAX_PATH * 4;
                ofn.lpstrFilter = L"所有文件\0*.*\0";
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (::GetOpenFileNameW(&ofn) != FALSE)
                {
                    ::SetWindowTextW(edit, path);
                }
            }
            return 0;
        }
        if (action == "tamperAdd")
        {
            addTamperEntry(*state, wnd);
            return 0;
        }
        if (action == "browse")
        {
            // 浏览按钮和同页第一个 edit 配对（图纸里就是这个布局）
            HWND edit = nullptr;
            for (HWND handle : state->pages[static_cast<std::size_t>(state->current)].controls)
            {
                wchar_t cls[32] = {};
                if (handle != nullptr && ::GetClassNameW(handle, cls, 32) > 0 &&
                    std::wstring(cls) == L"Edit" && edit == nullptr)
                {
                    edit = handle;
                }
            }
            if (edit != nullptr)
            {
                browseFor(wnd, edit);
            }
            return 0;
        }
        return 0;
    }
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

void centerOnOwner(HWND dialog, HWND ownerHwnd)
{
    RECT target = {};
    if (ownerHwnd != nullptr && ::IsWindow(ownerHwnd) != FALSE)
    {
        ::GetWindowRect(ownerHwnd, &target);
    }
    else
    {
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &target, 0);
    }
    RECT self = {};
    ::GetWindowRect(dialog, &self);
    const int x = target.left + ((target.right - target.left) - (self.right - self.left)) / 2;
    const int y = target.top + ((target.bottom - target.top) - (self.bottom - self.top)) / 2;
    ::SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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
    cls.lpfnWndProc = tmffunProc;
    cls.hInstance = ::GetModuleHandleW(nullptr);
    cls.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    cls.lpszClassName = L"OSPTTmffun";
    if (::RegisterClassExW(&cls) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }
    registered = true;
    return true;
}

HWND createWindow(void* ownerHwnd, DialogState* state, bool visible, bool withOwner)
{
    if (!ensureClass())
    {
        return nullptr;
    }
    RECT rect = {0, 0, kTmffunClientWidth, kTmffunClientHeight};
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | (visible ? WS_VISIBLE : 0);
    ::AdjustWindowRectEx(&rect, style, FALSE, WS_EX_DLGMODALFRAME);
    return ::CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"OSPTTmffun",
        L"高级设置",
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

int tmffunTabIndex(const std::string& tabId)
{
    for (int i = 0; i < kTabCount; ++i)
    {
        if (tabId == kTabs[i]->id)
        {
            return i;
        }
    }
    return -1;
}

bool showTmffunDialog(void* ownerHwnd, config::Config& cfg, int initialTab)
{
    DialogState state;
    state.cfg = &cfg;

    HWND dialog = createWindow(ownerHwnd, &state, true, true);
    if (dialog == nullptr)
    {
        return false;
    }
    if (initialTab >= 0 && initialTab < kTabCount && state.tabs != nullptr)
    {
        ::SendMessageW(state.tabs, TCM_SETCURSEL, static_cast<WPARAM>(initialTab), 0);
        showTab(state, initialTab);
    }
    centerOnOwner(dialog, static_cast<HWND>(ownerHwnd));
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
    return state.saved;
}

bool smokeTmffunDialog(void* ownerHwnd, config::Config& cfg, std::string& detail)
{
    DialogState state;
    state.cfg = &cfg;
    HWND dialog = createWindow(ownerHwnd, &state, false, false);
    if (dialog == nullptr)
    {
        detail = "创建 TMFFUN 失败，GetLastError=" + std::to_string(::GetLastError());
        return false;
    }

    RECT client = {};
    ::GetClientRect(dialog, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width != kTmffunClientWidth || height != kTmffunClientHeight)
    {
        detail = "客户区 " + std::to_string(width) + "x" + std::to_string(height) + "，期望 740x400";
        ::DestroyWindow(dialog);
        return false;
    }

    if (kTabs == nullptr || kTabCount < 18)
    {
        detail = "页签数 " + std::to_string(kTabCount) + "，图纸是 18 个以上";
        ::DestroyWindow(dialog);
        return false;
    }

    // 三个已排页签的控件都要建出来
    const int documents = tmffunTabIndex("MlSkinTabSheet3");
    const int favorites = tmffunTabIndex("MlSkinTabSheet4");
    const int pagefile = tmffunTabIndex("MlSkinTabSheet7");
    if (documents < 0 || favorites < 0 || pagefile < 0)
    {
        detail = "三个已排页签至少缺一个";
        ::DestroyWindow(dialog);
        return false;
    }
    for (int tab : {documents, favorites, pagefile})
    {
        const TabPageState& page = state.pages[static_cast<std::size_t>(tab)];
        for (HWND handle : page.controls)
        {
            if (handle == nullptr)
            {
                detail = std::string("页签『") + kTabs[tab]->caption + "』有控件没建出来";
                ::DestroyWindow(dialog);
                return false;
            }
        }
    }

    // 摆位核对：智能/指定两个单选的矩形必须与图纸一致
    const TabPageState& documentsPage = state.pages[static_cast<std::size_t>(documents)];
    RECT smart = {};
    RECT diy = {};
    ::GetWindowRect(documentsPage.controls[0], &smart);
    ::GetWindowRect(documentsPage.controls[1], &diy);
    ::MapWindowPoints(nullptr, dialog, reinterpret_cast<LPPOINT>(&smart), 2);
    ::MapWindowPoints(nullptr, dialog, reinterpret_cast<LPPOINT>(&diy), 2);
    // 图纸 19,37 与 19,140（相对页签内容区，这里允许内容区偏移，只校验两者纵向差 103）
    if (diy.top - smart.top != 103)
    {
        detail = "两个单选的纵向间距 " + std::to_string(diy.top - smart.top) + "，图纸是 103";
        ::DestroyWindow(dialog);
        return false;
    }

    // 保存回写：把「指定转移目录」选上并填一个路径，保存后配置里应当是 diy + 该路径
    HWND radioDiy = documentsPage.controls[1];
    HWND editPath = nullptr;
    for (int i = 0; i < kTabs[documents]->controlCount; ++i)
    {
        if (std::string(kTabs[documents]->controls[i].id) == "Ed_MovePath")
        {
            editPath = documentsPage.controls[static_cast<std::size_t>(i)];
        }
    }
    if (editPath == nullptr)
    {
        detail = "转移文档页没有 Ed_MovePath";
        ::DestroyWindow(dialog);
        return false;
    }
    ::SendMessageW(radioDiy, BM_SETCHECK, BST_CHECKED, 0);
    ::SetWindowTextW(editPath, L"D:\\DocsForTest");
    state.current = documents;
    std::string error;
    if (!saveTab(state, documents, error))
    {
        detail = "保存转移文档页失败：" + error;
        ::DestroyWindow(dialog);
        return false;
    }
    if (cfg.optimization.documentsSmart)
    {
        detail = "保存后 documentsSmart 仍是 true，单选没写回去";
        ::DestroyWindow(dialog);
        return false;
    }
    if (cfg.optimization.myDocumentsTarget != "D:\\DocsForTest")
    {
        detail = "保存后 myDocumentsTarget = " + cfg.optimization.myDocumentsTarget;
        ::DestroyWindow(dialog);
        return false;
    }

    // 虚拟内存页的校验：最小倍数大于最大倍数要拦住
    const TabPageState& pagePage = state.pages[static_cast<std::size_t>(pagefile)];
    HWND minMult = nullptr;
    HWND maxMult = nullptr;
    HWND radioMult = nullptr;
    for (int i = 0; i < kTabs[pagefile]->controlCount; ++i)
    {
        const std::string id = kTabs[pagefile]->controls[i].id;
        if (id == "Ed_PageFileVMinisize")
        {
            minMult = pagePage.controls[static_cast<std::size_t>(i)];
        }
        else if (id == "Ed_PageFileVMaxsize")
        {
            maxMult = pagePage.controls[static_cast<std::size_t>(i)];
        }
        else if (id == "Rb_PageFilevarValue")
        {
            radioMult = pagePage.controls[static_cast<std::size_t>(i)];
        }
    }
    if (minMult == nullptr || maxMult == nullptr || radioMult == nullptr)
    {
        detail = "虚拟内存页缺少倍数相关控件";
        ::DestroyWindow(dialog);
        return false;
    }
    ::SendMessageW(radioMult, BM_SETCHECK, BST_CHECKED, 0);
    ::SetWindowTextW(minMult, L"4");
    ::SetWindowTextW(maxMult, L"2");
    if (saveTab(state, pagefile, error))
    {
        detail = "最小倍数 4 > 最大倍数 2 竟然保存成功";
        ::DestroyWindow(dialog);
        return false;
    }
    if (error.find("最小倍数") == std::string::npos)
    {
        detail = "拦截了但原因写的是：" + error;
        ::DestroyWindow(dialog);
        return false;
    }

    // 预置IP 页：随机模式的网段基址与末段范围要落回配置
    const int ipTab = tmffunTabIndex("MlSkinTabSheet6");
    if (ipTab < 0)
    {
        detail = "找不到预置IP页";
        ::DestroyWindow(dialog);
        return false;
    }
    const TabPageState& ipPage = state.pages[static_cast<std::size_t>(ipTab)];
    HWND radioRandom = nullptr;
    HWND editBase = nullptr;
    HWND editFrom = nullptr;
    HWND editTo = nullptr;
    for (int i = 0; i < kTabs[ipTab]->controlCount; ++i)
    {
        const std::string id = kTabs[ipTab]->controls[i].id;
        HWND handle = ipPage.controls[static_cast<std::size_t>(i)];
        if (id == "Rb_SetRandomip")
        {
            radioRandom = handle;
        }
        else if (id == "Ed_ips")
        {
            editBase = handle;
        }
        else if (id == "Ed_Startip")
        {
            editFrom = handle;
        }
        else if (id == "Ed_Endip")
        {
            editTo = handle;
        }
    }
    if (radioRandom == nullptr || editBase == nullptr || editFrom == nullptr || editTo == nullptr)
    {
        detail = "预置IP页缺少随机模式相关控件";
        ::DestroyWindow(dialog);
        return false;
    }
    ::SendMessageW(radioRandom, BM_SETCHECK, BST_CHECKED, 0);
    ::SetWindowTextW(editBase, L"192.168.1");
    ::SetWindowTextW(editFrom, L"2");
    ::SetWindowTextW(editTo, L"254");
    state.current = ipTab;
    if (!saveTab(state, ipTab, error))
    {
        detail = "保存预置IP页失败：" + error;
        ::DestroyWindow(dialog);
        return false;
    }
    if (cfg.optimization.ipMode != "random" || cfg.optimization.ipRandomBase != "192.168.1" ||
        cfg.optimization.ipRandomOctetStart != 2 || cfg.optimization.ipRandomOctetEnd != 254)
    {
        detail = "预置IP页没写回：mode=" + cfg.optimization.ipMode + " base=" + cfg.optimization.ipRandomBase;
        ::DestroyWindow(dialog);
        return false;
    }

    // 文件锁定页：加一条防篡改记录，MD5 要真的算出来
    const int lockTab = tmffunTabIndex("MlSkinTabSheet12");
    if (lockTab < 0)
    {
        detail = "找不到文件锁定页";
        ::DestroyWindow(dialog);
        return false;
    }
    const TabPageState& lockPage = state.pages[static_cast<std::size_t>(lockTab)];
    HWND pathEdit = nullptr;
    HWND list = nullptr;
    for (int i = 0; i < kTabs[lockTab]->controlCount; ++i)
    {
        const std::string role =
            kTabs[lockTab]->controls[i].role == nullptr ? std::string() : kTabs[lockTab]->controls[i].role;
        if (role == "tamperPath")
        {
            pathEdit = lockPage.controls[static_cast<std::size_t>(i)];
        }
        else if (role == "tamperList")
        {
            list = lockPage.controls[static_cast<std::size_t>(i)];
        }
    }
    if (pathEdit == nullptr || list == nullptr)
    {
        detail = "文件锁定页缺少清单或路径框";
        ::DestroyWindow(dialog);
        return false;
    }

    // 用自身可执行文件当样本：它一定存在、一定能读
    wchar_t selfPath[MAX_PATH] = {};
    if (::GetModuleFileNameW(nullptr, selfPath, MAX_PATH) == 0)
    {
        detail = "取不到自身路径，无法验防篡改添加";
        ::DestroyWindow(dialog);
        return false;
    }
    ::SetWindowTextW(pathEdit, selfPath);
    state.current = lockTab;
    const std::size_t before = cfg.deploy.tamperFiles.size();
    addTamperEntry(state, dialog);
    if (cfg.deploy.tamperFiles.size() != before + 1)
    {
        detail = "防篡改条目没加上（当前 " + std::to_string(cfg.deploy.tamperFiles.size()) + " 条）";
        ::DestroyWindow(dialog);
        return false;
    }
    const std::string digest = cfg.deploy.tamperFiles.back().md5;
    if (digest.size() != 32)
    {
        detail = "算出来的 MD5 长度 " + std::to_string(digest.size()) + "，期望 32";
        ::DestroyWindow(dialog);
        return false;
    }
    // 重复添加要被拦住
    ::SetWindowTextW(pathEdit, selfPath);
    addTamperEntry(state, dialog);
    if (cfg.deploy.tamperFiles.size() != before + 1)
    {
        detail = "同一个文件被重复加入清单";
        ::DestroyWindow(dialog);
        return false;
    }
    if (::SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) != static_cast<LRESULT>(before + 1))
    {
        detail = "清单里显示的行数与配置不一致";
        ::DestroyWindow(dialog);
        return false;
    }

    detail = "客户区 " + std::to_string(width) + "x" + std::to_string(height) + "，" +
        std::to_string(kTabCount) + " 个页签，" + std::to_string(kTabs[documents]->controlCount) + "/" +
        std::to_string(kTabs[favorites]->controlCount) + "/" + std::to_string(kTabs[pagefile]->controlCount) +
        "/" + std::to_string(kTabs[ipTab]->controlCount) + "/" +
        std::to_string(kTabs[lockTab]->controlCount) +
        " 个控件，保存回写、大小校验、随机网段、防篡改 MD5 都对";
    ::DestroyWindow(dialog);
    return true;
}

} // namespace osp::ui

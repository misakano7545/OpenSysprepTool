#include "core/pppoe.hpp"

#include <vector>

#include <windows.h>
#include <ras.h>
#include <raserror.h>

#include "core/win7.hpp"

namespace osp::netcfg {
namespace {

std::wstring toWide(const std::string& text)
{
    if (text.empty())
    {
        return std::wstring();
    }
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 0)
    {
        return std::wstring();
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
    while (!wide.empty() && wide.back() == L'\0')
    {
        wide.pop_back();
    }
    return wide;
}

std::string rasErrorText(DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD length = ::RasGetErrorStringW(static_cast<UINT>(code), nullptr, 0);
    if (length > 0)
    {
        std::vector<wchar_t> text(length + 1, L'\0');
        if (::RasGetErrorStringW(static_cast<UINT>(code), text.data(), length + 1) == 0)
        {
            buffer = text.data();
            return osp::win7::toUtf8Path(buffer);
        }
    }
    return "RAS 错误码 " + std::to_string(code);
}

// 找 PPPoE 设备类型。RASDT_PPPoE 在 ras.h 里定义为一个 GUID 字符串，
// 但设备名要取本机的实际名（通常是 "WAN Miniport (PPPOE)"）。
bool findPppoeDevice(std::wstring& type, std::wstring& name, std::string& error)
{
    DWORD size = 0;
    DWORD count = 0;
    // 第一次调用只为了拿需要的缓冲区大小。注意它返回的是 RAS 自己的
    // ERROR_BUFFER_TOO_SMALL = 603，而 Windows 里同名的宏是 122 —— 直接比较常量
    // 会因为宏被 winerror.h 先定义而永远不等，所以这里只认「size 拿到了」。
    ::RasEnumDevicesW(nullptr, &size, &count);
    if (size == 0)
    {
        error = "枚举 RAS 设备失败（拿不到缓冲区大小）";
        return false;
    }
    std::vector<BYTE> buffer(size);
    auto* devices = reinterpret_cast<RASDEVINFOW*>(buffer.data());
    // 每个条目的 dwSize 必须先填好，否则返回「不正确的结构大小」。
    // 步长按返回的 size/count 算，避免 x86/x64 结构体对齐差异。
    const DWORD stride = (count > 0 && size % count == 0) ? size / count : sizeof(RASDEVINFOW);
    for (DWORD i = 0; i < count; ++i)
    {
        auto* slot = reinterpret_cast<RASDEVINFOW*>(buffer.data() + static_cast<std::size_t>(i) * stride);
        slot->dwSize = stride;
    }
    const DWORD status = ::RasEnumDevicesW(devices, &size, &count);
    if (status != 0)
    {
        error = "枚举 RAS 设备失败：" + rasErrorText(status);
        return false;
    }
    for (DWORD i = 0; i < count; ++i)
    {
        if (::lstrcmpiW(devices[i].szDeviceType, RASDT_PPPoE) == 0)
        {
            type = devices[i].szDeviceType;
            name = devices[i].szDeviceName;
            return true;
        }
    }
    error = "本机没有 PPPoE 设备（找不到 WAN Miniport (PPPOE)）";
    return false;
}

} // namespace

bool createPppoeEntry(
    const std::string& utf8PbkPath,
    const std::string& name,
    const std::string& userName,
    const std::string& password,
    std::string& error
)
{
    if (name.empty())
    {
        error = "连接名不能为空";
        return false;
    }

    std::wstring deviceType;
    std::wstring deviceName;
    if (!findPppoeDevice(deviceType, deviceName, error))
    {
        return false;
    }

    RASENTRYW entry = {};
    entry.dwSize = sizeof(entry);
    // 只要用户名/密码就够了；PPP 层用 MSCHAPv2（PPPoE 的常规配置）
    entry.dwfOptions = RASEO_PreviewUserPw | RASEO_RequireMsCHAP2 | RASEO_ModemLights;
    entry.dwfNetProtocols = RASNP_Ip;
    ::lstrcpynW(entry.szDeviceType, deviceType.c_str(), ARRAYSIZE(entry.szDeviceType));
    ::lstrcpynW(entry.szDeviceName, deviceName.c_str(), ARRAYSIZE(entry.szDeviceName));

    const std::wstring wideName = toWide(name);
    const std::wstring widePbk = osp::win7::pathFromUtf8(utf8PbkPath).wstring();
    if (wideName.empty() || widePbk.empty())
    {
        error = "连接名或 pbk 路径无法转换";
        return false;
    }

    const DWORD status = ::RasSetEntryPropertiesW(
        widePbk.empty() ? nullptr : widePbk.c_str(), wideName.c_str(), &entry, sizeof(entry), nullptr, 0
    );
    if (status != 0)
    {
        error = "创建 PPPoE 条目失败：" + rasErrorText(status);
        return false;
    }

    // 用户名/密码单独写：RasSetEntryProperties 只管条目结构，凭据在 credential 里
    const std::wstring wideUser = toWide(userName);
    const std::wstring widePass = toWide(password);
    if (!wideUser.empty())
    {
        RASCREDENTIALSW credentials = {};
        credentials.dwSize = sizeof(credentials);
        credentials.dwMask = RASCM_UserName | RASCM_Password;
        ::lstrcpynW(credentials.szUserName, wideUser.c_str(), ARRAYSIZE(credentials.szUserName));
        ::lstrcpynW(credentials.szPassword, widePass.c_str(), ARRAYSIZE(credentials.szPassword));
        const DWORD credentialStatus =
            ::RasSetCredentialsW(widePbk.c_str(), wideName.c_str(), &credentials, FALSE);
        if (credentialStatus != 0)
        {
            // 条目已经建好了，只是凭据没存进去；这不算彻底失败，但要如实报出来
            error = "条目已创建，但凭据写入失败：" + rasErrorText(credentialStatus);
        }
    }
    return true;
}

bool readPppoeEntry(
    const std::string& utf8PbkPath, const std::string& name, std::string& userName, std::string& error
)
{
    const std::wstring wideName = toWide(name);
    const std::wstring widePbk = osp::win7::pathFromUtf8(utf8PbkPath).wstring();
    if (wideName.empty())
    {
        error = "连接名不能为空";
        return false;
    }

    RASENTRYW entry = {};
    entry.dwSize = sizeof(entry);
    DWORD size = sizeof(entry);
    const DWORD status =
        ::RasGetEntryPropertiesW(widePbk.c_str(), wideName.c_str(), &entry, &size, nullptr, nullptr);
    if (status != 0)
    {
        error = "读不回条目：" + rasErrorText(status);
        return false;
    }
    // 读得到就说明条目存在；设备类型必须是我们写的那种
    if (::lstrcmpiW(entry.szDeviceType, RASDT_PPPoE) != 0)
    {
        error = "条目存在但设备类型不是 PPPoE：" + osp::win7::toUtf8Path(entry.szDeviceType);
        return false;
    }

    RASCREDENTIALSW credentials = {};
    credentials.dwSize = sizeof(credentials);
    credentials.dwMask = RASCM_UserName;
    if (::RasGetCredentialsW(widePbk.c_str(), wideName.c_str(), &credentials) == 0)
    {
        userName = osp::win7::toUtf8Path(credentials.szUserName);
    }
    return true;
}

bool deletePppoeEntry(const std::string& utf8PbkPath, const std::string& name, std::string& error)
{
    const std::wstring wideName = toWide(name);
    const std::wstring widePbk = osp::win7::pathFromUtf8(utf8PbkPath).wstring();
    const DWORD status = ::RasDeleteEntryW(widePbk.c_str(), wideName.c_str());
    if (status == 0)
    {
        return true;
    }
    // RasDeleteEntry 对「本来就没有这个条目」返回的码并不统一（不同 Windows 版本
    // 分别是 ERROR_NOT_FOUND / ERROR_INVALID_NAME 等）。这里不猜码，改为读一次：
    // 读不到就等于删掉了 —— 幂等语义靠结果判定，而不是靠错误码。
    std::string probeUser;
    std::string probeError;
    if (!readPppoeEntry(utf8PbkPath, name, probeUser, probeError))
    {
        return true;
    }
    error = "删除 PPPoE 条目失败：" + rasErrorText(status);
    return false;
}

} // namespace osp::netcfg

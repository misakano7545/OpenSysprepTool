// 部署前的三项闸门，对齐 SC 的部署设置：
//
//   Cb_Tampering           校验文件防篡改（锁定文件列表 + MD5，不匹配则提示并中止）
//   Btn_DisableVmrun       禁止在虚拟机里部署
//   SA_Rb_hijack_type1/2   部署主控被劫持：继续（防流氓模式可反劫持）或终止
//
// 都在**执行任何任务之前**判定，判定不过就不动目标机 —— 这是「部署闸门」的
// 意义所在，先跑一半再发现被篡改是最坏的情况。

#include "core/deploy.hpp"

#include <string>
#include <vector>

#include <windows.h>

#include "core/md5.hpp"
#include "core/win7.hpp"

namespace osp::deploy {
namespace {

// 是否跑在虚拟机里。判据取 CPUID 的 hypervisor present 位（0x40000000 叶子能读到
// 厂商串），这在 VMware / Hyper-V / VBox / KVM 上都成立，且不依赖 WMI。
bool runningInVirtualMachine(std::string& vendor)
{
    int registers[4] = {};
#if defined(_M_IX86) || defined(_M_X64)
    __cpuid(registers, 0x40000000);
#else
    return false;
#endif
    char text[13] = {};
    for (int i = 0; i < 3; ++i)
    {
        text[i * 4 + 0] = static_cast<char>((registers[i + 1] >> 0) & 0xFF);
        text[i * 4 + 1] = static_cast<char>((registers[i + 1] >> 8) & 0xFF);
        text[i * 4 + 2] = static_cast<char>((registers[i + 1] >> 16) & 0xFF);
        text[i * 4 + 3] = static_cast<char>((registers[i + 1] >> 24) & 0xFF);
    }
    // 没有 hypervisor 时 0x40000000 叶返回全 0 或直接回落到最大基本叶
    if (registers[0] < 0x40000000)
    {
        return false;
    }
    const std::string name(text, 12);
    if (name.find("VMware") != std::string::npos || name.find("VBox") != std::string::npos ||
        name.find("KVM") != std::string::npos || name.find("Microsoft Hv") != std::string::npos ||
        name.find("Xen") != std::string::npos || name.find("Parallels") != std::string::npos ||
        name.find("QEMU") != std::string::npos)
    {
        vendor = name;
        // 去掉尾部的 NUL/空格
        while (!vendor.empty() && (vendor.back() == '\0' || vendor.back() == ' '))
        {
            vendor.pop_back();
        }
        return true;
    }
    // 认得 hypervisor 位但厂商串不认识：仍然当作虚拟机，只是不报名字
    vendor = name.substr(0, name.find('\0') == std::string::npos ? name.size() : name.find('\0'));
    return !vendor.empty();
}

} // namespace

bool interactiveDesktopAvailable()
{
    const HWINSTA station = ::GetProcessWindowStation();
    if (station == nullptr)
    {
        return false;
    }
    USEROBJECTFLAGS flags = {};
    DWORD needed = 0;
    if (!::GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof(flags), &needed))
    {
        return false;
    }
    // WSF_VISIBLE 清掉说明跑在服务/CI 这类不可见窗口站里，弹框没人点
    return (flags.dwFlags & WSF_VISIBLE) != 0;
}

PreflightResult preflight(const Plan& plan)
{
    PreflightResult result;

    // 1) 禁止在虚拟机里部署（SC 的 Btn_DisableVmrun）
    if (plan.blockVirtualMachine)
    {
        std::string vendor;
        if (runningInVirtualMachine(vendor))
        {
            result.blocked = true;
            result.reason = "本机运行在虚拟机中（" + vendor + "），按部署设置禁止在虚拟机里部署";
            return result;
        }
        result.checked.push_back("虚拟机检测：未在虚拟机中运行");
    }

    // 2) 文件防篡改（SC 的 Cb_Tampering）
    if (plan.verifyIntegrity)
    {
        if (plan.tamperFiles.empty())
        {
            result.blocked = true;
            result.reason = "部署设置要求校验文件防篡改，但锁定文件列表为空";
            return result;
        }
        for (const auto& entry : plan.tamperFiles)
        {
            std::string error;
            const std::string actual = osp::crypto::md5File(entry.path, error);
            if (actual.empty())
            {
                result.blocked = true;
                result.reason = "防篡改校验失败：读不到 " + entry.path + "（" + error + "）";
                return result;
            }
            if (actual != entry.md5)
            {
                result.blocked = true;
                // 有用户填的说明就先摆出说明（SC 的 Ed_TamperingTip 就是这个用途），
                // 后面仍附上路径与两个摘要 —— 光有「部署已中止」帮不了排查。
                result.reason = (entry.tip.empty() ? std::string("文件被篡改：") : entry.tip + "：") +
                    entry.path + "（期望 " + entry.md5 + "，实际 " + actual + "）";
                result.tamperedFile = entry.path;
                return result;
            }
            result.checked.push_back("防篡改校验通过：" + entry.path);
        }
    }

    // 3) 部署主控被劫持：用自身可执行文件的摘要核对。
    //    SC 的语义是「部署主程序被替换」；这里把生成计划时记下的摘要与运行时比对，
    //    不一致就按 hijackMode 处理。计划里没记摘要时跳过（老计划兼容）。
    if (!plan.controllerMd5.empty())
    {
        wchar_t selfPath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            result.warnings.push_back("部署主控校验：读不到自身路径，跳过");
        }
        else
        {
            std::string error;
            const std::string actual = osp::crypto::md5File(win7::toUtf8Path(selfPath), error);
            if (actual.empty())
            {
                result.warnings.push_back("部署主控校验：读不到自身文件（" + error + "），跳过");
            }
            else if (actual != plan.controllerMd5)
            {
                const bool terminate = plan.hijackMode != "continue";
                result.controllerHijacked = true;
                const std::string text = plan.hijackTip.empty()
                    ? "部署主程序已被修改（可能被劫持）：期望 " + plan.controllerMd5 + "，实际 " + actual
                    : plan.hijackTip;
                // SA_Swb_Showbox 关掉时只记录不弹窗（tip 空 = 部署端不弹）；
                // 终止分支仍要一句原因，所以 reason 照写。
                result.tip = plan.hijackShowTip ? text : std::string();
                if (terminate)
                {
                    result.blocked = true;
                    result.reason = text;
                    return result;
                }
                // 提示文案可能已经写明「按设置继续」，别重复追加
                result.warnings.push_back(text);
            }
            else
            {
                result.checked.push_back("部署主控校验通过");
            }
        }
    }

    return result;
}

} // namespace osp::deploy

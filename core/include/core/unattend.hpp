#pragma once

#include <string>
#include <vector>

#include "core/config.hpp"

namespace osp::unattend {

// unattend.xml 的默认命名空间与 WCM 命名空间（宪章 M3 约束）
inline constexpr const char* kNamespace = "urn:schemas-microsoft-com:unattend";
inline constexpr const char* kWcmNamespace = "http://schemas.microsoft.com/WMIConfig/2002/State";

// 生成结果：成功时 xml 为完整文档文本（UTF-8，含 XML 声明）
struct GenResult
{
    bool ok = false;
    std::string xml;
    std::string error;
};

// 由配置生成 unattend.xml 文本。
//
// 元素顺序依据 ADK 附带的离线参考 Unattend.chm（1155 个页面）中各组件的
// "Child Elements" 列表顺序。该参考里唯一写明强制顺序的位置是
// Microsoft-Windows-TCPIP | Interfaces | Interface，本模块不生成该组件。
GenResult generate(const osp::config::Config& cfg);

// 写入文件，路径按 UTF-8 解释；失败时填 error
bool writeToFile(const std::string& utf8Path, const std::string& xml, std::string& error);

// 计算最终写入的 ComputerName 取值。
//   fixed        -> 原样使用，并按参考校验（≤15 字节、无空格、无禁用字符、不能全为数字）
//   random       -> "*"（参考规定星号表示由系统随机生成）
//   prefixRandom -> 前缀 + 4 位十六进制后缀，后缀由配置内容哈希导出，保证同样输入同样输出
bool resolveComputerName(const osp::config::SystemInfo& info, std::string& out, std::string& error);

// 元素顺序表：与 ADK Unattend.chm 组件页 Child Elements 顺序一致，供单元测试比对
const std::vector<const char*>& shellSetupSpecializeOrder();
const std::vector<const char*>& shellSetupOobeSystemOrder();
const std::vector<const char*>& oobeChildOrder();
const std::vector<const char*>& autoLogonChildOrder();
const std::vector<const char*>& displayChildOrder();
const std::vector<const char*>& synchronousCommandChildOrder();
const std::vector<const char*>& localAccountChildOrder();

} // namespace osp::unattend

#pragma once

// 系统优化项 —— 数据来自 SC 自带的优化表，不是自己编的
//
// SC 把优化项存成一串 `序号|类型|路径|键名|值` 的记录，序号 2 = HKCU、3 = HKLM。
// 这 27 条是从主程序字符串转储里**逐字提取**的（见 docs/SC功能对照.md 第一节），
// 每条都带 SC 自己的语言键（`optimize_*`），用途也能对上厂商写的说明文案。
//
// 唯一刻意偏离 SC 数据的地方：SC 写死 `SYSTEM\ControlSet001\...`，本模块改成
// `CurrentControlSet` —— 目标机的当前控制集不一定是 001，照抄会把设置写进一份
// 没被使用的控制集，看起来生效其实是空转。

#include <string>
#include <vector>

namespace osp::tweaks {

enum class Hive
{
    CurrentUser,  // SC 表里的序号 2
    LocalMachine, // SC 表里的序号 3
};

enum class ValueType
{
    Integer,
    String,
    Binary,
};

struct Tweak
{
    std::string id;      // 稳定标识，如 tweak.diskTimeout
    std::string scLabel; // SC 的语言键，如 optimize_diskouttime；查不到就留空
    std::string title;   // 中文说明，取自 SC 语言文件
    Hive hive = Hive::LocalMachine;
    std::string path; // 不含 hive 前缀
    std::string name; // 值名
    ValueType type = ValueType::Integer;
    std::string value; // 统一按文本存，写盘时按 type 转换
    bool requiresReboot = false;
};

// SC 的完整优化表（27 条，顺序与 SC 的序号一致）
const std::vector<Tweak>& all();

// 按 id 取一条；找不到返回 nullptr
const Tweak* find(const std::string& id);

// 语言文件里有描述、但二进制里没有单行记录的那几条（走命令或另有实现）。
// 这里只声明，不假装已支持 —— 让 --prepare 的 deferred 段能如实报出来。
const std::vector<std::string>& unresolvedLabels();

} // namespace osp::tweaks

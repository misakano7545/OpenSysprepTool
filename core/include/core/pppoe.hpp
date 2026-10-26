#pragma once

// 宽带（PPPoE）连接的静默创建
//
// SC 的 Cb_CreateConnection 有两个模式：Rb_Silence_nc「静默创建[无界面]」与
// Rb_Show_nc「倒计时创建[有界面]」。静默模式要求不弹任何界面就把连接建好。
//
// 为什么不是自己拼 .pbk 文本：rasphone.pbk 的段名与键没有公开的完整规范，
// 手写一份「看起来对」的文件风险很高——写错了不报错，只是用户点进去发现连不上。
// 这里改成让 Windows 自己写：RasSetEntryProperties 可以指向任意路径的 pbk，
// 由它生成条目，我们再把这个 pbk 复制到默认用户配置目录。格式永远是对的。

#include <string>

namespace osp::netcfg {

// 在指定 pbk 文件里创建一个 PPPoE 条目。
// utf8PbkPath 必须指向已存在的目录下的文件（不存在会被创建）。
bool createPppoeEntry(
    const std::string& utf8PbkPath,
    const std::string& name,
    const std::string& userName,
    const std::string& password,
    std::string& error
);

// 读回条目，确认 Windows 真的接受了。返回 false 表示读不到或名字对不上。
bool readPppoeEntry(
    const std::string& utf8PbkPath, const std::string& name, std::string& userName, std::string& error
);

// 删除条目（回滚用）。条目本就不存在时返回 true。
bool deletePppoeEntry(const std::string& utf8PbkPath, const std::string& name, std::string& error);

} // namespace osp::netcfg

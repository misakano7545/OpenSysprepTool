#pragma once

// 极简 INI 读写（只读，不含写入）
//
// 为什么自己写而不引第三方：需要的东西很少（节、键值、注释、引号），
// 而 SC 的配置文件是 Delphi TIniFile 写的标准 Windows INI —— 也就是
// GetPrivateProfileString 认得的那一套。引一个库比自己写代价更高。
//
// 编码是这里唯一容易踩坑的地方：SC 的 .ini 是 **GBK（CP936）**，本项目内部一律 UTF-8。
// 所以 loadFile() 的判定顺序是 UTF-8 BOM → UTF-16 BOM → 合法 UTF-8 → **固定 936** → 系统 ANSI。
// 固定 936 而不是跟随 CP_ACP：ACP 在英文系统上是 1252，同一份 GBK 会被解成乱码。

#include <string>
#include <vector>

namespace osp::ini {

// 一行键值。section 为空表示文件开头的全局区（INI 允许，但很少用）。
// key 为空表示该行是「无键行」：SC 的 nt5.ini 里 [Version] 段第一行就是
// 光秃秃的 "Microsoft Windows XP Professional"，不能当成语法错误丢掉。
struct Entry
{
    std::string section;
    std::string key;
    std::string value;
    int line = 0; // 1 起，报错时给位置
};

struct Document
{
    bool ok = false;
    std::string error;
    std::vector<Entry> entries;
    std::vector<std::string> warnings;

    bool has(const std::string& section, const std::string& key) const;
    // 取不到时返回 fallback
    std::string
    get(const std::string& section, const std::string& key, const std::string& fallback = {}) const;
    // 按出现顺序列出节名（去重，保持首次出现顺序）
    std::vector<std::string> sections() const;
    std::vector<Entry> inSection(const std::string& section) const;
};

// 解析已经解码好的 UTF-8 文本。
// 规则：'[' 开头到 ']' 是节名；';' 或 '#' 开头是整行注释；
// key=value 首次出现的 '=' 分隔；值两侧空白去掉；成对的双引号去掉。
// 同一节内重复键保留先出现的那个（TIniFile 语义），并在 warnings 里记一条。
Document parse(const std::string& utf8Text);

// 读文件并按编码解码成 UTF-8。
// 判定顺序：UTF-8 BOM -> UTF-16 BOM -> 合法 UTF-8 -> 固定 936（GBK） -> 系统 ANSI。
Document loadFile(const std::string& utf8Path);

// 把一段字节按上述规则解码成 UTF-8（loadFile 内部用，也给测试直接用）
std::string decodeToUtf8(const std::string& bytes);

} // namespace osp::ini

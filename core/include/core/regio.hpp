#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 注册表 RAII（宪章 §2 的 core/regio）
//
// 只做本项目需要的事：打开/创建键、读值、写值、删值、删树。
// 所有失败都通过 error 返回，不抛异常；句柄由 Key 的析构函数关闭。
//
// 64 位视图：HKLM 统一带 KEY_WOW64_64KEY。32 位进程在 64 位系统上访问
// HKLM\SOFTWARE 会被重定向到 WOW6432Node，写进去的设置在 64 位程序里看不到，
// 而本项目要写的都是系统级设置（策略、内存管理、服务启动类型）。
// HKCU 与 HKU\.DEFAULT 没有 WOW64 重定向，不加这个标志（32 位系统上更稳）。

namespace osp::regio {

enum class Hive
{
    LocalMachine,
    CurrentUser,
    UsersDefault, // HKEY_USERS\.DEFAULT
};

const char* hiveId(Hive hive); // "HKLM" / "HKCU" / "HKU\\.DEFAULT"
bool parseHive(const std::string& text, Hive& hive, std::string& error);

enum class ValueType
{
    String,
    ExpandString,
    Dword,
    Qword,
    MultiString,
    Binary,
};

const char* valueTypeId(ValueType type); // "sz" / "expandSz" / "dword" / "qword" / "multiSz" / "binary"
bool parseValueType(const std::string& text, ValueType& type, std::string& error);

struct Value
{
    ValueType type = ValueType::String;
    std::string text;                // String / ExpandString
    std::uint32_t dword = 0;         // Dword
    std::uint64_t qword = 0;         // Qword
    std::vector<std::string> multi;  // MultiString
    std::vector<std::uint8_t> bytes; // Binary
};

class Key
{
public:
    Key() = default;
    ~Key();

    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    Key(Key&& other) noexcept;
    Key& operator=(Key&& other) noexcept;

    bool valid() const;
    void close();

    // 原生 HKEY，供同模块的读写函数使用；外部不要长期保存
    void* native() const
    {
        return handle_;
    }

private:
    friend Key open(Hive hive, const std::string& subKey, bool writable, std::string& error);
    friend Key openUserHiveKey(
        const std::string& hiveName, const std::string& subKey, bool writable, std::string& error
    );
    friend bool
    createUserHiveKey(const std::string& hiveName, const std::string& subKey, Key& key, std::string& error);

    // 打开或创建「某个已加载 hive」下的子键（父键必须存在）
    bool
    createUserHiveKey(const std::string& hiveName, const std::string& subKey, Key& key, std::string& error);
    friend Key create(Hive hive, const std::string& subKey, std::string& error);

    void* handle_ = nullptr;
};

// 打开已存在的键。writable=false 时只要 KEY_READ。
Key open(Hive hive, const std::string& subKey, bool writable, std::string& error);

// 打开「某个已加载 hive」下的子键。hiveName 是 HKEY_USERS 下的名字，
// 例如 ".DEFAULT"，或离线加载时用的临时名 "OSPT_DEFAULT"。
Key openUserHiveKey(
    const std::string& hiveName, const std::string& subKey, bool writable, std::string& error
);

// 打开或创建「某个已加载 hive」下的子键（父键必须存在）
bool createUserHiveKey(const std::string& hiveName, const std::string& subKey, Key& key, std::string& error);

// 离线加载一个用户配置单元（NTUSER.DAT / UsrClass.dat 之类）。
//
// 为什么必须离线加载：SC 的「我的文档/收藏夹转移」「同步 IE 主页」要写进
// **所有新用户**的默认配置，落点就是 <系统盘>\Users\Default\NTUSER.DAT。
// 那个文件平时没有挂载，直接写不了，只能 RegLoadKey 挂到 HKEY_USERS 下再写。
//
// 析构时自动卸载 —— 失败路径也必须卸干净，否则下次加载会报「文件正在使用」。
class LoadedHive
{
public:
    LoadedHive() = default;
    ~LoadedHive();

    LoadedHive(const LoadedHive&) = delete;
    LoadedHive& operator=(const LoadedHive&) = delete;
    LoadedHive(LoadedHive&& other) noexcept;
    LoadedHive& operator=(LoadedHive&& other) noexcept;

    // utf8File 是 NTUSER.DAT 的完整路径；hiveName 是挂到 HKEY_USERS 下的临时名。
    // 同名已挂载时先尝试沿用（返回 true），避免重复加载报错。
    bool load(const std::string& utf8File, const std::string& hiveName, std::string& error);
    // 显式卸载；析构也会做，返回值只用于报告
    bool unload(std::string& error);

    bool loaded() const
    {
        return loaded_;
    }
    const std::string& name() const
    {
        return name_;
    }

private:
    bool loaded_ = false;
    std::string name_;
};

// 打开或创建键（父键必须存在）。
Key create(Hive hive, const std::string& subKey, std::string& error);

// 读一个值。exists=false 表示值不存在（不是错误）。
bool readValue(Key& key, const std::string& name, Value& value, bool& exists, std::string& error);

// 写一个值。类型取 value.type。
bool writeValue(Key& key, const std::string& name, const Value& value, std::string& error);

// 删除一个值。值本来就不存在时返回 true。
bool deleteValue(Key& key, const std::string& name, std::string& error);

// 递归删除整个键。键不存在时返回 true。
bool deleteTree(Hive hive, const std::string& subKey, std::string& error);

// 人类可读的值文本，供报告与日志使用。
std::string describeValue(const Value& value);

} // namespace osp::regio

#!/usr/bin/env python3
"""M3 验收脚本：校验 ospt --gen-unattend 产出的 unattend.xml。

宪章 M3 要求校验脚本按微软 schema 校验命名空间与元素顺序，并断言：
  1. 计算机名规则
  2. AutoLogon 次数
  3. FirstLogonCommands 条数

元素顺序的权威来源：ADK 附带的离线参考 `Unattend.chm`
（`<ADK>\\Docs\\Unattend.chm`，1155 个页面）。该参考里唯一写明强制顺序的位置是
`Microsoft-Windows-TCPIP | Interfaces | Interface`；本工具不生成该组件。
其余组件的子元素顺序取自各组件页的 `Child Elements` 列表，与 Windows SIM 输出一致。

用法:
  python tests/integration/verify_unattend.py <unattend.xml> [cfg.json]
未显式给出 cfg.json 时，自动探测 xml 同目录下的 cfg.json；找不到则跳过交叉断言
（会显式打印 SKIP，不静默通过）。

退出码: 0 全部通过 / 1 有失败项 / 2 用法或读取错误
"""

from __future__ import annotations

import base64
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

NS = "urn:schemas-microsoft-com:unattend"
WCM = "http://schemas.microsoft.com/WMIConfig/2002/State"

# ---------------------------------------------------------------- 顺序表
# 以下顺序逐条对应 ADK Unattend.chm 中各页面的 Child Elements 列表。
SHELL_SETUP_SPECIALIZE_ORDER = [
    "ComputerName",
    "ProductKey",
    "RegisteredOrganization",
    "RegisteredOwner",
]

SHELL_SETUP_OOBE_ORDER = [
    "AutoLogon",
    "Display",
    "FirstLogonCommands",
    "LogonCommands",
    # OEMInformation 在微软文档里排在 LogonCommands 与 OOBE 之间（字母序）
    "OEMInformation",
    "OOBE",
    "UserAccounts",
]

# OEMInformation 的子元素顺序（文档字母序）。Manufacturer/Model/SupportHours/
# SupportPhone 在 Win10 文档里已标注弃用，但 Win7 只认它们，所以两代一起写。
OEM_INFORMATION_CHILD_ORDER = [
    "Manufacturer",
    "Model",
    "SupportHours",
    "SupportPhone",
    "SupportProvider",
    "SupportURL",
]

OOBE_CHILD_ORDER = [
    "HideEULAPage",
    "HideLocalAccountScreen",
    "HideOEMRegistrationScreen",
    "HideOnlineAccountScreens",
    "HideWirelessSetupInOOBE",
    "NetworkLocation",
    "OEMAppID",
    "ProtectYourPC",
]

AUTOLOGON_CHILD_ORDER = ["Domain", "Enabled", "LogonCount", "Password", "Username"]

DISPLAY_CHILD_ORDER = [
    "ColorDepth",
    "DPI",
    "HorizontalResolution",
    "RefreshRate",
    "VerticalResolution",
]

SYNC_COMMAND_CHILD_ORDER = ["CommandLine", "Description", "Order", "RequiresUserInput"]

# AsynchronousCommand 页把 RequiresUserInput 标为 "This setting is not used."，不生成
ASYNC_COMMAND_CHILD_ORDER = ["CommandLine", "Description", "Order"]

LOCAL_ACCOUNT_CHILD_ORDER = ["Description", "DisplayName", "Group", "Name", "Password"]

DEPLOYMENT_CHILD_ORDER = [
    "AuditComputerName",
    "DuplicatorDescription",
    "ExtendOSPartition",
    "Generalize",
    "Reseal",
    "RunAsynchronous",
    "RunSynchronous",
]

IDENTIFICATION_CHILD_ORDER = [
    "Credentials",
    "DebugJoin",
    "DebugJoinOnlyOnThisError",
    "JoinDomain",
    "JoinWorkgroup",
    "MachineObjectOU",
    "MachinePassword",
    "Provisioning",
    "TimeoutPeriodInMinutes",
]

ALLOWED_PASSES = {
    "windowsPE",
    "offlineServicing",
    "generalize",
    "specialize",
    "auditSystem",
    "auditUser",
    "oobeSystem",
}

# 参考中各设置页 "Valid Configuration Passes" 的取值
VALID_PASSES = {
    ("Microsoft-Windows-Shell-Setup", "ComputerName"): {"offlineServicing", "specialize"},
    ("Microsoft-Windows-Shell-Setup", "OOBE"): {"oobeSystem"},
    ("Microsoft-Windows-Shell-Setup", "AutoLogon"): {"auditSystem", "oobeSystem", "specialize"},
    ("Microsoft-Windows-Shell-Setup", "FirstLogonCommands"): {"oobeSystem"},
    ("Microsoft-Windows-Shell-Setup", "LogonCommands"): {"oobeSystem"},
    ("Microsoft-Windows-Shell-Setup", "UserAccounts"): {"auditSystem", "oobeSystem"},
    ("Microsoft-Windows-Shell-Setup", "Display"): {
        "auditSystem",
        "auditUser",
        "oobeSystem",
        "specialize",
    },
    ("Microsoft-Windows-Deployment", "RunSynchronous"): {"specialize", "auditUser"},
    ("Microsoft-Windows-UnattendedJoin", "Identification"): {"specialize"},
}

# 父路径约束：元素名 -> 允许的直接父元素名集合（None 表示必须是 component 的直接子元素）
PARENT_RULES = {
    "ComputerName": {"component"},
    "ProductKey": {"component"},
    "RegisteredOwner": {"component"},
    "RegisteredOrganization": {"component"},
    "AutoLogon": {"component"},
    "Display": {"component"},
    "FirstLogonCommands": {"component"},
    "LogonCommands": {"component"},
    "OOBE": {"component"},
    "UserAccounts": {"component"},
    "RunSynchronous": {"component"},
    "Identification": {"component"},
    "HideEULAPage": {"OOBE"},
    "HideOnlineAccountScreens": {"OOBE"},
    "HideWirelessSetupInOOBE": {"OOBE"},
    "NetworkLocation": {"OOBE"},
    "ProtectYourPC": {"OOBE"},
    "Enabled": {"AutoLogon"},
    "LogonCount": {"AutoLogon"},
    "Username": {"AutoLogon"},
    "ColorDepth": {"Display"},
    "HorizontalResolution": {"Display"},
    "RefreshRate": {"Display"},
    "VerticalResolution": {"Display"},
    "SynchronousCommand": {"FirstLogonCommands"},
    "AsynchronousCommand": {"LogonCommands"},
    "CommandLine": {"SynchronousCommand", "AsynchronousCommand"},
    "Description": {"SynchronousCommand", "AsynchronousCommand"},
    "Order": {"SynchronousCommand", "AsynchronousCommand", "RunSynchronousCommand"},
    "RunSynchronousCommand": {"RunSynchronous"},
    "Path": {"RunSynchronousCommand"},
    "LocalAccounts": {"UserAccounts"},
    "LocalAccount": {"LocalAccounts"},
    "Group": {"LocalAccount"},
    "Name": {"LocalAccount"},
    "Key": {"ProductKey"},
    "JoinDomain": {"Identification"},
    "JoinWorkgroup": {"Identification"},
}

FORBIDDEN_NAME_CHARS = set("{}|~[]\\^':;<=>?@!\"#$%`()+/.,*&")

_failures: list[str] = []
_checks = 0
_skips = 0


def _say(text: str = "") -> None:
    sys.stdout.write(text + "\n")


def check(name: str, condition: bool, detail: str = "") -> bool:
    global _checks
    _checks += 1
    if condition:
        _say(f"  PASS  {name}" + (f"  ({detail})" if detail else ""))
    else:
        _say(f"  FAIL  {name}" + (f"  ({detail})" if detail else ""))
        _failures.append(name)
    return condition


def skip(name: str, reason: str) -> None:
    global _skips
    _skips += 1
    _say(f"  SKIP  {name}  —— {reason}")


def section(title: str) -> None:
    _say("")
    _say(f"[{title}]")


def local_name(tag: str) -> str:
    return tag.split("}", 1)[1] if tag.startswith("{") else tag


def namespace_of(tag: str) -> str:
    return tag[1:].split("}", 1)[0] if tag.startswith("{") else ""


def ordered_subset_check(label: str, actual: list[str], expected: list[str]) -> None:
    """actual 必须是 expected 的子序列：顺序一致，允许省略未生成的元素。"""
    cursor = 0
    bad: list[str] = []
    for name in actual:
        while cursor < len(expected) and expected[cursor] != name:
            cursor += 1
        if cursor >= len(expected):
            bad.append(name)
        else:
            cursor += 1
    check(
        f"{label} 元素顺序符合参考",
        not bad,
        "" if not bad else f"越序或未登记的元素: {', '.join(bad)}",
    )


def verify_root(root: ET.Element) -> None:
    section("命名空间与根元素")
    check("根元素名为 unattend", local_name(root.tag) == "unattend", root.tag)
    check("默认命名空间是 urn:schemas-microsoft-com:unattend",
          namespace_of(root.tag) == NS, namespace_of(root.tag))


def verify_wcm_declaration(raw: str) -> None:
    section("wcm 命名空间声明")
    check("xmlns:wcm 在文档中声明",
          'xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State"' in raw)
    check("xmlns 默认命名空间在文档中声明",
          f'xmlns="{NS}"' in raw)
    check("存在 XML 声明", raw.lstrip().startswith("<?xml"))


def verify_components(root: ET.Element) -> list[tuple[str, str, ET.Element]]:
    section("component 结构与配置传递")
    found: list[tuple[str, str, ET.Element]] = []
    seen: set[tuple[str, str]] = set()

    settings_nodes = [c for c in root if local_name(c.tag) == "settings"]
    check("至少一个 <settings>", len(settings_nodes) >= 1, f"{len(settings_nodes)} 个")

    for settings in settings_nodes:
        pass_name = settings.attrib.get("pass", "")
        check(f"pass 取值合法: {pass_name}", pass_name in ALLOWED_PASSES, pass_name)
        for component in settings:
            if local_name(component.tag) != "component":
                continue
            name = component.attrib.get("name", "")
            key = (pass_name, name)
            check(f"{name} 在同一 pass 内不重复", key not in seen, pass_name)
            seen.add(key)

            check(f"{name} 有 processorArchitecture",
                  component.attrib.get("processorArchitecture") == "amd64",
                  component.attrib.get("processorArchitecture", ""))
            check(f"{name} 有 publicKeyToken",
                  component.attrib.get("publicKeyToken") == "31bf3856ad364e35",
                  component.attrib.get("publicKeyToken", ""))
            check(f"{name} 有 language",
                  component.attrib.get("language") == "neutral",
                  component.attrib.get("language", ""))
            check(f"{name} 有 versionScope",
                  component.attrib.get("versionScope") == "nonSxS",
                  component.attrib.get("versionScope", ""))
            check(f"{name} 名称前缀合法", name.startswith("Microsoft-Windows-"), name)
            found.append((pass_name, name, component))
    return found


def verify_element_order(components: list[tuple[str, str, ET.Element]]) -> None:
    section("元素顺序（按 ADK Unattend.chm 的 Child Elements 列表）")

    for pass_name, name, component in components:
        children = [local_name(c.tag) for c in component if isinstance(c.tag, str)]

        if name == "Microsoft-Windows-Shell-Setup" and pass_name == "specialize":
            ordered_subset_check(f"{name}@{pass_name}", children, SHELL_SETUP_SPECIALIZE_ORDER)
        elif name == "Microsoft-Windows-Shell-Setup" and pass_name == "oobeSystem":
            ordered_subset_check(f"{name}@{pass_name}", children, SHELL_SETUP_OOBE_ORDER)
        elif name == "Microsoft-Windows-Deployment":
            ordered_subset_check(f"{name}@{pass_name}", children, DEPLOYMENT_CHILD_ORDER)

        for child in component:
            if not isinstance(child.tag, str):
                continue
            cname = local_name(child.tag)
            grandchildren = [local_name(g.tag) for g in child if isinstance(g.tag, str)]
            if cname == "OOBE":
                ordered_subset_check(f"{name}/OOBE", grandchildren, OOBE_CHILD_ORDER)
            elif cname == "AutoLogon":
                ordered_subset_check(f"{name}/AutoLogon", grandchildren, AUTOLOGON_CHILD_ORDER)
            elif cname == "Display":
                ordered_subset_check(f"{name}/Display", grandchildren, DISPLAY_CHILD_ORDER)
            elif cname == "Identification":
                ordered_subset_check(f"{name}/Identification", grandchildren,
                                     IDENTIFICATION_CHILD_ORDER)
            elif cname in ("SynchronousCommand", "RunSynchronousCommand"):
                ordered_subset_check(f"{name}/{cname}", grandchildren, SYNC_COMMAND_CHILD_ORDER)
            elif cname == "AsynchronousCommand":
                ordered_subset_check(f"{name}/{cname}", grandchildren, ASYNC_COMMAND_CHILD_ORDER)
            elif cname == "LocalAccount":
                ordered_subset_check(f"{name}/LocalAccount", grandchildren,
                                     LOCAL_ACCOUNT_CHILD_ORDER)


def walk(element: ET.Element, parent_name: str) -> None:
    for child in element:
        if not isinstance(child.tag, str):
            continue
        cname = local_name(child.tag)
        allowed = PARENT_RULES.get(cname)
        if allowed is not None:
            check(f"{cname} 的父元素合法（父={parent_name}）", parent_name in allowed,
                  f"允许: {', '.join(sorted(allowed))}")
        walk(child, cname)


def verify_nesting(root: ET.Element) -> None:
    section("父子嵌套（按参考 Parent Hierarchy）")
    for settings in root:
        for component in settings:
            if isinstance(component.tag, str) and local_name(component.tag) == "component":
                walk(component, "component")


def verify_pass_validity(components: list[tuple[str, str, ET.Element]]) -> None:
    section("设置所在 pass 合法（按参考 Valid Configuration Passes）")
    for pass_name, name, component in components:
        for child in component:
            if not isinstance(child.tag, str):
                continue
            allowed = VALID_PASSES.get((name, local_name(child.tag)))
            if allowed is None:
                continue
            check(f"{name}/{local_name(child.tag)} 出现在 {pass_name}",
                  pass_name in allowed, f"允许: {', '.join(sorted(allowed))}")


def verify_computer_name(components: list[tuple[str, str, ET.Element]], cfg: dict | None) -> None:
    section("验收：计算机名规则")
    node = None
    for pass_name, name, component in components:
        if name == "Microsoft-Windows-Shell-Setup" and pass_name == "specialize":
            found = component.find(f"{{{NS}}}ComputerName")
            if found is None:
                found = component.find("ComputerName")
            if found is not None:
                node = found
                break

    if not check("specialize 中存在 ComputerName", node is not None):
        return

    value = (node.text or "").strip()
    if value == "*":
        check("随机模式使用文档规定的星号", True, "*")
        if cfg:
            check("配置模式确为 random",
                  cfg.get("systemInfo", {}).get("computerNameMode") == "random",
                  cfg.get("systemInfo", {}).get("computerNameMode", ""))
        return

    check("ComputerName 非空", bool(value), value)
    check("ComputerName 不超过 15 字节", len(value.encode("utf-8")) <= 15,
          f"{len(value.encode('utf-8'))} 字节")
    check("ComputerName 不含空格", " " not in value, value)
    bad = sorted({c for c in value if c in FORBIDDEN_NAME_CHARS})
    check("ComputerName 不含禁用字符", not bad, "" if not bad else "".join(bad))
    check("ComputerName 不是纯数字", not re.fullmatch(r"\d+", value), value)

    if cfg is None:
        skip("计算机名与输入配置一致", "未提供 cfg.json")
        return

    info = cfg.get("systemInfo", {})
    mode = info.get("computerNameMode", "")
    configured = info.get("computerName", "")
    if mode == "fixed":
        check("fixed 模式取值与配置一致", value == configured, f"xml={value} cfg={configured}")
    elif mode == "prefixRandom":
        check("prefixRandom 模式保留前缀", value.startswith(configured),
              f"xml={value} prefix={configured}")
        check("prefixRandom 模式带随机后缀", len(value) > len(configured),
              f"xml={value} prefix={configured}")
    else:
        check(f"未知模式 {mode} 的行为符合预期", False, mode)


def verify_autologon(components: list[tuple[str, str, ET.Element]], cfg: dict | None) -> None:
    section("验收：AutoLogon 次数")
    component = None
    for pass_name, name, node in components:
        if name == "Microsoft-Windows-Shell-Setup" and pass_name == "oobeSystem":
            component = node
            break

    if not check("oobeSystem 中存在 Shell-Setup 组件", component is not None):
        return

    auto_logon = component.find(f"{{{NS}}}AutoLogon")
    if auto_logon is None:
        auto_logon = component.find("AutoLogon")

    if cfg is None:
        skip("AutoLogon 与输入配置一致", "未提供 cfg.json")
        return

    expected_enabled = bool(cfg.get("sysprep", {}).get("autoLogon", False))
    if not expected_enabled:
        check("配置未开启 AutoLogon 时不生成该元素", auto_logon is None)
        return

    if not check("配置开启 AutoLogon 时生成该元素", auto_logon is not None):
        return

    def child_text(parent: ET.Element, tag: str) -> str | None:
        node = parent.find(f"{{{NS}}}{tag}")
        if node is None:
            node = parent.find(tag)
        return None if node is None else (node.text or "").strip()

    enabled = child_text(auto_logon, "Enabled")
    count_raw = child_text(auto_logon, "LogonCount")
    username = child_text(auto_logon, "Username")

    check("AutoLogon/Enabled 为 true", enabled == "true", str(enabled))
    check("AutoLogon/LogonCount 存在", count_raw is not None, str(count_raw))

    if count_raw is not None and re.fullmatch(r"\d+", count_raw):
        expected_count = int(cfg.get("sysprep", {}).get("autoLogonCount", 1))
        check("LogonCount 与配置一致", int(count_raw) == expected_count,
              f"xml={count_raw} cfg={expected_count}")
    else:
        check("LogonCount 是整数", False, str(count_raw))

    expected_user = cfg.get("deploy", {}).get("userName") or "Administrator"
    check("AutoLogon/Username 与配置一致", username == expected_user,
          f"xml={username} cfg={expected_user}")

    # 密码：Winlogon 会静默忽略没有 <Password> 的 <AutoLogon>。
    # 有密码时必须写 base64(UTF-16LE(密码 + "Password")) 且 PlainText=false；
    # 空密码时不写 <Password>，改由 specialize 的 RunSynchronous 写 Winlogon 注册表。
    password = auto_logon.find(f"{{{NS}}}Password")
    if password is None:
        password = auto_logon.find("Password")
    plain = cfg.get("sysprep", {}).get("autoLogonPassword") or ""

    if not plain:
        check("空密码时不写 <AutoLogon>/<Password>", password is None)
        return

    if not check("有密码时生成 <AutoLogon>/<Password>", password is not None):
        return

    def password_child(tag: str) -> str | None:
        node = password.find(f"{{{NS}}}{tag}")
        if node is None:
            node = password.find(tag)
        return None if node is None else (node.text or "").strip()

    expected_value = base64.b64encode((plain + "Password").encode("utf-16-le")).decode("ascii")
    check("Password/Value 是 base64(UTF-16LE(密码+Password))",
          password_child("Value") == expected_value,
          f"xml={password_child('Value')} expected={expected_value}")
    check("Password/PlainText 为 false", password_child("PlainText") == "false",
          str(password_child("PlainText")))


def verify_autologon_known_vector() -> None:
    """已知向量：'abc' + 'Password' 的 base64 必须与独立计算值一致。

    这条不读产物，只固定编码规则本身，防止实现里悄悄改了填充或字节序。
    """
    section("验收：AutoLogon 密码编码已知向量")
    expected = base64.b64encode("abcPassword".encode("utf-16-le")).decode("ascii")
    check("已知向量 'abc' 的编码正确", expected == "YQBiAGMAUABhAHMAcwB3AG8AcgBkAA==", expected)
    check("已知向量长度是 4 的倍数", len(expected) % 4 == 0, str(len(expected)))


def verify_logon_landing(components: list[tuple[str, str, ET.Element]],
                         cfg: dict | None) -> None:
    """落点：登录时 -> FirstLogonCommands（同步）；进桌面 -> LogonCommands（异步）。"""
    section("验收：阶段落点与命令条数")
    component = None
    for pass_name, name, node in components:
        if name == "Microsoft-Windows-Shell-Setup" and pass_name == "oobeSystem":
            component = node
            break
    if component is None:
        check("oobeSystem 中存在 Shell-Setup 组件", False)
        return

    def collect(container_tag: str, child_tag: str) -> list[ET.Element]:
        container = component.find(f"{{{NS}}}{container_tag}")
        if container is None:
            container = component.find(container_tag)
        if container is None:
            return []
        return [c for c in container
                if isinstance(c.tag, str) and local_name(c.tag) == child_tag]

    sync_commands = collect("FirstLogonCommands", "SynchronousCommand")
    async_commands = collect("LogonCommands", "AsynchronousCommand")

    for label, commands in (("SynchronousCommand", sync_commands),
                            ("AsynchronousCommand", async_commands)):
        for index, command in enumerate(commands, start=1):
            order = None
            for child in command:
                if local_name(child.tag) == "Order":
                    order = (child.text or "").strip()
            check(f"第 {index} 条 {label} 的 Order 连续",
                  order is not None and order.isdigit() and int(order) == index, str(order))

    # AsynchronousCommand 不得出现 RequiresUserInput（参考标注为未使用）
    for command in async_commands:
        names = {local_name(c.tag) for c in command if isinstance(c.tag, str)}
        check("AsynchronousCommand 不含 RequiresUserInput", "RequiresUserInput" not in names,
              ", ".join(sorted(names)))

    if cfg is None:
        skip("阶段落点条数与输入一致", "未提供 cfg.json")
        return

    run_plan = cfg.get("runPlan", [])
    logon_tasks = [t for t in run_plan if t.get("phase") == "logon"]
    desktop_tasks = [t for t in run_plan if t.get("phase") == "desktop"]
    device_manager = 1 if cfg.get("firstLogon", {}).get("runDeviceManager") else 0

    expected_sync = len(logon_tasks) + device_manager
    check("FirstLogonCommands 条数与输入一致（设备管理器 + 登录时）",
          len(sync_commands) == expected_sync,
          f"xml={len(sync_commands)} cfg={expected_sync}")
    check("LogonCommands 条数与输入一致（进桌面）",
          len(async_commands) == len(desktop_tasks),
          f"xml={len(async_commands)} cfg={len(desktop_tasks)}")

    sync_lines = [child.text or "" for command in sync_commands for child in command
                  if local_name(child.tag) == "CommandLine"]
    async_lines = [child.text or "" for command in async_commands for child in command
                   if local_name(child.tag) == "CommandLine"]

    for task in logon_tasks:
        target = task.get("target", "")
        check(f"登录时任务落在 FirstLogonCommands: {target or '(空)'}",
              any(line.strip().startswith(target) for line in sync_lines),
              ", ".join(line.strip() for line in sync_lines))
    for task in desktop_tasks:
        target = task.get("target", "")
        check(f"进桌面任务落在 LogonCommands: {target or '(空)'}",
              any(line.strip().startswith(target) for line in async_lines),
              ", ".join(line.strip() for line in async_lines))

    if device_manager:
        check("设备管理器命令落在 FirstLogonCommands",
              any("devmgmt.msc" in line for line in sync_lines),
              ", ".join(line.strip() for line in sync_lines))


def verify_run_synchronous(components: list[tuple[str, str, ET.Element]],
                           cfg: dict | None) -> None:
    section("RunSynchronous（部署后阶段）")
    container = None
    for pass_name, name, component in components:
        if name == "Microsoft-Windows-Deployment" and pass_name == "specialize":
            container = component.find(f"{{{NS}}}RunSynchronous")
            if container is None:
                container = component.find("RunSynchronous")
            break

    if cfg is None:
        skip("RunSynchronous 与输入一致", "未提供 cfg.json")
        return

    post_install = [t for t in cfg.get("runPlan", []) if t.get("phase") == "postinstall"]
    commands = [] if container is None else [
        c for c in container if isinstance(c.tag, str) and local_name(c.tag) == "RunSynchronousCommand"
    ]

    # 空密码自动登录会在 specialize 里注入 4 条 reg add（AutoAdminLogon /
    # DefaultUserName / DefaultPassword / AutoLogonCount），计入期望条数。
    winlogon_commands = 0
    sysprep_cfg = cfg.get("sysprep", {})
    if sysprep_cfg.get("autoLogon") and not (sysprep_cfg.get("autoLogonPassword") or ""):
        winlogon_commands = 4 if int(sysprep_cfg.get("autoLogonCount", 1)) > 0 else 3

    expected = len(post_install) + winlogon_commands
    check("RunSynchronous 条数与输入一致", len(commands) == expected,
          f"xml={len(commands)} cfg={expected}（其中 Winlogon 注入 {winlogon_commands} 条）")

    if winlogon_commands:
        paths = []
        for command in commands:
            for child in command:
                if local_name(child.tag) == "Path":
                    paths.append(child.text or "")
        joined = "\n".join(paths)
        for token in ("AutoAdminLogon", "DefaultUserName", "DefaultPassword"):
            check(f"Winlogon 注入含 {token}", token in joined)
        check("DefaultPassword 写的是空字符串", '/v DefaultPassword /t REG_SZ /d "" /f' in joined)

    for index, command in enumerate(commands, start=1):
        order = None
        for child in command:
            if local_name(child.tag) == "Order":
                order = (child.text or "").strip()
        check(f"第 {index} 条 RunSynchronousCommand 的 Order 连续",
              order is not None and order.isdigit() and int(order) == index, str(order))


def verify_values(components: list[tuple[str, str, ET.Element]]) -> None:
    section("取值域")
    for pass_name, name, component in components:
        for element in component.iter():
            if not isinstance(element.tag, str):
                continue
            tag = local_name(element.tag)
            text = (element.text or "").strip()
            if tag == "NetworkLocation":
                check("NetworkLocation 取值合法", text in {"Home", "Work", "Other"}, text)
            elif tag == "ProtectYourPC":
                check("ProtectYourPC 取值合法", text in {"1", "2", "3"}, text)
            elif tag in ("Enabled", "HideEULAPage", "HideOnlineAccountScreens",
                         "HideWirelessSetupInOOBE", "RequiresUserInput"):
                check(f"{tag} 是布尔字面量", text in {"true", "false"}, text)
            elif tag == "Order":
                check("Order 是正整数", text.isdigit() and int(text) >= 1, text)
            elif tag == "Key":
                check("ProductKey/Key 含连字符", "-" in text, text)

        if name == "Microsoft-Windows-UnattendedJoin":
            identification = component.find(f"{{{NS}}}Identification")
            if identification is None:
                identification = component.find("Identification")
            if identification is not None:
                names = {local_name(c.tag) for c in identification if isinstance(c.tag, str)}
                check("Identification 只含 JoinDomain 或 JoinWorkgroup 之一",
                      len(names & {"JoinDomain", "JoinWorkgroup"}) == 1, ", ".join(sorted(names)))


def main(argv: list[str]) -> int:
    if len(argv) not in (2, 3):
        _say("usage: python tests/integration/verify_unattend.py <unattend.xml> [cfg.json]")
        return 2

    xml_path = argv[1]
    if not os.path.isfile(xml_path):
        _say(f"unattend.xml not found: {xml_path}")
        return 2

    try:
        with open(xml_path, "rb") as handle:
            raw = handle.read().decode("utf-8-sig")
    except OSError as exc:
        _say(f"cannot read {xml_path}: {exc}")
        return 2

    try:
        root = ET.fromstring(raw)
    except ET.ParseError as exc:
        _say(f"XML 解析失败: {exc}")
        return 1

    cfg = None
    cfg_path = argv[2] if len(argv) == 3 else os.path.join(os.path.dirname(xml_path), "cfg.json")
    if os.path.isfile(cfg_path):
        try:
            with open(cfg_path, "rb") as handle:
                cfg = json.loads(handle.read().decode("utf-8-sig"))
            _say(f"verify_unattend: {xml_path}")
            _say(f"config: {cfg_path}")
        except (OSError, json.JSONDecodeError) as exc:
            _say(f"cannot read config {cfg_path}: {exc}")
            return 2
    else:
        _say(f"verify_unattend: {xml_path}")
        _say(f"config: 未找到（{cfg_path}），交叉断言将跳过")

    verify_root(root)
    verify_wcm_declaration(raw)
    components = verify_components(root)
    verify_element_order(components)
    verify_nesting(root)
    verify_pass_validity(components)
    verify_computer_name(components, cfg)
    verify_autologon(components, cfg)
    verify_autologon_known_vector()
    verify_logon_landing(components, cfg)
    verify_run_synchronous(components, cfg)
    verify_values(components)

    _say("")
    if _failures:
        _say(f"RESULT: FAILED  ({len(_failures)}/{_checks} checks failed, {_skips} skipped)")
        for name in _failures:
            _say(f"  - {name}")
        return 1

    _say(f"RESULT: ALL PASSED  ({_checks} checks, {_skips} skipped)")
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass
    sys.exit(main(sys.argv))

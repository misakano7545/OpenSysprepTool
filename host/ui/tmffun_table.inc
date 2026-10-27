// 本文件由 tools/gen_tmffun.py 生成，不要手改。
// 坐标来自 docs/sc_layout/TMFFUN.json（DFM 逐字），
// 绑定与文案来自 tools/ui_bindings/tmffun.json。

namespace {

const ControlSpec kControlsSheet3[] = {
    {"Rb_SmartMoveDocuments", "radio", "智能转移目录", 19, 37, 134, 17, "optimization.documentsSmart", "moveDocuments", "smart", "", "", nullptr, 0, nullptr},
    {"Rb_DiyMoveDocuments", "radio", "指定转移目录", 19, 140, 134, 17, "optimization.documentsSmart", "moveDocuments", "diy", "", "", nullptr, 0, nullptr},
    {"Lab_SmartMoveDocumentsTip", "label", "说明:自动扫描目标系统最大剩余容量的分区，如果条件不成立则不转移。", 19, 68, 701, 37, "", "", "", "", "", nullptr, 0, nullptr},
    {"Lab_MoveDocumentsPath", "label", "转移目录:", 19, 177, 63, 21, "", "", "", "", "", nullptr, 0, nullptr},
    {"Btn_MoveDocuments_Open", "button", "浏览(&O)", 647, 171, 75, 27, "", "", "", "browse", "", nullptr, 0, nullptr},
    {"Lab_DiyMoveDocumentsTip", "label", "说明:转移到指定目录，如果条件不成立则执行智能转移目录机制。", 19, 212, 701, 45, "", "", "", "", "", nullptr, 0, nullptr},
    {"Btn_MoveDocuments_Exit", "button", "退出(&E)", 530, 303, 89, 27, "", "", "", "close", "", nullptr, 0, nullptr},
    {"Btn_MoveDocuments_Save", "button", "保存(&S)", 635, 303, 89, 27, "", "", "", "save", "", nullptr, 0, nullptr},
    {"Lab_Tip_MoveDocuments", "label", "", 21, 312, 487, 17, "", "", "", "", "status", nullptr, 0, nullptr},
    {"Ed_MovePath", "edit", "", 88, 172, 545, 26, "optimization.myDocumentsTarget", "", "", "", "", nullptr, 0, nullptr},
};
const int kControlCountSheet3 = static_cast<int>(sizeof(kControlsSheet3) / sizeof(kControlsSheet3[0]));

const ControlSpec kControlsSheet4[] = {
    {"Rb_SmartMoveFavorites", "radio", "智能转移目录", 19, 37, 134, 17, "optimization.favoritesSmart", "moveFavorites", "smart", "", "", nullptr, 0, nullptr},
    {"Lab_SmartMoveFavoritesTip", "label", "说明:自动扫描目标系统最大剩余容量的分区，如果条件不成立则不转移。", 19, 68, 701, 37, "", "", "", "", "", nullptr, 0, nullptr},
    {"Rb_DiyMoveFavorites", "radio", "指定转移目录", 19, 140, 134, 17, "optimization.favoritesSmart", "moveFavorites", "diy", "", "", nullptr, 0, nullptr},
    {"Lab_MoveFavoritesPath", "label", "转移目录:", 19, 177, 63, 21, "", "", "", "", "", nullptr, 0, nullptr},
    {"Ed_MoveFavorites_Path", "edit", "", 88, 172, 545, 26, "optimization.favoritesTarget", "", "", "", "", nullptr, 0, nullptr},
    {"Btn_MoveFavorites_Open", "button", "浏览(&O)", 647, 171, 75, 27, "", "", "", "browse", "", nullptr, 0, nullptr},
    {"Lab_DiyMoveFavoritesTip", "label", "说明:转移到指定目录，如果条件不成立则执行智能转移目录机制。", 19, 212, 701, 45, "", "", "", "", "", nullptr, 0, nullptr},
    {"Lab_Tip_MoveFavorites", "label", "", 21, 312, 487, 17, "", "", "", "", "status", nullptr, 0, nullptr},
    {"Btn_MoveFavorites_Exit", "button", "退出(&E)", 530, 303, 89, 27, "", "", "", "close", "", nullptr, 0, nullptr},
    {"Btn_MoveFavorites_Save", "button", "保存(&S)", 635, 303, 89, 27, "", "", "", "save", "", nullptr, 0, nullptr},
};
const int kControlCountSheet4 = static_cast<int>(sizeof(kControlsSheet4) / sizeof(kControlsSheet4[0]));

const char* const kItems_Cb_MovePageFileDrive[] = {
    "C:",
    "D:",
    "E:",
    "F:",
    "G:",
    "H:",
    "I:",
    "J:",
    "K:",
    "L:",
    "M:",
    "N:",
    "O:",
    "P:",
    "Q:",
    "R:",
    "S:",
    "T:",
    "U:",
    "V:",
    "W:",
    "X:",
    "Y:",
    "Z:",
};
const ControlSpec kControlsSheet7[] = {
    {"Rb_SmartMovePageFile", "radio", "智能转移", 19, 13, 134, 17, "optimization.pageFileSmart", "movePageFile", "smart", "", "", nullptr, 0, nullptr},
    {"Lab_SmartMovePageFileTip", "label", "说明:自动扫描目标系统最大剩余容量的分区，如果条件不成立则不转移。", 19, 44, 701, 37, "", "", "", "", "", nullptr, 0, nullptr},
    {"Rb_DiyMovePageFile", "radio", "指定转移", 19, 84, 134, 17, "optimization.pageFileSmart", "movePageFile", "diy", "", "", nullptr, 0, nullptr},
    {"Gb_DiyMovePageFile", "group", "", 19, 107, 701, 182, "", "", "", "", "", nullptr, 0, nullptr},
    {"Lab_MovePageFileDrive", "label", "指定转移到的盘符:", 3, 12, 110, 21, "", "", "", "", "", nullptr, 0, "Gb_DiyMovePageFile"},
    {"Cb_MovePageFileDrive", "combo", "", 128, 7, 140, 26, "optimization.pageFileDrive", "", "", "", "", kItems_Cb_MovePageFileDrive, 24, "Gb_DiyMovePageFile"},
    {"Lab_MovePageFileDriveTip", "label", "说明: 如果目标系统不存在你选择的盘符则执行智能转移机制。", 283, 12, 366, 21, "", "", "", "", "", nullptr, 0, "Gb_DiyMovePageFile"},
    {"Rb_PageFilevarValue", "radio", "指定虚拟内存值为物理内存的倍数", 3, 52, 214, 17, "optimization.pageFileSizeMode", "pageFileSize", "multiplier", "", "", nullptr, 0, "Gb_DiyMovePageFile"},
    {"Lab_PageFileVMinisize", "label", "虚拟内存最小值倍数:", 3, 90, 119, 21, "", "", "", "", "", nullptr, 0, "Gb_DiyMovePageFile"},
    {"Ed_PageFileVMinisize", "number", "", 128, 85, 140, 26, "optimization.pageFileMinMultiplier", "", "", "", "", nullptr, 0, "Gb_DiyMovePageFile"},
    {"Lab_PageFileVMaxsize", "label", "虚拟内存最大值倍数:", 3, 130, 119, 21, "", "", "", "", "", nullptr, 0, "Gb_DiyMovePageFile"},
    {"Ed_PageFileVMaxsize", "number", "", 128, 125, 140, 26, "optimization.pageFileMaxMultiplier", "", "", "", "", nullptr, 0, "Gb_DiyMovePageFile"},
    {"Rb_PageFileconstValue", "radio", "指定虚拟内存固定值", 339, 52, 214, 17, "optimization.pageFileSizeMode", "pageFileSize", "fixed", "", "", nullptr, 0, "Gb_DiyMovePageFile"},
    {"Lab_PageFileCMinisize", "label", "虚拟内存最小固定值:", 339, 90, 119, 21, "", "", "", "", "", nullptr, 0, "Gb_DiyMovePageFile"},
    {"Ed_PageFileCMinisize", "number", "", 464, 85, 140, 26, "optimization.pageFileMinFixedMb", "", "", "", "", nullptr, 0, "Gb_DiyMovePageFile"},
    {"Lab_PageFileCMaxsize", "label", "虚拟内存最大固定值:", 339, 130, 119, 21, "", "", "", "", "", nullptr, 0, "Gb_DiyMovePageFile"},
    {"Ed_PageFileCMaxsize", "number", "", 464, 125, 140, 26, "optimization.pageFileMaxFixedMb", "", "", "", "", nullptr, 0, "Gb_DiyMovePageFile"},
    {"Lab_Tip_MovePagefile", "label", "", 19, 320, 497, 17, "", "", "", "", "status", nullptr, 0, nullptr},
    {"Btn_MovePageFile_Exit", "button", "退出(&E)", 538, 311, 89, 27, "", "", "", "close", "", nullptr, 0, nullptr},
    {"Btn_MovePageFile_Save", "button", "保存(&S)", 643, 311, 89, 27, "", "", "", "save", "", nullptr, 0, nullptr},
};
const int kControlCountSheet7 = static_cast<int>(sizeof(kControlsSheet7) / sizeof(kControlsSheet7[0]));

const TabSpec kTabSheet1 = {
    "MlSkinTabSheet1",
    "模块1设置",
    "模块1设置的控件尚未逐个核对（见 docs/SC窗体对照.md §4.4），本轮不做",
    nullptr,
    0,
};

const TabSpec kTabSheet2 = {
    "MlSkinTabSheet2",
    "模块2设置",
    "模块2设置同上，本轮不做",
    nullptr,
    0,
};

const TabSpec kTabSheet3 = {
    "MlSkinTabSheet3",
    "转移文档",
    "转移文档：SC 用两个单选区分「智能转移目录」与「指定转移目录」",
    kControlsSheet3,
    kControlCountSheet3,
};

const TabSpec kTabSheet4 = {
    "MlSkinTabSheet4",
    "转移收藏夹",
    "转移收藏夹：与转移文档同构",
    kControlsSheet4,
    kControlCountSheet4,
};

const TabSpec kTabSheet5 = {
    "MlSkinTabSheet5",
    "新增用户",
    "新增用户：账号/组/密码的落地方式待定，本轮不做",
    nullptr,
    0,
};

const TabSpec kTabSheet6 = {
    "MlSkinTabSheet6",
    "预置IP",
    "预置IP：主窗体已能配置，这里的详细页待下一轮",
    nullptr,
    0,
};

const TabSpec kTabSheet7 = {
    "MlSkinTabSheet7",
    "转移虚拟内存",
    "转移虚拟内存：智能/指定盘符 + 大小（按物理内存倍数 或 固定值）",
    kControlsSheet7,
    kControlCountSheet7,
};

const TabSpec kTabSheet8 = {
    "MlSkinTabSheet8",
    "固定文件",
    "固定文件：主窗体字段已实现，这里的清单页待下一轮",
    nullptr,
    0,
};

const TabSpec kTabSheet9 = {
    "MlSkinTabSheet9",
    "取消固定",
    "取消固定：同上",
    nullptr,
    0,
};

const TabSpec kTabSheet10 = {
    "MlSkinTabSheet10",
    "OEM",
    "OEM：主窗体已能配置，这里的详细页待下一轮",
    nullptr,
    0,
};

const TabSpec kTabSheet11 = {
    "MlSkinTabSheet11",
    "选择网络",
    "选择网络：主窗体已能配置，这里的详细页待下一轮",
    nullptr,
    0,
};

const TabSpec kTabTrueTrans = {
    "TrueTrans",
    "TrueTrans",
    "TrueTrans 是 SC 的会员版部署模式，本工具不做",
    nullptr,
    0,
};

const TabSpec kTabSheet13 = {
    "MlSkinTabSheet13",
    "登录",
    "登录：自动登录项主窗体已能配置，这里的详细页待下一轮",
    nullptr,
    0,
};

const TabSpec kTabSheet12 = {
    "MlSkinTabSheet12",
    "文件锁定",
    "文件锁定：防篡改清单页待下一轮",
    nullptr,
    0,
};

const TabSpec kTabPage_Tampering = {
    "Page_Tampering",
    "Page_Tampering",
    "防篡改清单页：机制已实现（deploy.tamperFiles），清单编辑界面待下一轮",
    nullptr,
    0,
};

const TabSpec kTabSheet14 = {
    "MlSkinTabSheet14",
    "图片部署",
    "图片部署：主窗体已能配置，这里的详细页待下一轮",
    nullptr,
    0,
};

const TabSpec kTabSheet15 = {
    "MlSkinTabSheet15",
    "封装高级设置",
    "封装高级设置：待下一轮",
    nullptr,
    0,
};

const TabSpec kTabSheet16 = {
    "MlSkinTabSheet16",
    "补丁安装",
    "补丁安装：本工具不做补丁安装（MSI/MSU 安装与回滚是独立一块），此页留空",
    nullptr,
    0,
};

const TabSpec kTabSheet17 = {
    "MlSkinTabSheet17",
    "End",
    "End：SC 的内部结束页，没有界面内容",
    nullptr,
    0,
};

const TabSpec kTabSheet18 = {
    "MlSkinTabSheet18",
    "MlSkinTabSheet18",
    "空页（Caption 未本地化），不做",
    nullptr,
    0,
};

const TabSpec* const kTabs[] = {
    &kTabSheet1,
    &kTabSheet2,
    &kTabSheet3,
    &kTabSheet4,
    &kTabSheet5,
    &kTabSheet6,
    &kTabSheet7,
    &kTabSheet8,
    &kTabSheet9,
    &kTabSheet10,
    &kTabSheet11,
    &kTabTrueTrans,
    &kTabSheet13,
    &kTabSheet12,
    &kTabPage_Tampering,
    &kTabSheet14,
    &kTabSheet15,
    &kTabSheet16,
    &kTabSheet17,
    &kTabSheet18,
};
const int kTabCount = static_cast<int>(sizeof(kTabs) / sizeof(kTabs[0]));

} // namespace

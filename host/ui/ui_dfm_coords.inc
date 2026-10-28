// 本文件由 tools/gen_ui_coords.py 生成，勿手改。
// 依据：docs/sc_layout/TSCMF.json（DFM 原样坐标，800x398 设计空间）
//
// 每条对应「页面 / 分组框 / 控件」的矩形。分组框用绝对坐标，
// 组内控件用相对分组框的坐标 —— 与 DFM 的语义一致。

struct DfmCoord
{
    const char* page;
    const char* group;   // 空 = 页面直属控件
    const char* field;   // 空 = 这一条描述分组框本身
    int x;
    int y;
    int width;
    int height;
    bool visible;        // DFM 的 Visible；false = 点按钮才显示的面板
};

inline constexpr DfmCoord kDfmCoords[] = {
    {"system", "Gb_Osbasicinfo", "Lab_Osnamev", 80, 10, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_Osntverv", 80, 37, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_SysprepSupportv", 80, 223, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_Osdrivesv", 80, 91, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_Osusesizev", 80, 169, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_OsUserv", 80, 115, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_OsRelativesizev", 80, 196, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_RunEnvironv", 80, 142, 109, 21, true},
    {"system", "Gb_Osallinfo", "Btn_Scan", 228, 245, 140, 42, true},
    {"system", "Gb_ScanOutList", "Lab_ScanResults", 3, 7, 365, 20, true},
    {"system", "Gb_SRS", "Btn_PEloadsrs", 429, 202, 110, 33, true},
    {"system", "Gb_SRS", "RB_LoadSCsrs", 261, 92, 147, 17, true},
    {"system", "Gb_SRS", "RB_Loaddiy", 261, 130, 143, 17, true},
    {"system", "Gb_SRS", "Cb_osDrives", 346, 206, 62, 26, true},
    {"system", "Gb_SRS", "Ed_Loaddiy", 261, 158, 278, 26, true},
    {"system", "Gb_Osbasicinfo", "", 600, 9, 192, 264, true},
    {"system", "Gb_Tools", "", 600, 274, 192, 104, true},
    {"system", "Gb_Osallinfo", "", 16, 9, 572, 352, true},
    {"system", "Pan_Line1", "", 594, 34, 1, 344, true},
    {"system", "Gb_ScanOutList", "", 16, 9, 572, 352, false},
    {"system", "Gb_Repairend", "", 16, 9, 572, 352, false},
    {"system", "Btn_help_V", "", 18, 371, 74, 20, true},
    {"system", "Gb_HelpV", "", 16, 9, 572, 352, false},
    {"system", "Gb_SRS", "", 16, 9, 572, 352, false},
    {"tasks", "Gb_Tasklist", "Cb_Tasktype", 86, 267, 106, 26, true},
    {"tasks", "Gb_Tasklist", "Cb_RunWait", 278, 299, 106, 26, true},
    {"tasks", "Gb_Tasklist", "Cb_RunMode", 86, 299, 106, 26, true},
    {"tasks", "Gb_Tasklist", "Cb_RunAfter", 662, 299, 106, 26, true},
    {"tasks", "Gb_Tasklist", "Cb_RunPath", 86, 331, 106, 26, true},
    {"tasks", "Gb_Tasklist", "Ed_RunPath", 211, 331, 259, 26, true},
    {"tasks", "Gb_Tasklist", "Ed_CmdLine", 479, 331, 96, 26, true},
    {"tasks", "Gb_Tasklist", "Btn_Add", 662, 331, 106, 27, true},
    {"tasks", "Gb_Tasklist", "Lv_Tasklist", 15, 3, 753, 236, true},
    {"tasks", "Gb_Tasklist", "Sed_WaitTime", 469, 299, 90, 26, true},
    {"tasks", "Gb_Tasklist", "", 3, 3, 794, 375, true},
    {"deploy", "Gb_DeploySet", "Ed_PicPath", 119, 11, 322, 26, true},
    {"deploy", "Gb_DeploySet", "Ed_PicSpeed", 640, 11, 49, 26, true},
    {"deploy", "Gb_DeploySet", "Cb_DeployResolution", 119, 78, 97, 26, true},
    {"deploy", "Gb_DeploySet", "Ed_DeployResolution_Width", 222, 78, 69, 26, true},
    {"deploy", "Gb_DeploySet", "Ed_DeployResolution_Height", 309, 78, 69, 26, true},
    {"deploy", "Gb_DeploySet", "Cb_Skipoobe", 14, 122, 102, 17, true},
    {"deploy", "Gb_DeploySet", "Cb_AddUser", 273, 122, 154, 17, true},
    {"deploy", "Gb_DeploySet", "Ed_DeployFontcolour", 536, 80, 78, 26, true},
    {"deploy", "Gb_DeploySet", "Btn_DisableVmrun", 433, 122, 153, 17, true},
    {"deploy", "Gb_DeploySet", "Cb_Tampering", 599, 122, 116, 17, true},
    {"deploy", "Gb_DeploySet", "Cb_Admin", 119, 122, 148, 17, true},
    {"deploy", "Gb_DeploySet", "Ed_DeployTitle", 263, 44, 444, 26, true},
    {"deploy", "Gb_DeploySet", "", 36, 20, 725, 173, true},
    {"deploy", "Gb_DeployModular", "", 36, 199, 725, 162, true},
    {"sysprep", "Gb_Sysprep_Runset", "Cb_Uninstalldriver", 3, 12, 190, 17, true},
    {"sysprep", "Gb_Sysprep_Runset", "Cb_LoadSrsDrv", 210, 12, 198, 17, true},
    {"sysprep", "Gb_Sysprep_Runset", "Cb_Createnewsid", 427, 12, 286, 17, true},
    {"sysprep", "Gb_Sysprep_Type", "Rb_Sysprep_Type1", 3, 12, 190, 17, true},
    {"sysprep", "Gb_Sysprep_Sysset", "Cb_Cutdesktopfile", 3, 12, 190, 17, true},
    {"sysprep", "Gb_Sysprep_Sysset", "Cb_Cleanjunkfiles", 210, 12, 190, 17, true},
    {"sysprep", "Gb_Sysprep_Sysset", "Cb_Dllcache", 427, 11, 286, 17, true},
    {"sysprep", "Gb_Sysprep_Netset", "Rb_WorkGroup", 3, 14, 65, 17, true},
    {"sysprep", "Gb_Sysprep_Netset", "Rb_Domain", 169, 14, 67, 17, true},
    {"sysprep", "Gb_Sysprep_Netset", "Ed_Domain_name", 238, 9, 97, 26, true},
    {"sysprep", "Gb_Sysprep_Netset", "Ed_Domain_user", 418, 9, 97, 26, true},
    {"sysprep", "Gb_Sysprep_Netset", "Ed_Domain_pw", 616, 9, 97, 26, true},
    {"sysprep", "Gb_Sysprep_Netset", "Ed_WorkGroup", 70, 9, 75, 26, true},
    {"sysprep", "Gb_Sysprep_Ser", "Rb_EveryOne", 3, 12, 118, 17, true},
    {"sysprep", "Gb_Sysprep_Ser", "Rb_EverySer", 138, 12, 150, 17, true},
    {"sysprep", "Gb_Sysprep_Ser", "Se_Serconut", 294, 3, 83, 26, true},
    {"sysprep", "Btn_Syspreprun", "", 655, 342, 110, 36, true},
    {"sysprep", "Gb_Sysprep_Runset", "", 40, 79, 725, 53, true},
    {"sysprep", "Gb_Sysprep_Type", "", 40, 20, 725, 53, true},
    {"sysprep", "Gb_Sysprep_Sysset", "", 40, 138, 725, 53, true},
    {"sysprep", "Gb_Sysprep_Netset", "", 40, 197, 725, 53, true},
    {"sysprep", "Gb_Sysprep_Ser", "", 40, 256, 725, 53, true},
    {"sysprep", "Lab_AfterSysprep", "", 359, 352, 117, 20, true},
    {"sysprep", "Cb_AfterSysprep", "", 482, 347, 154, 26, true},
    {"sysprep", "Gb_Temp", "", 40, 315, 725, 14, true},
    {"sysprep", "Btn_Sysprepaset", "", 40, 342, 110, 36, true},
};

// system 页 DFM 里另有以下控件未进坐标表（模型里没有对应字段）：
//   Pic_Osname、Pic_Csize、Pic_SysprepSupportok、Pic_SysprepSupportno、Lab_Osname、Lab_OsBit、Lab_OsBitv、Lab_Osntver、Lab_SysprepSupport、Lab_Osdrives、Lab_Osusesize、Lab_OsUser、Lab_OsRelativesize、Lab_RunEnviron、Pic_Usm、Pic_DrvCeo、Pic_Mbox、Lab_Usm、Lab_DrvCeo、Lab_Mbox、Lab_SoftMore、Pan_Scan、Pic_Scan、Btn_ScanTip1、Btn_ScanTip2、Pan_Scanning、Pic_Scanning、Lab_Scanning、Mb_ScanOutList、Btn_OsRepair、ScanBar1、Lab_Rescanf、Btn_Repairend、Lab_Repairend、Lab_Rescan、Pic_helpv、Btn_Goback、Lab_OsDrive、Pic_pedrive、Lab_PEdrive、Lab_PEdriveinfo、Lab_PEdriveinfoshow
// tasks 页 DFM 里另有以下控件未进坐标表（模型里没有对应字段）：
//   Lab_Tasktype、Lab_RunPlan、Lab_RunWait、Lab_RunMode、Lab_RunAfter、Lab_RunPath、Btn_Open、Pl_RunPlan、Btn_RunPlan1、Btn_RunPlan2、Btn_RunPlan3、Btn_RunPlan4、Btn_RunPlan5、Bar2、Bar1、Lab_WaitTime_S、Lab_WaitTime
// deploy 页 DFM 里另有以下控件未进坐标表（模型里没有对应字段）：
//   Btn_Open_Pic、Lab_PicSpeed、Lab_PicTime、Cb_PicType、Lab_DeployResolution、Lab_DeployResolutionX、Lab_DeployFontcolour、Pan_DeployFontcolour、Btn_DeployFontcolour、Cb_DeployTitle、Lv_DeployModular、Bar_Dm1、Bar_Dm2
// sysprep 页 DFM 里另有以下控件未进坐标表（模型里没有对应字段）：
//   Rb_Sysprep_Type2、Rb_Sysprep_Type3、Lab_Domain_user、Lab_Domain_pw

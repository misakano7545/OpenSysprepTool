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
    {"system", "Gb_Osbasicinfo", "Lab_Osnamev", 0, 0, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_Osntverv", 0, 0, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_SysprepSupportv", 0, 0, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_Osdrivesv", 0, 0, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_Osusesizev", 0, 0, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_OsUserv", 0, 0, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_OsRelativesizev", 0, 0, 109, 21, true},
    {"system", "Gb_Osbasicinfo", "Lab_RunEnvironv", 0, 0, 109, 21, true},
    {"system", "Gb_Osallinfo", "Btn_Scan", 20, 11, 140, 42, true},
    {"system", "Gb_ScanOutList", "Lab_ScanResults", 0, 0, 365, 20, true},
    {"system", "Gb_SRS", "Btn_PEloadsrs", 0, 0, 110, 33, true},
    {"system", "Gb_SRS", "RB_LoadSCsrs", 0, 0, 147, 17, true},
    {"system", "Gb_SRS", "RB_Loaddiy", 0, 0, 143, 17, true},
    {"system", "Gb_SRS", "Cb_osDrives", 0, 0, 62, 26, true},
    {"system", "Gb_SRS", "Ed_Loaddiy", 0, 0, 278, 26, true},
    {"system", "Gb_Osbasicinfo", "", 600, 9, 192, 264, true},
    {"system", "Gb_Tools", "", 600, 274, 192, 104, true},
    {"system", "Gb_Osallinfo", "", 16, 9, 572, 352, true},
    {"system", "Pan_Line1", "", 594, 34, 1, 344, true},
    {"system", "Gb_ScanOutList", "", 16, 9, 572, 352, true},
    {"system", "Gb_Repairend", "", 16, 9, 572, 352, true},
    {"system", "Btn_help_V", "", 18, 371, 74, 20, true},
    {"system", "Gb_HelpV", "", 16, 9, 572, 352, true},
    {"system", "Gb_SRS", "", 16, 9, 572, 352, true},
    {"tasks", "Gb_Tasklist", "Cb_Tasktype", 0, 0, 106, 26, true},
    {"tasks", "Gb_Tasklist", "Cb_RunWait", 0, 0, 106, 26, true},
    {"tasks", "Gb_Tasklist", "Cb_RunMode", 0, 0, 106, 26, true},
    {"tasks", "Gb_Tasklist", "Cb_RunAfter", 0, 0, 106, 26, true},
    {"tasks", "Gb_Tasklist", "Cb_RunPath", 0, 0, 106, 26, true},
    {"tasks", "Gb_Tasklist", "Ed_RunPath", 0, 0, 259, 26, true},
    {"tasks", "Gb_Tasklist", "Ed_CmdLine", 0, 0, 96, 26, true},
    {"tasks", "Gb_Tasklist", "Btn_Add", 0, 0, 106, 27, true},
    {"tasks", "Gb_Tasklist", "Lv_Tasklist", 0, 0, 753, 236, true},
    {"tasks", "Gb_Tasklist", "Sed_WaitTime", 0, 0, 90, 26, true},
    {"tasks", "Gb_Tasklist", "", 3, 3, 794, 375, true},
    {"deploy", "Gb_DeploySet", "Ed_PicPath", 0, 0, 322, 26, true},
    {"deploy", "Gb_DeploySet", "Ed_PicSpeed", 0, 0, 49, 26, true},
    {"deploy", "Gb_DeploySet", "Cb_DeployResolution", 0, 0, 97, 26, true},
    {"deploy", "Gb_DeploySet", "Ed_DeployResolution_Width", 0, 0, 69, 26, true},
    {"deploy", "Gb_DeploySet", "Ed_DeployResolution_Height", 0, 0, 69, 26, true},
    {"deploy", "Gb_DeploySet", "Cb_Skipoobe", 0, 0, 102, 17, true},
    {"deploy", "Gb_DeploySet", "Cb_AddUser", 0, 0, 154, 17, true},
    {"deploy", "Gb_DeploySet", "Ed_DeployFontcolour", 0, 0, 78, 26, true},
    {"deploy", "Gb_DeploySet", "Btn_DisableVmrun", 0, 0, 153, 17, true},
    {"deploy", "Gb_DeploySet", "Cb_Tampering", 0, 0, 116, 17, true},
    {"deploy", "Gb_DeploySet", "Cb_Admin", 0, 0, 148, 17, true},
    {"deploy", "Gb_DeploySet", "Ed_DeployTitle", 0, 0, 444, 26, true},
    {"deploy", "Gb_DeploySet", "", 36, 20, 725, 173, true},
    {"deploy", "Gb_DeployModular", "", 36, 199, 725, 162, true},
    {"sysprep", "Gb_Sysprep_Runset", "Cb_Uninstalldriver", 0, 0, 190, 17, true},
    {"sysprep", "Gb_Sysprep_Runset", "Cb_LoadSrsDrv", 0, 0, 198, 17, true},
    {"sysprep", "Gb_Sysprep_Runset", "Cb_Createnewsid", 0, 0, 286, 17, true},
    {"sysprep", "Gb_Sysprep_Type", "Rb_Sysprep_Type1", 0, 0, 190, 17, true},
    {"sysprep", "Gb_Sysprep_Sysset", "Cb_Cutdesktopfile", 0, 0, 190, 17, true},
    {"sysprep", "Gb_Sysprep_Sysset", "Cb_Cleanjunkfiles", 0, 0, 190, 17, true},
    {"sysprep", "Gb_Sysprep_Sysset", "Cb_Dllcache", 0, 0, 286, 17, true},
    {"sysprep", "Gb_Sysprep_Netset", "Rb_WorkGroup", 0, 0, 65, 17, true},
    {"sysprep", "Gb_Sysprep_Netset", "Rb_Domain", 0, 0, 67, 17, true},
    {"sysprep", "Gb_Sysprep_Netset", "Ed_Domain_name", 0, 0, 97, 26, true},
    {"sysprep", "Gb_Sysprep_Netset", "Ed_Domain_user", 0, 0, 97, 26, true},
    {"sysprep", "Gb_Sysprep_Netset", "Ed_Domain_pw", 0, 0, 97, 26, true},
    {"sysprep", "Gb_Sysprep_Netset", "Ed_WorkGroup", 0, 0, 75, 26, true},
    {"sysprep", "Gb_Sysprep_Ser", "Rb_EveryOne", 0, 0, 118, 17, true},
    {"sysprep", "Gb_Sysprep_Ser", "Rb_EverySer", 0, 0, 150, 17, true},
    {"sysprep", "Gb_Sysprep_Ser", "Se_Serconut", 0, 0, 83, 26, true},
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

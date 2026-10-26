// 本文件由 tools/gen_ui_page.py 生成，勿手改。
// 依据：docs/sc_layout/TSCMF.json 的 MlSkinTabSheet2（DFM 原样坐标）
// 映射表：tools/ui_bindings/target.json

    {
        Group group;
        group.id = "Gb_Targetsys_Desktop";
        group.caption = "首次进桌面";
        group.x = 36;
        group.y = 108;
        group.width = 738;
        group.height = 146;

        {
            Field field;
            field.id = "Cb_ShowDevmgmt";
            field.dfmClass = "TMlSkinCheckBox";
            field.kind = FieldKind::Check;
            field.label = "运行设备管理器";
            field.binding = "firstLogon.runDeviceManager";
            field.checked = cfg.firstLogon.runDeviceManager;
            field.x = 3;
            field.y = 15;
            field.width = 163;
            field.height = 17;
            field.note = "行为落到运行计划的『进桌面』阶段";
            group.fields.push_back(std::move(field));
        }
        // 跳过 Cb_CreateConnection：宽带连接已实现（firstLogon.createConnection），但界面字段未接，先留空位不画
        {
            Field field;
            field.id = "Cb_AutoSetip";
            field.dfmClass = "TMlSkinCheckBox";
            field.kind = FieldKind::Check;
            field.label = "自动设置ip地址";
            field.binding = "optimization.autoSetIp";
            field.checked = cfg.optimization.autoSetIp;
            field.x = 554;
            field.y = 47;
            field.width = 163;
            field.height = 17;
            field.note = "三种模式见 core/netcfg";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Cb_CloseKeypad";
            field.dfmClass = "TMlSkinCheckBox";
            field.kind = FieldKind::Check;
            field.label = "关闭笔记本小键盘";
            field.binding = "optimization.closeNumLock";
            field.checked = cfg.optimization.closeNumLock;
            field.x = 554;
            field.y = 15;
            field.width = 163;
            field.height = 17;
            field.note = "写 InitialKeyboardIndicators";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Cb_MoveFavorites";
            field.dfmClass = "TMlSkinCheckBox";
            field.kind = FieldKind::Check;
            field.label = "智能转移IE收藏夹";
            field.binding = "optimization.moveFavorites";
            field.checked = cfg.optimization.moveFavorites;
            field.x = 188;
            field.y = 47;
            field.width = 163;
            field.height = 17;
            field.note = "对应 --prepare 的 HiveSet";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Cb_MoveDocuments";
            field.dfmClass = "TMlSkinCheckBox";
            field.kind = FieldKind::Check;
            field.label = "智能转移我的文档";
            field.binding = "optimization.moveDocuments";
            field.checked = cfg.optimization.moveDocuments;
            field.x = 3;
            field.y = 47;
            field.width = 163;
            field.height = 17;
            field.note = "对应 --prepare 的 HiveSet";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Cb_MovePageFile";
            field.dfmClass = "TMlSkinCheckBox";
            field.kind = FieldKind::Check;
            field.label = "智能转移虚拟内存";
            field.binding = "optimization.autoPagefileDrive";
            field.checked = cfg.optimization.autoPagefileDrive;
            field.x = 373;
            field.y = 47;
            field.width = 163;
            field.height = 17;
            field.note = "SC 的 Rb_SmartMovePageFile";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Cb_Reboot";
            field.dfmClass = "TMlSkinCheckBox";
            field.kind = FieldKind::Check;
            field.label = "运行完成后重启计算机";
            field.binding = "firstLogon.rebootAfterRun";
            field.checked = cfg.firstLogon.rebootAfterRun;
            field.x = 554;
            field.y = 107;
            field.width = 150;
            field.height = 17;
            field.note = "首登命令末尾追加 shutdown";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Cb_ClearSrs";
            field.dfmClass = "TMlSkinCheckBox";
            field.kind = FieldKind::Check;
            field.label = "清理多余的SRS驱动";
            field.binding = "optimization.clearSrsDrivers";
            field.checked = cfg.optimization.clearSrsDrivers;
            field.x = 373;
            field.y = 107;
            field.width = 133;
            field.height = 17;
            field.note = "配合 SRS 注入使用";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Cb_CloseRestartbox";
            field.dfmClass = "TMlSkinCheckBox";
            field.kind = FieldKind::Check;
            field.label = "关闭系统弹出的重启对话框";
            field.binding = "optimization.closeRebootDialog";
            field.checked = cfg.optimization.closeRebootDialog;
            field.x = 188;
            field.y = 77;
            field.width = 179;
            field.height = 17;
            field.note = "NoRebootDialog";
            group.fields.push_back(std::move(field));
        }
        // 跳过 Cb_Locktaskbar：任务栏固定的机制已实现，但 SC 的这组复选框是拿来推推广快捷方式的（§7），界面不给这个勾选框
        // 跳过 Cb_Unlocktaskbar：同上：取消固定的机制在 optimization.unlockTaskbarFiles，界面不给推广用的勾选框
        // 跳过 Cb_Saveiehp：§7 红线：改写浏览器主页
        // 跳过 Cb_SaveieFavorites：§7 红线：预置收藏夹
        {
            Field field;
            field.id = "Cb_Dormancy";
            field.dfmClass = "TMlSkinCheckBox";
            field.kind = FieldKind::Check;
            field.label = "开启笔记本休眠";
            field.binding = "optimization.enableHibernate";
            field.checked = cfg.optimization.enableHibernate;
            field.x = 373;
            field.y = 15;
            field.width = 163;
            field.height = 17;
            field.note = "powercfg /h on";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Cb_SetNetWork";
            field.dfmClass = "TMlSkinCheckBox";
            field.kind = FieldKind::Check;
            field.label = "自动设置网络位置";
            field.binding = "optimization.autoNetworkLocation";
            field.checked = cfg.optimization.autoNetworkLocation;
            field.x = 3;
            field.y = 77;
            field.width = 179;
            field.height = 17;
            field.note = "图纸只有一个勾选框；勾选即设为工作网络（networkLocation 留给命令行）";
            group.fields.push_back(std::move(field));
        }

        page.groups.push_back(std::move(group));
    }

    {
        Group group;
        group.id = "Gb_Targetsys_info";
        group.caption = "系统信息";
        group.x = 36;
        group.y = 9;
        group.width = 738;
        group.height = 93;

        {
            Field field;
            field.id = "Lab_Owner";
            field.dfmClass = "TMlSkinLabel";
            field.kind = FieldKind::Info;
            field.value = "注册名称:";
            field.x = 3;
            field.y = 12;
            field.width = 61;
            field.height = 21;
            field.note = "图纸 Caption 逐字";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Ed_Owner";
            field.dfmClass = "TMlSkinEdit";
            field.kind = FieldKind::Edit;
            field.label = "注册名称";
            field.binding = "systemInfo.registeredName";
            field.value = cfg.systemInfo.registeredName;
            field.x = 66;
            field.y = 5;
            field.width = 135;
            field.height = 26;
            field.note = "图纸 Caption 在 Lab_Owner 上";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Ed_Organization";
            field.dfmClass = "TMlSkinEdit";
            field.kind = FieldKind::Edit;
            field.label = "注册单位";
            field.binding = "systemInfo.registeredOrg";
            field.value = cfg.systemInfo.registeredOrg;
            field.x = 294;
            field.y = 3;
            field.width = 131;
            field.height = 26;
            field.note = "图纸 Caption 在 Lab_Organization 上";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Lab_Organization";
            field.dfmClass = "TMlSkinLabel";
            field.kind = FieldKind::Info;
            field.value = "注册单位:";
            field.x = 231;
            field.y = 10;
            field.width = 61;
            field.height = 21;
            field.note = "图纸 Caption 逐字";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Lab_ProductKey";
            field.dfmClass = "TMlSkinLabel";
            field.kind = FieldKind::Info;
            field.value = "序列号:";
            field.x = 459;
            field.y = 10;
            field.width = 46;
            field.height = 21;
            field.note = "图纸 Caption 逐字";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Ed_ProductKey";
            field.dfmClass = "TMlSkinEdit";
            field.kind = FieldKind::Edit;
            field.label = "序列号";
            field.binding = "systemInfo.serialNumber";
            field.value = cfg.systemInfo.serialNumber;
            field.x = 511;
            field.y = 3;
            field.width = 202;
            field.height = 26;
            field.note = "图纸 Caption 在 Lab_ProductKey 上";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Lab_Pcname";
            field.dfmClass = "TMlSkinLabel";
            field.kind = FieldKind::Info;
            field.value = "计算机名:";
            field.x = 3;
            field.y = 52;
            field.width = 61;
            field.height = 21;
            field.note = "图纸 Caption 逐字";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Cb_Pcname";
            field.dfmClass = "TMlSkinComboBox";
            field.kind = FieldKind::Combo;
            field.label = "计算机名";
            field.binding = "systemInfo.computerNameMode";
            field.options = {"固定名称", "随机名称", "前缀+随机"};
            field.optionIds = {"fixed", "random", "prefixRandom"};
            field.comboIndex = indexOf(field.optionIds, cfg.systemInfo.computerNameMode.empty() ? std::string("fixed") : cfg.systemInfo.computerNameMode, 0);
            field.x = 66;
            field.y = 45;
            field.width = 135;
            field.height = 26;
            field.note = "图纸 Items.Strings 缺；取值按 config 的 computerNameMode 定义";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Lab_Pcnameprefix";
            field.dfmClass = "TMlSkinLabel";
            field.kind = FieldKind::Info;
            field.value = "前缀名称:";
            field.x = 231;
            field.y = 52;
            field.width = 61;
            field.height = 21;
            field.note = "图纸 Caption 逐字";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Ed_Pcname";
            field.dfmClass = "TMlSkinEdit";
            field.kind = FieldKind::Edit;
            field.label = "前缀名称";
            field.binding = "systemInfo.computerName";
            field.value = cfg.systemInfo.computerName;
            field.x = 294;
            field.y = 45;
            field.width = 131;
            field.height = 26;
            field.note = "图纸 Caption 在 Lab_Pcnameprefix 上";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Lab_Boottime";
            field.dfmClass = "TMlSkinLabel";
            field.kind = FieldKind::Info;
            field.value = "菜单时间:";
            field.x = 431;
            field.y = 52;
            field.width = 74;
            field.height = 21;
            field.note = "图纸 Caption 逐字";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Se_Boottime";
            field.dfmClass = "TMlSkinSpinEdit";
            field.kind = FieldKind::Number;
            field.label = "菜单时间";
            field.binding = "systemInfo.bootMenuTimeoutSec";
            field.value = std::to_string(cfg.systemInfo.bootMenuTimeoutSec);
            field.x = 511;
            field.y = 45;
            field.width = 74;
            field.height = 26;
            field.note = "图纸 Caption 在 Lab_Boottime 上；默认 3";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Lab_TimeCompany";
            field.dfmClass = "TMlSkinLabel";
            field.kind = FieldKind::Info;
            field.value = "秒";
            field.x = 591;
            field.y = 52;
            field.width = 26;
            field.height = 21;
            field.note = "图纸 Caption 逐字";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Btn_OemSet";
            field.dfmClass = "TMlSkinColorButton";
            field.kind = FieldKind::Button;
            field.label = "OEM设置";
            field.action = "oemSet";
            field.x = 623;
            field.y = 45;
            field.width = 89;
            field.height = 27;
            field.note = "图纸 Caption 逐字";
            group.fields.push_back(std::move(field));
        }

        page.groups.push_back(std::move(group));
    }

    {
        Group group;
        group.id = "Gb_Targetsys_ResolutionSet";
        group.caption = "分辨率设置";
        group.x = 36;
        group.y = 260;
        group.width = 738;
        group.height = 52;

        {
            Field field;
            field.id = "Cb_SetResolution";
            field.dfmClass = "TMlSkinCheckBox";
            field.kind = FieldKind::Check;
            field.label = "智能设置分辨率";
            field.binding = "optimization.setResolution";
            field.checked = cfg.optimization.setResolution;
            field.x = 3;
            field.y = 15;
            field.width = 115;
            field.height = 17;
            field.note = "--prepare 写 Display 分辨率；与下方下拉配合";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Cb_Resolution";
            field.dfmClass = "TMlSkinComboBox";
            field.kind = FieldKind::Combo;
            field.label = "分辨率";
            field.binding = "firstLogon.resolutionX";
            field.options = {"800*600", "1024*768", "1280*800", "1280*1024", "1360*768", "1366*768", "1440*900", "1600*900", "1680*1050", "1920*1080", "1920*1200"};
            field.optionIds = {"800*600", "1024*768", "1280*800", "1280*1024", "1360*768", "1366*768", "1440*900", "1600*900", "1680*1050", "1920*1080", "1920*1200"};
            field.comboIndex = indexOf(field.optionIds, joinResolution(cfg.firstLogon.resolutionX, cfg.firstLogon.resolutionY), -1);
            field.x = 114;
            field.y = 11;
            field.width = 103;
            field.height = 26;
            field.note = "条目直接取自 DFM 的 Items.Strings（800*600 … 1920*1200，共 11 条）";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Ed_ResolutionX";
            field.dfmClass = "TMlSkinEdit";
            field.kind = FieldKind::Number;
            field.label = "宽";
            field.binding = "firstLogon.resolutionX";
            field.value = std::to_string(cfg.firstLogon.resolutionX);
            field.x = 223;
            field.y = 11;
            field.width = 49;
            field.height = 26;
            field.note = "图纸可手填";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Ed_ResolutionY";
            field.dfmClass = "TMlSkinEdit";
            field.kind = FieldKind::Number;
            field.label = "高";
            field.binding = "firstLogon.resolutionY";
            field.value = std::to_string(cfg.firstLogon.resolutionY);
            field.x = 290;
            field.y = 11;
            field.width = 49;
            field.height = 26;
            field.note = "图纸可手填";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Lab_Refresh";
            field.dfmClass = "TMlSkinLabel";
            field.kind = FieldKind::Info;
            field.value = "刷新率:";
            field.x = 345;
            field.y = 16;
            field.width = 49;
            field.height = 21;
            field.note = "图纸 Caption 逐字";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Ed_Refresh";
            field.dfmClass = "TMlSkinEdit";
            field.kind = FieldKind::Number;
            field.label = "刷新率";
            field.binding = "firstLogon.refreshRate";
            field.value = std::to_string(cfg.firstLogon.refreshRate);
            field.x = 396;
            field.y = 11;
            field.width = 41;
            field.height = 26;
            field.note = "默认 60";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Lab_ColorQuality";
            field.dfmClass = "TMlSkinLabel";
            field.kind = FieldKind::Info;
            field.value = "颜色质量:";
            field.x = 443;
            field.y = 16;
            field.width = 53;
            field.height = 21;
            field.note = "图纸 Caption 逐字";
            group.fields.push_back(std::move(field));
        }
        {
            Field field;
            field.id = "Ed_ColorQuality";
            field.dfmClass = "TMlSkinEdit";
            field.kind = FieldKind::Number;
            field.label = "颜色质量";
            field.binding = "firstLogon.colorDepth";
            field.value = std::to_string(cfg.firstLogon.colorDepth);
            field.x = 502;
            field.y = 11;
            field.width = 41;
            field.height = 26;
            field.note = "默认 32";
            group.fields.push_back(std::move(field));
        }
        // 跳过 Cb_ResolutionSos：黑屏急救归 M10 急救页，不在本页
        {
            Field field;
            field.id = "Lab_tempX";
            field.dfmClass = "TMlSkinLabel";
            field.kind = FieldKind::Info;
            field.value = "*";
            field.x = 278;
            field.y = 18;
            field.width = 6;
            field.height = 12;
            field.note = "图纸 Caption 逐字（宽高之间的乘号）";
            group.fields.push_back(std::move(field));
        }

        page.groups.push_back(std::move(group));
    }

// 跳过整组 Gb_ScTools：宪章 §7 红线：系统总裁产品整组（联网装驱动/装联盟软件/绑定联盟账户）不做
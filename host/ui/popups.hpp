#pragma once

// SC 的小确认弹窗 TSBOX（300×136，两个页签）
//
// DFM: Sbox / Sbpage（TMlSkinPageControl, 7,8 284x120）
//   页签 1「重启」：Rb_Silence「静默重启[无界面]」/ Rb_Show「倒计时重启[有界面]」+ Btn_Scrok「确定(&K)」
//   页签 2「宽带」：Rb_Silence_nc「静默创建[无界面]」/ Rb_Show_nc「倒计时创建[有界面]」+ Btn_Ncok「确定(&K)」
//
// 注意 DFM 里页签 2 的两个单选按钮 Caption 写的是「静默重启」——那是 SC 自己的复制粘贴
// 错误；运行时被语言文件（LANGS_SCCHS_2052.bin）覆盖成「静默创建」。这里以语言文件为准，
// 因为那才是用户实际看到的文案。
//
// 视觉：SC 是 bsNone 自绘边框，宪章 §8 决定不复刻换皮，所以这里用带细标题栏的普通弹窗，
// 但**客户区尺寸与控件坐标严格照 DFM**（300×136，控件按 7,8 偏移后落在原位）。

#include <string>

namespace osp::ui {

enum class ChoiceKind
{
    Restart,        // TSBOX 页签 1：重启方式
    BroadbandCreate // TSBOX 页签 2：宽带连接的创建方式
};

struct ChoiceResult
{
    bool confirmed = false; // 点了「确定(&K)」
    bool silent = false;    // true = 静默[无界面]，false = 倒计时[有界面]
};

// 弹出确认框并等待用户选择。owner 为父窗口（可为 nullptr）。
// 返回值：confirmed=false 表示用户关掉了窗口或按 Esc。
ChoiceResult showChoiceDialog(void* ownerHwnd, ChoiceKind kind, bool silentDefault);

// 冒烟测试用：不上屏（SW_HIDE），只验控件树、坐标与返回值。
// 返回 false 表示窗口没建起来或控件缺失。
bool smokeChoiceDialog(void* ownerHwnd, ChoiceKind kind, std::string& detail);

// DFM 的设计尺寸，供验收脚本比对
constexpr int kChoiceClientWidth = 300;
constexpr int kChoiceClientHeight = 136;

} // namespace osp::ui

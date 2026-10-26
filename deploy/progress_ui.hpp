#pragma once

// 图形化部署进度页（M10）
//
// 对标 SC 的四种部署模块：四格进度 / TrueTrans（Aero 风格）/ 多彩进度条 / 图片控件。
// 与 ConsoleProgressPage 的关系：同一个 Sink 接口的两种实现，由 osptd 按
// runplan.json 的 presentation 段和当前会话能不能开窗口来选。
//
// 为什么用 GDI 而不是 Direct2D：宪章把自绘与换肤后置，这里只需要「能显示背景图、
// 标题、进度条」，GDI 在 Win7 SP1 与 WinPE 里都在，且不引入任何新依赖。
// 图片解码：BMP 走 LoadImage；JPG/PNG 走动态加载的 gdiplus 扁平 API（不链 gdiplus.lib，
// 取不到就退回纯色背景，绝不因为缺 DLL 而崩）。

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

#include "core/deploy.hpp"

namespace osp::deploy {

// 呈现设置的解析结果（来自 runplan.json 的 presentation 段）
struct Presentation
{
    std::string module = "grid";              // grid / trueTrans / colorBar / image
    std::string background = "systemDefault"; // single / multi / systemDefault
    std::string title;
    std::string imagePath; // 单图路径，或多图目录
    int switchSeconds = 10;
    int resolutionX = 0;
    int resolutionY = 0;
    COLORREF fontColor = RGB(240, 240, 240);
    bool hasFontColor = false;

    static Presentation fromPlan(const Plan& plan);
};

// "#RRGGBB" -> COLORREF；解析失败返回 false
bool parseHexColor(const std::string& text, COLORREF& color);

// 图形页是否可用：需要能在当前窗口站上创建窗口。
// WinPE 里可用；纯服务会话或 CI 无桌面时会返回 false，调用方退回控制台页。
bool graphicalPageAvailable();

class GraphProgressPage : public Sink
{
public:
    // 构造即创建窗口。失败时 ok() 为 false，调用方应退回控制台页。
    GraphProgressPage(const Presentation& presentation, int totalTasks);
    ~GraphProgressPage() override;

    GraphProgressPage(const GraphProgressPage&) = delete;
    GraphProgressPage& operator=(const GraphProgressPage&) = delete;

    bool ok() const
    {
        return hwnd_ != nullptr;
    }

    void phaseStart(const Phase& phase, int ordinal, int total) override;
    void taskStart(const Phase& phase, const Task& task, int ordinal, int total) override;
    void taskFinish(const Phase& phase, const Task& task, const TaskOutcome& outcome) override;
    void phaseFinish(const Phase& phase, int okCount, int failedCount) override;

    // 进度页自己的消息泵。执行器在长任务之间会调用它，保证窗口不假死。
    void pump();

    // 把当前画面存成 24 位 BMP。
    // 验收需要它：CI 与锁屏会话里看不见窗口，只能靠「自己画出像素再存盘」证明
    // 页面真的渲染了，而不是「窗口建起来了但一片黑」。
    bool saveToBmp(const std::string& utf8Path, std::string& error);

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    bool createWindow();
    bool applyResolution();
    void restoreResolution();
    void loadImages();
    void advanceImageIfNeeded();
    void paint(HDC dc);
    void paintGrid(HDC dc, const RECT& area, int percent);
    void paintTrueTrans(HDC dc, const RECT& area, int percent);
    void paintColorBar(HDC dc, const RECT& area, int percent);
    void paintImageModule(HDC dc, const RECT& area, int percent);
    void drawBackground(HDC dc);
    void drawTitle(HDC dc);

    Presentation presentation_;
    int totalTasks_ = 0;
    int doneTasks_ = 0;
    int failedTasks_ = 0;

    std::string phaseLabel_;
    int phaseOrdinal_ = 0;
    int phaseTotal_ = 0;
    std::string taskText_;
    std::string statusLine_;
    std::vector<std::string> recent_;

    HWND hwnd_ = nullptr;
    HDC memoryDc_ = nullptr;
    HBITMAP backBuffer_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    int width_ = 0;
    int height_ = 0;

    // 背景图：多图模式下 imagePaths_ 里轮换，currentImage_ 是当前索引
    std::vector<std::string> imagePaths_;
    std::size_t currentImage_ = 0;
    std::uint64_t lastSwitchTick_ = 0;
    void* gdiplusImage_ = nullptr; // GpImage*

    std::uint64_t startedAtMs_ = 0;

    // 分辨率切换与黑屏急救
    bool resolutionChanged_ = false;
    DEVMODEW savedMode_ = {};
    bool hasSavedMode_ = false;
    bool hotkeyRegistered_ = false;
};

} // namespace osp::deploy

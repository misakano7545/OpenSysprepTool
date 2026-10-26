#pragma once

// 在 ospt 里拉起 osptd 跑部署，并把进度回传给界面
//
// SC 是同进程跑封装，`运行` 页直接刷进度；我们是两个进程，所以要有一条通道。
// osptd 用 `--progress-json` 输出一行一个 JSON（见 core/deploy_progress.hpp），
// 这里负责：
//   1. CreateProcess 起子进程，重定向 stdout；
//   2. 后台线程按行读、解析成 ProgressEvent，再通过回调交给界面线程；
//   3. 结束时把退出码与最终统计交出去。
//
// 为什么用后台线程而不是轮询：读取阻塞在管道上，界面线程不能等；回调必须回到界面
// 线程再碰控件（Win32 控件不是线程安全的），由调用方用 PostMessage 转过去。

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "core/deploy_progress.hpp"

namespace osp::ui {

struct DeployRunSummary
{
    bool started = false; // 子进程是否成功起来
    bool ok = false;      // 全部任务成功
    int exitCode = -1;
    int executed = 0;
    int failed = 0;
    std::uint64_t elapsedMs = 0;
    std::string error; // 起不来或中途读失败的原因
    std::string tail;  // 非进度行的最后若干行（排错用）
};

class DeployRunner
{
public:
    using ProgressCallback = std::function<void(const osp::deploy::ProgressEvent&)>;
    using FinishCallback = std::function<void(const DeployRunSummary&)>;

    DeployRunner() = default;
    ~DeployRunner();
    DeployRunner(const DeployRunner&) = delete;
    DeployRunner& operator=(const DeployRunner&) = delete;

    // planDir: 含 runplan.json 的目录；osptdPath: osptd.exe 的完整路径（UTF-8）。
    // 回调都在**后台线程**里触发，调用方负责转到界面线程。
    bool start(const std::string& osptdPath, const std::string& planDir, std::string& error);

    void setProgressCallback(ProgressCallback callback);
    void setFinishCallback(FinishCallback callback);

    bool running() const noexcept
    {
        return running_.load();
    }

    // 请求中止：杀子进程并让后台线程收尾（幂等）
    void cancel();

    // 等待后台线程结束（测试与退出时用），最多等 timeoutMs
    void wait(int timeoutMs = 30000);

private:
    void run(std::string osptdPath, std::string planDir);

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelRequested_{false};
    void* processHandle_ = nullptr; // HANDLE，避免在头文件里 include windows.h
    ProgressCallback onProgress_;
    FinishCallback onFinish_;
};

} // namespace osp::ui

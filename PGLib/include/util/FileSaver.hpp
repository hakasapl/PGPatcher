#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>

class FileSaver {
public:
    struct WriteTask {
        std::string data;
        std::filesystem::path filepath;

        WriteTask(std::string data, std::filesystem::path path)
            : data(std::move(data))
            , filepath(std::move(path))
        {
        }
    };

private:
    static constexpr int LOOP_INTERVAL = 10; /** Worker loop interval in milliseconds */

    std::queue<WriteTask> m_taskQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running { true };
    std::atomic<bool> m_isBusy { false };
    std::atomic<size_t> m_queuedTasks { 0 };
    std::thread m_workerThread;

    void workerLoop();
    static void saveToFile(const WriteTask& task);

public:
    FileSaver();
    ~FileSaver();
    FileSaver(const FileSaver&) = delete;
    auto operator=(const FileSaver&) -> FileSaver& = delete;
    FileSaver(FileSaver&&) = delete;
    auto operator=(FileSaver&&) -> FileSaver& = delete;

    // Queue a file save task
    void queueSave(std::string data, std::filesystem::path filepath);

    // Check if the thread is currently working or has queued tasks
    auto isWorking() const -> bool;

    // Get number of queued tasks (not including currently processing)
    auto getQueuedTaskCount() const -> size_t;

    // Check if currently processing a file
    auto isProcessing() const -> bool;

    // Wait for all tasks to complete
    void waitForCompletion() const;

    void shutdown();
};

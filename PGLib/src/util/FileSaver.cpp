#include "util/FileSaver.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

FileSaver::FileSaver() { m_workerThread = std::thread(&FileSaver::workerLoop, this); }

FileSaver::~FileSaver() { shutdown(); }

void FileSaver::workerLoop()
{
    while (m_running) {
        WriteTask task { "", "" };

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait(lock, [this] { return !m_taskQueue.empty() || !m_running; });

            if (!m_running && m_taskQueue.empty()) {
                break;
            }

            if (!m_taskQueue.empty()) {
                task = std::move(m_taskQueue.front());
                m_taskQueue.pop();
                m_queuedTasks--;
            }
        }

        if (!task.filepath.empty()) {
            m_isBusy = true;
            saveToFile(task);
            m_isBusy = false;
        }
    }
}

void FileSaver::saveToFile(const WriteTask& task)
{
    std::ofstream file(task.filepath, std::ios::binary);
    if (file.is_open()) {
        file.write(task.data.data(), static_cast<std::streamsize>(task.data.size()));
        file.close();
    }
}

void FileSaver::queueSave(std::string data, std::filesystem::path filepath)
{
    {
        std::scoped_lock const lock(m_queueMutex);
        m_taskQueue.emplace(std::move(data), std::move(filepath));
        m_queuedTasks++;
    }
    m_cv.notify_one();
}

auto FileSaver::isWorking() const -> bool { return m_isBusy || m_queuedTasks > 0; }

auto FileSaver::getQueuedTaskCount() const -> size_t { return m_queuedTasks; }

auto FileSaver::isProcessing() const -> bool { return m_isBusy; }

void FileSaver::waitForCompletion() const
{
    while (isWorking()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(LOOP_INTERVAL));
    }
}

void FileSaver::shutdown()
{
    m_running = false;
    m_cv.notify_one();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

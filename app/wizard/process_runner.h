// Runs a child process to completion with SDL3, with the ability to stop it. No console output is shown.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace dw3setup {

constexpr int kProcessNotStarted = -1;
constexpr int kProcessCancelled = -2;

// Returns the exit code, kProcessNotStarted if the program cannot be started (for instance, it is not installed), or
// kProcessCancelled after `cancel` became true and the process was killed.
// What the last failed process reported: the end of its output, or why it could not be started.
inline std::mutex& process_detail_mutex() {
    static std::mutex m;
    return m;
}
inline std::string& process_detail_storage() {
    static std::string s;
    return s;
}
inline void set_process_detail(std::string s) {
    std::lock_guard<std::mutex> lock(process_detail_mutex());
    process_detail_storage() = std::move(s);
}
inline std::string last_process_detail() {
    std::lock_guard<std::mutex> lock(process_detail_mutex());
    return process_detail_storage();
}

int run_process(const std::vector<std::string>& command, const std::atomic<bool>& cancel);

}  // namespace dw3setup

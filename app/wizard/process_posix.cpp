#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "process_runner.h"

extern char** environ;

namespace dw3setup {

int run_process_posix(const std::vector<std::string>& command, const std::atomic<bool>& cancel) {
    if (command.empty()) return kProcessNotStarted;
    std::vector<char*> argv;
    for (const std::string& a : command) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    int fds[2];
    if (pipe(fds) != 0) {
        set_process_detail(std::string("pipe: ") + std::strerror(errno));
        return kProcessNotStarted;
    }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[1]);
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        set_process_detail(std::string("could not start ") + command[0] + ": " + std::strerror(rc));
        return kProcessNotStarted;
    }
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);

    std::string tail;
    bool open = true;
    int status = 0;
    for (;;) {
        char buf[512];
        while (open) {
            const ssize_t n = read(fds[0], buf, sizeof buf);
            if (n > 0) {
                tail.append(buf, static_cast<std::size_t>(n));
                if (tail.size() > 1500) tail.erase(0, tail.size() - 1500);
            } else {
                if (n == 0 || errno != EAGAIN) open = false;
                break;
            }
        }
        if (cancel.load()) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            close(fds[0]);
            return kProcessCancelled;
        }
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (done < 0 && errno != EINTR) {
            close(fds[0]);
            set_process_detail(std::string("waitpid: ") + std::strerror(errno));
            return kProcessNotStarted;
        }
        usleep(20000);
    }
    // Whatever the process left in the pipe before it ended.
    for (char buf[512];;) {
        const ssize_t n = read(fds[0], buf, sizeof buf);
        if (n <= 0) break;
        tail.append(buf, static_cast<std::size_t>(n));
        if (tail.size() > 1500) tail.erase(0, tail.size() - 1500);
    }
    close(fds[0]);
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
    if (code != 0) set_process_detail(tail);
    return code;
}

}  // namespace dw3setup

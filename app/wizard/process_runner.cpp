#include "process_runner.h"

#include <SDL3/SDL.h>

#include <thread>

namespace dw3setup {

int run_process(const std::vector<std::string>& command, const std::atomic<bool>& cancel) {
    std::vector<const char*> argv;
    for (const std::string& a : command) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);  // read below: the tail is kept for error messages
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!process) {
        set_process_detail(std::string("could not start ") + (command.empty() ? "" : command[0]) + ": " + SDL_GetError());
        SDL_Log("process: %s", last_process_detail().c_str());
        return kProcessNotStarted;
    }
    std::string tail;
    std::thread reader;
    if (SDL_IOStream* out = SDL_GetProcessOutput(process)) {
        reader = std::thread([out, &tail] {
            char buf[512];
            size_t n;
            while ((n = SDL_ReadIO(out, buf, sizeof buf)) > 0) {
                tail.append(buf, n);
                if (tail.size() > 1500) tail.erase(0, tail.size() - 1500);
            }
        });
    }
    int exit_code = 0;
    bool cancelled = false;
    while (!SDL_WaitProcess(process, false, &exit_code)) {
        if (cancel.load()) {
            SDL_KillProcess(process, true);
            SDL_WaitProcess(process, true, &exit_code);
            cancelled = true;
            break;
        }
        SDL_Delay(50);
    }
    if (reader.joinable()) reader.join();
    SDL_DestroyProcess(process);
    if (cancelled) return kProcessCancelled;
    if (exit_code != 0) {
        set_process_detail(tail);
        SDL_Log("process exited with %d: %s", exit_code, tail.c_str());
    }
    return exit_code;
}

}  // namespace dw3setup

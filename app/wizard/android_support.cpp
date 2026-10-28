#include "android_support.h"

#include <SDL3/SDL.h>
#include <dlfcn.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "disc_import.h"
#include "dw3/platform.h"
#include "zip_extract.h"

namespace fs = std::filesystem;

namespace dw3setup::android {

namespace {

constexpr const char* kCompilerLibrary = "libzig.so";
constexpr const char* kToolchainAsset = "toolchain.zip";
constexpr const char* kToolchainStampAsset = "toolchain.stamp";

std::string utf8(const fs::path& p) { return dw3::platform::path_to_utf8(p); }

// The environment of the process and the copy SDL hands to the programs it starts.
void set_env(const char* name, const std::string& value) {
    setenv(name, value.c_str(), 1);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value.c_str(), true);
}

// Reads a whole APK asset (or any file SDL can open) into memory. Only for small files.
bool read_small(const char* name, std::string& out) {
    std::size_t size = 0;
    void* data = SDL_LoadFile(name, &size);
    if (!data) return false;
    out.assign(static_cast<const char*>(data), size);
    SDL_free(data);
    return true;
}

// Streams `source` (an asset, a content:// address or a path) into `target`. `done` is advanced by the bytes copied.
bool copy_stream(const std::string& source, const fs::path& target, const std::function<void(uint64_t)>& done, std::string& error) {
    SDL_IOStream* in = SDL_IOFromFile(source.c_str(), "rb");
    if (!in) {
        error = "Cannot open " + guess_file_name(source) + ": " + SDL_GetError();
        return false;
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) {
        SDL_CloseIO(in);
        error = "Cannot write " + utf8(target) + ".";
        return false;
    }
    std::vector<char> buffer(1 << 20);
    bool ok = true;
    for (;;) {
        const std::size_t n = SDL_ReadIO(in, buffer.data(), buffer.size());
        if (n == 0) {
            if (SDL_GetIOStatus(in) != SDL_IO_STATUS_EOF) {
                error = "Reading " + guess_file_name(source) + " failed: " + SDL_GetError();
                ok = false;
            }
            break;
        }
        out.write(buffer.data(), static_cast<std::streamsize>(n));
        if (!out) {
            error = "Writing " + utf8(target) + " failed (is the device full?).";
            ok = false;
            break;
        }
        if (done) done(n);
    }
    SDL_CloseIO(in);
    return ok;
}

// The folder the system unpacked the APK's native libraries into: the one holding this library.
fs::path native_library_dir() {
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&native_library_dir), &info) || !info.dli_fname) return {};
    return fs::path(info.dli_fname).parent_path();
}

}  // namespace

void init_environment() {
    const char* internal = SDL_GetAndroidInternalStoragePath();
    if (!internal) return;
    set_env("XDG_DATA_HOME", internal);
    set_env("HOME", internal);
}

std::optional<Toolchain> toolchain(const fs::path& user_data, std::string& error) {
    const fs::path compiler = native_library_dir() / kCompilerLibrary;
    std::error_code ec;
    if (!fs::exists(compiler, ec)) {
        error = "The compiler (" + utf8(compiler) + ") is missing from this installation.";
        return std::nullopt;
    }
    std::string stamp;
    if (!read_small(kToolchainStampAsset, stamp)) {
        error = "The toolchain is missing from this installation.";
        return std::nullopt;
    }
    const fs::path dir = user_data / "toolchain";
    std::string unpacked;
    {
        std::ifstream in(dir / "stamp", std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        unpacked = ss.str();
    }
    if (unpacked != stamp) {
        fs::remove_all(dir, ec);
        fs::create_directories(user_data, ec);
        const fs::path zip = user_data / "toolchain.zip.part";
        if (!copy_stream(kToolchainAsset, zip, nullptr, error)) return std::nullopt;
        const bool extracted = extract_zip(zip, dir, error);
        fs::remove(zip, ec);
        if (!extracted) return std::nullopt;
        std::ofstream(dir / "stamp", std::ios::binary) << stamp;
    }
    set_env("ZIG_LIB_DIR", utf8(dir / "lib"));
    set_env("ZIG_GLOBAL_CACHE_DIR", utf8(user_data / "zig-cache"));
    set_env("ZIG_LOCAL_CACHE_DIR", utf8(user_data / "zig-cache"));

    Toolchain t;
    t.compiler = compiler;
    t.include_dir = dir / "include";
    t.module_entry = dir / "module" / "module_entry.c";
    t.target = "aarch64-linux-none";
    t.compile_flags = {"-ffreestanding"};
    t.link_flags = {"-nostdlib", "-Wl,--no-undefined", "-Wl,-z,max-page-size=16384"};
    t.extra_sources = {dir / "module" / "module_libc.c"};
    return t;
}

bool import_disc(const std::vector<std::string>& sources, const fs::path& user_data, const std::function<void(double)>& progress,
                 std::string& disc_path, std::string& error) {
    if (sources.empty()) {
        error = "No file was picked.";
        return false;
    }
    // Sort the picks into the cue sheet and the data files, and learn the sizes for the progress bar.
    std::string cue_text;
    std::vector<std::string> data;
    uint64_t total = 0;
    bool total_known = true;
    for (const std::string& s : sources) {
        SDL_IOStream* in = SDL_IOFromFile(s.c_str(), "rb");
        if (!in) {
            error = "Cannot open " + guess_file_name(s) + ": " + SDL_GetError();
            return false;
        }
        const Sint64 size = SDL_GetIOSize(in);
        char head[4096];
        const std::size_t n = SDL_ReadIO(in, head, sizeof head);
        SDL_CloseIO(in);
        if (size >= 0 && size < 65536 && looks_like_cue(std::string_view(head, n))) {
            if (!cue_text.empty()) {
                error = "Pick only one .cue file.";
                return false;
            }
            if (!read_small(s.c_str(), cue_text)) {
                error = "Cannot read " + guess_file_name(s) + ".";
                return false;
            }
            continue;
        }
        data.push_back(s);
        if (size < 0) total_known = false;
        else total += static_cast<uint64_t>(size);
    }
    if (data.empty()) {
        error = "Pick the .bin file of the disc too, not only the .cue.";
        return false;
    }

    const fs::path dir = user_data / "disc";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    if (ec) {
        error = "Cannot create " + utf8(dir) + ".";
        return false;
    }
    std::vector<std::string> picked, copied;
    uint64_t done = 0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        const std::string name = data.size() == 1 ? "disc.bin" : "track" + std::to_string(i + 1) + ".bin";
        picked.push_back(guess_file_name(data[i]));
        copied.push_back(name);
        auto advance = [&](uint64_t n) {
            done += n;
            if (progress) progress(total_known && total > 0 ? static_cast<double>(done) / static_cast<double>(total) : -1.0);
        };
        if (!copy_stream(data[i], dir / name, advance, error)) {
            fs::remove_all(dir, ec);
            return false;
        }
    }
    if (cue_text.empty()) {
        if (data.size() != 1) {
            error = "This disc has several .bin files: pick its .cue file together with them.";
            return false;
        }
        disc_path = utf8(dir / copied[0]);
        return true;
    }
    std::string rewritten;
    if (!rewrite_cue(cue_text, picked, copied, rewritten, error)) return false;
    const fs::path cue = dir / "disc.cue";
    std::ofstream(cue, std::ios::binary) << rewritten;
    disc_path = utf8(cue);
    return true;
}

}  // namespace dw3setup::android

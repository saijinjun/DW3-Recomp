// What the Android build does differently from the desktop ones: where user data lives, where the module compiler
// comes from, and how the player's disc gets onto the device's own storage. Only built for Android.
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "module_build.h"

namespace dw3setup::android {

// Points the user data directory (dw3::platform::user_data_dir) at the app's internal storage. Call first thing in
// main, before anything reads settings or saves.
void init_environment();

// The toolchain the game module is compiled with on the device: zig ships in the APK as the native library libzig.so
// (the only place an app may run a program from), and its headers and the module sources come from the APK asset
// toolchain.zip, unpacked into the user data directory once per app version. The module is freestanding: it links no
// C library and carries the few C functions it needs.
std::optional<Toolchain> toolchain(const std::filesystem::path& user_data, std::string& error);

// Copies the picked disc files (content:// addresses from the system file picker, or plain paths) into
// <user data>/disc, since the game reads the image by path. A .cue is rewritten to name the copied .bin files.
// Returns the path of the image to use (the .cue, or the .bin when no .cue was picked). `progress` gets 0..1, or -1
// when the total size is unknown.
bool import_disc(const std::vector<std::string>& sources, const std::filesystem::path& user_data,
                 const std::function<void(double)>& progress, std::string& disc_path, std::string& error);

// A personal build may carry its owner's disc and the game module prepared from it as APK assets under bundle/
// (android/app/build.gradle, -PbundleDir). Unpacks them into the user data directory the first time this build runs
// and returns the disc image to use; nullopt when the APK has no bundle. `error` is set when unpacking fails.
std::optional<std::string> install_bundle(const std::filesystem::path& user_data, std::string& error);

}  // namespace dw3setup::android

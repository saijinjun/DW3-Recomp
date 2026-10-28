// Text handling for copying a disc image that was picked through a system file picker (Android), where the program
// gets addresses instead of paths and cannot open a .cue's .bin files by their names next to it.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dw3setup {

// The file name a picked address most likely refers to: the last segment of a path, or of a content:// address with
// its percent-escapes decoded (document addresses look like ".../document/primary%3ADownload%2FGame.cue").
std::string guess_file_name(std::string_view address);

// Whether the first bytes of a file read like a cue sheet rather than sector data.
bool looks_like_cue(std::string_view head);

// The FILE names a cue sheet lists, in order.
std::vector<std::string> cue_file_names(std::string_view cue);

// Rewrites the cue sheet's FILE lines to the names its files were copied under. `picked` are the guessed names of the
// picked data files and `copied` the names they were stored as (same order). A FILE entry is matched by name
// (ignoring case), else by position; it fails when the cue names more files than were picked.
bool rewrite_cue(std::string_view cue, const std::vector<std::string>& picked, const std::vector<std::string>& copied,
                 std::string& out, std::string& error);

}  // namespace dw3setup

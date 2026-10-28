#include "disc_import.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace dw3setup {

namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// A FILE line: `FILE "name" BINARY` or `FILE name BINARY`. Returns false for other lines; `before`/`after` are the
// text around the name so the line can be rebuilt.
bool parse_file_line(const std::string& line, std::string& name, std::string& before, std::string& after) {
    const std::string t = trim(line);
    if (t.size() < 5 || lower(t.substr(0, 4)) != "file" || !std::isspace(static_cast<unsigned char>(t[4]))) return false;
    std::size_t at = 4;
    while (at < t.size() && std::isspace(static_cast<unsigned char>(t[at]))) ++at;
    if (at >= t.size()) return false;
    before = t.substr(0, at);
    if (t[at] == '"') {
        const std::size_t end = t.find('"', at + 1);
        if (end == std::string::npos) return false;
        name = t.substr(at + 1, end - at - 1);
        after = t.substr(end + 1);
    } else {
        std::size_t end = at;
        while (end < t.size() && !std::isspace(static_cast<unsigned char>(t[end]))) ++end;
        name = t.substr(at, end - at);
        after = t.substr(end);
    }
    return true;
}

std::string base_name(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

std::string guess_file_name(std::string_view address) {
    std::string decoded;
    decoded.reserve(address.size());
    for (std::size_t i = 0; i < address.size(); ++i) {
        if (address[i] == '%' && i + 2 < address.size() && hex_value(address[i + 1]) >= 0 && hex_value(address[i + 2]) >= 0) {
            decoded.push_back(static_cast<char>(hex_value(address[i + 1]) * 16 + hex_value(address[i + 2])));
            i += 2;
        } else {
            decoded.push_back(address[i]);
        }
    }
    const std::size_t cut = decoded.find_last_of("/:\\");
    return cut == std::string::npos ? decoded : decoded.substr(cut + 1);
}

bool looks_like_cue(std::string_view head) {
    for (unsigned char c : head)
        if (c == 0 || (c < 0x20 && c != '\n' && c != '\r' && c != '\t')) return false;
    const std::string l = lower(std::string(head));
    return l.find("file") != std::string::npos && l.find("track") != std::string::npos;
}

std::vector<std::string> cue_file_names(std::string_view cue) {
    std::vector<std::string> names;
    std::istringstream in{std::string(cue)};
    std::string line, name, before, after;
    while (std::getline(in, line))
        if (parse_file_line(line, name, before, after)) names.push_back(name);
    return names;
}

bool rewrite_cue(std::string_view cue, const std::vector<std::string>& picked, const std::vector<std::string>& copied,
                 std::string& out, std::string& error) {
    out.clear();
    std::vector<bool> used(picked.size(), false);
    std::size_t position = 0;
    std::istringstream in{std::string(cue)};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string name, before, after;
        if (!parse_file_line(line, name, before, after)) {
            out += line + "\n";
            continue;
        }
        std::size_t match = picked.size();
        const std::string wanted = lower(base_name(name));
        for (std::size_t i = 0; i < picked.size() && match == picked.size(); ++i)
            if (!used[i] && lower(picked[i]) == wanted) match = i;
        while (match == picked.size() && position < picked.size()) {
            if (!used[position]) match = position;
            ++position;
        }
        if (match == picked.size()) {
            error = "The .cue file lists " + name + ", but it was not picked. Pick the .cue together with all of its .bin files.";
            return false;
        }
        used[match] = true;
        out += before + "\"" + copied[match] + "\"" + after + "\n";
    }
    return true;
}

}  // namespace dw3setup

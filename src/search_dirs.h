#pragma once
// Where the stand-alone program and the VST3 plug-in look for the ROMs, the OS disk image and the
// "scsi" folder: sub-folders called "roms" or "rom" (any upper/lower case) of a given folder, and on
// macOS the folder that holds the .app bundle (so a "roms" folder next to the app is found too).
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

// sub-folders of `parent` called roms / rom, in any case ("roms" before "rom")
inline std::vector<std::string> rom_subdirs(const std::string &parent) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<std::pair<int, std::string>> found;
    fs::path p = parent.empty() ? fs::path(".") : fs::u8path(parent);
    for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        std::string n = it->path().filename().u8string();
        for (auto &c : n) c = (char)std::tolower((unsigned char)c);
        if (n == "roms") found.push_back({0, it->path().u8string()});
        else if (n == "rom") found.push_back({1, it->path().u8string()});
    }
    std::sort(found.begin(), found.end());
    std::vector<std::string> out;
    for (auto &f : found) out.push_back(f.second);
    return out;
}

// macOS: the folder that contains X.app when `base` is X.app/Contents/Resources (or
// X.app/Contents/MacOS); empty when the program is not inside an app bundle
inline std::string bundle_parent(const std::string &base) {
    namespace fs = std::filesystem;
    if (base.empty()) return "";
    std::string b = base;
    while (b.size() > 1 && (b.back() == '/' || b.back() == '\\')) b.pop_back();
    fs::path p = fs::u8path(b);
    fs::path contents = p.parent_path(), app = contents.parent_path();
    if (contents.filename() == "Contents" && app.extension() == ".app") return app.parent_path().u8string();
    return "";
}

// the folders where the program's files can be, best first: the program folder's roms folder(s),
// the program folder, and (macOS) the roms folder(s) next to the .app and that folder itself
inline std::vector<std::string> program_rom_dirs(const std::string &base) {
    std::vector<std::string> d;
    if (!base.empty()) { for (auto &s : rom_subdirs(base)) d.push_back(s); d.push_back(base); }
    std::string bp = bundle_parent(base);
    if (!bp.empty()) { for (auto &s : rom_subdirs(bp)) d.push_back(s); d.push_back(bp); }
    return d;
}

// the folders where the OS disk image can be: the roms folder(s) of the program folder and (macOS)
// the roms folder(s) next to the .app (never a plain folder such as Downloads: any .img there would do)
inline std::vector<std::string> program_os_dirs(const std::string &base) {
    std::vector<std::string> d;
    if (!base.empty()) for (auto &s : rom_subdirs(base)) d.push_back(s);
    std::string bp = bundle_parent(base);
    if (!bp.empty()) for (auto &s : rom_subdirs(bp)) d.push_back(s);
    return d;
}

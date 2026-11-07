// Ensoniq single files (Giebler .EFE/.EFA/... : a 512-byte header followed by the file's blocks)
// put on a freshly formatted Ensoniq floppy image, so that a dropped instrument can be loaded
// from the disk drive like on the real machine.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace efe {

// true if the file starts with a Giebler header ("\r\nEps File: ... \x1a")
bool is_single_file(const std::string &path);

// Builds an Ensoniq disk image (800 KB, or 1.6 MB when allow_hd and the files need it) holding
// the given files. Returns "" on success, else an error message for the user.
// `label` receives a short name for the disk (the first file's name).
std::string make_disk(const std::vector<std::string> &files, bool allow_hd,
                      std::vector<uint8_t> &image, std::string &label);

// make_disk + write the image to a temporary file; returns "" and its path, or an error
std::string make_disk_file(const std::vector<std::string> &files, bool allow_hd,
                           const std::string &prefix, std::string &path, std::string &label);

}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#pragma once
// Reads the unencrypted .fsarchive containers of MSFS 2024 packages (minimal.fsarchive, html_ui/html_ui.fsarchive): the aircraft's
// tooltips (en-us.locpak) and, for some aircraft, the sim's own checklist. Layout: "RASA", u16 version (2), u16 flags (0 plain), u32 1,
// u32 length of the file list, 16 zero bytes, the file list as JSON at offset 32 ({"encryptionSetup":{"scheme":"notEncrypted"},
// "fileInfoList":[{"path","byteOffset","byteSize","uncompressed_size"}]}), then the files, each at 32 + list length + byteOffset.
// Encrypted archives (the aircraft's model and systems, flags 3) are refused: they are copy protected and stay closed.
#include <cstdint>
#include <string>
#include <vector>

namespace lr::sim {

struct FsArchiveFile {
    std::string path;  // as listed, with backslashes ("airplanes\\microsoft_sf50\\checklist\\checklist.xml")
    uint64_t offset = 0, size = 0, rawSize = 0;
    bool stored() const { return size == rawSize; }  // compressed files (.fsc) are not read
};

class FsArchive {
public:
    // Reads the file list. False (and err) for anything that is not a plain archive, encrypted ones included.
    bool open(const std::string& path, std::string& err);
    const std::vector<FsArchiveFile>& files() const { return files_; }
    // Case-insensitive, '/' and '\\' are the same.
    const FsArchiveFile* find(const std::string& path) const;
    bool read(const FsArchiveFile& f, std::string& out, std::string& err) const;
    // Parses an archive held in memory (the tests); `path` is only used in messages.
    bool parse(const std::string& bytes, const std::string& path, std::string& err);

private:
    bool parseHeader(const std::string& head, uint64_t fileSize, std::string& err);
    std::string path_, memory_;
    bool inMemory_ = false;
    uint64_t dataStart_ = 0, fileSize_ = 0;
    std::vector<FsArchiveFile> files_;
};

}  // namespace lr::sim

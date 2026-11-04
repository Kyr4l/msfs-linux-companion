// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "sim/FsArchive.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include <nlohmann/json.hpp>

namespace lr::sim {

namespace {
constexpr size_t kListOffset = 32;
constexpr uint32_t kMaxList = 64u << 20;  // a file list bigger than this is not a package archive

uint32_t le32(const std::string& b, size_t at) {
    return static_cast<uint32_t>(static_cast<unsigned char>(b[at])) | static_cast<uint32_t>(static_cast<unsigned char>(b[at + 1])) << 8 |
           static_cast<uint32_t>(static_cast<unsigned char>(b[at + 2])) << 16 | static_cast<uint32_t>(static_cast<unsigned char>(b[at + 3])) << 24;
}

uint16_t le16(const std::string& b, size_t at) {
    return static_cast<uint16_t>(static_cast<unsigned char>(b[at]) | static_cast<unsigned char>(b[at + 1]) << 8);
}

std::string normPath(std::string p) {
    for (char& c : p) c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return p;
}
}  // namespace

bool FsArchive::parseHeader(const std::string& head, uint64_t fileSize, std::string& err) {
    files_.clear();
    if (head.size() < kListOffset || head.compare(0, 4, "RASA") != 0) { err = path_ + ": not an fsarchive"; return false; }
    const uint16_t flags = le16(head, 6);
    const uint32_t listLen = le32(head, 12);
    if (flags != 0) { err = path_ + ": encrypted archive, not read"; return false; }
    if (listLen == 0 || listLen > kMaxList || kListOffset + listLen > fileSize) { err = path_ + ": bad file list length"; return false; }
    if (head.size() < kListOffset + listLen) { err = path_ + ": short read"; return false; }
    // the list is followed by padding inside its length: parse the first JSON value and ignore the rest
    std::string text = head.substr(kListOffset, listLen);
    const size_t end = text.find_last_of('}');
    if (end == std::string::npos) { err = path_ + ": no file list"; return false; }
    text.resize(end + 1);
    try {
        const auto j = nlohmann::json::parse(text);
        const auto& enc = j.at("encryptionSetup");
        if (enc.value("scheme", "") != "notEncrypted") { err = path_ + ": encrypted archive, not read"; return false; }
        for (const auto& f : j.at("fileInfoList")) {
            FsArchiveFile e;
            e.path = f.at("path").get<std::string>();
            e.offset = f.at("byteOffset").get<uint64_t>();
            e.size = f.at("byteSize").get<uint64_t>();
            e.rawSize = f.value("uncompressed_size", e.size);
            files_.push_back(std::move(e));
        }
    } catch (const std::exception& e) {
        err = path_ + ": file list: " + e.what();
        files_.clear();
        return false;
    }
    dataStart_ = kListOffset + listLen;
    fileSize_ = fileSize;
    return true;
}

bool FsArchive::open(const std::string& path, std::string& err) {
    path_ = path;
    inMemory_ = false;
    memory_.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "cannot open " + path; return false; }
    in.seekg(0, std::ios::end);
    const uint64_t size = static_cast<uint64_t>(in.tellg());
    in.seekg(0);
    std::string head(kListOffset, '\0');
    if (!in.read(head.data(), static_cast<std::streamsize>(head.size()))) { err = path + ": too short"; return false; }
    if (head.compare(0, 4, "RASA") == 0 && le16(head, 6) == 0) {
        const uint32_t listLen = le32(head, 12);
        if (listLen > 0 && listLen <= kMaxList && kListOffset + listLen <= size) {
            head.resize(kListOffset + listLen);
            if (!in.read(head.data() + kListOffset, listLen)) { err = path + ": short read"; return false; }
        }
    }
    return parseHeader(head, size, err);
}

bool FsArchive::parse(const std::string& bytes, const std::string& path, std::string& err) {
    path_ = path;
    if (!parseHeader(bytes, bytes.size(), err)) return false;
    memory_ = bytes;
    inMemory_ = true;
    return true;
}

const FsArchiveFile* FsArchive::find(const std::string& path) const {
    const std::string want = normPath(path);
    for (const auto& f : files_)
        if (normPath(f.path) == want) return &f;
    return nullptr;
}

bool FsArchive::read(const FsArchiveFile& f, std::string& out, std::string& err) const {
    if (!f.stored()) { err = f.path + ": compressed, not read"; return false; }
    const uint64_t at = dataStart_ + f.offset;
    if (at > fileSize_ || f.size > fileSize_ - at) { err = f.path + ": outside the archive"; return false; }
    if (inMemory_) {
        out = memory_.substr(at, f.size);
        return true;
    }
    std::ifstream in(path_, std::ios::binary);
    if (!in) { err = "cannot open " + path_; return false; }
    in.seekg(static_cast<std::streamoff>(at));
    out.assign(f.size, '\0');
    if (!in.read(out.data(), static_cast<std::streamsize>(f.size))) { err = f.path + ": short read"; return false; }
    return true;
}

}  // namespace lr::sim

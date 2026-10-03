// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace WorkspaceEditor
{
    inline constexpr size_t MaxFileBytes = 2 * 1024 * 1024;

    struct DecodedFile
    {
        std::wstring text;
        bool utf16 = false;
        bool bom = false;
        bool crlf = false;
    };

    struct FileIdentity
    {
        uint64_t volume{};
        std::array<unsigned char, 16> fileId{};

        bool operator==(const FileIdentity&) const = default;
    };

    struct FileSnapshot
    {
        std::string bytes;
        FileIdentity identity;

        bool operator==(const FileSnapshot&) const = default;
    };

    std::wstring NormalizeLineEndings(std::wstring_view text);
    DecodedFile Decode(std::string_view bytes);
    std::string Encode(std::wstring_view normalized, bool utf16, bool bom, bool crlf);
    FileSnapshot ReadSnapshot(const std::filesystem::path& path);
    std::string Read(const std::filesystem::path& path);
    std::wstring ReadOnlyReason(const std::filesystem::path& path);
    FileSnapshot AtomicSave(const std::filesystem::path& path, const FileSnapshot& original, std::string_view bytes);
    void AtomicSave(const std::filesystem::path& path, std::string_view original, std::string_view bytes);
}

// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#pragma once

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

// File access stays in the native host. The web editor receives only a document
// identity and text; its messages never grant access to a new path.
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

    inline std::wstring NormalizeLineEndings(const std::wstring_view text)
    {
        std::wstring normalized;
        normalized.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L'\r')
            {
                if (i + 1 < text.size() && text[i + 1] == L'\n')
                {
                    ++i;
                }
                normalized.push_back(L'\n');
            }
            else
            {
                normalized.push_back(text[i]);
            }
        }
        return normalized;
    }

    inline void ValidateText(const std::wstring_view text)
    {
        for (size_t i = 0; i < text.size(); ++i)
        {
            const auto ch = text[i];
            if (ch == 0 || (ch < 0x20 && ch != L'\t' && ch != L'\n' && ch != L'\r'))
            {
                throw std::runtime_error("Binary data is read-only.");
            }
            if (ch >= 0xD800 && ch <= 0xDBFF)
            {
                if (++i >= text.size() || text[i] < 0xDC00 || text[i] > 0xDFFF)
                {
                    throw std::runtime_error("Invalid UTF-16 is read-only.");
                }
            }
            else if (ch >= 0xDC00 && ch <= 0xDFFF)
            {
                throw std::runtime_error("Invalid UTF-16 is read-only.");
            }
        }
    }

    inline DecodedFile Decode(const std::string_view bytes)
    {
        if (bytes.size() > MaxFileBytes)
        {
            throw std::runtime_error("Files larger than 2 MiB are read-only.");
        }
        DecodedFile result;
        if (bytes.starts_with("\xFF\xFE"))
        {
            result.utf16 = result.bom = true;
            if (bytes.size() % 2)
            {
                throw std::runtime_error("Invalid UTF-16 is read-only.");
            }
            for (size_t i = 2; i < bytes.size(); i += 2)
            {
                result.text.push_back(static_cast<wchar_t>(static_cast<unsigned char>(bytes[i]) |
                                                           (static_cast<unsigned char>(bytes[i + 1]) << 8)));
            }
        }
        else
        {
            auto view = bytes;
            if (view.starts_with("\xEF\xBB\xBF"))
            {
                result.bom = true;
                view.remove_prefix(3);
            }
            if (!view.empty())
            {
                const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, view.data(), static_cast<int>(view.size()), nullptr, 0);
                if (!count)
                {
                    throw std::runtime_error("Only strict UTF-8 and UTF-16 LE with BOM can be edited.");
                }
                result.text.resize(count);
                if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, view.data(), static_cast<int>(view.size()), result.text.data(), count))
                {
                    throw std::runtime_error("The file could not be decoded.");
                }
            }
        }
        ValidateText(result.text);
        bool lf = false;
        for (size_t i = 0; i < result.text.size(); ++i)
        {
            if (result.text[i] == L'\r')
            {
                if (i + 1 == result.text.size() || result.text[i + 1] != L'\n')
                {
                    throw std::runtime_error("CR-only and mixed line endings are read-only.");
                }
                result.crlf = true;
                ++i;
            }
            else if (result.text[i] == L'\n')
            {
                lf = true;
            }
        }
        if (lf && result.crlf)
        {
            throw std::runtime_error("Mixed line endings are read-only.");
        }
        result.text = NormalizeLineEndings(result.text);
        return result;
    }

    inline std::string Encode(const std::wstring_view normalized, const bool utf16, const bool bom, const bool crlf)
    {
        ValidateText(normalized);
        std::wstring text;
        for (const auto ch : normalized)
        {
            if (crlf && ch == L'\n')
            {
                text.push_back(L'\r');
            }
            text.push_back(ch);
        }
        std::string bytes;
        if (utf16)
        {
            if (bom)
            {
                bytes.assign("\xFF\xFE", 2);
            }
            for (const auto ch : text)
            {
                bytes.push_back(static_cast<char>(ch & 0xFF));
                bytes.push_back(static_cast<char>(ch >> 8));
            }
        }
        else
        {
            const auto count = text.empty() ? 0 : WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (!text.empty() && !count)
            {
                throw std::runtime_error("The text could not be encoded as UTF-8.");
            }
            bytes.resize(count);
            if (count && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), bytes.data(), count, nullptr, nullptr))
            {
                throw std::runtime_error("The text could not be encoded as UTF-8.");
            }
            if (bom)
            {
                bytes.insert(0, "\xEF\xBB\xBF", 3);
            }
        }
        if (bytes.size() > MaxFileBytes)
        {
            throw std::runtime_error("The edited file exceeds the 2 MiB limit.");
        }
        return bytes;
    }

    inline std::string ReadHandle(const HANDLE file)
    {
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart < 0)
        {
            throw std::runtime_error("The file size could not be read.");
        }
        if (size.QuadPart > MaxFileBytes)
        {
            throw std::runtime_error("Files larger than 2 MiB are read-only.");
        }
        std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
        DWORD read{};
        if (!bytes.empty() && (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()))
        {
            throw std::runtime_error("The file could not be read completely.");
        }
        return bytes;
    }

    class FileHandle
    {
    public:
        explicit FileHandle(const HANDLE handle) :
            _handle{ handle }
        {
            if (_handle == INVALID_HANDLE_VALUE)
            {
                throw std::runtime_error("The file could not be opened. Check its permissions and whether another application is using it.");
            }
        }

        ~FileHandle()
        {
            Close();
        }

        FileHandle(const FileHandle&) = delete;
        FileHandle& operator=(const FileHandle&) = delete;

        operator HANDLE() const noexcept
        {
            return _handle;
        }

        void Close() noexcept
        {
            if (_handle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(_handle);
                _handle = INVALID_HANDLE_VALUE;
            }
        }

    private:
        HANDLE _handle;
    };

    inline std::string Read(const std::filesystem::path& path)
    {
        const FileHandle file{ CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr) };
        return ReadHandle(file);
    }

    inline void AtomicSave(const std::filesystem::path& path, const std::string_view original, const std::string_view bytes)
    {
        if (bytes.size() > MaxFileBytes)
        {
            throw std::runtime_error("The edited file exceeds the 2 MiB limit.");
        }
        // Deny in-place writes while checking the complete original snapshot.
        // Delete sharing allows ReplaceFile to preserve the original metadata.
        const FileHandle current{ CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr) };
        if (ReadHandle(current) != original)
        {
            throw std::runtime_error("The file changed on disk. Your buffer was kept; save was cancelled to avoid overwriting external changes.");
        }

        static std::atomic<uint64_t> sequence{ 0 };
        auto temporary = path;
        temporary += L".sansterminal-editor-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence) + L".tmp";
        struct Cleanup
        {
            std::filesystem::path path;
            ~Cleanup()
            {
                DeleteFileW(path.c_str());
            }
        };
        FileHandle output{ CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr) };
        const Cleanup cleanup{ temporary };
        DWORD written{};
        if ((!bytes.empty() && (!WriteFile(output, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size())) ||
            !FlushFileBuffers(output))
        {
            output.Close();
            throw std::runtime_error("The temporary file could not be written. The original file was kept.");
        }
        output.Close();
        // A program may have replaced the path while the temporary was written.
        // Check the path again, rather than only the initially opened file.
        const FileHandle latest{ CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr) };
        if (ReadHandle(latest) != original)
        {
            throw std::runtime_error("The file changed on disk. Your buffer was kept; save was cancelled to avoid overwriting external changes.");
        }
        if (!ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr))
        {
            throw std::runtime_error("Atomic replacement failed. Your buffer and the original file were kept.");
        }
    }
}

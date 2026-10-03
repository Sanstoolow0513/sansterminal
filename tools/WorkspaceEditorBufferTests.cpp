// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

// Standalone tests for the production file codec and atomic-save implementation.
// Compile in a Visual Studio developer shell with /std:c++20 /EHsc /utf-8.
#include "../src/cascadia/TerminalApp/WorkspaceEditorBuffer.h"

#include <fstream>
#include <iostream>

static void Check(const bool condition, const char* description)
{
    if (!condition)
    {
        throw std::runtime_error(description);
    }
}

template<typename F>
static void Reject(F&& operation, const char* description)
{
    bool rejected = false;
    try
    {
        operation();
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    Check(rejected, description);
}

static void Write(const std::filesystem::path& path, const std::string_view bytes)
{
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(bytes.data(), bytes.size());
    Check(static_cast<bool>(output), "fixture write failed");
}

int main()
try
{
    using namespace WorkspaceEditor;
    const std::string utf8 = "\xEF\xBB\xBF中文\r\nlast line";
    const auto decoded = Decode(utf8);
    Check(decoded.text == L"中文\nlast line" && decoded.bom && decoded.crlf && !decoded.utf16, "UTF-8 decode lost format metadata");
    Check(Encode(decoded.text, decoded.utf16, decoded.bom, decoded.crlf) == utf8, "UTF-8 BOM / CRLF roundtrip changed bytes");

    const auto utf16 = Encode(L"中文\nemoji: \xD83D\xDE00", true, true, true);
    const auto wide = Decode(utf16);
    Check(wide.utf16 && wide.bom && wide.crlf, "UTF-16 metadata was lost");
    Check(Encode(wide.text, wide.utf16, wide.bom, wide.crlf) == utf16, "UTF-16 LE roundtrip changed bytes");
    const std::string plain = "a\nb\n";
    const auto lf = Decode(plain);
    Check(Encode(lf.text, lf.utf16, lf.bom, lf.crlf) == plain, "LF / BOM-free roundtrip changed bytes");
    Check(Decode("").text.empty(), "empty files were rejected");
    Reject([] { Decode("\xC0\xAF"); }, "overlong UTF-8 was accepted");
    Reject([] { Decode(std::string{ "a\0b", 3 }); }, "binary data was accepted");
    Reject([] { Decode(std::string{ "\xFF\xFE\x61", 3 }); }, "odd UTF-16 was accepted");
    Reject([] { Decode(std::string{ "\xFF\xFE\0\xD8", 4 }); }, "invalid UTF-16 surrogate was accepted");
    Reject([] { Decode("a\r\nb\n"); }, "mixed EOL was accepted");
    Reject([] { Decode("a\rb"); }, "CR-only EOL was accepted");
    Reject([] { Decode(std::string(MaxFileBytes + 1, 'a')); }, "oversized file was accepted");
    Reject([] { Encode(std::wstring(MaxFileBytes + 1, L'a'), false, false, false); }, "oversized edit was accepted");

    wchar_t temporaryRoot[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, temporaryRoot) > 0, "temporary root unavailable");
    const auto fixtureRoot = std::filesystem::path{ temporaryRoot } / (L"sansterminal-buffer-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    Check(CreateDirectoryW(fixtureRoot.c_str(), nullptr), "fixture directory creation failed");
    const auto path = fixtureRoot / L"document.txt";
    struct Cleanup
    {
        std::filesystem::path file;
        std::filesystem::path directory;
        ~Cleanup()
        {
            DeleteFileW(file.c_str());
            RemoveDirectoryW(directory.c_str());
        }
    };
    const Cleanup cleanup{ path, fixtureRoot };
    Write(path, utf8);
    Check(Read(path) == utf8, "native read changed bytes");
    const auto edited = Encode(L"中文\nnew content", false, true, true);
    AtomicSave(path, utf8, edited);
    Check(Read(path) == edited, "atomic save failed");
    Write(path, "external edit");
    Reject([&] { AtomicSave(path, edited, "local edit"); }, "external edit conflict was overwritten");
    Check(Read(path) == "external edit", "conflict changed the external file");
    {
        const FileHandle external{ CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr) };
        Reject([&] { AtomicSave(path, "external edit", "local edit"); }, "replacement succeeded despite a deny-delete lock");
    }
    Check(Read(path) == "external edit", "failed replacement changed the original file");
    Check(std::distance(std::filesystem::directory_iterator{ fixtureRoot }, std::filesystem::directory_iterator{}) == 1, "failed save left temporary files");
    std::cout << "Workspace editor buffer: codec, size, binary, conflict, atomic replacement and cleanup checks passed.\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}

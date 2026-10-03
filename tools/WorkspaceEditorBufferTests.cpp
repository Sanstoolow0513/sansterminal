// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

// Standalone tests for the production file codec and atomic-save implementation.
// Build and run with tools/Test-WorkspaceNative.ps1.
#include "../src/cascadia/TerminalApp/WorkspaceDocument.h"
#include "../src/cascadia/TerminalApp/WorkspaceEditorSession.h"

#include <Windows.h>
#include <cfapi.h>
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
    DocumentSynchronization synchronization;
    synchronization.Failed();
    Check(synchronization.Incomplete(), "failed synchronization was forgotten");
    synchronization.Synchronized();
    Check(!synchronization.NeedsAttention(), "corrected live buffer still blocks close");
    synchronization.Failed();
    synchronization.HostUnavailable();
    Check(synchronization.Lost() && !synchronization.Incomplete(), "host failure still requires correcting a dead editor");
    synchronization.Synchronized();
    Check(synchronization.Lost(), "reloaded native snapshot silently acknowledged missing editor text");
    EditorSession session;
    session.Connected();
    const auto flush = session.BeginFlush();
    session.Flushed(L"stale-request");
    Check(session.FlushPending(), "stale flush completed a newer close");
    session.Flushed(flush);
    Check(!session.FlushPending(), "flush acknowledgment was ignored");
    session.BeginFlush();
    session.SynchronizationFailed();
    session.Unavailable();
    Check(!session.Ready() && !session.FlushPending() && session.Lost(), "host failure left an impossible flush pending or lost its warning");
    session.Connected();
    session.BeginFlush();
    Check(session.Lost(), "reconnect silently cleared transport data loss");
    Check(session.TakeLostContent() && !session.Lost(), "attributed transport loss leaked into later documents");

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
    Reject([] { Encode(L"a\r\nb", false, false, true); }, "non-normalized editor text silently changed EOL");
    Reject([] { Encode(L"text", true, false, false); }, "unsupported BOM-free UTF-16 was written");
    Reject([] { Decode(std::string{ "\xFE\xFF\0a", 4 }); }, "unsupported UTF-16 BE was accepted");

    wchar_t temporaryRoot[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, temporaryRoot) > 0, "temporary root unavailable");
    const auto fixtureRoot = std::filesystem::path{ temporaryRoot } / (L"sansterminal-buffer-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    Check(CreateDirectoryW(fixtureRoot.c_str(), nullptr), "fixture directory creation failed");
    const auto path = fixtureRoot / L"document.txt";
    const auto replacementPath = fixtureRoot / L"replacement.txt";
    struct Cleanup
    {
        std::filesystem::path file;
        std::filesystem::path replacement;
        std::filesystem::path directory;
        ~Cleanup()
        {
            DeleteFileW(file.c_str());
            DeleteFileW(replacement.c_str());
            RemoveDirectoryW(directory.c_str());
        }
    };
    const Cleanup cleanup{ path, replacementPath, fixtureRoot };
    {
        const auto cloudRoot = fixtureRoot / L"cloud";
        Check(CreateDirectoryW(cloudRoot.c_str(), nullptr), "cloud fixture directory creation failed");
        CF_SYNC_REGISTRATION registration{};
        registration.StructSize = sizeof(registration);
        registration.ProviderName = L"Sansterminal document regression test";
        registration.ProviderVersion = L"1.0";
        CF_SYNC_POLICIES policies{};
        policies.StructSize = sizeof(policies);
        policies.Hydration.Primary = CF_HYDRATION_POLICY_FULL;
        policies.Population.Primary = CF_POPULATION_POLICY_ALWAYS_FULL;
        const auto registered = CfRegisterSyncRoot(cloudRoot.c_str(), &registration, &policies, CF_REGISTER_FLAG_NONE);
        if (SUCCEEDED(registered))
        {
            const auto cloudPath = cloudRoot / L"hydrated.txt";
            struct CloudCleanup
            {
                std::filesystem::path root;
                std::filesystem::path file;
                ~CloudCleanup()
                {
                    DeleteFileW(file.c_str());
                    CfUnregisterSyncRoot(root.c_str());
                    RemoveDirectoryW(root.c_str());
                }
            } cloudCleanup{ cloudRoot, cloudPath };
            Write(cloudPath, "cloud text");
            const auto cloudFile = CreateFileW(cloudPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            Check(cloudFile != INVALID_HANDLE_VALUE, "cloud fixture open failed");
            const char identity[] = "hydrated-test-file";
            const auto converted = CfConvertToPlaceholder(cloudFile, identity, sizeof(identity), CF_CONVERT_FLAG_MARK_IN_SYNC | CF_CONVERT_FLAG_FORCE_CONVERT_TO_CLOUD_FILE, nullptr, nullptr);
            FILE_ATTRIBUTE_TAG_INFO cloudInfo{};
            Check(GetFileInformationByHandleEx(cloudFile, FileAttributeTagInfo, &cloudInfo, sizeof(cloudInfo)), "cloud fixture attributes unavailable");
            CloseHandle(cloudFile);
            Check(SUCCEEDED(converted), "cloud placeholder conversion failed");
            if ((cloudInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                auto cloud = DocumentService::Load(cloudPath);
                Check(!cloud.readOnly && cloud.text == L"cloud text", "hydrated cloud placeholder was read-only");
                Check(cloud.UpdateText(L"saved cloud text", 1), "cloud document rejected editing");
                Check(DocumentService::CompleteSave(cloud, DocumentService::Save(DocumentService::PrepareSave(cloud))), "cloud placeholder save failed");
                Check(Read(cloudPath) == "saved cloud text" && !cloud.Dirty(), "cloud save changed contents or lost baseline");
                std::cout << "PASS: real hydrated Cloud Files placeholder load/edit/atomic save.\n";
            }
            else
            {
                std::cout << "SKIP: CfConvertToPlaceholder returned success but this environment did not create a reparse point.\n";
            }
        }
        else
        {
            std::cout << "SKIP: Cloud Files sync root registration unavailable, HRESULT=" << std::hex << registered << std::dec << '\n';
            Check(RemoveDirectoryW(cloudRoot.c_str()), "cloud fixture cleanup failed");
        }
    }
    // Extended paths separate the NTFS component limit from legacy MAX_PATH.
    const auto extendedRoot = std::filesystem::path{ L"\\\\?\\" + fixtureRoot.native() };
    for (const auto length : { 234u, 255u })
    {
        const auto longPath = extendedRoot / (std::wstring(length - 4, L'x') + L".txt");
        Write(longPath, "original");
        auto longDocument = DocumentService::Load(longPath);
        Check(!longDocument.readOnly && longDocument.UpdateText(L"long-name edit", 1), "valid long filename was not editable");
        Check(DocumentService::CompleteSave(longDocument, DocumentService::Save(DocumentService::PrepareSave(longDocument))), "long filename save failed");
        Check(Read(longPath) == "long-name edit" && !longDocument.Dirty(), "long filename save lost contents or baseline");
        Check(std::distance(std::filesystem::directory_iterator{ extendedRoot }, std::filesystem::directory_iterator{}) == 1, "long filename save left temporary or recovery files");
        Check(longDocument.UpdateText(L"close-save edit", 2), "second long filename edit failed");
        Check(DocumentService::CompleteSave(longDocument, DocumentService::Save(DocumentService::PrepareSave(longDocument))), "long filename close-save snapshot failed");
        Check(DeleteFileW(longPath.c_str()), "long filename cleanup failed");
    }
    Write(path, utf8);
    const auto linkPath = fixtureRoot / L"link.txt";
    if (CreateSymbolicLinkW(linkPath.c_str(), path.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
    {
        Check(DocumentService::Load(linkPath).readOnly, "symbolic link was editable");
        Reject([&] { AtomicSave(linkPath, utf8, "overwrite"); }, "symbolic link was replaced");
        Check(Read(path) == utf8, "symbolic link save changed its target");
        Check(DeleteFileW(linkPath.c_str()), "symbolic link cleanup failed");
    }
    else
    {
        std::cout << "SKIP: symlink fixture needs Developer Mode or SeCreateSymbolicLinkPrivilege.\n";
    }
    Check(Read(path) == utf8, "native read changed bytes");
    const auto edited = Encode(L"中文\nnew content", false, true, true);
    AtomicSave(path, utf8, edited);
    Check(Read(path) == edited, "atomic save failed");
    Write(path, "external edit");
    Reject([&] { AtomicSave(path, edited, "local edit"); }, "external edit conflict was overwritten");
    Check(Read(path) == "external edit", "conflict changed the external file");
    {
        const auto external = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        Check(external != INVALID_HANDLE_VALUE, "deny-delete fixture failed");
        Reject([&] { AtomicSave(path, "external edit", "local edit"); }, "replacement succeeded despite a deny-delete lock");
        CloseHandle(external);
    }
    Check(Read(path) == "external edit", "failed replacement changed the original file");
    Check(std::distance(std::filesystem::directory_iterator{ fixtureRoot }, std::filesystem::directory_iterator{}) == 1, "failed save left temporary files");

    Write(path, utf8);
    auto document = DocumentService::Load(path);
    Check(document.loaded && !document.readOnly && !document.Dirty(), "document failed to establish a clean baseline");
    Check(document.UpdateText(L"中文\r\nfirst edit", 1), "first edit was rejected");
    Check(document.text == L"中文\nfirst edit" && document.Dirty(), "document edit lost text or did not become dirty");
    const auto request = DocumentService::PrepareSave(document);
    Check(document.UpdateText(L"中文\nedit while saving", 2), "concurrent edit was rejected");
    const auto firstSave = DocumentService::Save(request);
    Check(DocumentService::CompleteSave(document, firstSave), "save completion was rejected");
    Check(Read(path) == "\xEF\xBB\xBF中文\r\nfirst edit", "save did not use its immutable text snapshot");
    Check(document.text == L"中文\nedit while saving" && document.revision == 2 && document.Dirty(), "save completion discarded edits made during saving");
    Check(document.savedText == request.text, "save completion used the current text as its baseline");
    Check(!document.UpdateText(L"stale callback", 1) && document.text == L"中文\nedit while saving", "stale editor message overwrote the model");
    Check(!document.UpdateText(L"duplicate revision, different text", 2), "ambiguous revision was accepted");
    Check(DocumentService::CompleteSave(document, DocumentService::Save(DocumentService::PrepareSave(document))) && !document.Dirty(), "second save failed to update its own baseline");
    Check(!DocumentService::CompleteSave(document, firstSave) && !document.Dirty(), "obsolete completion rolled back the saved baseline");
    const auto saved = document.text;
    Check(document.UpdateText(L"another change", 3) && document.Dirty(), "later edit did not become dirty");
    Check(document.UpdateText(saved, 4) && !document.Dirty(), "undo to saved text remained dirty");

    Check(document.UpdateText(L"unsaved local content", 5), "local conflict edit was rejected");
    const auto unchangedBytes = Read(path);
    Write(replacementPath, unchangedBytes);
    Check(ReplaceFileW(path.c_str(), replacementPath.c_str(), nullptr, 0, nullptr, nullptr), "external identity-replacement fixture failed");
    Check(Read(path) == unchangedBytes, "external replacement fixture changed bytes");
    Reject([&] { DocumentService::Save(DocumentService::PrepareSave(document)); }, "same-byte external replacement bypassed identity conflict detection");
    Check(document.text == L"unsaved local content" && document.Dirty() && Read(path) == unchangedBytes, "identity conflict discarded local or external content");

    document = DocumentService::Load(path);
    Check(document.UpdateText(L"keep this buffer", 1), "failure fixture edit was rejected");
    Check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY), "read-only fixture failed");
    Reject([&] { DocumentService::Save(DocumentService::PrepareSave(document)); }, "file made read-only after opening was overwritten");
    Check(document.Dirty() && document.text == L"keep this buffer", "read-only save failure discarded the buffer");
    Check(DocumentService::Load(path).readOnly, "read-only file was editable");
    Check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL), "read-only fixture reset failed");
    Check(DeleteFileW(path.c_str()), "deleted-file fixture failed");
    Reject([&] { DocumentService::Save(DocumentService::PrepareSave(document)); }, "externally deleted file was silently recreated");
    Check(document.Dirty() && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES, "deleted-file conflict lost buffer or recreated target");
    Check(std::filesystem::is_empty(fixtureRoot), "failed model save left temporary or recovery files");

    Write(path, utf16);
    auto utf16Document = DocumentService::Load(path);
    Check(utf16Document.UpdateText(L"中文\nchanged emoji: \xD83D\xDE00", 1), "UTF-16 model edit was rejected");
    Check(DocumentService::CompleteSave(utf16Document, DocumentService::Save(DocumentService::PrepareSave(utf16Document))), "UTF-16 model save failed");
    Check(Read(path) == Encode(utf16Document.text, true, true, true) && !utf16Document.Dirty(), "UTF-16 save lost encoding, BOM or CRLF");
    Check(utf16Document.UpdateText(std::wstring{ L"a\0b", 3 }, 2), "invalid-text buffer should remain available for correction");
    Reject([&] { DocumentService::PrepareSave(utf16Document); }, "invalid-text buffer was encoded for saving");
    Check(utf16Document.Dirty() && utf16Document.text.size() == 3, "encoding failure discarded the editable buffer");
    std::cout << "Workspace document: codec, identity conflicts, atomic replacement, failed-save cleanup, immutable save snapshots, stale revisions and dirty-buffer preservation checks passed.\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}

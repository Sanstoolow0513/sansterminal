// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#pragma once

#include "WorkspaceEditorBuffer.h"

namespace WorkspaceEditor
{
    inline constexpr size_t MaxTextCodeUnits = 3 * 1024 * 1024;

    // Persistent native text survives editor reloads, tab switches and workspaces.
    // The UI owns this model on its dispatcher; background I/O owns value snapshots.
    struct Document
    {
        std::filesystem::path path;
        std::wstring text;
        std::wstring savedText;
        std::wstring readOnlyReason;
        FileSnapshot baseline;
        uint64_t revision{};
        bool loaded{ false };
        bool readOnly{ true };
        bool utf16{ false };
        bool bom{ false };
        bool crlf{ false };

        bool Dirty() const noexcept
        {
            return text != savedText;
        }

        bool UpdateText(const std::wstring_view value, const uint64_t incomingRevision)
        {
            if (!loaded || readOnly || incomingRevision < revision)
            {
                return false;
            }
            if (value.size() > MaxTextCodeUnits)
            {
                throw std::runtime_error("The editor buffer exceeds the 3 MiB text limit. Reduce its size before saving or closing.");
            }
            auto normalized = NormalizeLineEndings(value);
            if (incomingRevision == revision && normalized != text)
            {
                return false;
            }
            text = std::move(normalized);
            revision = incomingRevision;
            return true;
        }
    };

    struct SaveRequest
    {
        std::filesystem::path path;
        FileSnapshot baseline;
        std::wstring text;
        std::string bytes;
        uint64_t revision{};
    };

    struct SaveResult
    {
        std::filesystem::path path;
        FileSnapshot previousBaseline;
        FileSnapshot baseline;
        std::wstring text;
        uint64_t revision{};
    };

    class DocumentService
    {
    public:
        static Document Load(const std::filesystem::path& path)
        {
            Document document;
            document.path = path;
            document.baseline = ReadSnapshot(path);
            const auto decoded = Decode(document.baseline.bytes);
            document.text = document.savedText = decoded.text;
            document.utf16 = decoded.utf16;
            document.bom = decoded.bom;
            document.crlf = decoded.crlf;
            const auto attributes = GetFileAttributesW(path.c_str());
            document.readOnly = attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0;
            if (document.readOnly)
            {
                document.readOnlyReason = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ? L"Symbolic links and reparse points are read-only." : L"The file is marked read-only or is unavailable.";
            }
            document.loaded = true;
            return document;
        }

        static SaveRequest PrepareSave(const Document& document)
        {
            if (!document.loaded || document.readOnly)
            {
                throw std::runtime_error("This document is read-only.");
            }
            return { document.path, document.baseline, document.text, Encode(document.text, document.utf16, document.bom, document.crlf), document.revision };
        }

        // Run this value-only operation off the UI thread. Serialize saves of the
        // same document; a second request must use the first completion's baseline.
        static SaveResult Save(const SaveRequest& request)
        {
            auto baseline = AtomicSave(request.path, request.baseline, request.bytes);
            return { request.path, request.baseline, std::move(baseline), request.text, request.revision };
        }

        static bool CompleteSave(Document& document, SaveResult result)
        {
            if (document.path != result.path || document.baseline != result.previousBaseline)
            {
                return false;
            }
            // The user may have typed while Save ran. Only the saved snapshot
            // becomes the baseline; never overwrite the current text or revision.
            document.baseline = std::move(result.baseline);
            document.savedText = std::move(result.text);
            return true;
        }
    };
}

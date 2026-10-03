// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#include "WorkspaceDocument.h"

namespace WorkspaceEditor
{
    Document DocumentService::Load(const std::filesystem::path& path)
    {
        Document document;
        document.path = path;
        document.baseline = ReadSnapshot(path);
        const auto decoded = Decode(document.baseline.bytes);
        document.text = document.savedText = decoded.text;
        document.utf16 = decoded.utf16;
        document.bom = decoded.bom;
        document.crlf = decoded.crlf;
        document.readOnlyReason = ReadOnlyReason(path);
        document.readOnly = !document.readOnlyReason.empty();
        document.loaded = true;
        return document;
    }
}

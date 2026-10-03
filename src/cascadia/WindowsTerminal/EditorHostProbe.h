// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#pragma once
#include "WorkspaceEditorHost.h"

// Diagnostic scratch surface. Production uses WorkspaceEditorHost directly.
class EditorHostProbe final : public WorkspaceEditorHost
{
public:
    EditorHostProbe(HWND parent, HWND island, winrt::TerminalApp::TerminalWindow logic, std::function<void()> focusXaml);

private:
    void _CreateSurface() override;
    HRESULT _HandleWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args) override;
    void _UpdateSurfaceWidth() override;
    void _OnEditorFocused() override {}
    void _NotifyUnavailable() override {}
    void _ShowFailure() override;
    bool _HandlesWorkspaceKeys() const noexcept override { return false; }
};

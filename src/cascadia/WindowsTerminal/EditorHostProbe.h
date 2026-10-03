// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#pragma once

#include <WebView2.h>
#include <filesystem>

// Opt-in integration probe. Document ownership stays in TerminalPage. One instance
// owns one native WebView2 controller for the lifetime of its AppHost.
class EditorHostProbe : public std::enable_shared_from_this<EditorHostProbe>
{
public:
    EditorHostProbe(HWND parent, HWND island, winrt::TerminalApp::TerminalWindow logic, std::function<void()> focusXaml, bool workspace = false);
    ~EditorHostProbe();
    winrt::Windows::UI::Xaml::UIElement CreateContent();
    bool HasFocus() const noexcept;
    void Close() noexcept;

private:
    void _Start();
    HRESULT _Configure(ICoreWebView2Controller* controller);
    void _SyncBounds();
    winrt::fire_and_forget _FocusTerminal();
    winrt::fire_and_forget _FocusEditor();
    void _Report(std::wstring_view message);
    void _Fail(HRESULT result);
    winrt::fire_and_forget _ShowDialog();
    HRESULT _HandleWorkspaceMessage(ICoreWebView2WebMessageReceivedEventArgs* args);
    void _Reload();

    HWND _parent;
    HWND _island;
    wil::unique_hwnd _nativeWindow;
    winrt::TerminalApp::TerminalWindow _logic;
    std::function<void()> _focusXaml;
    winrt::Windows::UI::Xaml::FrameworkElement _layout{ nullptr };
    winrt::Windows::UI::Xaml::Controls::Border _surface{ nullptr };
    winrt::Windows::UI::Xaml::Controls::TextBlock _status{ nullptr };
    winrt::Windows::UI::Xaml::Controls::ColumnDefinition _editorColumn{ nullptr };
    winrt::Windows::UI::Xaml::FrameworkElement::Loaded_revoker _loaded;
    winrt::Windows::UI::Xaml::FrameworkElement::LayoutUpdated_revoker _layoutUpdated;
    winrt::TerminalApp::TerminalWindow::DialogVisibilityChanged_revoker _dialogVisibility;
    winrt::TerminalApp::TerminalWindow::WorkspaceEditorMessage_revoker _workspaceMessage;
    SafeDispatcherTimer _popupTimer;
    wil::com_ptr<ICoreWebView2Environment> _environment;
    wil::com_ptr<ICoreWebView2Controller> _controller;
    wil::com_ptr<ICoreWebView2> _webview;
    std::filesystem::path _assets;
    std::filesystem::path _userData;
    std::filesystem::path _log;
    RECT _bounds{};
    bool _started{ false };
    bool _closed{ false };
    bool _shown{ true };
    bool _visible{ false };
    bool _modal{ false };
    bool _restoreEditorFocus{ false };
    bool _focused{ false };
    bool _ready{ false };
    bool _failed{ false };
    bool _workspace{ false };
    double _width{ 480.0 };
};

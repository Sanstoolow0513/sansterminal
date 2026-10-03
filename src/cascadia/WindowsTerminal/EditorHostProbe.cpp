// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#include "pch.h"
#include "EditorHostProbe.h"
#include <fstream>
#include <wrl.h>
#include <winrt/Windows.Storage.h>

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;
using Microsoft::WRL::Callback;

namespace
{
    constexpr auto origin = L"editor-probe.sansterminal.invalid";
    constexpr auto pageUri = L"https://editor-probe.sansterminal.invalid/index.html";
}

EditorHostProbe::EditorHostProbe(HWND parent, HWND island, winrt::TerminalApp::TerminalWindow logic, std::function<void()> focusXaml) :
    _parent{ parent }, _island{ island }, _logic{ std::move(logic) }, _focusXaml{ std::move(focusXaml) }
{
    _assets = std::filesystem::path{ wil::GetModuleFileNameW<std::wstring>(nullptr) }.parent_path() / L"EditorHostProbe";
    std::filesystem::path cache;
    try
    {
        cache = std::wstring_view{ winrt::Windows::Storage::ApplicationData::Current().LocalCacheFolder().Path() };
    }
    catch (...)
    {
        wil::unique_cotaskmem_string path;
        THROW_IF_FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path));
        cache = path.get();
    }
    GUID id;
    THROW_IF_FAILED(CoCreateGuid(&id));
    _userData = cache / L"Sansterminal" / L"EditorHostProbe" / std::wstring_view{ winrt::to_hstring(winrt::guid{ id }) };
    std::filesystem::create_directories(_userData);
    _log = _userData / L"probe.log";
    _nativeWindow.reset(CreateWindowExW(0, L"STATIC", L"Sansterminal EditorHostProbe", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, _parent, nullptr, wil::GetModuleInstanceHandle(), nullptr));
    THROW_LAST_ERROR_IF_NULL(_nativeWindow.get());
    _Report(L"created: one host; offline scratch buffers; no project file writes");
}

EditorHostProbe::~EditorHostProbe()
{
    Close();
}

UIElement EditorHostProbe::CreateContent()
{
    const auto weak = weak_from_this();
    _layout = Grid{};
    RowDefinition toolbarRow;
    toolbarRow.Height(GridLengthHelper::Auto());
    _layout.RowDefinitions().Append(toolbarRow);
    _layout.RowDefinitions().Append(RowDefinition{});
    _layout.ColumnDefinitions().Append(ColumnDefinition{});
    ColumnDefinition dividerColumn;
    dividerColumn.Width(GridLengthHelper::FromPixels(6));
    _layout.ColumnDefinitions().Append(dividerColumn);
    _editorColumn = ColumnDefinition{};
    _editorColumn.Width(GridLengthHelper::FromPixels(_width));
    _layout.ColumnDefinitions().Append(_editorColumn);

    StackPanel toolbar;
    toolbar.Orientation(Orientation::Horizontal);
    Grid::SetColumnSpan(toolbar, 3);
    _layout.Children().Append(toolbar);
    const auto addButton = [&](std::wstring_view label, auto handler) {
        Button button;
        button.Content(winrt::box_value(winrt::hstring{ label }));
        button.Margin(ThicknessHelper::FromUniformLength(4));
        button.Click(handler);
        toolbar.Children().Append(button);
    };
    addButton(L"Editor probe: show/hide", [weak](auto&&, auto&&) {
        if (const auto self = weak.lock())
        {
            self->_shown = !self->_shown;
            self->_SyncBounds();
        }
    });
    addButton(L"Focus editor", [weak](auto&&, auto&&) {
        if (const auto self = weak.lock())
            self->_FocusEditor();
    });
    addButton(L"XAML dialog", [weak](auto&&, auto&&) {
        if (const auto self = weak.lock())
            self->_ShowDialog();
    });
    addButton(L"Self-test", [weak](auto&&, auto&&) {
        if (const auto self = weak.lock(); self && self->_ready)
            LOG_IF_FAILED(self->_webview->PostWebMessageAsString(L"run-smoke"));
    });

    const auto terminal = _logic.GetRoot().as<FrameworkElement>();
    Grid::SetRow(terminal, 1);
    _layout.Children().Append(terminal);
    Primitives::Thumb divider;
    divider.Background(Media::SolidColorBrush{ winrt::Windows::UI::Colors::Gray() });
    Grid::SetColumn(divider, 1);
    Grid::SetRow(divider, 1);
    divider.DragDelta([weak](auto&&, const Primitives::DragDeltaEventArgs& args) {
        if (const auto self = weak.lock())
        {
            self->_width = std::clamp(self->_width - args.HorizontalChange(), 160.0, std::max(160.0, self->_layout.ActualWidth() - 240.0));
            self->_SyncBounds();
        }
    });
    _layout.Children().Append(divider);
    _surface = Border{};
    _status = TextBlock{};
    _status.Text(L"Loading offline Monaco. Scratch buffers only; no save operation.");
    _status.TextWrapping(TextWrapping::Wrap);
    _status.Margin(ThicknessHelper::FromUniformLength(12));
    _surface.Child(_status);
    Grid::SetRow(_surface, 1);
    Grid::SetColumn(_surface, 2);
    _layout.Children().Append(_surface);
    _loaded = _layout.Loaded(winrt::auto_revoke, [weak](auto&&, auto&&) {
        if (const auto self = weak.lock())
            self->_Start();
    });
    _layoutUpdated = _layout.LayoutUpdated(winrt::auto_revoke, [weak](auto&&, auto&&) {
        if (const auto self = weak.lock())
            self->_SyncBounds();
    });
    _dialogVisibility = _logic.DialogVisibilityChanged(winrt::auto_revoke, [weak](auto&&, bool visible) {
        if (const auto self = weak.lock())
        {
            if (visible)
            {
                self->_restoreEditorFocus = self->HasFocus();
                self->_Report(self->_restoreEditorFocus ? L"dialog-focus: editor" : L"dialog-focus: xaml");
            }
            self->_modal = visible;
            self->_SyncBounds();
            self->_Report(visible ? L"dialog-open: native surface suppressed" : L"dialog-closed");
            if (!visible && std::exchange(self->_restoreEditorFocus, false))
                self->_FocusEditor();
        }
    });
    // Popups can have a separate layout root. Poll only in this opt-in probe;
    // a production host needs explicit overlay coordination for each surface.
    _popupTimer.Interval(std::chrono::milliseconds{ 50 });
    _popupTimer.Tick([weak](auto&&, auto&&) {
        if (const auto self = weak.lock())
            self->_SyncBounds();
    });
    return _layout;
}

void EditorHostProbe::_Start()
{
    if (_started || _closed)
        return;
    _started = true;
    _popupTimer.Start();
    if (!std::filesystem::exists(_assets / L"index.html"))
    {
        _Fail(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        return;
    }
    const auto weak = weak_from_this();
    const auto result = CreateCoreWebView2EnvironmentWithOptions(nullptr, _userData.c_str(), nullptr, Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([weak](HRESULT hr, ICoreWebView2Environment* environment) -> HRESULT {
                                                                                                          const auto self = weak.lock();
                                                                                                          if (!self || self->_closed)
                                                                                                              return S_OK;
                                                                                                          if (FAILED(hr) || !environment)
                                                                                                          {
                                                                                                              self->_Fail(FAILED(hr) ? hr : E_POINTER);
                                                                                                              return S_OK;
                                                                                                          }
                                                                                                          self->_environment = environment;
                                                                                                          const auto result = environment->CreateCoreWebView2Controller(self->_nativeWindow.get(),
                                                                                                                                                                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([weak](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                                                                                                                                                                            const auto owner = weak.lock();
                                                                                                                                                                            if (!owner || owner->_closed)
                                                                                                                                                                            {
                                                                                                                                                                                if (controller)
                                                                                                                                                                                    controller->Close();
                                                                                                                                                                                return S_OK;
                                                                                                                                                                            }
                                                                                                                                                                            if (SUCCEEDED(result) && controller)
                                                                                                                                                                                result = owner->_Configure(controller);
                                                                                                                                                                            else if (SUCCEEDED(result))
                                                                                                                                                                                result = E_POINTER;
                                                                                                                                                                            if (FAILED(result))
                                                                                                                                                                                owner->_Fail(result);
                                                                                                                                                                            return S_OK;
                                                                                                                                                                        }).Get());
                                                                                                          if (FAILED(result))
                                                                                                              self->_Fail(result);
                                                                                                          return S_OK;
                                                                                                      }).Get());
    if (FAILED(result))
        _Fail(result);
}

HRESULT EditorHostProbe::_Configure(ICoreWebView2Controller* controller)
try
{
    _controller = controller;
    RETURN_IF_FAILED(controller->get_CoreWebView2(&_webview));
    const auto mapped = _webview.query<ICoreWebView2_3>();
    RETURN_IF_FAILED(mapped->SetVirtualHostNameToFolderMapping(origin, _assets.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS));
    wil::com_ptr<ICoreWebView2Settings> settings;
    RETURN_IF_FAILED(_webview->get_Settings(&settings));
    RETURN_IF_FAILED(settings->put_AreHostObjectsAllowed(FALSE));
    RETURN_IF_FAILED(settings->put_AreDefaultScriptDialogsEnabled(FALSE));
    RETURN_IF_FAILED(settings->put_AreDevToolsEnabled(FALSE));
    RETURN_IF_FAILED(settings.query<ICoreWebView2Settings3>()->put_AreBrowserAcceleratorKeysEnabled(FALSE));
    RETURN_IF_FAILED(settings->put_IsStatusBarEnabled(FALSE));

    ::EventRegistrationToken token{};
    RETURN_IF_FAILED(_webview->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](auto*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                                          wil::unique_cotaskmem_string uri;
                                                          RETURN_IF_FAILED(args->get_Uri(&uri));
                                                          return args->put_Cancel(std::wstring_view{ uri.get() } != pageUri);
                                                      }).Get(),
                                                      &token));
    RETURN_IF_FAILED(_webview->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](auto*, auto* args) -> HRESULT { return args->put_Cancel(TRUE); }).Get(), &token));
    RETURN_IF_FAILED(_webview->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](auto*, auto* args) -> HRESULT { return args->put_Handled(TRUE); }).Get(), &token));
    RETURN_IF_FAILED(_webview->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](auto*, auto* args) -> HRESULT { return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); }).Get(), &token));
    RETURN_IF_FAILED(_webview.query<ICoreWebView2_4>()->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([](auto*, auto* args) -> HRESULT { return args->put_Cancel(TRUE); }).Get(), &token));

    const auto weak = weak_from_this();
    RETURN_IF_FAILED(_webview->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([weak](auto*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                                          const auto self = weak.lock();
                                                          if (!self || self->_closed)
                                                              return S_OK;
                                                          wil::unique_cotaskmem_string source;
                                                          wil::unique_cotaskmem_string text;
                                                          RETURN_IF_FAILED(args->get_Source(&source));
                                                          if (std::wstring_view{ source.get() } != pageUri)
                                                              return E_ACCESSDENIED;
                                                          RETURN_IF_FAILED(args->TryGetWebMessageAsString(&text));
                                                          const std::wstring_view message{ text.get() };
                                                          if (message.size() > 1024)
                                                              return E_INVALIDARG;
                                                          if (message == L"focus-terminal")
                                                              self->_FocusTerminal();
                                                          else if (message == L"show-dialog")
                                                              self->_ShowDialog();
                                                          else if (message == L"ready")
                                                          {
                                                              self->_ready = true;
                                                              self->_Report(message);
                                                          }
                                                          else if (message.starts_with(L"smoke-") || message.starts_with(L"page-error:") || message.starts_with(L"scratch-only:"))
                                                              self->_Report(message);
                                                          else
                                                              return E_INVALIDARG;
                                                          return S_OK;
                                                      }).Get(),
                                                      &token));
    RETURN_IF_FAILED(_webview->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([weak](auto*, auto*) -> HRESULT {
                                                     if (const auto self = weak.lock())
                                                         self->_Fail(E_UNEXPECTED);
                                                     return S_OK;
                                                 }).Get(),
                                                 &token));
    RETURN_IF_FAILED(controller->add_GotFocus(Callback<ICoreWebView2FocusChangedEventHandler>([weak](auto*, auto*) -> HRESULT {
                                                  if (const auto self = weak.lock())
                                                      self->_focused = true;
                                                  return S_OK;
                                              }).Get(),
                                              &token));
    RETURN_IF_FAILED(controller->add_LostFocus(Callback<ICoreWebView2FocusChangedEventHandler>([weak](auto*, auto*) -> HRESULT {
                                                   if (const auto self = weak.lock())
                                                       self->_focused = false;
                                                   return S_OK;
                                               }).Get(),
                                               &token));
    RETURN_IF_FAILED(controller->add_MoveFocusRequested(Callback<ICoreWebView2MoveFocusRequestedEventHandler>([weak](auto*, auto* args) -> HRESULT {
                                                            if (const auto self = weak.lock())
                                                                self->_FocusTerminal();
                                                            return args->put_Handled(TRUE);
                                                        }).Get(),
                                                        &token));
    _Report(L"controller-created: 1");
    UINT browserProcess = 0;
    RETURN_IF_FAILED(_webview->get_BrowserProcessId(&browserProcess));
    _Report(std::wstring{ L"browser-process: " } + std::to_wstring(browserProcess));
    _bounds = {};
    _visible = false;
    _SyncBounds();
    RETURN_IF_FAILED(_webview->Navigate(pageUri));
    return S_OK;
}
CATCH_RETURN();

void EditorHostProbe::_SyncBounds()
try
{
    if (_closed || !_layout)
        return;
    const auto width = _shown ? std::min(_width, std::max(0.0, _layout.ActualWidth() - 240.0)) : 0.0;
    if (_editorColumn.Width().Value != width)
        _editorColumn.Width(GridLengthHelper::FromPixels(width));
    const auto root = _layout.XamlRoot();
    if (!root)
        return;
    const auto popup = Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root).Size() != 0;
    const auto visible = _controller && !_failed && _shown && !_modal && !popup && _surface.ActualWidth() > 0 && _surface.ActualHeight() > 0 && !IsIconic(_parent);
    if (_visible != visible)
    {
        _visible = visible;
        if (_controller)
            LOG_IF_FAILED(_controller->put_IsVisible(visible));
        ShowWindow(_nativeWindow.get(), visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (!visible)
            _focused = false;
        _Report(visible ? L"surface-visible" : L"surface-hidden");
    }
    if (!visible)
        return;
    const auto position = _surface.TransformToVisual(root.Content()).TransformPoint({});
    const auto scale = root.RasterizationScale();
    POINT offset{};
    MapWindowPoints(_island, _parent, &offset, 1);
    const RECT bounds{
        offset.x + static_cast<LONG>(std::lround(position.X * scale)),
        offset.y + static_cast<LONG>(std::lround(position.Y * scale)),
        static_cast<LONG>(std::lround(_surface.ActualWidth() * scale)),
        static_cast<LONG>(std::lround(_surface.ActualHeight() * scale))
    };
    if (!EqualRect(&_bounds, &bounds))
    {
        _bounds = bounds;
        SetWindowPos(_nativeWindow.get(), HWND_TOP, bounds.left, bounds.top, bounds.right, bounds.bottom, SWP_NOACTIVATE);
        if (_controller)
            LOG_IF_FAILED(_controller->put_Bounds(RECT{ 0, 0, bounds.right, bounds.bottom }));
    }
    if (_controller)
        LOG_IF_FAILED(_controller->NotifyParentWindowPositionChanged());
}
CATCH_LOG();

bool EditorHostProbe::HasFocus() const noexcept
{
    if (_closed || !_visible)
        return false;
    GUITHREADINFO info{ sizeof(info) };
    if (GetGUIThreadInfo(GetWindowThreadProcessId(_parent, nullptr), &info))
        return info.hwndFocus == _nativeWindow.get() || IsChild(_nativeWindow.get(), info.hwndFocus);
    return _focused;
}

winrt::fire_and_forget EditorHostProbe::_FocusTerminal()
{
    const auto keepAlive = shared_from_this();
    try
    {
        co_await wil::resume_foreground(_layout.Dispatcher());
        if (_closed)
            co_return;
        _focusXaml();
        _logic.FocusActiveTerminal();
        _Report(L"focus-terminal");
    }
    CATCH_LOG();
}

winrt::fire_and_forget EditorHostProbe::_FocusEditor()
{
    const auto keepAlive = shared_from_this();
    try
    {
        co_await wil::resume_foreground(_layout.Dispatcher(), winrt::Windows::UI::Core::CoreDispatcherPriority::Low);
        if (_closed || !_visible || !_controller)
            co_return;
        LOG_IF_FAILED(_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC));
        LOG_IF_FAILED(_webview->PostWebMessageAsString(L"focus"));
        _Report(L"focus-editor");
    }
    CATCH_LOG();
}

winrt::fire_and_forget EditorHostProbe::_ShowDialog()
{
    const auto keepAlive = shared_from_this();
    try
    {
        ContentDialog dialog;
        dialog.Title(winrt::box_value(L"XAML / WebView2 layer test"));
        dialog.Content(winrt::box_value(L"The native editor is hidden while this modal is open. Closing it must restore the same buffers and controller."));
        dialog.CloseButtonText(L"Return to editor");
        co_await _logic.ShowDialog(dialog);
    }
    CATCH_LOG();
}

void EditorHostProbe::_Report(std::wstring_view message)
{
    std::ofstream stream{ _log, std::ios::app };
    stream << winrt::to_string(winrt::hstring{ message }) << '\n';
    OutputDebugStringW((std::wstring{ L"EditorHostProbe: " } + std::wstring{ message } + L"\n").c_str());
}

void EditorHostProbe::_Fail(HRESULT result)
{
    _ready = false;
    _failed = true;
    if (_controller)
        _controller->put_IsVisible(FALSE);
    ShowWindow(_nativeWindow.get(), SW_HIDE);
    _Report(std::wstring{ L"host-error: " } + std::wstring{ winrt::hresult_error{ result }.message() });
    _status.Text(L"Editor probe failed. See LocalCache/Sansterminal/EditorHostProbe/*/probe.log. The terminal remains available.");
}

void EditorHostProbe::Close() noexcept
{
    if (std::exchange(_closed, true))
        return;
    _loaded.revoke();
    _layoutUpdated.revoke();
    _dialogVisibility.revoke();
    _popupTimer.Destroy();
    if (_controller)
        LOG_IF_FAILED(_controller->Close());
    _webview.reset();
    _controller.reset();
    _environment.reset();
    _nativeWindow.reset();
    try
    {
        _Report(L"closed: controller released");
    }
    CATCH_LOG();
}

// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#include "pch.h"
#include "EditorHostProbe.h"
#include <fstream>
#include <wrl.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Data.Json.h>

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;
using Microsoft::WRL::Callback;

namespace
{
    constexpr auto origin = L"editor-probe.sansterminal.invalid";
    constexpr auto pageUri = L"https://editor-probe.sansterminal.invalid/index.html";
    constexpr auto workspacePageUri = L"https://editor-probe.sansterminal.invalid/index.html?workspace=1";
}

EditorHostProbe::EditorHostProbe(HWND parent, HWND island, winrt::TerminalApp::TerminalWindow logic, std::function<void()> focusXaml, bool workspace) :
    _parent{ parent }, _island{ island }, _logic{ std::move(logic) }, _focusXaml{ std::move(focusXaml) }, _workspace{ workspace }
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
    _Report(_workspace ? L"created: one workspace editor host; native document bridge" : L"created: one host; offline scratch buffers; no project file writes");
}

EditorHostProbe::~EditorHostProbe()
{
    Close();
}

UIElement EditorHostProbe::CreateContent()
{
    const auto weak = weak_from_this();
    if (_workspace)
    {
        _logic.SetWorkspaceEditorEnabled(true);
        _layout = _logic.GetRoot().as<FrameworkElement>();
        _surface = _logic.GetWorkspaceEditorSurface().as<Border>();
        _status = TextBlock{};
        _status.Text(L"正在加载离线编辑器…");
        _status.TextWrapping(TextWrapping::Wrap);
        _status.Margin(ThicknessHelper::FromUniformLength(12));
        _surface.Child(_status);
        _workspaceMessage = _logic.WorkspaceEditorMessage(winrt::auto_revoke, [weak](auto&&, const winrt::hstring& message) {
            if (const auto self = weak.lock(); self && !self->_closed)
            {
                const auto json = winrt::Windows::Data::Json::JsonObject::Parse(message);
                const auto type = json.GetNamedString(L"type", L"");
                if (type == L"focus-editor" || type == L"focus")
                    self->_FocusEditor();
                else if (self->_ready && self->_webview)
                    LOG_IF_FAILED(self->_webview->PostWebMessageAsJson(message.c_str()));
            }
        });
    }
    else
    {
        Grid layout;
        _layout = layout;
        RowDefinition toolbarRow;
        toolbarRow.Height(GridLengthHelper::Auto());
        layout.RowDefinitions().Append(toolbarRow);
        layout.RowDefinitions().Append(RowDefinition{});
        layout.ColumnDefinitions().Append(ColumnDefinition{});
        ColumnDefinition dividerColumn;
        dividerColumn.Width(GridLengthHelper::FromPixels(6));
        layout.ColumnDefinitions().Append(dividerColumn);
        _editorColumn = ColumnDefinition{};
        _editorColumn.Width(GridLengthHelper::FromPixels(_width));
        layout.ColumnDefinitions().Append(_editorColumn);

        StackPanel toolbar;
        toolbar.Orientation(Orientation::Horizontal);
        Grid::SetColumnSpan(toolbar, 3);
        layout.Children().Append(toolbar);
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
        layout.Children().Append(terminal);
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
        layout.Children().Append(divider);
        _surface = Border{};
        _status = TextBlock{};
        _status.Text(L"Loading offline Monaco. Scratch buffers only; no save operation.");
        _status.TextWrapping(TextWrapping::Wrap);
        _status.Margin(ThicknessHelper::FromUniformLength(12));
        _surface.Child(_status);
        Grid::SetRow(_surface, 1);
        Grid::SetColumn(_surface, 2);
        layout.Children().Append(_surface);
    }
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
    const auto allowedPage = _workspace ? workspacePageUri : pageUri;
    RETURN_IF_FAILED(_webview->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([allowedPage](auto*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                                          wil::unique_cotaskmem_string uri;
                                                          RETURN_IF_FAILED(args->get_Uri(&uri));
                                                          return args->put_Cancel(std::wstring_view{ uri.get() } != allowedPage);
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
                                                          if (std::wstring_view{ source.get() } != (self->_workspace ? workspacePageUri : pageUri))
                                                              return E_ACCESSDENIED;
                                                          if (self->_workspace)
                                                              return self->_HandleWorkspaceMessage(args);
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
                                                  {
                                                      self->_focused = true;
                                                      if (self->_workspace)
                                                          self->_logic.WorkspaceEditorFocused();
                                                  }
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
    RETURN_IF_FAILED(_webview->Navigate(allowedPage));
    return S_OK;
}
CATCH_RETURN();

HRESULT EditorHostProbe::_HandleWorkspaceMessage(ICoreWebView2WebMessageReceivedEventArgs* args)
try
{
    using namespace winrt::Windows::Data::Json;
    wil::unique_cotaskmem_string raw;
    RETURN_IF_FAILED(args->get_WebMessageAsJson(&raw));
    // Document content is capped separately by the native document service.
    // Check size before parsing, including all JSON string escaping overhead.
    if (std::wstring_view{ raw.get() }.size() > 16 * 1024 * 1024)
        return E_INVALIDARG;
    const auto message = JsonObject::Parse(raw.get());
    if (message.GetNamedNumber(L"version", 0) != 1)
        return E_INVALIDARG;
    const auto type = message.GetNamedString(L"type", L"");
    if (type == L"focus-terminal")
        _FocusTerminal();
    else if (type == L"focused")
        _logic.WorkspaceEditorFocused();
    else if (type == L"show-dialog")
        _ShowDialog();
    else if (type == L"report")
    {
        const auto diagnostic = message.GetNamedString(L"message", L"");
        const std::wstring_view diagnosticView{ diagnostic };
        if (diagnostic.size() > 1024 || !(diagnosticView.starts_with(L"smoke-") || diagnosticView.starts_with(L"page-error:")))
            return E_INVALIDARG;
        _Report(diagnostic);
    }
    else if (type == L"ready" || type == L"changed" || type == L"save" || type == L"flushed" || type == L"sync-error")
    {
        if (type == L"ready")
        {
            _ready = true;
            _Report(L"ready: workspace document bridge");
        }
        _logic.HandleWorkspaceEditorMessage(raw.get());
    }
    else
        return E_INVALIDARG;
    return S_OK;
}
CATCH_RETURN();

void EditorHostProbe::_SyncBounds()
try
{
    if (_closed || !_layout)
        return;
    if (!_workspace)
    {
        const auto width = _shown ? std::min(_width, std::max(0.0, _layout.ActualWidth() - 240.0)) : 0.0;
        if (_editorColumn.Width().Value != width)
            _editorColumn.Width(GridLengthHelper::FromPixels(width));
    }
    const auto root = _layout.XamlRoot();
    if (!root)
        return;
    const auto popup = Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root).Size() != 0;
    bool ancestorsVisible = _surface.IsLoaded();
    for (DependencyObject element = _surface; element && ancestorsVisible; element = Media::VisualTreeHelper::GetParent(element))
    {
        if (const auto visual = element.try_as<UIElement>())
            ancestorsVisible = visual.Visibility() == Visibility::Visible;
    }
    const auto visible = _controller && !_failed && _shown && !_modal && !popup && ancestorsVisible && _surface.ActualWidth() > 0 && _surface.ActualHeight() > 0 && !IsIconic(_parent);
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
        _SyncBounds();
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
    if (_workspace)
        _logic.HandleWorkspaceEditorMessage(L"{\"version\":1,\"type\":\"unavailable\"}");
    if (_controller)
        _controller->put_IsVisible(FALSE);
    ShowWindow(_nativeWindow.get(), SW_HIDE);
    _Report(std::wstring{ L"host-error: " } + std::wstring{ winrt::hresult_error{ result }.message() });
    if (_workspace)
    {
        _status = TextBlock{};
        _status.TextWrapping(TextWrapping::Wrap);
        _status.Margin(ThicknessHelper::FromUniformLength(12));
        _status.Text(L"编辑器加载失败。文档缓冲区仍保留在当前窗口，可重新加载编辑器。详情见 LocalCache/Sansterminal/EditorHostProbe/*/probe.log。");
        StackPanel recovery;
        recovery.Children().Append(_status);
        Button retry;
        retry.Content(winrt::box_value(L"重新加载编辑器"));
        const auto weak = weak_from_this();
        retry.Click([weak](auto&&, auto&&) {
            if (const auto self = weak.lock())
                self->_Reload();
        });
        recovery.Children().Append(retry);
        _surface.Child(recovery);
    }
    else
        _status.Text(L"Editor probe failed. See LocalCache/Sansterminal/EditorHostProbe/*/probe.log. The terminal remains available.");
}

void EditorHostProbe::_Reload()
{
    if (_closed)
        return;
    if (_controller)
        LOG_IF_FAILED(_controller->Close());
    _webview.reset();
    _controller.reset();
    _environment.reset();
    _failed = false;
    _started = false;
    _visible = false;
    _status = TextBlock{};
    _status.TextWrapping(TextWrapping::Wrap);
    _status.Margin(ThicknessHelper::FromUniformLength(12));
    _status.Text(L"正在重新加载编辑器…");
    _surface.Child(_status);
    _Report(L"reload: replay native document buffers; undo history resets");
    _Start();
}

void EditorHostProbe::Close() noexcept
{
    if (std::exchange(_closed, true))
        return;
    _loaded.revoke();
    _layoutUpdated.revoke();
    _dialogVisibility.revoke();
    _workspaceMessage.revoke();
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

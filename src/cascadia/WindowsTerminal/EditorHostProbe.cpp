// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#include "pch.h"
#include "EditorHostProbe.h"

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;

EditorHostProbe::EditorHostProbe(HWND parent, HWND island, winrt::TerminalApp::TerminalWindow logic, std::function<void()> focusXaml) :
    WorkspaceEditorHost{ parent, island, std::move(logic), std::move(focusXaml), L"EditorHostProbe", L"https://editor-probe.sansterminal.invalid/index.html" }
{
}

void EditorHostProbe::_CreateSurface()
{
    const auto weak = weak_from_this();
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
        if (const auto self = std::static_pointer_cast<EditorHostProbe>(weak.lock()))
        {
            self->_shown = !self->_shown;
            self->_SyncBounds();
        }
    });
    addButton(L"Focus editor", [weak](auto&&, auto&&) {
        if (const auto self = std::static_pointer_cast<EditorHostProbe>(weak.lock()))
            self->_FocusEditor();
    });
    addButton(L"XAML dialog", [weak](auto&&, auto&&) {
        if (const auto self = std::static_pointer_cast<EditorHostProbe>(weak.lock()))
            self->_ShowDialog();
    });
    addButton(L"Self-test", [weak](auto&&, auto&&) {
        if (const auto self = std::static_pointer_cast<EditorHostProbe>(weak.lock()); self && self->_ready)
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
        if (const auto self = std::static_pointer_cast<EditorHostProbe>(weak.lock()))
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

void EditorHostProbe::_UpdateSurfaceWidth()
{
    const auto width = _shown ? std::min(_width, std::max(0.0, _layout.ActualWidth() - 240.0)) : 0.0;
    if (_editorColumn.Width().Value != width)
        _editorColumn.Width(GridLengthHelper::FromPixels(width));
}

HRESULT EditorHostProbe::_HandleWebMessage(ICoreWebView2WebMessageReceivedEventArgs* args)
{
    wil::unique_cotaskmem_string text;
    RETURN_IF_FAILED(args->TryGetWebMessageAsString(&text));
    const std::wstring_view message{ text.get() };
    if (message.size() > 1024)
        return E_INVALIDARG;
    if (message == L"focus-terminal")
        _FocusTerminal();
    else if (message == L"show-dialog")
        _ShowDialog();
    else if (message == L"ready")
    {
        _ready = true;
        _Report(message);
    }
    else if (message.starts_with(L"smoke-") || message.starts_with(L"page-error:") || message.starts_with(L"scratch-only:"))
        _Report(message);
    else
        return E_INVALIDARG;
    return S_OK;
}

void EditorHostProbe::_ShowFailure()
{
    _status.Text(L"Editor probe failed. See LocalCache/Sansterminal/EditorHostProbe/probe.log. The terminal remains available.");
}

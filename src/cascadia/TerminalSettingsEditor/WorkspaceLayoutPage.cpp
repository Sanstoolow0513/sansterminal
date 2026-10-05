// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#include "pch.h"
#include "WorkspaceLayoutPage.h"
#include "WorkspaceLayoutPage.g.cpp"
#include "MainPage.h"
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>

using namespace winrt;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;
namespace LayoutModel = Sansterminal::WorkspaceLayout;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    WorkspaceLayoutPage::WorkspaceLayoutPage()
    {
        InitializeComponent();
        const auto weak = get_weak();
        const auto canvas = WorkspaceLayoutPreview();
        canvas.AddHandler(UIElement::PointerMovedEvent(), box_value(Input::PointerEventHandler{ [weak](auto&&, const Input::PointerRoutedEventArgs& args) {
            if (const auto self = weak.get()) self->_PreviewPointerMoved(args);
        } }), true);
        canvas.AddHandler(UIElement::PointerReleasedEvent(), box_value(Input::PointerEventHandler{ [weak](auto&&, const Input::PointerRoutedEventArgs& args) {
            if (const auto self = weak.get()) self->_PreviewPointerReleased(args);
        } }), true);
        canvas.PointerCaptureLost([weak](auto&&, const Input::PointerRoutedEventArgs& args) {
            if (const auto self = weak.get()) self->_PreviewPointerCaptureLost(args);
        });
        canvas.PointerCanceled([weak](auto&&, const Input::PointerRoutedEventArgs& args) {
            if (const auto self = weak.get(); self && self->_previewPointer && self->_previewPointer.PointerId() == args.Pointer().PointerId()) self->_CancelPreviewDrag();
        });
    }

    void WorkspaceLayoutPage::OnNavigatedTo(const Navigation::NavigationEventArgs& e)
    {
        const auto args = e.Parameter().as<Editor::NavigateToPageArgs>();
        _GlobalSettings = args.ViewModel().as<Model::GlobalAppSettings>();
        _layout = LayoutModel::Layout::Parse(std::wstring_view{ _GlobalSettings.WorkspaceLayout() });
        _SyncControls();
        BringIntoViewWhenLoaded(args.ElementToFocus());
    }

    void WorkspaceLayoutPage::_SyncControls()
    {
        _initializing = true;
        WorkspaceShowFiles().IsOn(_GlobalSettings.WorkspaceShowFiles());
        WorkspaceShowTerminal().IsOn(_GlobalSettings.WorkspaceShowTerminal());
        WorkspaceShowEditor().IsOn(_GlobalSettings.WorkspaceShowEditor());
        WorkspaceShowTabs().IsOn(_GlobalSettings.WorkspaceShowTabs());
        const auto geometry = _layout.Evaluate({ true, true, true }, { 0, 0, 100, 100 });
        const std::array sliders{ WorkspaceLayoutRootRatio(), WorkspaceLayoutInnerRatio() };
        for (size_t i = 0; i < sliders.size(); ++i)
        {
            const auto& split = geometry.splits[i];
            sliders[i].Tag(box_value(hstring{ std::wstring{ split.path.begin(), split.path.end() } }));
            sliders[i].Value(split.ratio * 100);
        }
        _initializing = false;
        _RefreshPreview();
    }

    void WorkspaceLayoutPage::VisibilityToggled(const IInspectable&, const RoutedEventArgs&)
    {
        if (_initializing || !_GlobalSettings) return;
        _GlobalSettings.WorkspaceShowFiles(WorkspaceShowFiles().IsOn());
        _GlobalSettings.WorkspaceShowTerminal(WorkspaceShowTerminal().IsOn());
        _GlobalSettings.WorkspaceShowEditor(WorkspaceShowEditor().IsOn());
        _GlobalSettings.WorkspaceShowTabs(WorkspaceShowTabs().IsOn());
        _RefreshPreview();
    }

    void WorkspaceLayoutPage::_Move()
    {
        if (_layout.Move(static_cast<LayoutModel::Pane>(WorkspaceLayoutSource().SelectedIndex()),
                         static_cast<LayoutModel::Pane>(WorkspaceLayoutTarget().SelectedIndex()),
                         static_cast<LayoutModel::Side>(WorkspaceLayoutSide().SelectedIndex())))
        {
            _GlobalSettings.WorkspaceLayout(hstring{ _layout.Serialize() });
            WorkspaceLayoutFeedback().Text({});
            _SyncControls();
        }
        else WorkspaceLayoutFeedback().Text(RS_(L"WorkspaceLayoutChooseDifferentPanes"));
    }

    void WorkspaceLayoutPage::MoveClicked(const IInspectable&, const RoutedEventArgs&)
    {
        _Move();
    }

    void WorkspaceLayoutPage::ResetClicked(const IInspectable&, const RoutedEventArgs&)
    {
        _layout = LayoutModel::Layout::Default();
        _GlobalSettings.WorkspaceLayout(hstring{ _layout.Serialize() });
        _GlobalSettings.WorkspaceShowFiles(true);
        _GlobalSettings.WorkspaceShowTerminal(true);
        _GlobalSettings.WorkspaceShowEditor(true);
        _GlobalSettings.WorkspaceShowTabs(true);
        WorkspaceLayoutFeedback().Text({});
        _SyncControls();
    }

    void WorkspaceLayoutPage::RatioChanged(const IInspectable& sender, const Primitives::RangeBaseValueChangedEventArgs& args)
    {
        if (_initializing || !_GlobalSettings) return;
        const auto path = unbox_value<hstring>(sender.as<Slider>().Tag());
        if (_layout.SetRatio(to_string(path), args.NewValue() / 100))
        {
            _GlobalSettings.WorkspaceLayout(hstring{ _layout.Serialize() });
            _RefreshPreview();
        }
    }

    void WorkspaceLayoutPage::PreviewSizeChanged(const IInspectable&, const SizeChangedEventArgs&)
    {
        _RefreshPreview();
    }

    void WorkspaceLayoutPage::_RefreshPreview()
    {
        if (!_GlobalSettings || _updatingPreview || _previewPointer) return;
        _updatingPreview = true;
        const auto reset = wil::scope_exit([&] { _updatingPreview = false; });
        const auto canvas = WorkspaceLayoutPreview();
        canvas.Children().Clear();
        const auto width = std::max(280.0, canvas.ActualWidth());
        const auto height = canvas.Height();
        const bool tabsVisible = _GlobalSettings.WorkspaceShowTabs();
        const auto tabsWidth = tabsVisible ? 80.0 : 0.0;
        const auto geometry = _layout.Evaluate({ _GlobalSettings.WorkspaceShowFiles(), _GlobalSettings.WorkspaceShowTerminal(), _GlobalSettings.WorkspaceShowEditor() },
                                               { tabsWidth, 0, width - tabsWidth, height });
        _previewGeometry = geometry;
        const std::array names{ RS_(L"WorkspacePaneFiles"), RS_(L"WorkspacePaneTerminal"), RS_(L"WorkspacePaneEditor"), RS_(L"WorkspacePaneTabs") };
        const std::array ids{ L"WorkspacePreviewFiles", L"WorkspacePreviewTerminal", L"WorkspacePreviewEditor", L"WorkspacePreviewTabs" };
        for (size_t i = 0; i < 4; ++i)
        {
            if (i == 3 ? !tabsVisible : !geometry.visible[i]) continue;
            const auto rect = i == 3 ? LayoutModel::Rect{ 0, 0, tabsWidth - LayoutModel::Gutter, height } : geometry.panes[i];
            Button pane;
            pane.Content(box_value(names[i]));
            pane.Width(rect.width);
            pane.Height(rect.height);
            pane.IsTabStop(false);
            pane.Tag(box_value(static_cast<uint32_t>(i)));
            pane.Background(SolidColorBrush{ i == 0 ? Windows::UI::Color{ 255, 40, 74, 95 } : i == 1 ? Windows::UI::Color{ 255, 38, 47, 62 } : i == 2 ? Windows::UI::Color{ 255, 57, 65, 82 } : Windows::UI::Color{ 255, 59, 48, 74 } });
            pane.Foreground(SolidColorBrush{ Windows::UI::Colors::White() });
            Automation::AutomationProperties::SetAutomationId(pane, ids[i]);
            Automation::AutomationProperties::SetName(pane, names[i]);
            Automation::AutomationProperties::SetAccessibilityView(pane, Automation::Peers::AccessibilityView::Content);
            Canvas::SetLeft(pane, rect.x);
            Canvas::SetTop(pane, rect.y);
            if (i < 3)
            {
                const auto weak = get_weak();
                pane.Click([weak, i](auto&&, auto&&) { if (const auto self = weak.get()) self->WorkspaceLayoutSource().SelectedIndex(static_cast<int32_t>(i)); });
                pane.CanDrag(false);
                pane.AddHandler(UIElement::PointerPressedEvent(), box_value(Input::PointerEventHandler{ [weak](const auto& sender, const Input::PointerRoutedEventArgs& args) {
                    if (const auto self = weak.get()) self->_PreviewPanePressed(sender, args);
                } }), true);
            }
            canvas.Children().Append(pane);
        }
        _previewDropIndicator = Border{};
        _previewDropIndicator.Background(SolidColorBrush{ Windows::UI::Color{ 255, 68, 170, 255 } });
        _previewDropIndicator.IsHitTestVisible(false);
        _previewDropIndicator.Visibility(Visibility::Collapsed);
        canvas.Children().Append(_previewDropIndicator);
    }

    void WorkspaceLayoutPage::_PreviewPanePressed(const IInspectable& sender, const Input::PointerRoutedEventArgs& args)
    try
    {
        if (_previewPointer || !_GlobalSettings) return;
        const auto canvas = WorkspaceLayoutPreview();
        const auto point = args.GetCurrentPoint(canvas);
        if (args.Pointer().PointerDeviceType() == Windows::Devices::Input::PointerDeviceType::Mouse && !point.Properties().IsLeftButtonPressed()) return;
        const auto source = sender.as<Button>();
        const auto pane = unbox_value<uint32_t>(source.Tag());
        if (pane >= 3) return;
        WorkspaceLayoutSource().SelectedIndex(static_cast<int32_t>(pane));
        // Button normally owns capture for Click. Transfer ownership to the
        // canvas, whose release event cannot be consumed by Button's handler.
        source.ReleasePointerCapture(args.Pointer());
        if (!canvas.CapturePointer(args.Pointer())) return;
        _previewPointer = args.Pointer();
        _previewDragSource = static_cast<LayoutModel::Pane>(pane);
        _previewDragTarget.reset();
        _previewDragMoved = false;
        _previewDragStart = point.Position();
        args.Handled(true);
    }
    CATCH_LOG()

    void WorkspaceLayoutPage::_PreviewPointerMoved(const Input::PointerRoutedEventArgs& args)
    try
    {
        if (!_previewPointer || _previewPointer.PointerId() != args.Pointer().PointerId()) return;
        _UpdatePreviewDrag(args.GetCurrentPoint(WorkspaceLayoutPreview()).Position());
        args.Handled(true);
    }
    CATCH_LOG()

    void WorkspaceLayoutPage::_UpdatePreviewDrag(const Windows::Foundation::Point point)
    {
        if (!_previewDragSource) return;
        const auto distance = std::hypot(point.X - _previewDragStart.X, point.Y - _previewDragStart.Y);
        _previewDragMoved = _previewDragMoved || distance >= 4.0;
        if (!_previewDragMoved) return;
        _previewDragTarget.reset();
        _previewDropIndicator.Visibility(Visibility::Collapsed);
        for (size_t pane = 0; pane < 3; ++pane)
        {
            if (!_previewGeometry.visible[pane] || pane == static_cast<size_t>(*_previewDragSource)) continue;
            const auto rect = _previewGeometry.panes[pane];
            if (point.X < rect.x || point.X > rect.x + rect.width || point.Y < rect.y || point.Y > rect.y + rect.height) continue;
            const auto x = (point.X - rect.x) / std::max(1.0, rect.width);
            const auto y = (point.Y - rect.y) / std::max(1.0, rect.height);
            const std::array distances{ x, 1 - x, y, 1 - y };
            const auto side = static_cast<size_t>(std::min_element(distances.begin(), distances.end()) - distances.begin());
            _previewDragTarget = static_cast<LayoutModel::Pane>(pane);
            _previewDragSide = static_cast<LayoutModel::Side>(side);
            const bool vertical = side < 2;
            _previewDropIndicator.Width(vertical ? 4.0 : rect.width);
            _previewDropIndicator.Height(vertical ? rect.height : 4.0);
            Canvas::SetLeft(_previewDropIndicator, rect.x + (side == 1 ? std::max(0.0, rect.width - 4.0) : 0.0));
            Canvas::SetTop(_previewDropIndicator, rect.y + (side == 3 ? std::max(0.0, rect.height - 4.0) : 0.0));
            _previewDropIndicator.Visibility(Visibility::Visible);
            const std::array names{ RS_(L"WorkspacePaneFiles"), RS_(L"WorkspacePaneTerminal"), RS_(L"WorkspacePaneEditor") };
            const std::array sides{ RS_(L"WorkspaceSideLeft/Content"), RS_(L"WorkspaceSideRight/Content"), RS_(L"WorkspaceSideAbove/Content"), RS_(L"WorkspaceSideBelow/Content") };
            WorkspaceLayoutFeedback().Text(names[static_cast<size_t>(*_previewDragSource)] + L" → " + names[pane] + L" · " + sides[side]);
            return;
        }
        WorkspaceLayoutFeedback().Text({});
    }

    void WorkspaceLayoutPage::_PreviewPointerReleased(const Input::PointerRoutedEventArgs& args)
    try
    {
        if (!_previewPointer || _previewPointer.PointerId() != args.Pointer().PointerId()) return;
        _UpdatePreviewDrag(args.GetCurrentPoint(WorkspaceLayoutPreview()).Position());
        const auto source = _previewDragSource;
        const auto target = _previewDragTarget;
        const auto side = _previewDragSide;
        const bool move = _previewDragMoved && source && target;
        _CancelPreviewDrag();
        args.Handled(true);
        if (move)
        {
            WorkspaceLayoutSource().SelectedIndex(static_cast<int32_t>(*source));
            WorkspaceLayoutTarget().SelectedIndex(static_cast<int32_t>(*target));
            WorkspaceLayoutSide().SelectedIndex(static_cast<int32_t>(side));
            _Move();
        }
        else _RefreshPreview();
    }
    CATCH_LOG()

    void WorkspaceLayoutPage::_PreviewPointerCaptureLost(const Input::PointerRoutedEventArgs& args)
    try
    {
        // Ignore the source Button's capture-lost event when it bubbles after
        // ownership was transferred. Only loss of the canvas capture ends drag.
        if (args.OriginalSource() != WorkspaceLayoutPreview()) return;
        if (!_previewPointer || _previewPointer.PointerId() != args.Pointer().PointerId()) return;
        if (!args.GetCurrentPoint(WorkspaceLayoutPreview()).IsInContact())
        {
            _PreviewPointerReleased(args);
        }
        else
        {
            _CancelPreviewDrag();
            _RefreshPreview();
        }
    }
    CATCH_LOG()

    void WorkspaceLayoutPage::_CancelPreviewDrag()
    {
        const auto pointer = std::exchange(_previewPointer, nullptr);
        _previewDragSource.reset();
        _previewDragTarget.reset();
        _previewDragMoved = false;
        if (_previewDropIndicator) _previewDropIndicator.Visibility(Visibility::Collapsed);
        WorkspaceLayoutFeedback().Text({});
        if (pointer) WorkspaceLayoutPreview().ReleasePointerCapture(pointer);
    }
}

// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#pragma once

#include "WorkspaceLayoutPage.g.h"
#include "Utils.h"
#include "../TerminalSettingsModel/WorkspaceLayout.h"

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    struct WorkspaceLayoutPage : public HasScrollViewer<WorkspaceLayoutPage>, WorkspaceLayoutPageT<WorkspaceLayoutPage>
    {
        WorkspaceLayoutPage();
        void OnNavigatedTo(const Windows::UI::Xaml::Navigation::NavigationEventArgs& e);
        void VisibilityToggled(const Windows::Foundation::IInspectable&, const Windows::UI::Xaml::RoutedEventArgs&);
        void MoveClicked(const Windows::Foundation::IInspectable&, const Windows::UI::Xaml::RoutedEventArgs&);
        void ResetClicked(const Windows::Foundation::IInspectable&, const Windows::UI::Xaml::RoutedEventArgs&);
        void PreviewSizeChanged(const Windows::Foundation::IInspectable&, const Windows::UI::Xaml::SizeChangedEventArgs&);
        void RatioChanged(const Windows::Foundation::IInspectable& sender, const Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs& args);

        WINRT_PROPERTY(Model::GlobalAppSettings, GlobalSettings, nullptr);

    private:
        Sansterminal::WorkspaceLayout::Layout _layout;
        bool _initializing{ true };
        bool _updatingPreview{ false };
        Sansterminal::WorkspaceLayout::Geometry _previewGeometry;
        Windows::UI::Xaml::Input::Pointer _previewPointer{ nullptr };
        Windows::Foundation::Point _previewDragStart{};
        std::optional<Sansterminal::WorkspaceLayout::Pane> _previewDragSource;
        std::optional<Sansterminal::WorkspaceLayout::Pane> _previewDragTarget;
        Sansterminal::WorkspaceLayout::Side _previewDragSide{};
        bool _previewDragMoved{ false };
        Windows::UI::Xaml::Controls::Border _previewDropIndicator{ nullptr };
        void _SyncControls();
        void _RefreshPreview();
        void _Move();
        void _PreviewPanePressed(const Windows::Foundation::IInspectable& sender, const Windows::UI::Xaml::Input::PointerRoutedEventArgs& args);
        void _PreviewPointerMoved(const Windows::UI::Xaml::Input::PointerRoutedEventArgs& args);
        void _PreviewPointerReleased(const Windows::UI::Xaml::Input::PointerRoutedEventArgs& args);
        void _PreviewPointerCaptureLost(const Windows::UI::Xaml::Input::PointerRoutedEventArgs& args);
        void _UpdatePreviewDrag(const Windows::Foundation::Point point);
        void _CancelPreviewDrag();
    };
}

namespace winrt::Microsoft::Terminal::Settings::Editor::factory_implementation
{
    BASIC_FACTORY(WorkspaceLayoutPage);
}

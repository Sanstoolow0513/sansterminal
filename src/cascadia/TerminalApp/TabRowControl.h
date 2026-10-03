// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "winrt/Microsoft.UI.Xaml.Controls.h"

#include "TabRowControl.g.h"

namespace winrt::TerminalApp::implementation
{
    struct TabRowControl : TabRowControlT<TabRowControl>
    {
        TabRowControl();

        void SetVertical(bool vertical);

        void OnNewTabButtonClick(const Windows::Foundation::IInspectable& sender, const Microsoft::UI::Xaml::Controls::SplitButtonClickEventArgs& args);
        void OnNewTabButtonDrop(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::DragEventArgs& e);
        void OnNewTabButtonDragOver(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::DragEventArgs& e);

        // x:Bind helpers for the workspace label in horizontal and side tabs.
        winrt::Windows::UI::Xaml::Visibility SidebarVisibility(bool isVertical);
        winrt::Windows::UI::Xaml::Visibility HorizontalWorkspaceNameVisibility(bool isVertical, winrt::hstring name);

        til::property_changed_event PropertyChanged;
        WINRT_OBSERVABLE_PROPERTY(bool, ShowElevationShield, PropertyChanged.raise, false);
        WINRT_OBSERVABLE_PROPERTY(bool, ShowWorkspacesButton, PropertyChanged.raise, true);
        WINRT_OBSERVABLE_PROPERTY(winrt::hstring, WorkspaceName, PropertyChanged.raise, L"");
        WINRT_OBSERVABLE_PROPERTY(winrt::hstring, SidebarWorkspaceLabel, PropertyChanged.raise, L"");
        WINRT_OBSERVABLE_PROPERTY(bool, IsVertical, PropertyChanged.raise, false);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(TabRowControl);
}

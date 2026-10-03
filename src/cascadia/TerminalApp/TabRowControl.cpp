// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "TabRowControl.h"

#include "TabRowControl.g.cpp"

using namespace winrt::Windows::ApplicationModel::DataTransfer;

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Windows::UI::Text;

namespace winrt
{
    namespace MUX = Microsoft::UI::Xaml;
    namespace WUX = Windows::UI::Xaml;
}

namespace winrt::TerminalApp::implementation
{
    TabRowControl::TabRowControl()
    {
        InitializeComponent();
    }

    // Method Description:
    // - Switches the tab strip into vertical ("side tabs") mode, used when
    //   "tabPosition": "left" is active. Merges the vertical style resources
    //   and applies the vertical TabView template before layout.
    void TabRowControl::SetVertical(bool vertical)
    {
        if (!vertical)
        {
            return;
        }

        WUX::ResourceDictionary verticalResources{};
        verticalResources.Source(winrt::Windows::Foundation::Uri{ L"ms-appx:///TerminalApp/VerticalTabViewResources.xaml" });
        Resources().MergedDictionaries().Append(verticalResources);

        // Keep the lookup scoped to the dictionary that defines the style.
        TabView().Style(verticalResources.Lookup(winrt::box_value(L"VerticalTabViewStyle")).as<WUX::Style>());
        TabView().VerticalAlignment(WUX::VerticalAlignment::Stretch);
        VerticalContentAlignment(WUX::VerticalAlignment::Stretch);

        // The sidebar header is a two-row toolbar above the session list:
        //   row 0: [page-owned sidebar toggle] ... [new tab]
        //   row 1: [workspace navigation, full width]
        // TerminalPage floats the sidebar toggle over the leading edge of
        // row 0 (SideTabDock), so that space stays empty here.
        IsVertical(true);

        static constexpr auto toolbarRowHeight = 52.0;
        static constexpr auto sidebarToggleSlot = 52.0;

        const auto header = HeaderChrome();
        header.RowDefinitions().GetAt(0).Height(WUX::GridLengthHelper::FromPixels(toolbarRowHeight));
        HeaderChromeFillColumn().Width(WUX::GridLengthHelper::FromValueAndType(1, WUX::GridUnitType::Star));

        ElevationShieldIcon().Margin(WUX::ThicknessHelper::FromLengths(sidebarToggleSlot, 0, 0, 0));
        ElevationShieldIcon().VerticalAlignment(WUX::VerticalAlignment::Center);

        uint32_t index = 0;
        if (FooterChrome().Children().IndexOf(NewTabButtonHost(), index))
        {
            FooterChrome().Children().RemoveAt(index);
            header.Children().Append(NewTabButtonHost());
        }
        WUX::Controls::Grid::SetColumn(NewTabButtonHost(), 2);
        NewTabButtonHost().Margin(WUX::ThicknessHelper::FromLengths(0, 0, 8, 0));
        NewTabButtonHost().VerticalAlignment(WUX::VerticalAlignment::Center);
        TabStripBottomBorder().Visibility(WUX::Visibility::Collapsed);
        NewTabButton().Height(32);
        NewTabButton().FontSize(16);
        NewTabButton().Margin(WUX::ThicknessHelper::FromLengths(0, 0, 0, 0));
        FooterChrome().Visibility(WUX::Visibility::Collapsed);

        const auto workspaceButton = WorkspaceHomeButton();
        WUX::Controls::Grid::SetRow(workspaceButton, 1);
        WUX::Controls::Grid::SetColumn(workspaceButton, 0);
        WUX::Controls::Grid::SetColumnSpan(workspaceButton, 3);
        workspaceButton.HorizontalAlignment(WUX::HorizontalAlignment::Stretch);
        workspaceButton.HorizontalContentAlignment(WUX::HorizontalAlignment::Stretch);
        workspaceButton.VerticalAlignment(WUX::VerticalAlignment::Center);
        workspaceButton.Height(40);
        workspaceButton.Margin(WUX::ThicknessHelper::FromLengths(8, 0, 8, 4));
        workspaceButton.Padding(WUX::ThicknessHelper::FromLengths(12, 0, 10, 0));
        workspaceButton.CornerRadius(WUX::CornerRadius{ 8, 8, 8, 8 });
        WorkspaceHomeIcon().FontSize(14);
        SidebarWorkspaceLabelText().FontSize(13);
    }

    winrt::Windows::UI::Xaml::Visibility TabRowControl::SidebarVisibility(bool isVertical)
    {
        return isVertical ? winrt::Windows::UI::Xaml::Visibility::Visible :
                            winrt::Windows::UI::Xaml::Visibility::Collapsed;
    }

    winrt::Windows::UI::Xaml::Visibility TabRowControl::HorizontalWorkspaceNameVisibility(bool isVertical, winrt::hstring name)
    {
        if (isVertical || name.empty())
        {
            return winrt::Windows::UI::Xaml::Visibility::Collapsed;
        }
        return winrt::Windows::UI::Xaml::Visibility::Visible;
    }

    // Method Description:
    // - Bound in the Xaml editor to the [+] button.
    // Arguments:
    // <unused>
    void TabRowControl::OnNewTabButtonClick(const IInspectable&, const Controls::SplitButtonClickEventArgs&)
    {
    }

    // Method Description:
    // - Bound in Drag&Drop of the Xaml editor to the [+] button.
    // Arguments:
    // <unused>
    void TabRowControl::OnNewTabButtonDrop(const IInspectable&, const winrt::Windows::UI::Xaml::DragEventArgs&)
    {
    }

    // Method Description:
    // - Bound in Drag-over of the Xaml editor to the [+] button.
    // Allows drop of 'StorageItems' which will be used as StartingDirectory
    // Arguments:
    //  - <unused>
    //  - e: DragEventArgs which hold the items
    void TabRowControl::OnNewTabButtonDragOver(const IInspectable&, const winrt::Windows::UI::Xaml::DragEventArgs& e)
    {
        // We can only handle drag/dropping StorageItems (files).
        // If the format on the clipboard is anything else, returning
        // early here will prevent the drag/drop from doing anything.
        if (!e.DataView().Contains(StandardDataFormats::StorageItems()))
        {
            return;
        }

        // Make sure to set the AcceptedOperation, so that we can later receive the path in the Drop event
        e.AcceptedOperation(DataPackageOperation::Copy);

        const auto modifiers = static_cast<uint32_t>(e.Modifiers());
        if (WI_IsFlagSet(modifiers, static_cast<uint32_t>(DragDrop::DragDropModifiers::Alt)))
        {
            e.DragUIOverride().Caption(RS_(L"DropPathTabSplit/Text"));
        }
        else
        {
            e.DragUIOverride().Caption(RS_(L"DropPathTabRun/Text"));
        }

        // Sets if the caption is visible
        e.DragUIOverride().IsCaptionVisible(true);
        // Sets if the dragged content is visible
        e.DragUIOverride().IsContentVisible(false);
        // Sets if the glyph is visible
        e.DragUIOverride().IsGlyphVisible(false);
    }
}

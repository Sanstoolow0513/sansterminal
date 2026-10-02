// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"

#include "../TerminalApp/TerminalPage.h"
#include "../TerminalApp/TerminalWindow.h"
#include "../TerminalApp/SettingsLoadEventArgs.h"
#include "../TerminalApp/MinMaxCloseControl.h"
#include "../TerminalApp/TabRowControl.h"
#include "../TerminalApp/ShortcutActionDispatch.h"
#include "../TerminalApp/Tab.h"
#include "../TerminalApp/CommandPalette.h"
#include "../TerminalApp/CommandPaletteItems.h"
#include "../TerminalApp/ContentManager.h"
#include "../TerminalApp/TerminalSettingsCache.h"
#include "CppWinrtTailored.h"
#include <fstream>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Automation.Provider.h>

using namespace Microsoft::Console;
using namespace TerminalApp;
using namespace winrt::TerminalApp;
using namespace winrt::Microsoft::Terminal::Settings::Model;

using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;

using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::System;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Text;

namespace winrt
{
    namespace MUX = Microsoft::UI::Xaml;
    namespace WUX = Windows::UI::Xaml;
    using IInspectable = Windows::Foundation::IInspectable;
}

namespace TerminalAppLocalTests
{
    struct WorkspaceDialogPresenter : winrt::implements<WorkspaceDialogPresenter, winrt::TerminalApp::IDialogPresenter>
    {
        ContentDialogResult result{ ContentDialogResult::None };
        uint32_t calls{ 0 };

        winrt::Windows::Foundation::IAsyncOperation<ContentDialogResult> ShowDialog(const ContentDialog& dialog)
        {
            ++calls;
            VERIFY_ARE_EQUAL(ContentDialogButton::Close, dialog.DefaultButton());
            VERIFY_IS_FALSE(winrt::unbox_value<winrt::hstring>(dialog.Content()).empty());
            co_return result;
        }
    };

    // Settings navigation uses the same Tab/Pane interfaces as other content.
    // Keep this test independent of the settings application's resource setup.
    struct WorkspaceSettingsContent : winrt::implements<WorkspaceSettingsContent, IPaneContent>, winrt::TerminalApp::implementation::BasicPaneEvents
    {
        Grid root{};
        FocusState focusState{ FocusState::Unfocused };
        FrameworkElement GetRoot() { return root; }
        void UpdateSettings(const CascadiaSettings&, const WindowSettings&) {}
        winrt::Windows::Foundation::Size MinimumSize() { return { 1, 1 }; }
        void Focus(FocusState state) { focusState = state; }
        void Close() {}
        INewContentArgs GetNewTerminalArgs(BuildStartupKind) const { return BaseContentArgs{ L"settings" }; }
        winrt::hstring Title() { return L"Settings"; }
        uint64_t TaskbarState() { return 0; }
        uint64_t TaskbarProgress() { return 0; }
        bool ReadOnly() { return false; }
        winrt::hstring Icon() const { return L"\xE713"; }
        winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color> TabColor() const noexcept { return nullptr; }
        Media::Brush BackgroundBrush() { return nullptr; }
    };

    // TODO:microsoft/terminal#3838:
    // Unfortunately, these tests _WILL NOT_ work in our CI. We're waiting for
    // an updated TAEF that will let us install framework packages when the test
    // package is deployed. Until then, these tests won't deploy in CI.

    class TabTests
    {
        // For this set of tests, we need to activate some XAML content. For
        // release builds, the application runs as a centennial application,
        // which lets us run full trust, and means that we need to use XAML
        // Islands to host our UI. However, in these tests, we don't really need
        // to run full trust - we just need to get some UI elements created. So
        // we can just rely on the normal UWP activation to create us.
        //
        // IMPORTANTLY! When tests need to make XAML objects, or do XAML things,
        // make sure to use RunOnUIThread. This helper will dispatch a lambda to
        // be run on the UI thread.

        BEGIN_TEST_CLASS(TabTests)
            TEST_CLASS_PROPERTY(L"RunAs", L"UAP")
            TEST_CLASS_PROPERTY(L"UAP:AppXManifest", L"TestHostAppXManifest.xml")
        END_TEST_CLASS()

        // These four tests act as canary tests. If one of them fails, then they
        // can help you identify if something much lower in the stack has
        // failed.
        TEST_METHOD(EnsureTestsActivate);
        TEST_METHOD(TryCreateConnectionType);
        TEST_METHOD(TryCreateXamlObjects);

        TEST_METHOD(TryInitializePage);

        TEST_METHOD(CreateSimpleTerminalXamlType);
        TEST_METHOD(CreateTerminalMuxXamlType);
        TEST_METHOD(VerticalTabViewLayout);
        TEST_METHOD(SideTabsPageLayout);
        TEST_METHOD(WorkspaceNavigationLifecycle);
        TEST_METHOD(WorkspaceExplorerContext);
        TEST_METHOD(EmptyWorkspaceWindowClose);
        TEST_METHOD(WorkspaceBulkClose);
        TEST_METHOD(WorkspaceLaunchArguments);
        TEST_METHOD(TopTabWorkspaceNavigation);
        TEST_METHOD(WorkspaceTabActivation);
        TEST_METHOD(WorkspaceSurfaceDimensions);
        TEST_METHOD(WorkspaceTabSwitcherModes);
        TEST_METHOD(WorkspaceSidebarTabColors);
        TEST_METHOD(WorkspaceResizeCallbacks);
        TEST_METHOD(WorkspaceLayoutTabOrder);
        TEST_METHOD(WorkspaceLayoutReplayOwnership);
        TEST_METHOD(WorkspaceMoveTabNeighbors);
        TEST_METHOD(WorkspaceHeaderRestorationAndThemeReload);
        TEST_METHOD(WorkspaceCloseSelectedMruTab);
        TEST_METHOD(WorkspaceCompactViewRestoration);
        TEST_METHOD(WorkspacePreviewFocusOnClose);
        TEST_METHOD(WorkspacePreviewThemeChanges);
        TEST_METHOD(WorkspaceNavigationCloseButtonHover);

        TEST_METHOD(CreateTerminalPage);

        TEST_METHOD(TryDuplicateBadTab);
        TEST_METHOD(TryDuplicateBadPane);

        TEST_METHOD(TryZoomPane);
        TEST_METHOD(MoveFocusFromZoomedPane);
        TEST_METHOD(CloseZoomedPane);

        TEST_METHOD(SwapPanes);

        TEST_METHOD(NextMRUTab);
        TEST_METHOD(VerifyCommandPaletteTabSwitcherOrder);

        TEST_METHOD(TestWindowRenameSuccessful);
        TEST_METHOD(TestWindowRenameFailure);

        TEST_METHOD(TestPreviewCommitScheme);
        TEST_METHOD(TestPreviewDismissScheme);
        TEST_METHOD(TestPreviewSchemeWhilePreviewing);

        TEST_METHOD(TestClampSwitchToTab);

        TEST_CLASS_SETUP(ClassSetup)
        {
            return true;
        }

        TEST_METHOD_CLEANUP(MethodCleanup)
        {
            return true;
        }

    private:
        void _initializeTerminalPage(winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage>& page,
                                     CascadiaSettings initialSettings);
        winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> _commonSetup(TabPosition position = TabPosition::Top);
        winrt::com_ptr<winrt::TerminalApp::implementation::WindowProperties> _windowProperties;
        winrt::com_ptr<winrt::TerminalApp::implementation::ContentManager> _contentManager;
    };

    template<typename TFunction>
    void TestOnUIThread(const TFunction& function)
    {
        const auto result = RunOnUIThread([&]() {
            try
            {
                function();
            }
            catch (const winrt::hresult_error& error)
            {
                Log::Comment(NoThrowString().Format(L"WinRT error 0x%08X: %s", static_cast<HRESULT>(error.code()), error.message().c_str()));
                throw;
            }
        });
        VERIFY_SUCCEEDED(result);
    }

    void TabTests::EnsureTestsActivate()
    {
        // This test was originally used to ensure that XAML Islands was
        // initialized correctly. Now, it's used to ensure that the tests
        // actually deployed and activated. This test _should_ always pass.
        VERIFY_IS_TRUE(true);
    }

    void TabTests::TryCreateConnectionType()
    {
        // Verify we can create a WinRT type we authored
        // Just creating it is enough to know that everything is working.
        winrt::Microsoft::Terminal::TerminalConnection::EchoConnection conn{};
        VERIFY_IS_NOT_NULL(conn);
    }

    void TabTests::TryCreateXamlObjects()
    {
        auto result = RunOnUIThread([]() {
            VERIFY_IS_TRUE(true, L"Congrats! We're running on the UI thread!");

            auto v = winrt::Windows::ApplicationModel::Core::CoreApplication::GetCurrentView();
            VERIFY_IS_NOT_NULL(v, L"Ensure we have a current view");
            // Verify we can create a some XAML objects
            // Just creating all of them is enough to know that everything is working.
            winrt::Windows::UI::Xaml::Controls::UserControl controlRoot;
            VERIFY_IS_NOT_NULL(controlRoot, L"Try making a UserControl");
            winrt::Windows::UI::Xaml::Controls::Grid root;
            VERIFY_IS_NOT_NULL(root, L"Try making a Grid");
            winrt::Windows::UI::Xaml::Controls::SwapChainPanel swapChainPanel;
            VERIFY_IS_NOT_NULL(swapChainPanel, L"Try making a SwapChainPanel");
            winrt::Windows::UI::Xaml::Controls::Primitives::ScrollBar scrollBar;
            VERIFY_IS_NOT_NULL(scrollBar, L"Try making a ScrollBar");
        });

        VERIFY_SUCCEEDED(result);
    }

    void TabTests::CreateSimpleTerminalXamlType()
    {
        winrt::com_ptr<winrt::TerminalApp::implementation::MinMaxCloseControl> mmcc{ nullptr };

        auto result = RunOnUIThread([&mmcc]() {
            mmcc = winrt::make_self<winrt::TerminalApp::implementation::MinMaxCloseControl>();
            VERIFY_IS_NOT_NULL(mmcc);
        });
        VERIFY_SUCCEEDED(result);
    }

    void TabTests::CreateTerminalMuxXamlType()
    {
        winrt::com_ptr<winrt::TerminalApp::implementation::TabRowControl> tabRowControl{ nullptr };

        auto result = RunOnUIThread([&tabRowControl]() {
            tabRowControl = winrt::make_self<winrt::TerminalApp::implementation::TabRowControl>();
            VERIFY_IS_NOT_NULL(tabRowControl);
        });
        VERIFY_SUCCEEDED(result);
    }

    void TabTests::VerticalTabViewLayout()
    {
        winrt::TerminalApp::TabRowControl row{ nullptr };
        ::details::Event loaded;
        VERIFY_IS_TRUE(loaded.IsValid());

        TestOnUIThread([&]() {
            row = winrt::TerminalApp::TabRowControl{};
            Log::Comment(L"Load the vertical tab resources");
            row.SetVertical(true);
            row.Width(200);
            row.Height(320);
            const auto tabView = row.TabView();
            tabView.TabWidthMode(winrt::MUX::Controls::TabViewWidthMode::SizeToContent);
            for (auto i = 0; i < 20; ++i)
            {
                winrt::MUX::Controls::TabViewItem tab;
                tab.Header(winrt::box_value(L"A long terminal tab title that must fit in the sidebar"));
                tabView.TabItems().Append(tab);
            }
            tabView.SelectedIndex(0);

            // Creating a control alone does not exercise its template contract.
            // The old vertical template fails when WinUI applies/measures it.
            Log::Comment(L"Apply the vertical tab template");
            tabView.ApplyTemplate();
            row.Loaded([&](auto&&, auto&&) { loaded.Set(); });
            Window::Current().Content(row);
            Window::Current().Activate();
            row.UpdateLayout();
        });
        VERIFY_SUCCEEDED(Thread_Wait_For(loaded.m_handle, 10000));

        TestOnUIThread([&]() {
            row.UpdateLayout();
            const auto tabView = row.TabView();
            const auto first = tabView.ContainerFromIndex(0).as<winrt::MUX::Controls::TabViewItem>();
            const auto second = tabView.ContainerFromIndex(1).as<winrt::MUX::Controls::TabViewItem>();
            const auto firstPosition = first.TransformToVisual(row).TransformPoint({});
            const auto secondPosition = second.TransformToVisual(row).TransformPoint({});
            VERIFY_ARE_EQUAL(firstPosition.X, secondPosition.X);
            VERIFY_IS_TRUE(secondPosition.Y > firstPosition.Y);
            VERIFY_IS_TRUE(first.ActualWidth() > 100.0);
            VERIFY_IS_TRUE(first.ActualWidth() <= row.ActualWidth());
            VERIFY_IS_TRUE(first.ActualHeight() >= 36.0);

            // Sidebar rows mark the selection with a fill only; there is no
            // accent rail in the item template.
            const auto findNamed = [](DependencyObject root, winrt::hstring const& name) -> winrt::Windows::UI::Xaml::FrameworkElement {
                std::vector<DependencyObject> pending{ root };
                while (!pending.empty())
                {
                    const auto current = pending.back();
                    pending.pop_back();
                    if (const auto element = current.try_as<FrameworkElement>(); element && element.Name() == name)
                    {
                        return element;
                    }
                    for (auto i = 0; i < Media::VisualTreeHelper::GetChildrenCount(current); ++i)
                    {
                        pending.push_back(Media::VisualTreeHelper::GetChild(current, i));
                    }
                }
                return nullptr;
            };
            VERIFY_IS_NOT_NULL(findNamed(first, L"TabContainer"));
            VERIFY_IS_NULL(findNamed(first, L"SelectionRail"));
            VERIFY_IS_NULL(findNamed(second, L"SelectionRail"));

            // The sidebar header is a two-row toolbar above the session list:
            // the new tab button first, then the full-width workspace home button.
            VERIFY_IS_TRUE(row.IsVertical());
            const auto rowImpl = winrt::get_self<winrt::TerminalApp::implementation::TabRowControl>(row);
            const auto button = rowImpl->WorkspaceHomeButton();
            const auto buttonPosition = button.TransformToVisual(row).TransformPoint({});
            VERIFY_IS_TRUE(buttonPosition.Y + button.ActualHeight() <= firstPosition.Y);
            VERIFY_IS_TRUE(button.ActualWidth() > row.ActualWidth() * 0.7);
            const auto newTab = rowImpl->NewTabButton();
            const auto newTabPosition = newTab.TransformToVisual(row).TransformPoint({});
            VERIFY_IS_TRUE(newTabPosition.Y + newTab.ActualHeight() <= buttonPosition.Y);
            VERIFY_IS_TRUE(newTabPosition.X > row.ActualWidth() * 0.5);
            VERIFY_ARE_EQUAL(Visibility::Collapsed, rowImpl->FooterChrome().Visibility());

            // Inspect the actual scroll viewport, not just the declared style.
            ScrollViewer scroller{ nullptr };
            std::vector<DependencyObject> pending{ row };
            while (!pending.empty() && !scroller)
            {
                const auto current = pending.back();
                pending.pop_back();
                scroller = current.try_as<ScrollViewer>();
                for (auto i = 0; i < Media::VisualTreeHelper::GetChildrenCount(current); ++i)
                {
                    pending.push_back(Media::VisualTreeHelper::GetChild(current, i));
                }
            }
            VERIFY_IS_NOT_NULL(scroller);
            VERIFY_IS_TRUE(scroller.ScrollableHeight() > 0.0);
            VERIFY_ARE_EQUAL(0.0, scroller.ScrollableWidth());
            VERIFY_ARE_EQUAL(ScrollMode::Enabled, scroller.VerticalScrollMode());

            tabView.SelectedIndex(1);
            VERIFY_ARE_EQUAL(second, tabView.SelectedItem().as<winrt::MUX::Controls::TabViewItem>());
            tabView.TabItems().RemoveAt(1);
            row.UpdateLayout();
            VERIFY_ARE_EQUAL(19u, tabView.TabItems().Size());
            VERIFY_IS_NOT_NULL(tabView.SelectedItem());

            Window::Current().Content(nullptr);
            row = nullptr;
        });
    }

    void TabTests::CreateTerminalPage()
    {
        winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> page{ nullptr };

        _windowProperties = winrt::make_self<winrt::TerminalApp::implementation::WindowProperties>();
        winrt::TerminalApp::WindowProperties props = *_windowProperties;

        _contentManager = winrt::make_self<winrt::TerminalApp::implementation::ContentManager>();
        winrt::TerminalApp::ContentManager contentManager = *_contentManager;

        auto result = RunOnUIThread([&page, props, contentManager]() {
            page = winrt::make_self<winrt::TerminalApp::implementation::TerminalPage>(props, contentManager);
            VERIFY_IS_NOT_NULL(page);
        });
        VERIFY_SUCCEEDED(result);
    }

    // Method Description:
    // - This is a helper to set up a TerminalPage for a unittest. This method
    //   does a couple things:
    //   * Create()'s a TerminalPage with the given settings. Constructing a
    //     TerminalPage so that we can get at its implementation is wacky, so
    //     this helper will do it correctly for you, even if this doesn't make a
    //     ton of sense on the surface. This is also why you need to pass both a
    //     projection and a com_ptr to this method.
    //   * It will use the provided settings object to initialize the TerminalPage
    //   * It will add the TerminalPage to the test Application, so that we can
    //     get actual layout events. Much of the Terminal assumes there's a
    //     non-zero ActualSize to the Terminal window, and adding the Page to
    //     the Application will make it behave as expected.
    //   * It will wait for the TerminalPage to finish initialization before
    //     returning control to the caller. It does this by creating an event and
    //     only setting the event when the TerminalPage raises its Initialized
    //     event, to signal that startup is complete. At this point, there will
    //     be one tab with the default profile in the page.
    //   * It will also ensure that the first tab is focused, since that happens
    //     asynchronously in the application typically.
    // Arguments:
    // - page: a TerminalPage implementation ptr that will receive the new TerminalPage instance
    // - initialSettings: a CascadiaSettings to initialize the TerminalPage with.
    // Return Value:
    // - <none>
    void TabTests::_initializeTerminalPage(winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage>& page,
                                           CascadiaSettings initialSettings)
    {
        // This is super wacky, but we can't just initialize the
        // com_ptr<impl::TerminalPage> in the lambda and assign it back out of
        // the lambda. We'll crash trying to get a weak_ref to the TerminalPage
        // during TerminalPage::Create() below.
        //
        // Instead, create the winrt object, then get a com_ptr to the
        // implementation _from_ the winrt object. This seems to work, even if
        // it's weird.
        winrt::TerminalApp::TerminalPage projectedPage{ nullptr };

        _windowProperties = winrt::make_self<winrt::TerminalApp::implementation::WindowProperties>();
        winrt::TerminalApp::WindowProperties props = *_windowProperties;
        _contentManager = winrt::make_self<winrt::TerminalApp::implementation::ContentManager>();
        winrt::TerminalApp::ContentManager contentManager = *_contentManager;
        Log::Comment(NoThrowString().Format(L"Construct the TerminalPage"));
        auto result = RunOnUIThread([&projectedPage, &page, initialSettings, props, contentManager]() {
            projectedPage = winrt::TerminalApp::TerminalPage(props, contentManager);
            page.copy_from(winrt::get_self<winrt::TerminalApp::implementation::TerminalPage>(projectedPage));
            page->SetSettings(initialSettings, false);
        });
        VERIFY_SUCCEEDED(result);

        VERIFY_IS_NOT_NULL(page);
        VERIFY_IS_NOT_NULL(page->_settings);

        ::details::Event waitForInitEvent;
        if (!waitForInitEvent.IsValid())
        {
            VERIFY_SUCCEEDED(HRESULT_FROM_WIN32(::GetLastError()));
        }
        page->Initialized([&waitForInitEvent](auto&&, auto&&) {
            waitForInitEvent.Set();
        });

        Log::Comment(L"Create() the TerminalPage");

        result = RunOnUIThread([&page]() {
            VERIFY_IS_NOT_NULL(page);
            VERIFY_IS_NOT_NULL(page->_settings);
            page->Create();
            Log::Comment(L"Create()'d the page successfully");

            // Build a NewTab action, to make sure we start with one. The real
            // Terminal will always get one from AppCommandlineArgs.
            NewTerminalArgs newTerminalArgs{};
            NewTabArgs args{ newTerminalArgs };
            ActionAndArgs newTabAction{ ShortcutAction::NewTab, args };
            // push the arg onto the front
            page->_startupActions.push_back(std::move(newTabAction));
            Log::Comment(L"Added a single newTab action");

            auto app = ::winrt::Windows::UI::Xaml::Application::Current();

            winrt::TerminalApp::TerminalPage pp = *page;
            winrt::Windows::UI::Xaml::Window::Current().Content(pp);
            winrt::Windows::UI::Xaml::Window::Current().Activate();
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(L"Wait for the page to finish initializing...");
        VERIFY_SUCCEEDED(waitForInitEvent.Wait());
        Log::Comment(L"...Done");

        result = RunOnUIThread([&page]() {
            // In the real app, this isn't a problem, but doesn't happen
            // reliably in the unit tests.
            Log::Comment(L"Ensure we set the first tab as the selected one.");
            auto tab = page->_tabs.GetAt(0);
            auto tabImpl = page->_GetTabImpl(tab);
            page->_tabView.SelectedItem(tabImpl->TabViewItem());
            page->_UpdatedSelectedTab(tab);
        });
        VERIFY_SUCCEEDED(result);
    }

    void TabTests::TryInitializePage()
    {
        // This is a very simple test to prove we can create settings and a
        // TerminalPage and not only create them successfully, but also create a
        // tab using those settings successfully.

        // - - - IMPORTANT - - -
        // GH#14623: "closeOnExit": "never" is important for all test profiles. Without
        // it, the spawned process exits immediately in the UAP test environment,
        // and the default "automatic" close-on-exit behavior removes the
        // tab/pane asynchronously, racing against test assertions.
        static constexpr std::wstring_view settingsJson0{ LR"(
        {
            "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
            "profiles": [
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
                    "historySize": 1,
                    "closeOnExit": "never"
                },
                {
                    "name" : "profile1",
                    "guid": "{6239a42c-2222-49a3-80bd-e8fdd045185c}",
                    "historySize": 2,
                    "closeOnExit": "never"
                }
            ]
        })" };

        CascadiaSettings settings0{ settingsJson0, {} };
        VERIFY_IS_NOT_NULL(settings0);

        // This is super wacky, but we can't just initialize the
        // com_ptr<impl::TerminalPage> in the lambda and assign it back out of
        // the lambda. We'll crash trying to get a weak_ref to the TerminalPage
        // during TerminalPage::Create() below.
        //
        // Instead, create the winrt object, then get a com_ptr to the
        // implementation _from_ the winrt object. This seems to work, even if
        // it's weird.
        winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> page{ nullptr };
        _initializeTerminalPage(page, settings0);

        auto result = RunOnUIThread([&page]() {
            VERIFY_ARE_EQUAL(1u, page->_tabs.Size());
        });
        VERIFY_SUCCEEDED(result);
    }

    void TabTests::SideTabsPageLayout()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"Data:showTabsInTitlebar", L"{true, false}")
        END_TEST_METHOD_PROPERTIES();

        bool showTabsInTitlebar;
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"showTabsInTitlebar", showTabsInTitlebar));
        CascadiaSettings settings{ LR"({
            "tabPosition": "left",
            "alwaysShowTabs": true,
            "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
            "profiles": [{
                "name": "Side tabs test",
                "guid": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
                "closeOnExit": "never"
            }]
        })",
                                   {} };
        settings.WindowSettings(L"").ShowTabsInTitlebar(showTabsInTitlebar);
        winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> page;
        _initializeTerminalPage(page, settings);

        TestOnUIThread([&]() {
            // The UWP test host has no native titlebar. Verify detachment,
            // then host the same header in the page for layout assertions.
            if (showTabsInTitlebar)
            {
                VERIFY_IS_NULL(page->WorkspaceHeader().Parent());
                page->Root().Children().Append(page->WorkspaceHeader());
            }
            page->UpdateLayout();
            VERIFY_ARE_EQUAL(TabPosition::Left, page->_tabPosition);
            VERIFY_ARE_EQUAL(Visibility::Collapsed, page->_tabRow.Visibility());
            VERIFY_ARE_EQUAL(Visibility::Visible, page->WorkspaceNavigation().Visibility());
            VERIFY_ARE_EQUAL(Visibility::Visible, page->SideTabDock().Visibility());
            VERIFY_ARE_EQUAL(Visibility::Visible, page->SideTabOverlay().Visibility());
            VERIFY_ARE_EQUAL(200.0, page->SideTabColumn().ActualWidth());
            VERIFY_ARE_EQUAL(1u, page->_tabContent.Children().Size());
            VERIFY_ARE_EQUAL(1u, page->WorkspaceNavigation().RootNodes().Size());
            VERIFY_ARE_EQUAL(1u, page->WorkspaceNavigation().RootNodes().GetAt(0).Children().Size());

            const auto header = page->WorkspaceHeader();
            const auto dockPosition = page->SideTabDock().TransformToVisual(header).TransformPoint({});
            const auto newTabPosition = page->_newTabButton.TransformToVisual(header).TransformPoint({});
            const auto homePosition = page->_workspaceHomeButton.TransformToVisual(header).TransformPoint({});
            VERIFY_IS_TRUE(newTabPosition.X > homePosition.X);
            VERIFY_IS_TRUE(homePosition.X >= dockPosition.X + 40.0);
            VERIFY_IS_TRUE(std::abs(dockPosition.Y + 20 - newTabPosition.Y - page->_newTabButton.ActualHeight() / 2) < 2);
            VERIFY_ARE_EQUAL(40.0, header.ActualHeight());

            const auto contentHeight = page->_tabContent.ActualHeight();
            TextBlock notification{};
            notification.Height(40);
            page->InfoBarContainer().Children().Append(notification);
            page->UpdateLayout();
            VERIFY_ARE_EQUAL(contentHeight - 40, page->_tabContent.ActualHeight());
            page->InfoBarContainer().Children().Clear();

            page->_SideTabDividerDragStarted(nullptr, winrt::WUX::Controls::Primitives::DragStartedEventArgs{ 0, 0 });
            page->_SideTabDividerDragDelta(nullptr, winrt::WUX::Controls::Primitives::DragDeltaEventArgs{ 60, 0 });
            page->_SideTabDividerDragCompleted(nullptr, winrt::WUX::Controls::Primitives::DragCompletedEventArgs{ 60, 0, false });
            page->UpdateLayout();
            VERIFY_ARE_EQUAL(260.0, page->SideTabColumn().ActualWidth());
            page->_ShowSideTabOverlay(false);
            page->UpdateLayout();
            VERIFY_ARE_EQUAL(0.0, page->SideTabColumn().ActualWidth());
            VERIFY_ARE_EQUAL(Visibility::Visible, header.Visibility());
            VERIFY_ARE_EQUAL(L"\xE8A0", page->SideTabDockIcon().Glyph());
            page->_ShowSideTabOverlay(true);
            page->UpdateLayout();
            VERIFY_ARE_EQUAL(260.0, page->SideTabColumn().ActualWidth());
            VERIFY_ARE_EQUAL(contentHeight, page->_tabContent.ActualHeight());

            // The file explorer introduces a second boundary. Both handles
            // must remain at their own panel edges and resize independently.
            const auto workspace = page->_FindWorkspace(page->_activeWorkspaceId);
            workspace->root = winrt::Windows::Storage::ApplicationData::Current().TemporaryFolder().Path().c_str();
            workspace->currentDirectory = workspace->root;
            page->_UpdateWorkspaceFilesUI();
            page->UpdateLayout();
            const auto layout = page->SideTabLayout();
            VERIFY_ARE_EQUAL(260.0f, page->SideTabDivider().TransformToVisual(layout).TransformPoint({}).X);
            const auto filesWidth = page->WorkspaceFilesColumn().ActualWidth();
            VERIFY_IS_TRUE(filesWidth > 0);
            VERIFY_ARE_EQUAL(static_cast<float>(260.0 + filesWidth), page->WorkspaceFilesDivider().TransformToVisual(layout).TransformPoint({}).X);
            page->_SideTabDividerDragStarted(nullptr, winrt::WUX::Controls::Primitives::DragStartedEventArgs{ 0, 0 });
            page->_SideTabDividerDragDelta(nullptr, winrt::WUX::Controls::Primitives::DragDeltaEventArgs{ -20, 0 });
            page->UpdateLayout();
            VERIFY_ARE_EQUAL(240.0, page->SideTabColumn().ActualWidth());
            VERIFY_ARE_EQUAL(filesWidth, page->WorkspaceFilesColumn().ActualWidth());
            page->_WorkspaceFilesDividerDragDelta(nullptr, winrt::WUX::Controls::Primitives::DragDeltaEventArgs{ -20, 0 });
            page->UpdateLayout();
            VERIFY_ARE_EQUAL(240.0, page->SideTabColumn().ActualWidth());
            VERIFY_ARE_EQUAL(filesWidth - 20, page->WorkspaceFilesColumn().ActualWidth());
            page->_ResizeSideTabColumn(260);

            page->_ShowWorkspaceHub();
            VERIFY_ARE_EQUAL(Visibility::Visible, page->SideTabLayout().Visibility());
            VERIFY_ARE_EQUAL(260.0, page->WorkspaceHub().Margin().Left);
            page->_ShowWorkspaceContent();
            page->SetFocusMode(true);
            VERIFY_ARE_EQUAL(Visibility::Collapsed, header.Visibility());
            VERIFY_ARE_EQUAL(Visibility::Collapsed, page->SideTabOverlay().Visibility());
            page->SetFocusMode(false);
            VERIFY_ARE_EQUAL(Visibility::Visible, header.Visibility());
            page->SetFullscreen(true);
            page->SetShowTabsFullscreen(false);
            VERIFY_ARE_EQUAL(Visibility::Collapsed, header.Visibility());
            page->SetShowTabsFullscreen(true);
            VERIFY_ARE_EQUAL(Visibility::Visible, header.Visibility());
            page->SetFullscreen(false);
        });
    }

    void TabTests::WorkspaceNavigationLifecycle()
    {
        CascadiaSettings settings{ LR"({
            "tabPosition": "left", "showTabsInTitlebar": false,
            "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
            "profiles": [{ "name": "Workspace test", "guid": "{6239a42c-1111-49a3-80bd-e8fdd045185c}", "closeOnExit": "never" }]
        })",
                                   {} };
        winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> page;
        _initializeTerminalPage(page, settings);
        TestOnUIThread([&]() {
            const auto firstWorkspace = page->_activeWorkspaceId;
            const auto firstTab = page->_tabs.GetAt(0);
            const auto firstContent = firstTab.Content();
            const auto firstConnection = page->_GetTabImpl(firstTab)->GetActiveTerminalControl().Connection();
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{}));
            page->_SwitchWorkspace(L"navigation-test-B");
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{}));
            page->_SwitchWorkspace(L"navigation-test-C");
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{}));
            VERIFY_ARE_EQUAL(6u, page->_tabs.Size());
            VERIFY_ARE_EQUAL(3u, page->WorkspaceNavigation().RootNodes().Size());
            for (const auto& node : page->WorkspaceNavigation().RootNodes())
            {
                VERIFY_ARE_EQUAL(2u, node.Children().Size());
                VERIFY_IS_TRUE(node.IsExpanded());
            }
            uint32_t index{};
            VERIFY_IS_TRUE(page->_tabs.IndexOf(firstTab, index));
            page->_SelectTab(index);
            VERIFY_ARE_EQUAL(firstWorkspace, page->_activeWorkspaceId);
            VERIFY_IS_TRUE(firstTab.Content() == firstContent);
            VERIFY_IS_TRUE(page->_GetTabImpl(firstTab)->GetActiveTerminalControl().Connection() == firstConnection);
            VERIFY_IS_TRUE(page->_GetFocusedTab() == firstTab);
            VERIFY_ARE_EQUAL(6u, page->_tabs.Size());
            // The single global add command targets the activated workspace.
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{}));
            VERIFY_ARE_EQUAL(firstWorkspace, page->_WorkspaceForTab(page->_GetFocusedTab()));
            VERIFY_ARE_EQUAL(3u, page->_FindWorkspace(firstWorkspace)->navigationNode.Children().Size());
            page->_SwitchWorkspace(L"navigation-test-B", false);
            std::vector<winrt::TerminalApp::Tab> toClose;
            for (const auto& tab : page->_tabs)
            {
                if (page->_IsTabInActiveWorkspace(tab))
                {
                    toClose.push_back(tab);
                }
            }
            for (const auto& tab : toClose)
            {
                tab.Close();
            }
            VERIFY_ARE_EQUAL(L"navigation-test-B", page->_activeWorkspaceId);
            VERIFY_ARE_EQUAL(Visibility::Collapsed, page->WorkspaceHub().Visibility());
            VERIFY_ARE_EQUAL(0u, page->_tabContent.Children().Size());
            VERIFY_IS_NOT_NULL(page->_FindWorkspace(L"navigation-test-B"));
            const auto count = page->_tabs.Size();
            page->_OpenWorkspace(L"navigation-test-B");
            VERIFY_ARE_EQUAL(count, page->_tabs.Size());
            const auto close = page->_CloseWorkspace(L"navigation-test-B");
            close.GetResults();
            VERIFY_IS_NULL(page->_FindWorkspace(L"navigation-test-B"));
            VERIFY_ARE_EQUAL(2u, page->WorkspaceNavigation().RootNodes().Size());
            // Removing a recent entry must not terminate or forget live sessions.
            Button remove{};
            remove.Tag(winrt::box_value(winrt::hstring{ L"navigation-test-C" }));
            page->_WorkspaceHubDeleteClick(remove, RoutedEventArgs{});
            VERIFY_IS_NOT_NULL(page->_FindWorkspace(L"navigation-test-C"));
            VERIFY_ARE_EQUAL(count, page->_tabs.Size());

            // Cancellation preserves the background workspace and its sessions;
            // confirmation closes only that workspace, leaving focus in A.
            const auto presenter = winrt::make_self<WorkspaceDialogPresenter>();
            page->_dialogPresenter = winrt::make_weak(presenter.as<winrt::TerminalApp::IDialogPresenter>());
            const auto cancel = page->_CloseWorkspace(L"navigation-test-C");
            cancel.GetResults();
            VERIFY_ARE_EQUAL(1u, presenter->calls);
            VERIFY_IS_NOT_NULL(page->_FindWorkspace(L"navigation-test-C"));
            VERIFY_ARE_EQUAL(count, page->_tabs.Size());
            VERIFY_ARE_EQUAL(firstWorkspace, page->_activeWorkspaceId);
            presenter->result = ContentDialogResult::Primary;
            const auto confirm = page->_CloseWorkspace(L"navigation-test-C");
            confirm.GetResults();
            VERIFY_ARE_EQUAL(2u, presenter->calls);
            VERIFY_IS_NULL(page->_FindWorkspace(L"navigation-test-C"));
            VERIFY_ARE_EQUAL(count - 2, page->_tabs.Size());
            VERIFY_ARE_EQUAL(firstWorkspace, page->_activeWorkspaceId);
            const std::vector<winrt::TerminalApp::Tab> remaining{ page->_tabs.begin(), page->_tabs.end() };
            for (const auto& tab : remaining)
            {
                tab.Close();
            }
            VERIFY_ARE_EQUAL(0u, page->_tabs.Size());
            VERIFY_ARE_EQUAL(Visibility::Collapsed, page->WorkspaceHub().Visibility());
            VERIFY_IS_NOT_NULL(page->_FindWorkspace(page->_activeWorkspaceId));
            const auto closeLastWorkspace = page->_CloseWorkspace(firstWorkspace);
            closeLastWorkspace.GetResults();
            VERIFY_ARE_EQUAL(Visibility::Visible, page->WorkspaceHub().Visibility());
            VERIFY_ARE_EQUAL(0u, page->WorkspaceNavigation().RootNodes().Size());
        });
    }

    void TabTests::EmptyWorkspaceWindowClose()
    {
        const auto page = _commonSetup();
        TestOnUIThread([&]() {
            page->_settings.GlobalSettings().ConfirmOnClose(ConfirmOnClose::Automatic);
            page->_tabs.GetAt(0).Close();
            VERIFY_ARE_EQUAL(0u, page->_tabs.Size());
            VERIFY_IS_FALSE(page->_ShouldWarnOnClose());
            uint32_t closeRequests{};
            const auto token = page->CloseWindowRequested([&](auto&&, auto&&) { ++closeRequests; });
            const auto cleanup = wil::scope_exit([&]() { page->CloseWindowRequested(token); });
            page->CloseWindow();
            VERIFY_ARE_EQUAL(1u, closeRequests);

            const auto closeWorkspace = page->_CloseWorkspace(page->_activeWorkspaceId);
            closeWorkspace.GetResults();
            VERIFY_IS_TRUE(page->_workspaces.empty());
            VERIFY_ARE_EQUAL(Visibility::Visible, page->WorkspaceHub().Visibility());
            page->CloseWindow();
            VERIFY_ARE_EQUAL(2u, closeRequests);
            page->_settings.GlobalSettings().ConfirmOnClose(ConfirmOnClose::Always);
            VERIFY_IS_TRUE(page->_ShouldWarnOnClose());
        });
    }

    void TabTests::WorkspaceBulkClose()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"Data:sideTabs", L"{true, false}")
            TEST_METHOD_PROPERTY(L"Data:closeOthers", L"{true, false}")
        END_TEST_METHOD_PROPERTIES();

        bool sideTabs;
        bool closeOthers;
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"sideTabs", sideTabs));
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"closeOthers", closeOthers));
        CascadiaSettings settings{ LR"({
            "showTabsInTitlebar": false,
            "newTabPosition": "afterLastTab",
            "warning.confirmOnClose": "never",
            "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
            "profiles": [{ "name": "Bulk close test", "guid": "{6239a42c-1111-49a3-80bd-e8fdd045185c}", "closeOnExit": "never" }]
        })", {} };
        settings.WindowSettings(L"").TabPosition(sideTabs ? TabPosition::Left : TabPosition::Top);
        winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> page;
        _initializeTerminalPage(page, settings);
        TestOnUIThread([&]() {
            const auto workspaceA = page->_activeWorkspaceId;
            const auto first = page->_tabs.GetAt(0);
            page->_SwitchWorkspace(L"bulk-close-B");
            const auto background = page->_GetFocusedTab();
            const auto connection = page->_GetFocusedTabImpl()->GetActiveTerminalControl().Connection();
            page->_SwitchWorkspace(workspaceA, false);
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{}));
            const auto middle = page->_GetFocusedTab();
            page->_SwitchWorkspace(L"bulk-close-B", false);
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{}));
            const auto otherBackground = page->_GetFocusedTab();
            page->_SwitchWorkspace(workspaceA, false);
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{}));
            // Keep B active while the explicitly indexed action targets A.
            page->_SwitchWorkspace(L"bulk-close-B", false);
            uint32_t index{};
            const auto target = closeOthers ? middle : first;
            VERIFY_IS_TRUE(page->_tabs.IndexOf(target, index));
            if (closeOthers)
            {
                VERIFY_IS_TRUE(page->_actionDispatch->DoAction(ActionAndArgs{ ShortcutAction::CloseOtherTabs, CloseOtherTabsArgs{ index } }));
            }
            else
            {
                VERIFY_IS_TRUE(page->_actionDispatch->DoAction(ActionAndArgs{ ShortcutAction::CloseTabsAfter, CloseTabsAfterArgs{ index } }));
            }
            VERIFY_ARE_EQUAL(3u, page->_tabs.Size());
            VERIFY_IS_TRUE(page->_tabs.IndexOf(target, index));
            VERIFY_IS_TRUE(page->_tabs.IndexOf(background, index));
            VERIFY_IS_TRUE(page->_tabs.IndexOf(otherBackground, index));
            VERIFY_IS_TRUE(page->_GetTabImpl(background)->GetActiveTerminalControl().Connection() == connection);
            VERIFY_ARE_EQUAL(L"bulk-close-B", page->_activeWorkspaceId);
            VERIFY_IS_FALSE(page->_actionDispatch->DoAction(ActionAndArgs{ ShortcutAction::CloseOtherTabs, CloseOtherTabsArgs{ 99 } }));
            VERIFY_IS_FALSE(page->_actionDispatch->DoAction(ActionAndArgs{ ShortcutAction::CloseTabsAfter, CloseTabsAfterArgs{ 99 } }));
            VERIFY_ARE_EQUAL(3u, page->_tabs.Size());
        });
    }

    void TabTests::WorkspaceLaunchArguments()
    {
        const auto page = _commonSetup();
        TestOnUIThread([&]() {
            // Tab initialization normally replaces the control's settings with
            // cached profile settings after creating the connection. Keep this
            // profile out of the reload cache so we can inspect its launch settings.
            CascadiaSettings cacheSettings{ LR"({
                "defaultProfile": "{6239a42c-4444-49a3-80bd-e8fdd045185c}",
                "profiles": [{ "name": "Unused cache profile", "guid": "{6239a42c-4444-49a3-80bd-e8fdd045185c}" }]
            })", {} };
            page->_terminalSettingsCache->Reset(cacheSettings, cacheSettings.WindowSettings(L""));
            const auto appData = winrt::Windows::Storage::ApplicationData::Current();
            const auto folderA = appData.LocalFolder().Path();
            const auto folderB = appData.TemporaryFolder().Path();
            NewTerminalArgs args{};
            const ActionAndArgs action{ ShortcutAction::NewTab, NewTabArgs{ args } };
            for (const auto& folder : { folderA, folderB })
            {
                page->_SwitchWorkspace(folder, false);
                VERIFY_IS_TRUE(page->_actionDispatch->DoAction(action));
                VERIFY_IS_TRUE(args.StartingDirectory().empty());
                VERIFY_ARE_EQUAL(folder, page->_GetFocusedTabImpl()->GetActiveTerminalControl().Settings().StartingDirectory());
            }
            // Explicit directories still override the workspace, including reused actions.
            args.StartingDirectory(folderA);
            VERIFY_IS_TRUE(page->_actionDispatch->DoAction(action));
            VERIFY_ARE_EQUAL(folderA, page->_GetFocusedTabImpl()->GetActiveTerminalControl().Settings().StartingDirectory());
            VERIFY_ARE_EQUAL(folderA, args.StartingDirectory());
            // The default new-tab path has no content arguments.
            VERIFY_SUCCEEDED(page->_OpenNewTab(nullptr));
            VERIFY_ARE_EQUAL(folderB, page->_GetFocusedTabImpl()->GetActiveTerminalControl().Settings().StartingDirectory());
        });
    }

    void TabTests::TopTabWorkspaceNavigation()
    {
        const auto page = _commonSetup();
        TestOnUIThread([&]() {
            VERIFY_ARE_EQUAL(TabPosition::Top, page->_tabPosition);
            const auto untitled = page->_activeWorkspaceId;
            const auto terminal = page->_GetFocusedTab();
            const auto connection = page->_GetFocusedTabImpl()->GetActiveTerminalControl().Connection();
            page->_SwitchWorkspace(L"top-navigation-B");
            ApplicationState::SharedInstance().ForgetRecentWorkspace(L"top-navigation-B");
            page->_ShowWorkspaceHub();
            const auto row = winrt::get_self<winrt::TerminalApp::implementation::TabRowControl>(page->_tabRow);
            VERIFY_ARE_EQUAL(Visibility::Visible, row->WorkspaceSwitcher().Visibility());
            VERIFY_ARE_EQUAL(2u, row->WorkspaceSwitcherFlyout().Items().Size());
            const auto activate = [&](uint32_t index) {
                const auto workspace = row->WorkspaceSwitcherFlyout().Items().GetAt(index).as<MenuFlyoutSubItem>();
                const auto item = workspace.Items().GetAt(0).as<MenuFlyoutItem>();
                const Automation::Peers::MenuFlyoutItemAutomationPeer peer{ item };
                peer.GetPattern(Automation::Peers::PatternInterface::Invoke).as<Automation::Provider::IInvokeProvider>().Invoke();
            };
            activate(0);
            VERIFY_ARE_EQUAL(untitled, page->_activeWorkspaceId);
            VERIFY_IS_TRUE(page->_GetFocusedTab() == terminal);
            VERIFY_IS_TRUE(page->_GetFocusedTabImpl()->GetActiveTerminalControl().Connection() == connection);
            VERIFY_ARE_EQUAL(Visibility::Visible, terminal.TabViewItem().Visibility());
            VERIFY_ARE_EQUAL(Visibility::Collapsed, page->WorkspaceHub().Visibility());
            activate(1);
            VERIFY_ARE_EQUAL(L"top-navigation-B", page->_activeWorkspaceId);
            VERIFY_ARE_EQUAL(2u, page->_tabs.Size());

            const auto presenter = winrt::make_self<WorkspaceDialogPresenter>();
            page->_dialogPresenter = winrt::make_weak(presenter.as<winrt::TerminalApp::IDialogPresenter>());
            const auto close = [&]() {
                const auto workspace = row->WorkspaceSwitcherFlyout().Items().GetAt(1).as<MenuFlyoutSubItem>();
                const auto item = workspace.Items().GetAt(1).as<MenuFlyoutItem>();
                const Automation::Peers::MenuFlyoutItemAutomationPeer peer{ item };
                peer.GetPattern(Automation::Peers::PatternInterface::Invoke).as<Automation::Provider::IInvokeProvider>().Invoke();
            };
            close();
            VERIFY_ARE_EQUAL(1u, presenter->calls);
            VERIFY_IS_NOT_NULL(page->_FindWorkspace(L"top-navigation-B"));
            presenter->result = ContentDialogResult::Primary;
            close();
            VERIFY_ARE_EQUAL(2u, presenter->calls);
            VERIFY_IS_NULL(page->_FindWorkspace(L"top-navigation-B"));
            VERIFY_ARE_EQUAL(untitled, page->_activeWorkspaceId);
            VERIFY_IS_TRUE(page->_GetFocusedTab() == terminal);
            VERIFY_ARE_EQUAL(1u, row->WorkspaceSwitcherFlyout().Items().Size());

            terminal.Close();
            const auto last = row->WorkspaceSwitcherFlyout().Items().GetAt(0).as<MenuFlyoutSubItem>().Items().GetAt(1).as<MenuFlyoutItem>();
            const Automation::Peers::MenuFlyoutItemAutomationPeer peer{ last };
            peer.GetPattern(Automation::Peers::PatternInterface::Invoke).as<Automation::Provider::IInvokeProvider>().Invoke();
            VERIFY_ARE_EQUAL(2u, presenter->calls);
            VERIFY_IS_TRUE(page->_workspaces.empty());
            VERIFY_ARE_EQUAL(Visibility::Visible, page->WorkspaceHub().Visibility());
        });
    }

    void TabTests::WorkspaceTabActivation()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"Data:sideTabs", L"{true, false}")
        END_TEST_METHOD_PROPERTIES();
        bool sideTabs;
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"sideTabs", sideTabs));
        const auto page = _commonSetup(sideTabs ? TabPosition::Left : TabPosition::Top);
        TestOnUIThread([&]() {
            const auto workspaceA = page->_activeWorkspaceId;
            const auto terminalA = page->_GetFocusedTab();
            const auto settingsContent = winrt::make_self<WorkspaceSettingsContent>();
            page->_settingsTab = page->_CreateNewTabFromPane(std::make_shared<Pane>(*settingsContent));
            const auto settings = page->_settingsTab;
            page->_SwitchWorkspace(L"activation-test-B");
            const auto terminalB = page->_GetFocusedTab();

            page->OpenSettingsUI();
            VERIFY_ARE_EQUAL(workspaceA, page->_activeWorkspaceId);
            VERIFY_IS_TRUE(page->_GetFocusedTab() == settings);
            VERIFY_IS_TRUE(page->_tabContent.Children().GetAt(0) == settings.Content());
            VERIFY_ARE_EQUAL(Visibility::Collapsed, terminalB.TabViewItem().Visibility());
            page->ProcessStartupActions({}, {}, {});
            VERIFY_ARE_EQUAL(FocusState::Programmatic, settingsContent->focusState);

            // TabView selection and the asynchronous pane-move focus path
            // must also activate the selected tab's owning workspace.
            page->_tabView.SelectedItem(terminalB.TabViewItem());
            if (sideTabs)
            {
                // The hidden TabView does not realize items or raise its
                // selection event. Exercise the event handler explicitly.
                page->_OnTabSelectionChanged(page->_tabView, nullptr);
            }
            VERIFY_ARE_EQUAL(L"activation-test-B", page->_activeWorkspaceId);
            VERIFY_IS_TRUE(page->_tabContent.Children().GetAt(0) == terminalB.Content());
            page->_SetFocusedTab(terminalA);
            VERIFY_ARE_EQUAL(workspaceA, page->_activeWorkspaceId);
            VERIFY_IS_TRUE(page->_GetFocusedTab() == terminalA);
            VERIFY_IS_TRUE(page->_tabContent.Children().GetAt(0) == terminalA.Content());

            page->FocusTab(terminalB);
            MovePaneArgs args{ page->_GetTabIndex(terminalA).value(), L"" };
            VERIFY_IS_TRUE(page->_MovePane(args));
            VERIFY_ARE_EQUAL(workspaceA, page->_activeWorkspaceId);
            VERIFY_IS_TRUE(page->_GetFocusedTab() == terminalA);
            VERIFY_IS_TRUE(page->_tabContent.Children().GetAt(0) == terminalA.Content());
            VERIFY_ARE_EQUAL(2, page->_GetTabImpl(terminalA)->GetLeafPaneCount());
        });
    }

    void TabTests::WorkspaceSurfaceDimensions()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"Data:sideTabs", L"{true, false}")
        END_TEST_METHOD_PROPERTIES();
        bool sideTabs;
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"sideTabs", sideTabs));
        const auto page = _commonSetup(sideTabs ? TabPosition::Left : TabPosition::Top);
        TestOnUIThread([&]() {
            GUID unique{};
            VERIFY_SUCCEEDED(CoCreateGuid(&unique));
            const auto root = std::filesystem::path{ winrt::Windows::Storage::ApplicationData::Current().TemporaryFolder().Path().c_str() } / winrt::to_hstring(unique).c_str();
            std::filesystem::create_directories(root);
            const auto cleanup = wil::scope_exit([&]() noexcept {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            });
            const auto file = root / L"dimensions.txt";
            std::ofstream{ file } << "workspace dimensions";
            const winrt::hstring id{ root.native() };
            page->Width(1400);
            page->Height(850);
            page->UpdateLayout();
            page->_SwitchWorkspace(id);
            page->_OpenWorkspaceDocument(file, true);
            page->UpdateLayout();
            const auto verifySize = [&]() {
                const auto size = page->GetWindowLayout().InitialSize().Value();
                VERIFY_ARE_EQUAL(static_cast<float>(page->SideTabLayout().ActualWidth()), size.Width);
                VERIFY_ARE_EQUAL(static_cast<float>(page->SideTabLayout().ActualHeight()), size.Height);
                VERIFY_IS_TRUE(size.Width > page->_tabContent.ActualWidth());
            };
            verifySize();
            page->_FindWorkspace(id)->explorerWidth = 700;
            page->_ResizeWorkspaceFilesColumn();
            page->UpdateLayout();
            VERIFY_ARE_EQUAL(700.0, page->WorkspaceFilesColumn().ActualWidth());

            page->Width(600);
            page->UpdateLayout();
            VERIFY_IS_TRUE(page->WorkspaceContentArea().ActualWidth() > 0);
            VERIFY_IS_TRUE(page->SideTabColumn().ActualWidth() + page->WorkspaceFilesColumn().ActualWidth() < 600);
            page->_OpenWorkspaceDocument(file, true);
            page->UpdateLayout();
            VERIFY_ARE_EQUAL(0.0, page->_tabContent.ActualWidth());
            verifySize();

            page->_SwitchWorkspace(L"dimensions-test-other", false);
            // An inactive workspace can retain its width from a larger window.
            page->_FindWorkspace(id)->explorerWidth = 900;
            page->_SwitchWorkspace(id, false);
            page->UpdateLayout();
            VERIFY_IS_TRUE(page->WorkspaceContentArea().ActualWidth() > 0);
            VERIFY_IS_TRUE(page->_FindWorkspace(id)->explorerWidth < 900);
            verifySize();

            if (sideTabs)
            {
                page->_ResizeSideTabColumn(280);
                page->UpdateLayout();
                VERIFY_IS_TRUE(page->WorkspaceContentArea().ActualWidth() > 0);
            }
        });
    }

    void TabTests::WorkspaceTabSwitcherModes()
    {
        const auto page = _commonSetup();
        TestOnUIThread([&]() {
            page->_settings.WindowSettingsDefaults().TabSwitcherMode(TabSwitcherMode::MostRecentlyUsed);
            const auto first = page->_GetFocusedTab();
            VERIFY_SUCCEEDED(page->_OpenNewTab(nullptr));
            const auto second = page->_GetFocusedTab();
            VERIFY_SUCCEEDED(page->_OpenNewTab(nullptr));
            const auto third = page->_GetFocusedTab();
            page->FocusTab(first);
            page->_SwitchWorkspace(L"switcher-test-B");
            page->FocusTab(second);

            page->_SelectNextTab(true, nullptr);
            const auto palette = winrt::get_self<winrt::TerminalApp::implementation::CommandPalette>(page->CommandPaletteElement());
            VERIFY_ARE_EQUAL(TabSwitcherMode::MostRecentlyUsed, palette->_tabSwitcherMode);
            VERIFY_ARE_EQUAL(3u, palette->_tabActions.Size());
            VERIFY_ARE_EQUAL(3u, palette->_mruTabActions.Size());
            const auto tabAt = [](const auto& actions, uint32_t index) {
                return winrt::get_self<winrt::TerminalApp::implementation::TabPaletteItem>(actions.GetAt(index).Item().template as<winrt::TerminalApp::TabPaletteItem>())->Tab();
            };
            VERIFY_IS_TRUE(tabAt(palette->_mruTabActions, 0) == second);
            VERIFY_IS_TRUE(tabAt(palette->_mruTabActions, 1) == first);
            VERIFY_IS_TRUE(tabAt(palette->_mruTabActions, 2) == third);

            page->FocusTab(second);
            page->_SelectNextTab(true, winrt::box_value(TabSwitcherMode::InOrder).as<winrt::Windows::Foundation::IReference<TabSwitcherMode>>());
            VERIFY_ARE_EQUAL(TabSwitcherMode::InOrder, palette->_tabSwitcherMode);
            VERIFY_ARE_EQUAL(1u, palette->_switcherStartIdx);
            VERIFY_IS_TRUE(tabAt(palette->_tabActions, 0) == first);
            VERIFY_IS_TRUE(tabAt(palette->_tabActions, 1) == second);
            VERIFY_IS_TRUE(tabAt(palette->_tabActions, 2) == third);

            page->CommandPaletteElement().Visibility(Visibility::Collapsed);
            page->_settings.WindowSettingsDefaults().TabSwitcherMode(TabSwitcherMode::Disabled);
            page->FocusTab(third);
            page->_SelectNextTab(true, nullptr);
            VERIFY_IS_TRUE(page->_GetFocusedTab() == first);
            page->_SelectNextTab(false, nullptr);
            VERIFY_IS_TRUE(page->_GetFocusedTab() == third);
        });
    }

    void TabTests::WorkspaceSidebarTabColors()
    {
        CascadiaSettings settings{ LR"({
            "tabPosition": "left",
            "showTabsInTitlebar": false,
            "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
            "profiles": [{ "name": "Color test", "guid": "{6239a42c-1111-49a3-80bd-e8fdd045185c}", "tabColor": "#ffffff", "closeOnExit": "never" }]
        })",
                                   {} };
        winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> page;
        _initializeTerminalPage(page, settings);
        TestOnUIThread([&]() {
            const auto tab = page->_GetFocusedTabImpl();
            page->UpdateLayout();
            VERIFY_ARE_EQUAL(TabPosition::Left, page->_tabPosition);
            VERIFY_ARE_EQUAL(1u, page->_workspaceNavigationEntries.size());
            if (page->_workspaceNavigationEntries.empty())
            {
                return;
            }
            const auto row = page->_workspaceNavigationEntries.front().node.Content().as<Grid>();
            const auto title = tab->_headerControl.FindName(L"HeaderTextBlock").as<TextBlock>();
            VERIFY_ARE_EQUAL(winrt::Windows::UI::Colors::White(), row.Background().as<Media::SolidColorBrush>().Color());
            VERIFY_ARE_EQUAL(winrt::Windows::UI::Colors::Black(), title.Foreground().as<Media::SolidColorBrush>().Color());
            const auto close = row.Children().GetAt(2).as<Button>();
            VERIFY_ARE_EQUAL(winrt::Windows::UI::Colors::Black(), close.Foreground().as<Media::SolidColorBrush>().Color());
            tab->SetRuntimeTabColor(winrt::Windows::UI::Colors::Black());
            VERIFY_ARE_EQUAL(winrt::Windows::UI::Colors::Black(), row.Background().as<Media::SolidColorBrush>().Color());
            VERIFY_ARE_EQUAL(winrt::Windows::UI::Colors::White(), title.Foreground().as<Media::SolidColorBrush>().Color());
            page->_OpenNewTab(nullptr);
            VERIFY_IS_TRUE(std::abs(row.Background().Opacity() - 0.3) < 0.01);
            page->FocusTab(*tab);
            VERIFY_ARE_EQUAL(1.0, row.Background().Opacity());
            tab->ResetRuntimeTabColor();
            VERIFY_ARE_EQUAL(winrt::Windows::UI::Colors::White(), row.Background().as<Media::SolidColorBrush>().Color());
            VERIFY_ARE_EQUAL(winrt::Windows::UI::Colors::Black(), title.Foreground().as<Media::SolidColorBrush>().Color());

            page->_FindWorkspace(page->_activeWorkspaceId)->navigationNode.IsExpanded(false);
            page->_HandleOpenTabColorPicker(*tab, ActionEventArgs{});
        });
        TestOnUIThread([&]() {
            page->UpdateLayout();
            const auto item = page->WorkspaceNavigation().ContainerFromNode(page->_workspaceNavigationEntries.front().node).try_as<FrameworkElement>();
            VERIFY_IS_NOT_NULL(item);
            VERIFY_IS_TRUE(page->_tabColorPicker.Target() == item);
            VERIFY_IS_NOT_NULL(Media::VisualTreeHelper::GetParent(item));
            const auto row = page->_workspaceNavigationEntries.front().node.Content().as<Grid>();
            VERIFY_IS_NOT_NULL(Media::VisualTreeHelper::GetParent(row));
            const auto title = page->_GetFocusedTabImpl()->_headerControl.FindName(L"HeaderTextBlock").as<TextBlock>();
            VERIFY_ARE_EQUAL(winrt::Windows::UI::Colors::Black(), title.Foreground().as<Media::SolidColorBrush>().Color());
            VERIFY_IS_TRUE(page->_FindWorkspace(page->_activeWorkspaceId)->navigationNode.IsExpanded());
            page->_tabColorPicker.Hide();
        });
    }

    void TabTests::WorkspaceResizeCallbacks()
    {
        const auto page = _commonSetup();
        TestOnUIThread([&]() {
            const auto workspaceA = page->_activeWorkspaceId;
            const auto control = page->_GetActiveControl();
            // The page only forwards the size arguments. Exercise callback
            // routing without depending on the control's private args factory.
            const winrt::Microsoft::Terminal::Control::WindowSizeChangedEventArgs args{ nullptr };
            uint32_t resizeRequests = 0;
            const auto token = page->WindowSizeChanged([&](auto&&, auto&&) { ++resizeRequests; });
            const auto revoke = wil::scope_exit([&]() noexcept { page->WindowSizeChanged(token); });
            page->_WindowSizeChanged(control, args);
            VERIFY_ARE_EQUAL(1u, resizeRequests);
            page->SetFullscreen(true);
            page->_WindowSizeChanged(control, args);
            VERIFY_ARE_EQUAL(1u, resizeRequests);
            page->SetFullscreen(false);

            page->_SwitchWorkspace(L"resize-empty-B", false);
            VERIFY_ARE_EQUAL(1u, page->NumberOfTabs());
            VERIFY_IS_NULL(page->_GetFocusedTabImpl());
            page->_WindowSizeChanged(control, args);
            VERIFY_ARE_EQUAL(1u, resizeRequests);

            page->_SwitchWorkspace(workspaceA, false);
            page->_WindowSizeChanged(Grid{}, args);
            VERIFY_ARE_EQUAL(1u, resizeRequests);
            page->_WindowSizeChanged(control, args);
            VERIFY_ARE_EQUAL(2u, resizeRequests);

            // A queued callback from a removed tab must not resize the sole
            // remaining tab's window, even though it now has a focused tab.
            const auto originalTab = page->_GetFocusedTab();
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{ 1 }));
            const auto remainingControl = page->_GetActiveControl();
            originalTab.Close();
            VERIFY_ARE_EQUAL(1u, page->NumberOfTabs());
            page->_WindowSizeChanged(control, args);
            VERIFY_ARE_EQUAL(2u, resizeRequests);
            page->_WindowSizeChanged(remainingControl, args);
            VERIFY_ARE_EQUAL(3u, resizeRequests);
        });
    }

    void TabTests::WorkspaceLayoutTabOrder()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"Data:sideTabs", L"{true, false}")
            TEST_METHOD_PROPERTY(L"Data:markers", L"{true, false}")
        END_TEST_METHOD_PROPERTIES();
        bool sideTabs;
        bool markers;
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"sideTabs", sideTabs));
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"markers", markers));
        const auto page = _commonSetup(sideTabs ? TabPosition::Left : TabPosition::Top);
        const wil::unique_handle completed{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        VERIFY_IS_NOT_NULL(completed.get());
        winrt::event_token completionToken{};
        winrt::hstring workspaceA;
        TestOnUIThread([&]() {
            page->_settings.WindowSettings(L"").NewTabPosition(NewTabPosition::AfterLastTab);
            workspaceA = page->_activeWorkspaceId;
            page->_SwitchWorkspace(L"layout-order-B", false);
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{ 1 }));
            page->_SwitchWorkspace(workspaceA, false);
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{ 2 }));
            page->_SelectTab(1);
            const auto layout = page->GetWindowLayout();
            VERIFY_IS_NOT_NULL(layout);
            auto actions = wil::to_vector(layout.TabLayout());
            if (!markers)
            {
                std::erase_if(actions, [](const auto& action) { return action.Action() == ShortcutAction::OpenWorkspace; });
            }
            const std::vector<winrt::TerminalApp::Tab> tabs{ page->_tabs.begin(), page->_tabs.end() };
            for (const auto& tab : tabs)
            {
                tab.Close();
            }
            page->_SwitchWorkspace(workspaceA, false);
            page->_settings.WindowSettings(L"").NewTabPosition(NewTabPosition::AfterCurrentTab);
            completionToken = page->_actionDispatch->SwitchToTab([event = completed.get()](auto&&, auto&&) { SetEvent(event); });
            page->ProcessStartupActions(std::move(actions), {}, {}, !markers);
        });
        const auto revoke = wil::scope_exit([&]() noexcept {
            RunOnUIThread([&]() { page->_actionDispatch->SwitchToTab(completionToken); });
        });
        VERIFY_ARE_EQUAL(static_cast<DWORD>(WAIT_OBJECT_0), WaitForSingleObject(completed.get(), 10000));
        TestOnUIThread([&]() {
            VERIFY_ARE_EQUAL(3u, page->_tabs.Size());
            VERIFY_ARE_EQUAL(L"Profile 0", page->_tabs.GetAt(0).Title());
            VERIFY_ARE_EQUAL(L"Profile 1", page->_tabs.GetAt(1).Title());
            VERIFY_ARE_EQUAL(L"Profile 2", page->_tabs.GetAt(2).Title());
            VERIFY_ARE_EQUAL(1u, page->_GetFocusedTabIndex().value());
            VERIFY_ARE_EQUAL(markers ? winrt::hstring{ L"layout-order-B" } : workspaceA, page->_activeWorkspaceId);

            // Replaying a layout must not change the insertion policy for the
            // user's next interactive new-tab command.
            page->_SelectTab(0);
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{ 3 }));
            VERIFY_ARE_EQUAL(L"Profile 3", page->_tabs.GetAt(1).Title());
            VERIFY_ARE_EQUAL(L"Profile 1", page->_tabs.GetAt(2).Title());
        });
    }

    void TabTests::WorkspaceLayoutReplayOwnership()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"Data:sideTabs", L"{true, false}")
            TEST_METHOD_PROPERTY(L"Data:markers", L"{true, false}")
        END_TEST_METHOD_PROPERTIES();
        bool sideTabs;
        bool markers;
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"sideTabs", sideTabs));
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"markers", markers));
        const auto page = _commonSetup(sideTabs ? TabPosition::Left : TabPosition::Top);
        const wil::unique_handle completed{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        VERIFY_IS_NOT_NULL(completed.get());
        winrt::event_token newTabToken{}, splitToken{}, completionToken{};
        int64_t labelToken{};
        winrt::hstring restoredWorkspace;
        uint32_t userSwitches = 0;
        bool markerInterrupted = false;
        TestOnUIThread([&]() {
            GUID unique{};
            VERIFY_SUCCEEDED(CoCreateGuid(&unique));
            restoredWorkspace = winrt::to_hstring(unique);
            const auto interactiveWorkspace = page->_activeWorkspaceId;
            const auto queueUserSwitch = [&, interactiveWorkspace]() {
                page->Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [&, interactiveWorkspace]() {
                    VERIFY_IS_FALSE(page->_restoringLayout);
                    page->_SwitchWorkspace(interactiveWorkspace, false);
                    ++userSwitches;
                });
            };
            // Interrupt the marker (or legacy workspace activation) before
            // the first NewTab resumes, then interrupt between later actions.
            labelToken = page->WorkspaceHeaderName().RegisterPropertyChangedCallback(TextBlock::TextProperty(), [&, queueUserSwitch](auto&&, auto&&) {
                if (!markerInterrupted && page->WorkspaceHeaderName().Text() == restoredWorkspace)
                {
                    markerInterrupted = true;
                    queueUserSwitch();
                }
            });
            newTabToken = page->_actionDispatch->NewTab([queueUserSwitch](auto&&, auto&&) { queueUserSwitch(); });
            splitToken = page->_actionDispatch->SplitPane([queueUserSwitch](auto&&, auto&&) { queueUserSwitch(); });
            completionToken = page->_actionDispatch->SwitchToTab([event = completed.get()](auto&&, auto&&) { SetEvent(event); });
            std::vector<ActionAndArgs> actions;
            if (markers)
            {
                actions.emplace_back(ShortcutAction::OpenWorkspace, OpenWorkspaceArgs{ restoredWorkspace });
            }
            actions.emplace_back(ShortcutAction::NewTab, NewTabArgs{ NewTerminalArgs{ 1 } });
            actions.emplace_back(ShortcutAction::SplitPane, SplitPaneArgs{ SplitType::Duplicate });
            actions.emplace_back(ShortcutAction::NewTab, NewTabArgs{ NewTerminalArgs{ 3 } });
            actions.emplace_back(ShortcutAction::SwitchToTab, SwitchToTabArgs{ uint32_t{ 0 } });
            WindowLayout layout;
            layout.TabLayout(winrt::single_threaded_vector<ActionAndArgs>(std::move(actions)));
            ApplicationState::SharedInstance().SaveWorkspace(restoredWorkspace, layout);
            page->_settings.WindowSettings(L"").NewTabPosition(NewTabPosition::AfterCurrentTab);
            page->_OpenWorkspace(restoredWorkspace);
        });
        const auto revoke = wil::scope_exit([&]() noexcept {
            RunOnUIThread([&]() {
                page->WorkspaceHeaderName().UnregisterPropertyChangedCallback(TextBlock::TextProperty(), labelToken);
                page->_actionDispatch->NewTab(newTabToken);
                page->_actionDispatch->SplitPane(splitToken);
                page->_actionDispatch->SwitchToTab(completionToken);
            });
        });
        VERIFY_ARE_EQUAL(static_cast<DWORD>(WAIT_OBJECT_0), WaitForSingleObject(completed.get(), 10000));
        TestOnUIThread([&]() {
            VERIFY_IS_TRUE(markerInterrupted);
            VERIFY_ARE_EQUAL(4u, userSwitches);
            VERIFY_ARE_EQUAL(3u, page->_tabs.Size());
            VERIFY_ARE_EQUAL(restoredWorkspace, page->_WorkspaceForTab(page->_tabs.GetAt(1)));
            VERIFY_ARE_EQUAL(restoredWorkspace, page->_WorkspaceForTab(page->_tabs.GetAt(2)));
            VERIFY_ARE_EQUAL(1, page->_GetTabImpl(page->_tabs.GetAt(0))->GetLeafPaneCount());
            VERIFY_ARE_EQUAL(2, page->_GetTabImpl(page->_tabs.GetAt(1))->GetLeafPaneCount());
            VERIFY_ARE_EQUAL(L"Profile 3", page->_tabs.GetAt(2).Title());
            VERIFY_ARE_EQUAL(restoredWorkspace, page->_activeWorkspaceId);
            VERIFY_ARE_EQUAL(1u, page->_GetFocusedTabIndex().value());
            VERIFY_IS_FALSE(page->_restoringLayout);
        });
    }

    void TabTests::WorkspaceMoveTabNeighbors()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"IsolationLevel", L"Method")
            TEST_METHOD_PROPERTY(L"Data:sideTabs", L"{true, false}")
        END_TEST_METHOD_PROPERTIES();
        bool sideTabs;
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"sideTabs", sideTabs));
        const auto page = _commonSetup(sideTabs ? TabPosition::Left : TabPosition::Top);
        TestOnUIThread([&]() {
            page->_settings.WindowSettings(L"").NewTabPosition(NewTabPosition::AfterLastTab);
            const auto workspaceA = page->_activeWorkspaceId;
            const auto a1 = page->_GetFocusedTab();
            page->_SwitchWorkspace(L"move-neighbors-B");
            const auto b1 = page->_GetFocusedTab();
            page->_SwitchWorkspace(workspaceA, false);
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{ 1 }));
            const auto a2 = page->_GetFocusedTab();
            page->_SwitchWorkspace(L"move-neighbors-C");
            page->_SwitchWorkspace(L"move-neighbors-B", false);
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{ 2 }));
            const auto b2 = page->_GetFocusedTab();
            const auto verifyOrder = [&](const winrt::hstring& workspaceId, const auto& first, const auto& second) {
                std::vector<winrt::TerminalApp::Tab> tabs;
                for (const auto& tab : page->_tabs)
                {
                    if (page->_WorkspaceForTab(tab) == workspaceId)
                    {
                        tabs.push_back(tab);
                    }
                }
                VERIFY_ARE_EQUAL(2u, tabs.size());
                VERIFY_IS_TRUE(tabs[0] == first);
                VERIFY_IS_TRUE(tabs[1] == second);
                if (sideTabs)
                {
                    const auto children = page->_FindWorkspace(workspaceId)->navigationNode.Children();
                    for (const auto& entry : page->_workspaceNavigationEntries)
                    {
                        if (entry.tab == first)
                        {
                            VERIFY_IS_TRUE(children.GetAt(0) == entry.node);
                        }
                        if (entry.tab == second)
                        {
                            VERIFY_IS_TRUE(children.GetAt(1) == entry.node);
                        }
                    }
                }
                for (uint32_t i = 0; i < page->_tabs.Size(); ++i)
                {
                    VERIFY_IS_TRUE(page->_tabs.GetAt(i).TabViewItem() == page->_tabView.TabItems().GetAt(i));
                }
            };
            VERIFY_IS_TRUE(page->_tabs.GetAt(0) == a1);
            VERIFY_IS_TRUE(page->_tabs.GetAt(1) == b1);
            VERIFY_IS_TRUE(page->_tabs.GetAt(2) == a2);
            page->FocusTab(a1);
            VERIFY_IS_TRUE(page->_MoveTab(page->_GetTabImpl(a1), MoveTabArgs{ L"", MoveTabDirection::Forward }));
            verifyOrder(workspaceA, a2, a1);
            verifyOrder(L"move-neighbors-B", b1, b2);
            VERIFY_IS_TRUE(page->_MoveTab(page->_GetTabImpl(a1), MoveTabArgs{ L"", MoveTabDirection::Backward }));
            verifyOrder(workspaceA, a1, a2);

            // Workspace boundaries are no-ops even when global neighbors exist.
            const std::vector<winrt::TerminalApp::Tab> before{ page->_tabs.begin(), page->_tabs.end() };
            page->_MoveTab(page->_GetTabImpl(a1), MoveTabArgs{ L"", MoveTabDirection::Backward });
            page->_MoveTab(page->_GetTabImpl(a2), MoveTabArgs{ L"", MoveTabDirection::Forward });
            VERIFY_IS_TRUE(before == std::vector<winrt::TerminalApp::Tab>(page->_tabs.begin(), page->_tabs.end()));
            page->_MoveTab(page->_GetTabImpl(b1), MoveTabArgs{ L"", MoveTabDirection::Forward });
            verifyOrder(L"move-neighbors-B", b2, b1);
            verifyOrder(workspaceA, a1, a2);
            page->_MoveTab(page->_GetTabImpl(b1), MoveTabArgs{ L"", MoveTabDirection::Backward });
            verifyOrder(L"move-neighbors-B", b1, b2);
        });
    }

    void TabTests::WorkspaceHeaderRestorationAndThemeReload()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"Data:sideTabs", L"{true, false}")
            TEST_METHOD_PROPERTY(L"Data:focusMode", L"{true, false}")
            TEST_METHOD_PROPERTY(L"Data:dpi", L"{96, 144}")
        END_TEST_METHOD_PROPERTIES();
        bool sideTabs;
        bool focusMode;
        uint32_t dpi;
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"sideTabs", sideTabs));
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"focusMode", focusMode));
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"dpi", dpi));
        CascadiaSettings settings{ LR"({
            "showTabsInTitlebar": false, "alwaysShowTabs": false,
            "theme": "header-test",
            "themes": [
                { "name": "header-test", "window": { "showWorkspacesButton": false } },
                { "name": "header-shown", "window": { "showWorkspacesButton": true } }
            ],
            "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
            "profiles": [{ "name": "Header test", "guid": "{6239a42c-1111-49a3-80bd-e8fdd045185c}", "closeOnExit": "never" }]
        })",
                                   {} };
        settings.WindowSettings(L"").TabPosition(sideTabs ? TabPosition::Left : TabPosition::Top);
        winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> page;
        _initializeTerminalPage(page, settings);
        TestOnUIThread([&]() {
            page->Width(1000);
            page->Height(800);
            page->_isInFocusMode = focusMode;
            const auto loadResult = winrt::make<winrt::TerminalApp::implementation::SettingsLoadEventArgs>(false, S_OK, L"", nullptr, settings);
            const auto window = winrt::make_self<winrt::TerminalApp::implementation::TerminalWindow>(loadResult, *_contentManager);
            const auto scale = static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI;
            for (const bool showButton : { false, true, false })
            {
                settings.WindowSettings(L"").Theme(ThemePair{ showButton ? L"header-shown" : L"header-test" });
                page->SetSettings(settings, true);
                page->UpdateLayout();
                VERIFY_ARE_EQUAL(showButton, page->_tabRow.ShowWorkspacesButton());
                const auto expectedVisibility = showButton && !focusMode ? Visibility::Visible : Visibility::Collapsed;
                VERIFY_ARE_EQUAL(expectedVisibility, sideTabs ? page->WorkspaceHeader().Visibility() : page->_tabView.Visibility());

                // Feed each restored size back into persistence. Neither DPI
                // scaling nor repeated launches should change the client size.
                for (auto restart = 0; restart < 2; ++restart)
                {
                    page->_isMaximized = restart != 0;
                    window->SetPersistedLayout(page->GetWindowLayout());
                    const auto dimensions = window->GetLaunchDimensions(dpi);
                    VERIFY_ARE_EQUAL(1000.0f * scale, dimensions.Width);
                    VERIFY_ARE_EQUAL(800.0f * scale, dimensions.Height);
                    page->Height(dimensions.Height / scale);
                    page->UpdateLayout();
                }
            }

            // Multiple terminals or workspaces also keep the header visible
            // when the theme disables the workspace button.
            VERIFY_SUCCEEDED(page->_OpenNewTab(nullptr));
            for (const bool anotherWorkspace : { false, true })
            {
                if (anotherWorkspace)
                {
                    page->_SwitchWorkspace(L"header-restoration-B");
                }
                page->UpdateLayout();
                const auto expectedVisibility = focusMode ? Visibility::Collapsed : Visibility::Visible;
                VERIFY_ARE_EQUAL(expectedVisibility, sideTabs ? page->WorkspaceHeader().Visibility() : page->_tabView.Visibility());
                window->SetPersistedLayout(page->GetWindowLayout());
                VERIFY_ARE_EQUAL(800.0f * scale, window->GetLaunchDimensions(dpi).Height);
            }
        });
    }

    void TabTests::WorkspaceCloseSelectedMruTab()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"Data:sideTabs", L"{true, false}")
        END_TEST_METHOD_PROPERTIES();
        bool sideTabs;
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"sideTabs", sideTabs));
        const auto page = _commonSetup(sideTabs ? TabPosition::Left : TabPosition::Top);
        TestOnUIThread([&]() {
            page->_settings.WindowSettings(L"").NewTabPosition(NewTabPosition::AfterLastTab);
            const auto workspace = page->_activeWorkspaceId;
            const auto first = page->_GetFocusedTab();
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{ 1 }));
            const auto adjacent = page->_GetFocusedTab();
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{ 2 }));
            const auto selected = page->_GetFocusedTab();
            page->FocusTab(first);
            page->FocusTab(selected);
            page->_SwitchWorkspace(L"close-mru-background");
            const auto background = page->_GetFocusedTab();
            page->_SwitchWorkspace(workspace, false);
            VERIFY_IS_TRUE(page->_GetFocusedTab() == selected);
            selected.Close();
            VERIFY_IS_TRUE(page->_GetFocusedTab() == first);
            VERIFY_IS_TRUE(page->_tabContent.Children().GetAt(0) == first.Content());
            VERIFY_ARE_EQUAL(workspace, page->_activeWorkspaceId);
            adjacent.Close();
            VERIFY_IS_TRUE(page->_GetFocusedTab() == first);
            first.Close();
            VERIFY_IS_NULL(page->_GetFocusedTab());
            VERIFY_ARE_EQUAL(0u, page->_tabContent.Children().Size());
            VERIFY_ARE_EQUAL(1u, page->_tabs.Size());
            VERIFY_IS_TRUE(page->_tabs.GetAt(0) == background);
        });
    }

    void TabTests::WorkspaceCompactViewRestoration()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"Data:sideTabs", L"{true, false}")
        END_TEST_METHOD_PROPERTIES();
        bool sideTabs;
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"sideTabs", sideTabs));
        const auto page = _commonSetup(sideTabs ? TabPosition::Left : TabPosition::Top);
        TestOnUIThread([&]() {
            GUID unique{};
            VERIFY_SUCCEEDED(CoCreateGuid(&unique));
            const auto file = std::filesystem::path{ winrt::Windows::Storage::ApplicationData::Current().TemporaryFolder().Path().c_str() } / (std::wstring{ winrt::to_hstring(unique) } + L".txt");
            const auto cleanup = wil::scope_exit([&]() noexcept {
                std::error_code error;
                std::filesystem::remove(file, error);
            });
            std::ofstream{ file } << "compact document preview";
            const auto workspaceA = page->_activeWorkspaceId;
            const auto terminal = page->_GetFocusedTab();
            page->WorkspaceContentArea().Width(480);
            page->UpdateLayout();
            page->_OpenWorkspaceDocument(file, true);
            VERIFY_ARE_EQUAL(0.0, page->WorkspaceTerminalColumn().Width().Value);
            page->_SwitchWorkspace(L"compact-restoration-B", false);
            page->_SwitchWorkspace(workspaceA, false);
            VERIFY_IS_FALSE(page->_FindWorkspace(workspaceA)->preferTerminalInCompactView);
            VERIFY_ARE_EQUAL(Visibility::Visible, page->WorkspaceDocumentPanel().Visibility());
            VERIFY_ARE_EQUAL(0.0, page->WorkspaceTerminalColumn().Width().Value);
            VERIFY_IS_TRUE(page->_GetFocusedTab() == terminal);
            VERIFY_ARE_NOT_EQUAL(FocusState::Unfocused, page->WorkspaceDocumentEditor().FocusState());
            VERIFY_ARE_EQUAL(FocusState::Unfocused, page->_GetTabImpl(terminal)->_focusState);

            // Activating the already-selected terminal is still an explicit
            // request to show it in compact mode, in either tab-strip layout.
            page->FocusTab(terminal);
            VERIFY_IS_TRUE(page->_FindWorkspace(workspaceA)->preferTerminalInCompactView);
            VERIFY_ARE_EQUAL(Visibility::Collapsed, page->WorkspaceDocumentPanel().Visibility());
            page->_SwitchWorkspace(L"compact-restoration-B", false);
            page->_SwitchWorkspace(workspaceA, false);
            VERIFY_IS_TRUE(page->_FindWorkspace(workspaceA)->preferTerminalInCompactView);
            VERIFY_ARE_EQUAL(Visibility::Collapsed, page->WorkspaceDocumentPanel().Visibility());
        });
    }

    void TabTests::WorkspacePreviewFocusOnClose()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"Data:sideTabs", L"{true, false}")
            TEST_METHOD_PROPERTY(L"Data:compact", L"{true, false}")
        END_TEST_METHOD_PROPERTIES();
        bool sideTabs;
        bool compact;
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"sideTabs", sideTabs));
        VERIFY_SUCCEEDED(TestData::TryGetValue(L"compact", compact));
        const auto page = _commonSetup(sideTabs ? TabPosition::Left : TabPosition::Top);
        TestOnUIThread([&]() {
            GUID unique{};
            VERIFY_SUCCEEDED(CoCreateGuid(&unique));
            const auto file = std::filesystem::path{ winrt::Windows::Storage::ApplicationData::Current().TemporaryFolder().Path().c_str() } / (std::wstring{ winrt::to_hstring(unique) } + L".txt");
            const auto cleanup = wil::scope_exit([&]() noexcept {
                std::error_code error;
                std::filesystem::remove(file, error);
            });
            std::ofstream{ file } << "focused preview";
            const auto selected = page->_GetFocusedTab();
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{ 1 }));
            const auto remaining = page->_GetFocusedTab();
            VERIFY_SUCCEEDED(page->_OpenNewTab(NewTerminalArgs{ 2 }));
            const auto background = page->_GetFocusedTab();
            page->FocusTab(selected);
            page->WorkspaceContentArea().Width(compact ? 480 : 1000);
            page->UpdateLayout();
            page->_OpenWorkspaceDocument(file, true);
            const auto verifyPreviewFocus = [&]() {
                VERIFY_ARE_EQUAL(Visibility::Visible, page->WorkspaceDocumentPanel().Visibility());
                VERIFY_ARE_NOT_EQUAL(FocusState::Unfocused, page->WorkspaceDocumentEditor().FocusState());
                for (const auto& tab : page->_tabs)
                {
                    VERIFY_ARE_EQUAL(FocusState::Unfocused, page->_GetTabImpl(tab)->_focusState);
                }
                if (compact)
                {
                    VERIFY_ARE_EQUAL(0.0, page->WorkspaceTerminalColumn().Width().Value);
                }
            };
            background.Close();
            VERIFY_IS_TRUE(page->_GetFocusedTab() == selected);
            verifyPreviewFocus();
            selected.Close();
            VERIFY_IS_TRUE(page->_GetFocusedTab() == remaining);
            verifyPreviewFocus();
            remaining.Close();
            VERIFY_IS_NULL(page->_GetFocusedTab());
            verifyPreviewFocus();
        });
    }

    void TabTests::WorkspacePreviewThemeChanges()
    {
        const auto page = _commonSetup();
        std::filesystem::path file;
        winrt::hstring workspaceA;
        const auto cleanup = wil::scope_exit([&]() noexcept {
            std::error_code error;
            std::filesystem::remove(file, error);
        });
        TestOnUIThread([&]() {
            GUID unique{};
            VERIFY_SUCCEEDED(CoCreateGuid(&unique));
            file = std::filesystem::path{ winrt::Windows::Storage::ApplicationData::Current().TemporaryFolder().Path().c_str() } / (std::wstring{ winrt::to_hstring(unique) } + L".cpp");
            std::ofstream{ file } << "int value = 42; // preview\n";
            workspaceA = page->_activeWorkspaceId;
            page->RequestedTheme(ElementTheme::Dark);
            page->UpdateLayout();
            page->_OpenWorkspaceDocument(file, true);
            page->UpdateLayout();
            // Change theme while the first palette's asynchronous batches may
            // still be pending. Those batches must not overwrite the new one.
            page->RequestedTheme(ElementTheme::Light);
            page->UpdateLayout();
        });
        const auto verifyPalette = [&](bool dark, bool verifySelection) {
            const auto bodyColor = dark ? winrt::Windows::UI::Color{ 255, 212, 212, 212 } : winrt::Windows::UI::Color{ 255, 32, 32, 32 };
            const auto keywordColor = dark ? winrt::Windows::UI::Color{ 255, 86, 156, 214 } : winrt::Windows::UI::Color{ 255, 0, 0, 180 };
            auto applied = false;
            for (auto attempt = 0; attempt < 200 && !applied; ++attempt)
            {
                TestOnUIThread([&]() {
                    const auto document = page->WorkspaceDocumentEditor().Document();
                    applied = document.GetRange(4, 5).CharacterFormat().ForegroundColor() == bodyColor &&
                              document.GetRange(0, 1).CharacterFormat().ForegroundColor() == keywordColor;
                });
                if (!applied)
                {
                    Sleep(10);
                }
            }
            VERIFY_IS_TRUE(applied);
            TestOnUIThread([&]() {
                const auto document = page->WorkspaceDocumentEditor().Document();
                if (verifySelection)
                {
                    VERIFY_ARE_EQUAL(4, document.Selection().StartPosition());
                    VERIFY_ARE_EQUAL(9, document.Selection().EndPosition());
                }
                VERIFY_IS_TRUE(page->WorkspaceDocumentEditor().IsReadOnly());
                winrt::hstring text;
                document.GetText(TextGetOptions::None, text);
                VERIFY_IS_TRUE(std::wstring_view{ text }.starts_with(L"int value = 42; // preview"));
            });
        };
        verifyPalette(false, false);
        TestOnUIThread([&]() {
            page->WorkspaceDocumentEditor().Document().Selection().SetRange(4, 9);
            VERIFY_ARE_EQUAL(4, page->WorkspaceDocumentEditor().Document().Selection().StartPosition());
            page->RequestedTheme(ElementTheme::Dark);
            page->UpdateLayout();
        });
        verifyPalette(true, true);
        TestOnUIThread([&]() {
            page->_SwitchWorkspace(L"preview-theme-B", false);
            std::ofstream{ file } << "externally changed";
            page->RequestedTheme(ElementTheme::Light);
            page->UpdateLayout();
            page->_SwitchWorkspace(workspaceA, false);
        });
        verifyPalette(false, true);
    }

    void TabTests::WorkspaceNavigationCloseButtonHover()
    {
        CascadiaSettings settings{ LR"({
            "tabPosition": "left", "showTabsInTitlebar": false,
            "theme": "hover-test",
            "themes": [{ "name": "hover-test", "tab": { "showCloseButton": "hover" } }],
            "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
            "profiles": [{ "name": "Hover test", "guid": "{6239a42c-1111-49a3-80bd-e8fdd045185c}", "closeOnExit": "never" }]
        })",
                                   {} };
        winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> page;
        _initializeTerminalPage(page, settings);
        TestOnUIThread([&]() {
            const auto tab = page->_GetFocusedTabImpl();
            const auto row = page->_workspaceNavigationEntries.front().node.Content().as<Grid>();
            const auto close = row.Children().GetAt(2).as<Button>();
            VERIFY_IS_TRUE(tab->TabViewItem().IsClosable());
            VERIFY_ARE_EQUAL(Visibility::Collapsed, close.Visibility());
            tab->_NavigationRowPointerEntered(row, nullptr);
            VERIFY_ARE_EQUAL(Visibility::Visible, close.Visibility());
            tab->_NavigationRowPointerExited(row, nullptr);
            VERIFY_ARE_EQUAL(Visibility::Collapsed, close.Visibility());

            tab->CloseButtonVisibility(TabCloseButtonVisibility::Always);
            VERIFY_ARE_EQUAL(Visibility::Visible, close.Visibility());
            tab->CloseButtonVisibility(TabCloseButtonVisibility::Hover);
            VERIFY_ARE_EQUAL(Visibility::Collapsed, close.Visibility());
            tab->_NavigationRowPointerEntered(row, nullptr);
            tab->ReadOnly(true);
            page->_updateAllTabCloseButtons();
            VERIFY_ARE_EQUAL(Visibility::Collapsed, close.Visibility());
            tab->ReadOnly(false);
            page->_updateAllTabCloseButtons();
            VERIFY_ARE_EQUAL(Visibility::Visible, close.Visibility());
            tab->CloseButtonVisibility(TabCloseButtonVisibility::Never);
            VERIFY_ARE_EQUAL(Visibility::Collapsed, close.Visibility());
            tab->CloseButtonVisibility(TabCloseButtonVisibility::ActiveOnly);
            VERIFY_ARE_EQUAL(Visibility::Visible, close.Visibility());
            tab->Focus(FocusState::Unfocused);
            tab->CloseButtonVisibility(TabCloseButtonVisibility::ActiveOnly);
            VERIFY_ARE_EQUAL(Visibility::Collapsed, close.Visibility());
            tab->Focus(FocusState::Programmatic);
            tab->CloseButtonVisibility(TabCloseButtonVisibility::ActiveOnly);
            VERIFY_ARE_EQUAL(Visibility::Visible, close.Visibility());
        });
    }

    void TabTests::WorkspaceExplorerContext()
    {
        const auto page = _commonSetup();
        TestOnUIThread([&]() {
            GUID unique{};
            VERIFY_SUCCEEDED(CoCreateGuid(&unique));
            const auto root = std::filesystem::path{ winrt::Windows::Storage::ApplicationData::Current().TemporaryFolder().Path().c_str() } / winrt::to_hstring(unique).c_str();
            const auto cleanup = wil::scope_exit([&]() noexcept {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            });
            std::filesystem::create_directories(root / L"nested");
            const auto file = root / L"nested" / L"context.txt";
            std::ofstream{ file } << "original preview\nsecond line";
            const winrt::hstring workspaceId{ root.native() };
            page->_SwitchWorkspace(workspaceId);
            const auto treeRoot = page->WorkspaceFileTree().RootNodes().GetAt(0);
            const auto directory = treeRoot.Children().GetAt(0);
            page->_PopulateWorkspaceFileNode(directory);
            directory.IsExpanded(true);
            const auto fileNode = directory.Children().GetAt(0);
            page->WorkspaceFileTree().SelectedNode(fileNode);
            page->UpdateLayout();
            const auto container = page->WorkspaceFileTree().ContainerFromNode(fileNode);
            VERIFY_IS_NOT_NULL(container);
            const auto hasFileLabel = [&](const auto& self, const DependencyObject& element) -> bool {
                if (const auto text = element.try_as<TextBlock>(); text && text.Text() == L"context.txt")
                {
                    return true;
                }
                using winrt::Windows::UI::Xaml::Media::VisualTreeHelper;
                for (int32_t i = 0; i < VisualTreeHelper::GetChildrenCount(element); ++i)
                {
                    if (self(self, VisualTreeHelper::GetChild(element, i)))
                    {
                        return true;
                    }
                }
                return false;
            };
            VERIFY_IS_TRUE(hasFileLabel(hasFileLabel, container));
            page->_OpenWorkspaceDocument(file, true);
            VERIFY_ARE_NOT_EQUAL(FocusState::Unfocused, page->WorkspaceDocumentEditor().FocusState());
            page->WorkspaceDocumentEditor().Document().Selection().SetRange(3, 8);
            page->WorkspaceFileSearchBox().Text(L"context");
            page->_SwitchWorkspace(L"context-test-other", false);
            std::ofstream{ file } << "externally changed";
            page->_SwitchWorkspace(workspaceId, false);
            VERIFY_IS_TRUE(page->WorkspaceFileTree().RootNodes().GetAt(0) == treeRoot);
            VERIFY_IS_TRUE(directory.IsExpanded());
            VERIFY_IS_TRUE(page->WorkspaceFileTree().SelectedNode() == fileNode);
            VERIFY_ARE_EQUAL(L"context", page->WorkspaceFileSearchBox().Text());
            winrt::hstring text;
            page->WorkspaceDocumentEditor().Document().GetText(TextGetOptions::None, text);
            VERIFY_IS_TRUE(std::wstring_view{ text }.starts_with(L"original preview"));
            VERIFY_ARE_EQUAL(3, page->WorkspaceDocumentEditor().Document().Selection().StartPosition());
            VERIFY_ARE_EQUAL(8, page->WorkspaceDocumentEditor().Document().Selection().EndPosition());
            page->WorkspaceFileSearchBox().Text(L"");
            VERIFY_IS_TRUE(page->WorkspaceFileTree().RootNodes().GetAt(0) == treeRoot);
            VERIFY_IS_TRUE(directory.IsExpanded());
            for (auto i = 0; i < 5; ++i)
            {
                page->_SwitchWorkspace(L"context-test-other", false);
                page->UpdateLayout();
                page->_SwitchWorkspace(workspaceId, false);
                page->UpdateLayout();
                VERIFY_IS_TRUE(page->WorkspaceFileTree().RootNodes().GetAt(0) == treeRoot);
                VERIFY_IS_TRUE(directory.IsExpanded());
                VERIFY_IS_TRUE(page->WorkspaceFileTree().SelectedNode() == fileNode);
            }
            // In a narrow window, explicit terminal navigation must reveal the
            // terminal instead of leaving the document covering its content.
            page->WorkspaceContentArea().Width(480);
            page->UpdateLayout();
            page->_OpenWorkspaceDocument(file, true);
            VERIFY_ARE_EQUAL(0.0, page->WorkspaceTerminalColumn().Width().Value);
            const auto terminalIndex = page->_GetFocusedTabIndex().value();
            page->_SelectTab(0);
            page->_SelectTab(terminalIndex);
            VERIFY_ARE_EQUAL(Visibility::Collapsed, page->WorkspaceDocumentPanel().Visibility());
            VERIFY_ARE_EQUAL(GridUnitType::Star, page->WorkspaceTerminalColumn().Width().GridUnitType);
            page->_GetFocusedTab().Close();
            VERIFY_ARE_EQUAL(Visibility::Visible, page->WorkspaceFilesPanel().Visibility());
            VERIFY_ARE_EQUAL(Visibility::Visible, page->WorkspaceDocumentPanel().Visibility());
            VERIFY_ARE_EQUAL(Visibility::Collapsed, page->WorkspaceHub().Visibility());
            page->WorkspaceContentArea().Width(NAN);
        });
    }

    void TabTests::TryDuplicateBadTab()
    {
        // * Create a tab with a profile with GUID 1
        // * Reload the settings so that GUID 1 is no longer in the list of profiles
        // * Try calling _DuplicateFocusedTab on tab 1
        // * No new tab should be created (and more importantly, the app should not crash)
        //
        // Created to test GH#2455

        static constexpr std::wstring_view settingsJson0{ LR"(
        {
            "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
            "profiles": [
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
                    "historySize": 1,
                    "closeOnExit": "never"
                },
                {
                    "name" : "profile1",
                    "guid": "{6239a42c-2222-49a3-80bd-e8fdd045185c}",
                    "historySize": 2,
                    "closeOnExit": "never"
                }
            ]
        })" };

        static constexpr std::wstring_view settingsJson1{ LR"(
        {
            "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
            "profiles": [
                {
                    "name" : "profile1",
                    "guid": "{6239a42c-2222-49a3-80bd-e8fdd045185c}",
                    "historySize": 2,
                    "closeOnExit": "never"
                }
            ]
        })" };

        CascadiaSettings settings0{ settingsJson0, {} };
        VERIFY_IS_NOT_NULL(settings0);

        CascadiaSettings settings1{ settingsJson1, {} };
        VERIFY_IS_NOT_NULL(settings1);

        const auto guid1 = Microsoft::Console::Utils::GuidFromString(L"{6239a42c-1111-49a3-80bd-e8fdd045185c}");
        const auto guid2 = Microsoft::Console::Utils::GuidFromString(L"{6239a42c-2222-49a3-80bd-e8fdd045185c}");
        const auto guid3 = Microsoft::Console::Utils::GuidFromString(L"{6239a42c-3333-49a3-80bd-e8fdd045185c}");

        // This is super wacky, but we can't just initialize the
        // com_ptr<impl::TerminalPage> in the lambda and assign it back out of
        // the lambda. We'll crash trying to get a weak_ref to the TerminalPage
        // during TerminalPage::Create() below.
        //
        // Instead, create the winrt object, then get a com_ptr to the
        // implementation _from_ the winrt object. This seems to work, even if
        // it's weird.
        winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> page{ nullptr };
        _initializeTerminalPage(page, settings0);

        auto result = RunOnUIThread([&page]() {
            VERIFY_ARE_EQUAL(1u, page->_tabs.Size());
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(L"Duplicate the first tab");
        result = RunOnUIThread([&page]() {
            page->_DuplicateFocusedTab();
            VERIFY_ARE_EQUAL(2u, page->_tabs.Size());
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(NoThrowString().Format(
            L"Change the settings of the TerminalPage so the first profile is "
            L"no longer in the list of profiles"));
        result = RunOnUIThread([&page, settings1]() {
            page->_settings = settings1;
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(L"Duplicate the tab, and don't crash");
        result = RunOnUIThread([&page]() {
            page->_DuplicateFocusedTab();
            VERIFY_ARE_EQUAL(3u, page->_tabs.Size(), L"We should successfully duplicate a tab hosting a deleted profile.");
        });
        VERIFY_SUCCEEDED(result);
    }

    void TabTests::TryDuplicateBadPane()
    {
        // * Create a tab with a profile with GUID 1
        // * Reload the settings so that GUID 1 is no longer in the list of profiles
        // * Try calling _SplitPane(Duplicate) on tab 1
        // * No new pane should be created (and more importantly, the app should not crash)
        //
        // Created to test GH#2455

        static constexpr std::wstring_view settingsJson0{ LR"(
        {
            "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
            "profiles": [
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
                    "historySize": 1,
                    "closeOnExit": "never"
                },
                {
                    "name" : "profile1",
                    "guid": "{6239a42c-2222-49a3-80bd-e8fdd045185c}",
                    "historySize": 2,
                    "closeOnExit": "never"
                }
            ]
        })" };

        static constexpr std::wstring_view settingsJson1{ LR"(
        {
            "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
            "profiles": [
                {
                    "name" : "profile1",
                    "guid": "{6239a42c-2222-49a3-80bd-e8fdd045185c}",
                    "historySize": 2,
                    "closeOnExit": "never"
                }
            ]
        })" };

        CascadiaSettings settings0{ settingsJson0, {} };
        VERIFY_IS_NOT_NULL(settings0);

        CascadiaSettings settings1{ settingsJson1, {} };
        VERIFY_IS_NOT_NULL(settings1);

        const auto guid1 = Microsoft::Console::Utils::GuidFromString(L"{6239a42c-1111-49a3-80bd-e8fdd045185c}");
        const auto guid2 = Microsoft::Console::Utils::GuidFromString(L"{6239a42c-2222-49a3-80bd-e8fdd045185c}");
        const auto guid3 = Microsoft::Console::Utils::GuidFromString(L"{6239a42c-3333-49a3-80bd-e8fdd045185c}");

        // This is super wacky, but we can't just initialize the
        // com_ptr<impl::TerminalPage> in the lambda and assign it back out of
        // the lambda. We'll crash trying to get a weak_ref to the TerminalPage
        // during TerminalPage::Create() below.
        //
        // Instead, create the winrt object, then get a com_ptr to the
        // implementation _from_ the winrt object. This seems to work, even if
        // it's weird.
        winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> page{ nullptr };
        _initializeTerminalPage(page, settings0);

        auto result = RunOnUIThread([&page]() {
            VERIFY_ARE_EQUAL(1u, page->_tabs.Size());
        });
        VERIFY_SUCCEEDED(result);

        result = RunOnUIThread([&page]() {
            VERIFY_ARE_EQUAL(1u, page->_tabs.Size());
            auto tab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(1, tab->GetLeafPaneCount());
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(NoThrowString().Format(L"Duplicate the first pane"));
        result = RunOnUIThread([&page]() {
            page->_SplitPane(nullptr, SplitDirection::Automatic, 0.5f, page->_MakePane(nullptr, page->_GetFocusedTab(), nullptr));

            VERIFY_ARE_EQUAL(1u, page->_tabs.Size());
            auto tab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(2, tab->GetLeafPaneCount());
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(NoThrowString().Format(
            L"Change the settings of the TerminalPage so the first profile is "
            L"no longer in the list of profiles"));
        result = RunOnUIThread([&page, settings1]() {
            page->_settings = settings1;
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(NoThrowString().Format(L"Duplicate the pane, and don't crash"));
        result = RunOnUIThread([&page]() {
            page->_SplitPane(nullptr, SplitDirection::Automatic, 0.5f, page->_MakePane(nullptr, page->_GetFocusedTab(), nullptr));

            VERIFY_ARE_EQUAL(1u, page->_tabs.Size());
            auto tab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(3,
                             tab->GetLeafPaneCount(),
                             L"We should successfully duplicate a pane hosting a deleted profile.");
        });
        VERIFY_SUCCEEDED(result);

        auto cleanup = wil::scope_exit([] {
            auto result = RunOnUIThread([]() {
                // There's something causing us to crash north of
                // TSFInputControl::NotifyEnter, or LayoutRequested. It's very
                // unclear what that issue is. Since these tests don't run in
                // CI, simply log a message so that the dev running these tests
                // knows it's expected.
                Log::Comment(L"This test often crashes on cleanup, even when it succeeds. If it succeeded, then crashes, that's okay.");
            });
            VERIFY_SUCCEEDED(result);
        });
    }

    // Method Description:
    // - This is a helper method for setting up a TerminalPage with some common
    //   settings, and creating the first tab.
    // Arguments:
    // - <none>
    // Return Value:
    // - The initialized TerminalPage, ready to use.
    winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> TabTests::_commonSetup(TabPosition position)
    {
        static constexpr std::wstring_view settingsJson0{ LR"(
        {
            "defaultProfile": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
            "showTabsInTitlebar": false,
            "profiles": [
                {
                    "name" : "profile0",
                    "guid": "{6239a42c-1111-49a3-80bd-e8fdd045185c}",
                    "tabTitle" : "Profile 0",
                    "historySize": 1,
                    "closeOnExit": "never"
                },
                {
                    "name" : "profile1",
                    "guid": "{6239a42c-2222-49a3-80bd-e8fdd045185c}",
                    "tabTitle" : "Profile 1",
                    "historySize": 2,
                    "closeOnExit": "never"
                },
                {
                    "name" : "profile2",
                    "guid": "{6239a42c-3333-49a3-80bd-e8fdd045185c}",
                    "tabTitle" : "Profile 2",
                    "historySize": 3,
                    "closeOnExit": "never"
                },
                {
                    "name" : "profile3",
                    "guid": "{6239a42c-4444-49a3-80bd-e8fdd045185c}",
                    "tabTitle" : "Profile 3",
                    "historySize": 4,
                    "closeOnExit": "never"
                }
            ],
            "schemes":
            [
                {
                    "name": "Campbell",
                    "foreground": "#CCCCCC",
                    "background": "#0C0C0C",
                    "cursorColor": "#FFFFFF",
                    "black": "#0C0C0C",
                    "red": "#C50F1F",
                    "green": "#13A10E",
                    "yellow": "#C19C00",
                    "blue": "#0037DA",
                    "purple": "#881798",
                    "cyan": "#3A96DD",
                    "white": "#CCCCCC",
                    "brightBlack": "#767676",
                    "brightRed": "#E74856",
                    "brightGreen": "#16C60C",
                    "brightYellow": "#F9F1A5",
                    "brightBlue": "#3B78FF",
                    "brightPurple": "#B4009E",
                    "brightCyan": "#61D6D6",
                    "brightWhite": "#F2F2F2"
                },
                {
                    "name": "Vintage",
                    "foreground": "#C0C0C0",
                    "background": "#000000",
                    "cursorColor": "#FFFFFF",
                    "black": "#000000",
                    "red": "#800000",
                    "green": "#008000",
                    "yellow": "#808000",
                    "blue": "#000080",
                    "purple": "#800080",
                    "cyan": "#008080",
                    "white": "#C0C0C0",
                    "brightBlack": "#808080",
                    "brightRed": "#FF0000",
                    "brightGreen": "#00FF00",
                    "brightYellow": "#FFFF00",
                    "brightBlue": "#0000FF",
                    "brightPurple": "#FF00FF",
                    "brightCyan": "#00FFFF",
                    "brightWhite": "#FFFFFF"
                },
                {
                    "name": "One Half Light",
                    "foreground": "#383A42",
                    "background": "#FAFAFA",
                    "cursorColor": "#4F525D",
                    "black": "#383A42",
                    "red": "#E45649",
                    "green": "#50A14F",
                    "yellow": "#C18301",
                    "blue": "#0184BC",
                    "purple": "#A626A4",
                    "cyan": "#0997B3",
                    "white": "#FAFAFA",
                    "brightBlack": "#4F525D",
                    "brightRed": "#DF6C75",
                    "brightGreen": "#98C379",
                    "brightYellow": "#E4C07A",
                    "brightBlue": "#61AFEF",
                    "brightPurple": "#C577DD",
                    "brightCyan": "#56B5C1",
                    "brightWhite": "#FFFFFF"
                }
            ]
        })" };

        CascadiaSettings settings0{ settingsJson0, {} };
        VERIFY_IS_NOT_NULL(settings0);
        settings0.WindowSettings(L"").TabPosition(position);

        const auto guid1 = Microsoft::Console::Utils::GuidFromString(L"{6239a42c-1111-49a3-80bd-e8fdd045185c}");
        const auto guid2 = Microsoft::Console::Utils::GuidFromString(L"{6239a42c-2222-49a3-80bd-e8fdd045185c}");

        // This is super wacky, but we can't just initialize the
        // com_ptr<impl::TerminalPage> in the lambda and assign it back out of
        // the lambda. We'll crash trying to get a weak_ref to the TerminalPage
        // during TerminalPage::Create() below.
        //
        // Instead, create the winrt object, then get a com_ptr to the
        // implementation _from_ the winrt object. This seems to work, even if
        // it's weird.
        winrt::com_ptr<winrt::TerminalApp::implementation::TerminalPage> page{ nullptr };
        _initializeTerminalPage(page, settings0);

        auto result = RunOnUIThread([&page]() {
            VERIFY_ARE_EQUAL(1u, page->_tabs.Size());
        });
        VERIFY_SUCCEEDED(result);

        return page;
    }

    void TabTests::TryZoomPane()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"IsolationLevel", L"Method")
        END_TEST_METHOD_PROPERTIES()

        auto page = _commonSetup();

        Log::Comment(L"Create a second pane");
        auto result = RunOnUIThread([&page]() {
            SplitPaneArgs args{ SplitType::Duplicate };
            ActionEventArgs eventArgs{ args };
            page->_HandleSplitPane(nullptr, eventArgs);
            auto firstTab = page->_GetTabImpl(page->_tabs.GetAt(0));

            VERIFY_ARE_EQUAL(2, firstTab->GetLeafPaneCount());
            VERIFY_IS_FALSE(firstTab->IsZoomed());
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(L"Zoom in on the pane");
        result = RunOnUIThread([&page]() {
            ActionEventArgs eventArgs{};
            page->_HandleTogglePaneZoom(nullptr, eventArgs);
            auto firstTab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(2, firstTab->GetLeafPaneCount());
            VERIFY_IS_TRUE(firstTab->IsZoomed());
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(L"Zoom out of the pane");
        result = RunOnUIThread([&page]() {
            ActionEventArgs eventArgs{};
            page->_HandleTogglePaneZoom(nullptr, eventArgs);
            auto firstTab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(2, firstTab->GetLeafPaneCount());
            VERIFY_IS_FALSE(firstTab->IsZoomed());
        });
        VERIFY_SUCCEEDED(result);
    }

    void TabTests::MoveFocusFromZoomedPane()
    {
        auto page = _commonSetup();

        Log::Comment(L"Create a second pane");
        auto result = RunOnUIThread([&page]() {
            // Set up action
            SplitPaneArgs args{ SplitType::Duplicate };
            ActionEventArgs eventArgs{ args };
            page->_HandleSplitPane(nullptr, eventArgs);
            auto firstTab = page->_GetTabImpl(page->_tabs.GetAt(0));

            VERIFY_ARE_EQUAL(2, firstTab->GetLeafPaneCount());
            VERIFY_IS_FALSE(firstTab->IsZoomed());
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(L"Zoom in on the pane");
        result = RunOnUIThread([&page]() {
            // Set up action
            ActionEventArgs eventArgs{};

            page->_HandleTogglePaneZoom(nullptr, eventArgs);

            auto firstTab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(2, firstTab->GetLeafPaneCount());
            VERIFY_IS_TRUE(firstTab->IsZoomed());
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(L"Move focus. We should still be zoomed.");
        result = RunOnUIThread([&page]() {
            // Set up action
            MoveFocusArgs args{ FocusDirection::Left };
            ActionEventArgs eventArgs{ args };

            page->_HandleMoveFocus(nullptr, eventArgs);

            auto firstTab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(2, firstTab->GetLeafPaneCount());
            VERIFY_IS_TRUE(firstTab->IsZoomed());
        });
        VERIFY_SUCCEEDED(result);
    }

    void TabTests::CloseZoomedPane()
    {
        auto page = _commonSetup();

        Log::Comment(L"Create a second pane");
        auto result = RunOnUIThread([&page]() {
            // Set up action
            SplitPaneArgs args{ SplitType::Duplicate };
            ActionEventArgs eventArgs{ args };
            page->_HandleSplitPane(nullptr, eventArgs);
            auto firstTab = page->_GetTabImpl(page->_tabs.GetAt(0));

            VERIFY_ARE_EQUAL(2, firstTab->GetLeafPaneCount());
            VERIFY_IS_FALSE(firstTab->IsZoomed());
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(L"Zoom in on the pane");
        result = RunOnUIThread([&page]() {
            // Set up action
            ActionEventArgs eventArgs{};

            page->_HandleTogglePaneZoom(nullptr, eventArgs);

            auto firstTab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(2, firstTab->GetLeafPaneCount());
            VERIFY_IS_TRUE(firstTab->IsZoomed());
        });
        VERIFY_SUCCEEDED(result);

        Log::Comment(L"Close Pane. This should cause us to un-zoom, and remove the second pane from the tree");
        result = RunOnUIThread([&page]() {
            // Set up action
            ActionEventArgs eventArgs{};

            page->_HandleClosePane(nullptr, eventArgs);

            auto firstTab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_IS_FALSE(firstTab->IsZoomed());
        });
        VERIFY_SUCCEEDED(result);

        // Introduce a slight delay to let the events finish propagating
        Sleep(250);

        Log::Comment(L"Check to ensure there's only one pane left.");

        result = RunOnUIThread([&page]() {
            auto firstTab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(1, firstTab->GetLeafPaneCount());
            VERIFY_IS_FALSE(firstTab->IsZoomed());
        });
        VERIFY_SUCCEEDED(result);
    }

    void TabTests::SwapPanes()
    {
        auto page = _commonSetup();

        Log::Comment(L"Setup 4 panes.");
        // Create the following layout
        // -------------------
        // |   1    |   2    |
        // |        |        |
        // -------------------
        // |   3    |   4    |
        // |        |        |
        // -------------------
        uint32_t firstId = 0, secondId = 0, thirdId = 0, fourthId = 0;
        TestOnUIThread([&]() {
            VERIFY_ARE_EQUAL(1u, page->_tabs.Size());
            auto tab = page->_GetTabImpl(page->_tabs.GetAt(0));
            firstId = tab->_activePane->Id().value();
            // We start with 1 tab, split vertically to get
            // -------------------
            // |   1    |   2    |
            // |        |        |
            // -------------------
            page->_SplitPane(nullptr, SplitDirection::Right, 0.5f, page->_MakePane(nullptr, page->_GetFocusedTab(), nullptr));
            secondId = tab->_activePane->Id().value();
        });
        Sleep(250);
        TestOnUIThread([&]() {
            // After this the `2` pane is focused, go back to `1` being focused
            page->_MoveFocus(FocusDirection::Left);
        });
        Sleep(250);
        TestOnUIThread([&]() {
            // Split again to make the 3rd tab
            // -------------------
            // |   1    |        |
            // |        |        |
            // ---------|   2    |
            // |   3    |        |
            // |        |        |
            // -------------------
            page->_SplitPane(nullptr, SplitDirection::Down, 0.5f, page->_MakePane(nullptr, page->_GetFocusedTab(), nullptr));
            auto tab = page->_GetTabImpl(page->_tabs.GetAt(0));
            // Split again to make the 3rd tab
            thirdId = tab->_activePane->Id().value();
        });
        Sleep(250);
        TestOnUIThread([&]() {
            // After this the `3` pane is focused, go back to `2` being focused
            page->_MoveFocus(FocusDirection::Right);
        });
        Sleep(250);
        TestOnUIThread([&]() {
            // Split to create the final pane
            // -------------------
            // |   1    |   2    |
            // |        |        |
            // -------------------
            // |   3    |   4    |
            // |        |        |
            // -------------------
            page->_SplitPane(nullptr, SplitDirection::Down, 0.5f, page->_MakePane(nullptr, page->_GetFocusedTab(), nullptr));
            auto tab = page->_GetTabImpl(page->_tabs.GetAt(0));
            fourthId = tab->_activePane->Id().value();
        });

        Sleep(250);
        TestOnUIThread([&]() {
            auto tab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(4, tab->GetLeafPaneCount());
            // just to be complete, make sure we actually have 4 different ids
            VERIFY_ARE_NOT_EQUAL(firstId, fourthId);
            VERIFY_ARE_NOT_EQUAL(secondId, fourthId);
            VERIFY_ARE_NOT_EQUAL(thirdId, fourthId);
            VERIFY_ARE_NOT_EQUAL(firstId, thirdId);
            VERIFY_ARE_NOT_EQUAL(secondId, thirdId);
            VERIFY_ARE_NOT_EQUAL(firstId, secondId);
        });

        // Gratuitous use of sleep to make sure that the UI has updated properly
        // after each operation.
        Sleep(250);
        // Now try to move the pane through the tree
        Log::Comment(L"Move pane to the left. This should swap panes 3 and 4");
        // -------------------
        // |   1    |   2    |
        // |        |        |
        // -------------------
        // |   4    |   3    |
        // |        |        |
        // -------------------
        TestOnUIThread([&]() {
            // Set up action
            SwapPaneArgs args{ FocusDirection::Left };
            ActionEventArgs eventArgs{ args };

            page->_HandleSwapPane(nullptr, eventArgs);
        });

        Sleep(250);

        TestOnUIThread([&]() {
            auto tab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(4, tab->GetLeafPaneCount());
            // Our currently focused pane should be `4`
            VERIFY_ARE_EQUAL(fourthId, tab->_activePane->Id().value());

            // Inspect the tree to make sure we swapped
            VERIFY_ARE_EQUAL(fourthId, tab->_rootPane->_firstChild->_secondChild->Id().value());
            VERIFY_ARE_EQUAL(thirdId, tab->_rootPane->_secondChild->_secondChild->Id().value());
        });

        Sleep(250);

        Log::Comment(L"Move pane to up. This should swap panes 1 and 4");
        // -------------------
        // |   4    |   2    |
        // |        |        |
        // -------------------
        // |   1    |   3    |
        // |        |        |
        // -------------------
        TestOnUIThread([&]() {
            // Set up action
            SwapPaneArgs args{ FocusDirection::Up };
            ActionEventArgs eventArgs{ args };

            page->_HandleSwapPane(nullptr, eventArgs);
        });

        Sleep(250);

        TestOnUIThread([&]() {
            auto tab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(4, tab->GetLeafPaneCount());
            // Our currently focused pane should be `4`
            VERIFY_ARE_EQUAL(fourthId, tab->_activePane->Id().value());

            // Inspect the tree to make sure we swapped
            VERIFY_ARE_EQUAL(fourthId, tab->_rootPane->_firstChild->_firstChild->Id().value());
            VERIFY_ARE_EQUAL(firstId, tab->_rootPane->_firstChild->_secondChild->Id().value());
        });

        Sleep(250);

        Log::Comment(L"Move pane to the right. This should swap panes 2 and 4");
        // -------------------
        // |   2    |   4    |
        // |        |        |
        // -------------------
        // |   1    |   3    |
        // |        |        |
        // -------------------
        TestOnUIThread([&]() {
            // Set up action
            SwapPaneArgs args{ FocusDirection::Right };
            ActionEventArgs eventArgs{ args };

            page->_HandleSwapPane(nullptr, eventArgs);
        });

        Sleep(250);

        TestOnUIThread([&]() {
            auto tab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(4, tab->GetLeafPaneCount());
            // Our currently focused pane should be `4`
            VERIFY_ARE_EQUAL(fourthId, tab->_activePane->Id().value());

            // Inspect the tree to make sure we swapped
            VERIFY_ARE_EQUAL(fourthId, tab->_rootPane->_secondChild->_firstChild->Id().value());
            VERIFY_ARE_EQUAL(secondId, tab->_rootPane->_firstChild->_firstChild->Id().value());
        });

        Sleep(250);

        Log::Comment(L"Move pane down. This should swap panes 3 and 4");
        // -------------------
        // |   2    |   3    |
        // |        |        |
        // -------------------
        // |   1    |   4    |
        // |        |        |
        // -------------------
        TestOnUIThread([&]() {
            // Set up action
            SwapPaneArgs args{ FocusDirection::Down };
            ActionEventArgs eventArgs{ args };

            page->_HandleSwapPane(nullptr, eventArgs);
        });

        Sleep(250);

        TestOnUIThread([&]() {
            auto tab = page->_GetTabImpl(page->_tabs.GetAt(0));
            VERIFY_ARE_EQUAL(4, tab->GetLeafPaneCount());
            // Our currently focused pane should be `4`
            VERIFY_ARE_EQUAL(fourthId, tab->_activePane->Id().value());

            // Inspect the tree to make sure we swapped
            VERIFY_ARE_EQUAL(fourthId, tab->_rootPane->_secondChild->_secondChild->Id().value());
            VERIFY_ARE_EQUAL(thirdId, tab->_rootPane->_secondChild->_firstChild->Id().value());
        });
    }

    void TabTests::NextMRUTab()
    {
        // This is a test for GH#8025 - we want to make sure that MRU tab
        // ordering works correctly and that in-order/disabled switching works.
        //
        // Note: We test MRU ordering directly rather than going through the
        // command palette tab switcher, because the palette's anchor key
        // handling auto-dismisses when no modifier keys are held (which we
        // can't simulate in the test environment).

        auto page = _commonSetup();

        Log::Comment(L"Create Tab[1]");
        TestOnUIThread([&page]() {
            NewTerminalArgs newTerminalArgs{ 1 };
            page->_OpenNewTab(newTerminalArgs);
        });
        VERIFY_ARE_EQUAL(2u, page->_tabs.Size());

        Log::Comment(L"Create Tab[2]");
        TestOnUIThread([&page]() {
            NewTerminalArgs newTerminalArgs{ 2 };
            page->_OpenNewTab(newTerminalArgs);
        });
        VERIFY_ARE_EQUAL(3u, page->_tabs.Size());

        Log::Comment(L"Create Tab[3]");
        TestOnUIThread([&page]() {
            NewTerminalArgs newTerminalArgs{ 3 };
            page->_OpenNewTab(newTerminalArgs);
        });
        VERIFY_ARE_EQUAL(4u, page->_tabs.Size());

        TestOnUIThread([&page]() {
            auto focusedIndex = page->_GetFocusedTabIndex().value_or(-1);
            VERIFY_ARE_EQUAL(3u, focusedIndex, L"Verify Tab[3] is focused");
        });

        Log::Comment(L"Select Tab[1]");
        TestOnUIThread([&page]() {
            page->_SelectTab(1);
        });

        TestOnUIThread([&page]() {
            auto focusedIndex = page->_GetFocusedTabIndex().value_or(-1);
            VERIFY_ARE_EQUAL(1u, focusedIndex, L"Verify Tab[1] is focused");
        });

        // MRU order should now be: Tab[1], Tab[3], Tab[2], Tab[0]
        // Verify the MRU list directly.
        Log::Comment(L"Verify MRU order: MRU[0]=Tab[1], MRU[1]=Tab[3]");
        TestOnUIThread([&page]() {
            VERIFY_ARE_EQUAL(4u, page->_mruTabs.Size());
            uint32_t mruIdx;
            page->_tabs.IndexOf(page->_mruTabs.GetAt(0), mruIdx);
            VERIFY_ARE_EQUAL(1u, mruIdx, L"MRU[0] should be Tab[1] (most recent)");
            page->_tabs.IndexOf(page->_mruTabs.GetAt(1), mruIdx);
            VERIFY_ARE_EQUAL(3u, mruIdx, L"MRU[1] should be Tab[3] (last tab added)");
        });

        Log::Comment(L"Select MRU[1]=Tab[3] directly");
        TestOnUIThread([&page]() {
            // The next MRU tab after Tab[1] is Tab[3]
            uint32_t nextMruIdx;
            page->_tabs.IndexOf(page->_mruTabs.GetAt(1), nextMruIdx);
            page->_SelectTab(nextMruIdx);
        });

        TestOnUIThread([&page]() {
            auto focusedIndex = page->_GetFocusedTabIndex().value_or(-1);
            VERIFY_ARE_EQUAL(3u, focusedIndex, L"Verify Tab[3] is focused");
        });

        Log::Comment(L"Select MRU[1]=Tab[1] directly");
        TestOnUIThread([&page]() {
            uint32_t nextMruIdx;
            page->_tabs.IndexOf(page->_mruTabs.GetAt(1), nextMruIdx);
            page->_SelectTab(nextMruIdx);
        });

        TestOnUIThread([&page]() {
            auto focusedIndex = page->_GetFocusedTabIndex().value_or(-1);
            VERIFY_ARE_EQUAL(1u, focusedIndex, L"Verify Tab[1] is focused");
        });

        // The Disabled tab switcher mode uses direct index-based switching
        // without the command palette, so it works in the test environment.
        Log::Comment(L"Change the tab switch order to not use the tab switcher (which is in-order always)");
        page->_settings.WindowSettingsDefaults().TabSwitcherMode(TabSwitcherMode::Disabled);

        Log::Comment(L"Switch to the next in-order tab: Tab[2]");
        TestOnUIThread([&page]() {
            page->_SelectNextTab(true, nullptr);
        });
        TestOnUIThread([&page]() {
            auto focusedIndex = page->_GetFocusedTabIndex().value_or(-1);
            VERIFY_ARE_EQUAL(2u, focusedIndex, L"Verify Tab[2] is focused");
        });

        Log::Comment(L"Switch to the next in-order tab: Tab[3]");
        TestOnUIThread([&page]() {
            page->_SelectNextTab(true, nullptr);
        });
        TestOnUIThread([&page]() {
            auto focusedIndex = page->_GetFocusedTabIndex().value_or(-1);
            VERIFY_ARE_EQUAL(3u, focusedIndex, L"Verify Tab[3] is focused");
        });
    }

    void TabTests::VerifyCommandPaletteTabSwitcherOrder()
    {
        // This is a test for GH#8188 - we want to make sure that the MRU
        // ordering is correctly maintained as tabs are selected.
        //
        // Note: We verify MRU ordering directly rather than going through
        // the command palette tab switcher, because the palette's anchor key
        // handling auto-dismisses when no modifier keys are held (which we
        // can't simulate in the test environment).

        auto page = _commonSetup();

        Log::Comment(L"Create 3 additional tabs");
        RunOnUIThread([&page]() {
            NewTerminalArgs newTerminalArgs{ 1 };
            page->_OpenNewTab(newTerminalArgs);
            page->_OpenNewTab(newTerminalArgs);
            page->_OpenNewTab(newTerminalArgs);
        });
        VERIFY_ARE_EQUAL(4u, page->_mruTabs.Size());

        Log::Comment(L"give alphabetical names to all tabs");
        TestOnUIThread([&page]() {
            page->_GetTabImpl(page->_tabs.GetAt(0))->Title(L"a");
        });
        TestOnUIThread([&page]() {
            page->_GetTabImpl(page->_tabs.GetAt(1))->Title(L"b");
        });
        TestOnUIThread([&page]() {
            page->_GetTabImpl(page->_tabs.GetAt(2))->Title(L"c");
        });
        TestOnUIThread([&page]() {
            page->_GetTabImpl(page->_tabs.GetAt(3))->Title(L"d");
        });

        TestOnUIThread([&page]() {
            Log::Comment(L"Sanity check the titles of our tabs are what we set them to.");

            VERIFY_ARE_EQUAL(L"a", page->_tabs.GetAt(0).Title());
            VERIFY_ARE_EQUAL(L"b", page->_tabs.GetAt(1).Title());
            VERIFY_ARE_EQUAL(L"c", page->_tabs.GetAt(2).Title());
            VERIFY_ARE_EQUAL(L"d", page->_tabs.GetAt(3).Title());

            // MRU order after creating Tab[0]-Tab[3]: MRU[0]=Tab[3], MRU[3]=Tab[0]
            VERIFY_ARE_EQUAL(L"d", page->_mruTabs.GetAt(0).Title());
            VERIFY_ARE_EQUAL(L"c", page->_mruTabs.GetAt(1).Title());
            VERIFY_ARE_EQUAL(L"b", page->_mruTabs.GetAt(2).Title());
            VERIFY_ARE_EQUAL(L"a", page->_mruTabs.GetAt(3).Title());
        });

        Log::Comment(L"Select Tab[0] through Tab[3] to establish MRU order");
        RunOnUIThread([&page]() {
            page->_UpdatedSelectedTab(page->_tabs.GetAt(0));
            page->_UpdatedSelectedTab(page->_tabs.GetAt(1));
            page->_UpdatedSelectedTab(page->_tabs.GetAt(2));
            page->_UpdatedSelectedTab(page->_tabs.GetAt(3));
        });

        Log::Comment(L"Verify MRU order: MRU[0]='d', MRU[1]='c', MRU[2]='b', MRU[3]='a'");
        VERIFY_ARE_EQUAL(4u, page->_mruTabs.Size());
        VERIFY_ARE_EQUAL(L"d", page->_mruTabs.GetAt(0).Title());
        VERIFY_ARE_EQUAL(L"c", page->_mruTabs.GetAt(1).Title());
        VERIFY_ARE_EQUAL(L"b", page->_mruTabs.GetAt(2).Title());
        VERIFY_ARE_EQUAL(L"a", page->_mruTabs.GetAt(3).Title());

        Log::Comment(L"Select Tab[2]='c' (MRU[1] after 'd')");
        TestOnUIThread([&page]() {
            page->_SelectTab(2);
        });

        Log::Comment(L"Verify MRU order updated: MRU[0]='c', MRU[1]='d', MRU[2]='b', MRU[3]='a'");
        TestOnUIThread([&page]() {
            VERIFY_ARE_EQUAL(L"c", page->_mruTabs.GetAt(0).Title());
            VERIFY_ARE_EQUAL(L"d", page->_mruTabs.GetAt(1).Title());
            VERIFY_ARE_EQUAL(L"b", page->_mruTabs.GetAt(2).Title());
            VERIFY_ARE_EQUAL(L"a", page->_mruTabs.GetAt(3).Title());
        });
    }

    void TabTests::TestWindowRenameSuccessful()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"IsolationLevel", L"Method")
        END_TEST_METHOD_PROPERTIES()

        auto page = _commonSetup();
        page->RenameWindowRequested([&page, this](auto&&, const winrt::TerminalApp::RenameWindowRequestedArgs args) {
            // In the real terminal, this would bounce up to the monarch and
            // come back down. Instead, immediately call back and set the name.
            //
            // This replicates how TerminalWindow works
            _windowProperties->WindowName(args.ProposedName());
        });

        auto windowNameChanged = false;
        _windowProperties->PropertyChanged([&page, &windowNameChanged](auto&&, const winrt::WUX::Data::PropertyChangedEventArgs& args) mutable {
            if (args.PropertyName() == L"WindowNameForDisplay")
            {
                windowNameChanged = true;
            }
        });

        TestOnUIThread([&page]() {
            page->_RequestWindowRename(winrt::hstring{ L"Foo" });
        });
        TestOnUIThread([&]() {
            VERIFY_ARE_EQUAL(L"Foo", page->WindowProperties().WindowName());
            VERIFY_IS_TRUE(windowNameChanged,
                           L"The window name should have changed, and we should have raised a notification that WindowNameForDisplay changed");
        });
    }
    void TabTests::TestWindowRenameFailure()
    {
        BEGIN_TEST_METHOD_PROPERTIES()
            TEST_METHOD_PROPERTY(L"IsolationLevel", L"Method")
        END_TEST_METHOD_PROPERTIES()

        auto page = _commonSetup();
        auto windowNameChanged = false;

        page->PropertyChanged([&page, &windowNameChanged](auto&&, const winrt::WUX::Data::PropertyChangedEventArgs& args) mutable {
            if (args.PropertyName() == L"WindowNameForDisplay")
            {
                windowNameChanged = true;
            }
        });

        TestOnUIThread([&page]() {
            page->_RequestWindowRename(winrt::hstring{ L"Foo" });
        });
        TestOnUIThread([&]() {
            VERIFY_IS_FALSE(windowNameChanged,
                            L"The window name should not have changed, we should have rejected the change.");
        });
    }

    static til::color _getControlBackgroundColor(winrt::TerminalApp::implementation::ContentManager* contentManager,
                                                 const winrt::Microsoft::Terminal::Control::TermControl& c)
    {
        auto interactivity{ contentManager->TryLookupCore(c.ContentId()) };
        VERIFY_IS_NOT_NULL(interactivity);
        const auto core{ interactivity.Core() };
        return til::color{ core.BackgroundColor() };
    }

    void TabTests::TestPreviewCommitScheme()
    {
        Log::Comment(L"Preview a color scheme. Make sure it's applied, then committed accordingly");

        auto page = _commonSetup();
        VERIFY_IS_NOT_NULL(page);

        TestOnUIThread([&page, this]() {
            const auto& activeControl{ page->_GetActiveControl() };
            VERIFY_IS_NOT_NULL(activeControl);

            const auto backgroundColor{ _getControlBackgroundColor(_contentManager.get(), activeControl) };
            VERIFY_ARE_EQUAL(til::color{ 0xff0c0c0c }, backgroundColor);
        });

        TestOnUIThread([&page]() {
            Log::Comment(L"Emulate previewing the SetColorScheme action");
            SetColorSchemeArgs args{ L"Vintage" };
            ActionAndArgs actionAndArgs{ ShortcutAction::SetColorScheme, args };
            page->_PreviewAction(actionAndArgs);
        });

        TestOnUIThread([&page, this]() {
            const auto& activeControl{ page->_GetActiveControl() };
            VERIFY_IS_NOT_NULL(activeControl);

            Log::Comment(L"Color should be changed to the preview");
            const auto backgroundColor{ _getControlBackgroundColor(_contentManager.get(), activeControl) };
            VERIFY_ARE_EQUAL(til::color{ 0xff000000 }, backgroundColor);

            // And we should have stored a function to revert the change.
            VERIFY_ARE_EQUAL(1u, page->_restorePreviewFuncs.size());
        });

        TestOnUIThread([&page]() {
            Log::Comment(L"Emulate committing the SetColorScheme action");

            SetColorSchemeArgs args{ L"Vintage" };
            page->_EndPreview();
            page->_HandleSetColorScheme(nullptr, ActionEventArgs{ args });
        });

        TestOnUIThread([&page, this]() {
            const auto& activeControl{ page->_GetActiveControl() };
            VERIFY_IS_NOT_NULL(activeControl);

            Log::Comment(L"Color should be changed");
            const auto backgroundColor{ _getControlBackgroundColor(_contentManager.get(), activeControl) };
            VERIFY_ARE_EQUAL(til::color{ 0xff000000 }, backgroundColor);

            // After preview there should be no more restore functions to execute.
            VERIFY_ARE_EQUAL(0u, page->_restorePreviewFuncs.size());
        });

        Log::Comment(L"Sleep to let events propagate");
        // If you don't do this, we will _sometimes_ crash as we're tearing down
        // the control from this test as we start the next one. We crash
        // somewhere in the CursorPositionChanged handler. It's annoying, but
        // this works.
        Sleep(250);
    }

    void TabTests::TestPreviewDismissScheme()
    {
        Log::Comment(L"Preview a color scheme. Make sure it's applied, then dismissed accordingly");

        auto page = _commonSetup();
        VERIFY_IS_NOT_NULL(page);

        TestOnUIThread([&page, this]() {
            const auto& activeControl{ page->_GetActiveControl() };
            VERIFY_IS_NOT_NULL(activeControl);

            const auto backgroundColor{ _getControlBackgroundColor(_contentManager.get(), activeControl) };
            VERIFY_ARE_EQUAL(til::color{ 0xff0c0c0c }, backgroundColor);
        });

        TestOnUIThread([&page]() {
            Log::Comment(L"Emulate previewing the SetColorScheme action");
            SetColorSchemeArgs args{ L"Vintage" };
            ActionAndArgs actionAndArgs{ ShortcutAction::SetColorScheme, args };
            page->_PreviewAction(actionAndArgs);
        });

        TestOnUIThread([&page, this]() {
            const auto& activeControl{ page->_GetActiveControl() };
            VERIFY_IS_NOT_NULL(activeControl);

            Log::Comment(L"Color should be changed to the preview");
            const auto backgroundColor{ _getControlBackgroundColor(_contentManager.get(), activeControl) };
            VERIFY_ARE_EQUAL(til::color{ 0xff000000 }, backgroundColor);
        });

        TestOnUIThread([&page]() {
            Log::Comment(L"Emulate dismissing the SetColorScheme action");
            page->_EndPreview();
        });

        TestOnUIThread([&page, this]() {
            const auto& activeControl{ page->_GetActiveControl() };
            VERIFY_IS_NOT_NULL(activeControl);

            Log::Comment(L"Color should be the same as it originally was");
            const auto backgroundColor{ _getControlBackgroundColor(_contentManager.get(), activeControl) };
            VERIFY_ARE_EQUAL(til::color{ 0xff0c0c0c }, backgroundColor);
        });
        Log::Comment(L"Sleep to let events propagate");
        Sleep(250);
    }

    void TabTests::TestPreviewSchemeWhilePreviewing()
    {
        Log::Comment(L"Preview a color scheme, then preview another scheme. ");

        Log::Comment(L"Preview a color scheme. Make sure it's applied, then committed accordingly");

        auto page = _commonSetup();
        VERIFY_IS_NOT_NULL(page);

        TestOnUIThread([&page, this]() {
            const auto& activeControl{ page->_GetActiveControl() };
            VERIFY_IS_NOT_NULL(activeControl);

            const auto backgroundColor{ _getControlBackgroundColor(_contentManager.get(), activeControl) };
            VERIFY_ARE_EQUAL(til::color{ 0xff0c0c0c }, backgroundColor);
        });

        TestOnUIThread([&page]() {
            Log::Comment(L"Emulate previewing the SetColorScheme action");
            SetColorSchemeArgs args{ L"Vintage" };
            page->_PreviewColorScheme(args);
        });

        TestOnUIThread([&page, this]() {
            const auto& activeControl{ page->_GetActiveControl() };
            VERIFY_IS_NOT_NULL(activeControl);

            Log::Comment(L"Color should be changed to the preview");
            const auto backgroundColor{ _getControlBackgroundColor(_contentManager.get(), activeControl) };
            VERIFY_ARE_EQUAL(til::color{ 0xff000000 }, backgroundColor);
        });

        TestOnUIThread([&page]() {
            Log::Comment(L"Now, preview another scheme");
            SetColorSchemeArgs args{ L"One Half Light" };
            page->_PreviewColorScheme(args);
        });

        TestOnUIThread([&page, this]() {
            const auto& activeControl{ page->_GetActiveControl() };
            VERIFY_IS_NOT_NULL(activeControl);

            Log::Comment(L"Color should be changed to the preview");
            const auto backgroundColor{ _getControlBackgroundColor(_contentManager.get(), activeControl) };
            VERIFY_ARE_EQUAL(til::color{ 0xffFAFAFA }, backgroundColor);
        });

        TestOnUIThread([&page]() {
            Log::Comment(L"Emulate committing the SetColorScheme action");

            SetColorSchemeArgs args{ L"One Half Light" };
            page->_EndPreview();
            page->_HandleSetColorScheme(nullptr, ActionEventArgs{ args });
        });

        TestOnUIThread([&page, this]() {
            const auto& activeControl{ page->_GetActiveControl() };
            VERIFY_IS_NOT_NULL(activeControl);

            Log::Comment(L"Color should be changed");
            const auto backgroundColor{ _getControlBackgroundColor(_contentManager.get(), activeControl) };
            VERIFY_ARE_EQUAL(til::color{ 0xffFAFAFA }, backgroundColor);
        });
        Log::Comment(L"Sleep to let events propagate");
        Sleep(250);
    }

    void TabTests::TestClampSwitchToTab()
    {
        Log::Comment(L"Test that switching to a tab index higher than the number of tabs just clamps to the last tab.");

        auto page = _commonSetup();
        VERIFY_IS_NOT_NULL(page);

        Log::Comment(L"Create a second tab");
        TestOnUIThread([&page]() {
            NewTerminalArgs newTerminalArgs{ 1 };
            page->_OpenNewTab(newTerminalArgs);
        });
        VERIFY_ARE_EQUAL(2u, page->_tabs.Size());

        Log::Comment(L"Create a third tab");
        TestOnUIThread([&page]() {
            NewTerminalArgs newTerminalArgs{ 2 };
            page->_OpenNewTab(newTerminalArgs);
        });
        VERIFY_ARE_EQUAL(3u, page->_tabs.Size());

        TestOnUIThread([&page]() {
            auto focusedTabIndexOpt{ page->_GetFocusedTabIndex() };
            VERIFY_IS_TRUE(focusedTabIndexOpt.has_value());
            VERIFY_ARE_EQUAL(2u, focusedTabIndexOpt.value());
        });

        TestOnUIThread([&page]() {
            Log::Comment(L"Switch to the first tab");
            page->_SelectTab(0);
        });

        TestOnUIThread([&page]() {
            auto focusedTabIndexOpt{ page->_GetFocusedTabIndex() };

            VERIFY_IS_TRUE(focusedTabIndexOpt.has_value());
            VERIFY_ARE_EQUAL(0u, focusedTabIndexOpt.value());
        });

        TestOnUIThread([&page]() {
            Log::Comment(L"Switch to the tab 6, which is greater than number of tabs. This should switch to the third tab");
            page->_SelectTab(6);
        });

        TestOnUIThread([&page]() {
            auto focusedTabIndexOpt{ page->_GetFocusedTabIndex() };
            VERIFY_IS_TRUE(focusedTabIndexOpt.has_value());
            VERIFY_ARE_EQUAL(2u, focusedTabIndexOpt.value());
        });
    }

}

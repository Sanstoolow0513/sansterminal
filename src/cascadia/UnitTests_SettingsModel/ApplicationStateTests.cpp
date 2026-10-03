// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"

#include "../TerminalSettingsModel/ApplicationState.h"

using namespace Microsoft::Console;
using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace SettingsModelUnitTests
{
    // Covers the workspace-persistence APIs added to ApplicationState:
    //   SaveWorkspace / RemoveWorkspace / RenameWorkspace / TakeWorkspace /
    //   AllPersistedWorkspaces.
    // All tests operate on a throw-away ApplicationState instance pointed at
    // a temp directory, so they don't touch the real user state.
    class ApplicationStateTests
    {
        TEST_CLASS(ApplicationStateTests);

        TEST_METHOD(SaveAndLookupWorkspace);
        TEST_METHOD(RemoveWorkspaceReturnsFalseWhenMissing);
        TEST_METHOD(RenameWorkspaceMigratesEntry);
        TEST_METHOD(RenameWorkspaceNoOpForEmptyOrEqualNames);
        TEST_METHOD(RenameWorkspaceNoOpForMissingEntry);
        TEST_METHOD(TakeWorkspaceRemovesAndReturns);
        TEST_METHOD(TakeWorkspaceReturnsNullWhenMissing);
        TEST_METHOD(RecentWorkspacesSurviveSerialization);
        TEST_METHOD(LegacyWorkspacesBecomeRecent);
        TEST_METHOD(ExplicitRecentWorkspacesAreNotRepopulated);

    private:
        static std::filesystem::path _tempRoot()
        {
            auto root = std::filesystem::temp_directory_path() / L"WT_ApplicationStateTests";
            std::error_code ec;
            std::filesystem::create_directories(root, ec);
            // Best-effort clean of any leftover state.json from a prior run so
            // tests see an empty starting point.
            std::filesystem::remove(root / L"state.json", ec);
            std::filesystem::remove(root / L"elevated-state.json", ec);
            return root;
        }

        static winrt::com_ptr<implementation::ApplicationState> _make()
        {
            return winrt::make_self<implementation::ApplicationState>(_tempRoot());
        }

        static WindowLayout _makeLayout()
        {
            WindowLayout layout;
            layout.TabLayout(winrt::single_threaded_vector<ActionAndArgs>());
            return layout;
        }
    };

    void ApplicationStateTests::SaveAndLookupWorkspace()
    {
        auto state = _make();
        const auto layout = _makeLayout();
        state->SaveWorkspace(L"win1", layout);

        const auto all = state->AllPersistedWorkspaces();
        VERIFY_IS_NOT_NULL(all);
        VERIFY_IS_TRUE(all.HasKey(L"win1"));
    }

    void ApplicationStateTests::RemoveWorkspaceReturnsFalseWhenMissing()
    {
        auto state = _make();
        VERIFY_IS_FALSE(state->RemoveWorkspace(L"does-not-exist"));

        state->SaveWorkspace(L"win1", _makeLayout());
        VERIFY_IS_TRUE(state->RemoveWorkspace(L"win1"));
        VERIFY_IS_FALSE(state->RemoveWorkspace(L"win1"));
    }

    void ApplicationStateTests::RenameWorkspaceMigratesEntry()
    {
        auto state = _make();
        state->SaveWorkspace(L"oldName", _makeLayout());

        VERIFY_IS_TRUE(state->RenameWorkspace(L"oldName", L"newName"));

        const auto all = state->AllPersistedWorkspaces();
        VERIFY_IS_NOT_NULL(all);
        VERIFY_IS_FALSE(all.HasKey(L"oldName"));
        VERIFY_IS_TRUE(all.HasKey(L"newName"));
    }

    void ApplicationStateTests::RenameWorkspaceNoOpForEmptyOrEqualNames()
    {
        auto state = _make();
        state->SaveWorkspace(L"win1", _makeLayout());

        VERIFY_IS_FALSE(state->RenameWorkspace(L"win1", L"win1"));
        VERIFY_IS_FALSE(state->RenameWorkspace(L"", L"win2"));

        // Renaming to an empty name removes the stale entry under the old name.
        VERIFY_IS_TRUE(state->RenameWorkspace(L"win1", L""));
        const auto all = state->AllPersistedWorkspaces();
        if (all)
        {
            VERIFY_IS_FALSE(all.HasKey(L"win1"));
            VERIFY_IS_FALSE(all.HasKey(L""));
        }

        // Calling again is now a no-op because the entry is gone.
        VERIFY_IS_FALSE(state->RenameWorkspace(L"win1", L""));
    }

    void ApplicationStateTests::RenameWorkspaceNoOpForMissingEntry()
    {
        auto state = _make();
        VERIFY_IS_FALSE(state->RenameWorkspace(L"missing", L"newName"));
    }

    void ApplicationStateTests::TakeWorkspaceRemovesAndReturns()
    {
        auto state = _make();
        state->SaveWorkspace(L"win1", _makeLayout());

        const auto taken = state->TakeWorkspace(L"win1");
        VERIFY_IS_NOT_NULL(taken);

        // Subsequent Take for the same name must return null.
        // This is the atomicity guarantee the startup path relies on.
        VERIFY_IS_NULL(state->TakeWorkspace(L"win1"));
    }

    void ApplicationStateTests::TakeWorkspaceReturnsNullWhenMissing()
    {
        auto state = _make();
        VERIFY_IS_NULL(state->TakeWorkspace(L"missing"));
    }

    void ApplicationStateTests::RecentWorkspacesSurviveSerialization()
    {
        auto state = _make();
        state->RecordRecentWorkspace(L"first");
        state->RecordRecentWorkspace(LR"(C:\Projects\sample)");
        state->RecordRecentWorkspace(L"first");

        auto recent = state->AllRecentWorkspaces();
        VERIFY_ARE_EQUAL(2u, recent.Size());
        VERIFY_IS_TRUE(recent.GetAt(0) == L"first");
        VERIFY_IS_TRUE(recent.GetAt(1) == LR"(C:\Projects\sample)");

        const auto saved = state->ToJson(implementation::FileSource::Local);
        state->ForgetRecentWorkspace(L"first");
        state->FromJson(saved, implementation::FileSource::Local);
        recent = state->AllRecentWorkspaces();
        VERIFY_ARE_EQUAL(2u, recent.Size());
        VERIFY_IS_TRUE(recent.GetAt(0) == L"first");
        VERIFY_IS_TRUE(recent.GetAt(1) == LR"(C:\Projects\sample)");

        VERIFY_IS_TRUE(state->ForgetRecentWorkspace(L"first"));
        VERIFY_IS_FALSE(state->ForgetRecentWorkspace(L"first"));
        VERIFY_ARE_EQUAL(1u, state->AllRecentWorkspaces().Size());
    }

    void ApplicationStateTests::LegacyWorkspacesBecomeRecent()
    {
        auto state = _make();
        state->SaveWorkspace(L"legacy", _makeLayout());
        auto legacy = state->ToJson(implementation::FileSource::Local);
        legacy.removeMember("recentWorkspaces");
        state->FromJson(legacy, implementation::FileSource::Local);

        auto recent = state->AllRecentWorkspaces();
        VERIFY_ARE_EQUAL(1u, recent.Size());
        VERIFY_IS_TRUE(recent.GetAt(0) == L"legacy");

        // Recording a new workspace before the hub is shown must preserve the migration.
        state->RecordRecentWorkspace(L"new");
        recent = state->AllRecentWorkspaces();
        VERIFY_ARE_EQUAL(2u, recent.Size());
        VERIFY_IS_TRUE(recent.GetAt(0) == L"new");
        VERIFY_IS_TRUE(recent.GetAt(1) == L"legacy");

        VERIFY_IS_TRUE(state->ForgetRecentWorkspace(L"legacy"));
        VERIFY_IS_TRUE(state->ForgetRecentWorkspace(L"new"));
        const auto saved = state->ToJson(implementation::FileSource::Local);
        VERIFY_IS_TRUE(saved["recentWorkspaces"].isArray());
        state->FromJson(saved, implementation::FileSource::Local);
        VERIFY_ARE_EQUAL(0u, state->AllRecentWorkspaces().Size());
        VERIFY_IS_TRUE(state->AllPersistedWorkspaces().HasKey(L"legacy"));
    }

    void ApplicationStateTests::ExplicitRecentWorkspacesAreNotRepopulated()
    {
        auto state = _make();
        state->SaveWorkspace(L"hidden", _makeLayout());
        state->RecordRecentWorkspace(L"visible");
        auto saved = state->ToJson(implementation::FileSource::Local);
        state->FromJson(saved, implementation::FileSource::Local);
        const auto recent = state->AllRecentWorkspaces();
        VERIFY_ARE_EQUAL(1u, recent.Size());
        VERIFY_IS_TRUE(recent.GetAt(0) == L"visible");

        saved["recentWorkspaces"] = Json::Value{ Json::arrayValue };
        state->FromJson(saved, implementation::FileSource::Local);
        VERIFY_ARE_EQUAL(0u, state->AllRecentWorkspaces().Size());
        // Reading shared state cannot reintroduce local workspace history.
        saved.removeMember("recentWorkspaces");
        state->FromJson(saved, implementation::FileSource::Shared);
        VERIFY_ARE_EQUAL(0u, state->AllRecentWorkspaces().Size());
    }
}

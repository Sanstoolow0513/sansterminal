// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#include "../src/cascadia/TerminalSettingsModel/WorkspaceLayout.h"

#include <deque>
#include <iostream>
#include <set>

using namespace Sansterminal::WorkspaceLayout;

static void Require(const bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

static std::wstring Shape(const Layout& layout)
{
    auto result = layout.Serialize();
    size_t offset = 0;
    while ((offset = result.find(L"\"ratio\":", offset)) != std::wstring::npos)
    {
        offset += 8;
        const auto end = result.find(L',', offset);
        result.replace(offset, end - offset, L"_");
    }
    return result;
}

static bool Near(const double a, const double b)
{
    return std::abs(a - b) < 0.001;
}

static void CheckGeometry(const Layout& layout, const Rect area, const unsigned int mask)
{
    const std::array<bool, 3> visible{ (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0 };
    const auto geometry = layout.Evaluate(visible, area);
    Require(geometry.visible == visible, "Independent pane visibility was not preserved");
    const auto count = static_cast<size_t>(std::count(visible.begin(), visible.end(), true));
    Require(geometry.splits.size() == (count == 0 ? 0 : count - 1), "Hidden branches leave unnecessary dividers");
    for (size_t pane = 0; pane < visible.size(); ++pane)
    {
        if (!visible[pane])
        {
            continue;
        }
        const auto& rect = geometry.panes[pane];
        Require(std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) && std::isfinite(rect.height), "Non-finite pane geometry");
        Require(rect.width >= 0 && rect.height >= 0, "Negative pane size");
        Require(rect.x >= area.x && rect.y >= area.y && rect.x + rect.width <= area.x + area.width + 0.001 && rect.y + rect.height <= area.y + area.height + 0.001, "Pane extends outside workspace");
        if (count == 1)
        {
            Require(Near(rect.x, area.x) && Near(rect.y, area.y) && Near(rect.width, area.width) && Near(rect.height, area.height), "Sole visible pane does not fill workspace");
        }
        for (size_t other = pane + 1; other < visible.size(); ++other)
        {
            if (!visible[other])
            {
                continue;
            }
            const auto& second = geometry.panes[other];
            const auto overlapWidth = std::min(rect.x + rect.width, second.x + second.width) - std::max(rect.x, second.x);
            const auto overlapHeight = std::min(rect.y + rect.height, second.y + second.height) - std::max(rect.y, second.y);
            Require(overlapWidth <= 0.001 || overlapHeight <= 0.001, "Workspace panes overlap");
        }
    }
}

int main()
{
    try
    {
        std::set<std::wstring> seen;
        std::deque<Layout> pending{ Layout::Default() };
        size_t geometryChecks = 0;
        while (!pending.empty())
        {
            auto layout = std::move(pending.front());
            pending.pop_front();
            if (!seen.insert(Shape(layout)).second)
            {
                continue;
            }
            Require(Layout::Parse(layout.Serialize()).Serialize() == layout.Serialize(), "Layout serialization loses a node or ratio");
            for (const auto area : { Rect{ 17, 29, 1200, 760 }, Rect{ 0, 0, 360, 220 }, Rect{ 0, 0, 5, 3 }, Rect{} })
            {
                for (unsigned int mask = 0; mask < 8; ++mask)
                {
                    CheckGeometry(layout, area, mask);
                    ++geometryChecks;
                }
            }
            for (unsigned int source = 0; source < 3; ++source)
            {
                for (unsigned int target = 0; target < 3; ++target)
                {
                    for (unsigned int side = 0; side < 4; ++side)
                    {
                        const auto originalTree = layout.Serialize();
                        auto moved = layout;
                        Require(moved.Move(static_cast<Pane>(source), static_cast<Pane>(target), static_cast<Side>(side)) == (source != target), "Move accepted self-target or rejected a valid composition");
                        Require(layout.Serialize() == originalTree, "Moving a clone corrupted the original tree");
                        if (source != target)
                        {
                            pending.push_back(std::move(moved));
                        }
                    }
                }
            }
        }
        Require(seen.size() == 48, "Relative composition cannot reach all 48 oriented three-pane trees");

        auto layout = Layout::Default();
        const auto original = layout.Serialize();
        Require(!layout.SetRatio("", 0) && !layout.SetRatio("", 1) && !layout.SetRatio("bad", 0.5) && !layout.SetRatio("0", 0.5), "Invalid divider update was accepted");
        Require(layout.Serialize() == original, "Rejected divider update modified layout");
        Require(layout.SetRatio("1", 0.7), "Nested divider ratio cannot be updated");
        const auto geometry = layout.Evaluate({ false, true, true }, { 0, 0, 1000, 500 });
        Require(geometry.splits.size() == 1 && geometry.splits.front().path == "1", "Hiding parent sibling lost stable nested divider identity");
        Require(geometry.panes[1].width > geometry.panes[2].width, "Nested resize was not applied after hiding another pane");
        std::cout << "PASS: all 48 layouts, " << geometryChecks << " geometry/visibility cases, serialization, independent clones and nested resize\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

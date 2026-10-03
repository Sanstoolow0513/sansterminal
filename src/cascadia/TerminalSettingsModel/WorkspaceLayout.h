// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#pragma once

#include "WorkspaceLayoutDefaults.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Json
{
    class Value;
}

// A shared value model for the settings preview and live workspace. It owns no
// controls, documents or terminal connections; hiding a leaf only changes geometry.
namespace Sansterminal::WorkspaceLayout
{
    enum class Pane
    {
        Files,
        Terminal,
        Editor
    };
    enum class Side
    {
        Left,
        Right,
        Above,
        Below
    };
    enum class Direction
    {
        Row,
        Column
    };
    inline constexpr double Gutter = 6.0;

    struct Rect
    {
        double x{};
        double y{};
        double width{};
        double height{};
    };

    struct Split
    {
        std::string path;
        Rect rect;
        Direction direction;
        double ratio;
    };

    struct Geometry
    {
        std::array<Rect, 3> panes{};
        std::array<bool, 3> visible{};
        std::vector<Split> splits;
    };

    class Layout
    {
        struct Node
        {
            std::optional<Pane> pane;
            Direction direction{ Direction::Row };
            double ratio{ 0.5 };
            std::unique_ptr<Node> first;
            std::unique_ptr<Node> second;

            Node() = default;
            explicit Node(const Pane value) : pane{ value } {}
            Node(const Node& other) :
                pane{ other.pane }, direction{ other.direction }, ratio{ other.ratio }, first{ other.first ? std::make_unique<Node>(*other.first) : nullptr }, second{ other.second ? std::make_unique<Node>(*other.second) : nullptr } {}
        };

    public:
        Layout() : Layout{ Parse(DefaultJson) } {}
        Layout(const Layout& other) : _root{ std::make_unique<Node>(*other._root) } {}
        Layout(Layout&&) noexcept = default;
        Layout& operator=(Layout&&) noexcept = default;
        Layout& operator=(const Layout& other)
        {
            if (this != &other)
                _root = std::make_unique<Node>(*other._root);
            return *this;
        }

        static Layout Default() { return Parse(DefaultJson); }
        static Layout Parse(std::wstring_view value);
        static Layout FromJson(const Json::Value& value);
        Json::Value ToJson() const;
        std::wstring Serialize() const;

        bool Move(const Pane source, const Pane target, const Side side)
        {
            if (source == target || static_cast<unsigned int>(source) >= 3 || static_cast<unsigned int>(target) >= 3 || static_cast<unsigned int>(side) >= 4)
                return false;
            auto updated = *this;
            Remove(updated._root, source);
            if (!Insert(updated._root, source, target, side))
                return false;
            *this = std::move(updated);
            return true;
        }

        bool SetRatio(const std::string_view path, const double ratio)
        {
            if (!std::isfinite(ratio) || ratio <= 0 || ratio >= 1)
                return false;
            auto node = _root.get();
            for (const auto step : path)
            {
                if (!node || node->pane || (step != '0' && step != '1'))
                    return false;
                node = step == '0' ? node->first.get() : node->second.get();
            }
            if (!node || node->pane)
                return false;
            node->ratio = ratio;
            return true;
        }

        Geometry Evaluate(const std::array<bool, 3>& visible, Rect area) const
        {
            Geometry geometry;
            area.width = std::max(0.0, area.width);
            area.height = std::max(0.0, area.height);
            Place(*_root, visible, area, {}, geometry);
            return geometry;
        }

    private:
        std::unique_ptr<Node> _root;
        explicit Layout(std::unique_ptr<Node> root) : _root{ std::move(root) } {}

        static void Write(const Node& node, std::wstring& value);
        static std::unique_ptr<Node> ReadNode(const Json::Value& value, size_t depth, unsigned int& panes);
        static Json::Value WriteNode(const Node& node);

        static bool Remove(std::unique_ptr<Node>& node, const Pane pane)
        {
            if (node->pane)
            {
                if (*node->pane != pane)
                    return false;
                node.reset();
                return true;
            }
            if (!Remove(node->first, pane) && !Remove(node->second, pane))
                return false;
            if (!node->first)
            {
                auto remaining = std::move(node->second);
                node = std::move(remaining);
            }
            else if (!node->second)
            {
                auto remaining = std::move(node->first);
                node = std::move(remaining);
            }
            return true;
        }

        static bool Insert(std::unique_ptr<Node>& node, const Pane source, const Pane target, const Side side)
        {
            if (node->pane)
            {
                if (*node->pane != target)
                    return false;
                auto split = std::make_unique<Node>();
                split->direction = side == Side::Left || side == Side::Right ? Direction::Row : Direction::Column;
                const bool before = side == Side::Left || side == Side::Above;
                split->first = before ? std::make_unique<Node>(source) : std::move(node);
                split->second = before ? std::move(node) : std::make_unique<Node>(source);
                node = std::move(split);
                return true;
            }
            return Insert(node->first, source, target, side) || Insert(node->second, source, target, side);
        }

        static bool Visible(const Node& node, const std::array<bool, 3>& visible)
        {
            return node.pane ? visible[static_cast<size_t>(*node.pane)] : Visible(*node.first, visible) || Visible(*node.second, visible);
        }

        static void Place(const Node& node, const std::array<bool, 3>& visible, const Rect area, const std::string& path, Geometry& geometry)
        {
            if (!Visible(node, visible))
                return;
            if (node.pane)
            {
                const auto pane = static_cast<size_t>(*node.pane);
                geometry.visible[pane] = true;
                geometry.panes[pane] = area;
                return;
            }
            const bool firstVisible = Visible(*node.first, visible);
            const bool secondVisible = Visible(*node.second, visible);
            if (!firstVisible)
            {
                Place(*node.second, visible, area, path + '1', geometry);
                return;
            }
            if (!secondVisible)
            {
                Place(*node.first, visible, area, path + '0', geometry);
                return;
            }
            geometry.splits.push_back({ path, area, node.direction, node.ratio });
            auto first = area;
            auto second = area;
            const bool row = node.direction == Direction::Row;
            const auto extent = row ? area.width : area.height;
            const auto gap = std::min(Gutter, extent);
            const auto cut = std::clamp(extent * node.ratio, gap / 2, extent - gap / 2);
            if (row)
            {
                first.width = cut - gap / 2;
                second.x += cut + gap / 2;
                second.width = extent - cut - gap / 2;
            }
            else
            {
                first.height = cut - gap / 2;
                second.y += cut + gap / 2;
                second.height = extent - cut - gap / 2;
            }
            Place(*node.first, visible, first, path + '0', geometry);
            Place(*node.second, visible, second, path + '1', geometry);
        }
    };
}

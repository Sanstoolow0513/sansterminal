// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
    inline constexpr std::wstring_view DefaultJson = LR"({"direction":"row","ratio":0.24,"first":"files","second":{"direction":"row","ratio":0.5,"first":"terminal","second":"editor"}})";

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

        class Parser
        {
        public:
            explicit Parser(const std::wstring_view text) : _text{ text }
            {
                if (text.size() > 4096)
                    Fail();
            }

            std::unique_ptr<Node> Parse()
            {
                auto node = ReadNode(0);
                Space();
                if (_position != _text.size() || _panes != 7)
                    Fail();
                return node;
            }

        private:
            std::wstring_view _text;
            size_t _position{};
            unsigned int _panes{};

            [[noreturn]] static void Fail()
            {
                throw std::invalid_argument("Workspace layout must be a binary split tree containing files, terminal and editor exactly once, with ratios between 0 and 1.");
            }

            void Space()
            {
                while (_position < _text.size() && (_text[_position] == L' ' || _text[_position] == L'\t' || _text[_position] == L'\n' || _text[_position] == L'\r'))
                    ++_position;
            }

            bool Take(const wchar_t ch)
            {
                Space();
                if (_position < _text.size() && _text[_position] == ch)
                {
                    ++_position;
                    return true;
                }
                return false;
            }

            void Expect(const wchar_t ch)
            {
                if (!Take(ch))
                    Fail();
            }

            std::wstring String()
            {
                Expect(L'"');
                std::wstring value;
                while (_position < _text.size())
                {
                    auto ch = _text[_position++];
                    if (ch == L'"')
                        return value;
                    if (ch < 0x20)
                        Fail();
                    if (ch == L'\\')
                    {
                        if (_position == _text.size())
                            Fail();
                        ch = _text[_position++];
                        if (ch == L'u')
                        {
                            unsigned int code{};
                            for (auto i = 0; i < 4; ++i)
                            {
                                if (_position == _text.size())
                                    Fail();
                                const auto digit = _text[_position++];
                                const auto hexValue = digit >= L'0' && digit <= L'9' ? digit - L'0' : digit >= L'a' && digit <= L'f' ? digit - L'a' + 10 :
                                                                                               digit >= L'A' && digit <= L'F'     ? digit - L'A' + 10 :
                                                                                                                                    -1;
                                if (hexValue < 0)
                                    Fail();
                                code = code * 16 + hexValue;
                            }
                            ch = static_cast<wchar_t>(code);
                        }
                        else if (ch != L'"' && ch != L'\\' && ch != L'/')
                            Fail();
                    }
                    value.push_back(ch);
                }
                Fail();
            }

            double Number()
            {
                Space();
                const auto start = _position;
                if (_position == _text.size() || (_text[_position] != L'-' && (_text[_position] < L'0' || _text[_position] > L'9')))
                    Fail();
                while (_position < _text.size() && ((_text[_position] >= L'0' && _text[_position] <= L'9') || _text[_position] == L'.' || _text[_position] == L'-' || _text[_position] == L'+' || _text[_position] == L'e' || _text[_position] == L'E'))
                    ++_position;
                std::string number;
                number.reserve(_position - start);
                for (auto i = start; i < _position; ++i) number.push_back(static_cast<char>(_text[i]));
                double result{};
                const auto parsed = std::from_chars(number.data(), number.data() + number.size(), result);
                if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || !std::isfinite(result) || result <= 0 || result >= 1)
                    Fail();
                return result;
            }

            std::unique_ptr<Node> ReadNode(const size_t depth)
            {
                if (depth > 3)
                    Fail();
                Space();
                if (_position < _text.size() && _text[_position] == L'"')
                {
                    const auto name = String();
                    const auto pane = name == L"files" ? Pane::Files : name == L"terminal" ? Pane::Terminal :
                                                                   name == L"editor"       ? Pane::Editor :
                                                                                             static_cast<Pane>(3);
                    const auto bit = 1u << static_cast<unsigned int>(pane);
                    if (pane == static_cast<Pane>(3) || (_panes & bit))
                        Fail();
                    _panes |= bit;
                    return std::make_unique<Node>(pane);
                }
                Expect(L'{');
                auto node = std::make_unique<Node>();
                unsigned int fields{};
                do
                {
                    const auto key = String();
                    const auto bit = key == L"direction" ? 1u : key == L"ratio" ? 2u :
                                                            key == L"first"     ? 4u :
                                                            key == L"second"    ? 8u :
                                                                                  0u;
                    if (bit == 0 || (fields & bit))
                        Fail();
                    fields |= bit;
                    Expect(L':');
                    if (bit == 1)
                    {
                        const auto direction = String();
                        if (direction != L"row" && direction != L"column")
                            Fail();
                        node->direction = direction == L"row" ? Direction::Row : Direction::Column;
                    }
                    else if (bit == 2)
                        node->ratio = Number();
                    else if (bit == 4)
                        node->first = ReadNode(depth + 1);
                    else
                        node->second = ReadNode(depth + 1);
                } while (Take(L','));
                Expect(L'}');
                if (fields != 15)
                    Fail();
                return node;
            }
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
        static Layout Parse(const std::wstring_view value)
        {
            return Layout{ Parser{ value }.Parse() };
        }

        std::wstring Serialize() const
        {
            std::wstring result;
            Write(*_root, result);
            return result;
        }

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

        static void Write(const Node& node, std::wstring& value)
        {
            if (node.pane)
            {
                static constexpr std::array names{ L"\"files\"", L"\"terminal\"", L"\"editor\"" };
                value += names[static_cast<size_t>(*node.pane)];
                return;
            }
            value += node.direction == Direction::Row ? L"{\"direction\":\"row\",\"ratio\":" : L"{\"direction\":\"column\",\"ratio\":";
            std::array<char, 64> buffer{};
            const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), node.ratio);
            value.append(buffer.data(), result.ptr);
            value += L",\"first\":";
            Write(*node.first, value);
            value += L",\"second\":";
            Write(*node.second, value);
            value += L'}';
        }

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

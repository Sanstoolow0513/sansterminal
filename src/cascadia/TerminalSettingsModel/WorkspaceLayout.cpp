// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#include "WorkspaceLayout.h"
#include <json/json.h>
#include <charconv>

namespace Sansterminal::WorkspaceLayout
{
    namespace
    {
        [[noreturn]] void InvalidLayout()
        {
            throw std::invalid_argument("Workspace layout must be a binary split tree containing files, terminal and editor exactly once, with ratios between 0 and 1.");
        }
    }

    Layout Layout::Parse(const std::wstring_view value)
    {
        if (value.size() > 4096)
            InvalidLayout();
        // Every supported field and enum is ASCII. JSON Unicode escapes remain
        // supported by jsoncpp; literal non-ASCII identifiers cannot be valid.
        std::string text;
        text.reserve(value.size());
        for (const auto ch : value)
        {
            if (ch > 0x7f)
                InvalidLayout();
            text.push_back(static_cast<char>(ch));
        }
        Json::CharReaderBuilder builder;
        Json::CharReaderBuilder::strictMode(&builder.settings_);
        builder["stackLimit"] = 8;
        const std::unique_ptr<Json::CharReader> reader{ builder.newCharReader() };
        Json::Value json;
        if (!reader->parse(text.data(), text.data() + text.size(), &json, nullptr))
            InvalidLayout();
        return FromJson(json);
    }

    Layout Layout::FromJson(const Json::Value& value)
    {
        unsigned int panes{};
        auto root = ReadNode(value, 0, panes);
        if (panes != 7)
            InvalidLayout();
        return Layout{ std::move(root) };
    }

    std::unique_ptr<Layout::Node> Layout::ReadNode(const Json::Value& value, const size_t depth, unsigned int& panes)
    {
        if (depth > 2)
            InvalidLayout();
        if (value.isString())
        {
            const auto name = value.asString();
            const auto pane = name == "files" ? Pane::Files : name == "terminal" ? Pane::Terminal :
                                                          name == "editor"       ? Pane::Editor :
                                                                                   static_cast<Pane>(3);
            const auto bit = 1u << static_cast<unsigned int>(pane);
            if (pane == static_cast<Pane>(3) || (panes & bit))
                InvalidLayout();
            panes |= bit;
            return std::make_unique<Node>(pane);
        }
        if (!value.isObject() || value.size() != 4 || !value.isMember("direction") || !value.isMember("ratio") || !value.isMember("first") || !value.isMember("second") || !value["direction"].isString() || !value["ratio"].isNumeric())
            InvalidLayout();
        const auto direction = value["direction"].asString();
        const auto ratio = value["ratio"].asDouble();
        if ((direction != "row" && direction != "column") || !std::isfinite(ratio) || ratio <= 0 || ratio >= 1)
            InvalidLayout();
        auto node = std::make_unique<Node>();
        node->direction = direction == "row" ? Direction::Row : Direction::Column;
        node->ratio = ratio;
        node->first = ReadNode(value["first"], depth + 1, panes);
        node->second = ReadNode(value["second"], depth + 1, panes);
        return node;
    }

    Json::Value Layout::WriteNode(const Node& node)
    {
        if (node.pane)
        {
            static constexpr std::array names{ "files", "terminal", "editor" };
            return Json::Value{ names[static_cast<size_t>(*node.pane)] };
        }
        Json::Value result{ Json::objectValue };
        result["direction"] = node.direction == Direction::Row ? "row" : "column";
        result["ratio"] = node.ratio;
        result["first"] = WriteNode(*node.first);
        result["second"] = WriteNode(*node.second);
        return result;
    }

    Json::Value Layout::ToJson() const
    {
        return WriteNode(*_root);
    }

    std::wstring Layout::Serialize() const
    {
        std::wstring result;
        Write(*_root, result);
        return result;
    }

    void Layout::Write(const Node& node, std::wstring& value)
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

}

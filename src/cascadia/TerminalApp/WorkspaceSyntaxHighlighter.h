// Copyright (c) Sanstoolow contributors.
// Licensed under the MIT license.

#pragma once

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <vector>

namespace WorkspaceSyntax
{
    enum class Kind
    {
        Keyword,
        String,
        Comment,
        Number,
        Heading,
        Tag,
        Variable,
    };

    struct Span
    {
        size_t start;
        size_t end;
        Kind kind;
    };

    struct Result
    {
        std::vector<Span> spans;
        bool limited{ false };
    };

    enum class Language
    {
        Plain,
        Code,
        Python,
        PowerShell,
        Json,
        Markdown,
        Xml,
    };

    inline Language Detect(const std::filesystem::path& path)
    {
        auto extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) { return std::towlower(ch); });
        if (extension == L".md" || extension == L".markdown")
        {
            return Language::Markdown;
        }
        if (extension == L".xml" || extension == L".xaml" || extension == L".html" || extension == L".htm" || extension == L".svg")
        {
            return Language::Xml;
        }
        if (extension == L".json" || extension == L".jsonc")
        {
            return Language::Json;
        }
        if (extension == L".py")
        {
            return Language::Python;
        }
        if (extension == L".ps1" || extension == L".psm1" || extension == L".psd1")
        {
            return Language::PowerShell;
        }
        if (extension == L".c" || extension == L".cc" || extension == L".cpp" || extension == L".h" ||
            extension == L".hpp" || extension == L".cs" || extension == L".js" || extension == L".jsx" ||
            extension == L".ts" || extension == L".tsx" || extension == L".java" || extension == L".rs" ||
            extension == L".go" || extension == L".css")
        {
            return Language::Code;
        }
        return Language::Plain;
    }

    inline bool _IsIdentifierStart(const wchar_t ch)
    {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch == L'_';
    }

    inline bool _IsIdentifierContinuation(const wchar_t ch)
    {
        return _IsIdentifierStart(ch) || (ch >= L'0' && ch <= L'9');
    }

    inline bool _IsKeyword(const std::wstring_view word, const Language language)
    {
        static constexpr std::wstring_view common[] = {
            L"as", L"async", L"auto", L"await", L"bool", L"break", L"case", L"catch", L"char", L"class", L"const", L"continue", L"def", L"default", L"delete", L"do", L"double", L"else", L"enum", L"except", L"export", L"extends", L"false", L"float", L"finally", L"for", L"foreach", L"from", L"function", L"if", L"import", L"in", L"interface", L"int", L"let", L"long", L"namespace", L"new", L"null", L"nullptr", L"private", L"protected", L"public", L"return", L"static", L"struct", L"switch", L"this", L"throw", L"true", L"try", L"using", L"unsigned", L"var", L"virtual", L"void", L"while", L"yield"
        };
        static constexpr std::wstring_view python[] = { L"and", L"elif", L"False", L"global", L"is", L"lambda", L"None", L"not", L"or", L"pass", L"raise", L"self", L"True", L"with" };
        static constexpr std::wstring_view powershell[] = { L"begin", L"end", L"param", L"process", L"trap" };
        return std::find(std::begin(common), std::end(common), word) != std::end(common) ||
               (language == Language::Python && std::find(std::begin(python), std::end(python), word) != std::end(python)) ||
               (language == Language::PowerShell && std::find(std::begin(powershell), std::end(powershell), word) != std::end(powershell));
    }

    inline Result Highlight(const std::wstring_view text, const Language language)
    {
        Result result;
        if (language == Language::Plain || text.empty())
        {
            return result;
        }

        constexpr size_t maxCharacters = 128 * 1024;
        constexpr size_t maxSpans = 6000;
        const auto limit = std::min(text.size(), maxCharacters);
        result.limited = text.size() > limit;
        const auto add = [&](const size_t start, const size_t end, const Kind kind) {
            if (end > start && result.spans.size() < maxSpans)
            {
                result.spans.push_back({ start, end, kind });
            }
            else if (end > start)
            {
                result.limited = true;
            }
        };

        for (size_t i = 0; i < limit;)
        {
            const auto start = i;
            const auto ch = text[i];
            const bool lineStart = i == 0 || text[i - 1] == L'\n' || text[i - 1] == L'\r';

            if (language == Language::Markdown)
            {
                if (lineStart && ch == L'#')
                {
                    while (i < limit && text[i] != L'\n' && text[i] != L'\r')
                    {
                        ++i;
                    }
                    add(start, i, Kind::Heading);
                    continue;
                }
                if (ch == L'`')
                {
                    ++i;
                    while (i < limit && text[i] != L'`' && text[i] != L'\n' && text[i] != L'\r')
                    {
                        ++i;
                    }
                    if (i < limit && text[i] == L'`')
                    {
                        ++i;
                    }
                    add(start, i, Kind::String);
                    continue;
                }
                ++i;
                continue;
            }

            if (language == Language::Xml)
            {
                if (i + 3 < limit && text.substr(i, 4) == L"<!--")
                {
                    i += 4;
                    while (i + 2 < limit && text.substr(i, 3) != L"-->")
                    {
                        ++i;
                    }
                    i = std::min(i + 3, limit);
                    add(start, i, Kind::Comment);
                    continue;
                }
                if (ch == L'<')
                {
                    ++i;
                    while (i < limit && text[i] != L'>')
                    {
                        ++i;
                    }
                    i = std::min(i + 1, limit);
                    add(start, i, Kind::Tag);
                    continue;
                }
                ++i;
                continue;
            }

            if ((language == Language::Code || language == Language::Json) && i + 1 < limit && text.substr(i, 2) == L"//")
            {
                i += 2;
                while (i < limit && text[i] != L'\n' && text[i] != L'\r')
                {
                    ++i;
                }
                add(start, i, Kind::Comment);
                continue;
            }
            if (language == Language::Code && i + 1 < limit && text.substr(i, 2) == L"/*")
            {
                i += 2;
                while (i + 1 < limit && text.substr(i, 2) != L"*/")
                {
                    ++i;
                }
                i = std::min(i + 2, limit);
                add(start, i, Kind::Comment);
                continue;
            }
            if ((language == Language::Python || language == Language::PowerShell) && ch == L'#')
            {
                ++i;
                while (i < limit && text[i] != L'\n' && text[i] != L'\r')
                {
                    ++i;
                }
                add(start, i, Kind::Comment);
                continue;
            }
            if (language == Language::PowerShell && ch == L'$' && i + 1 < limit && _IsIdentifierStart(text[i + 1]))
            {
                i += 2;
                while (i < limit && _IsIdentifierContinuation(text[i]))
                {
                    ++i;
                }
                add(start, i, Kind::Variable);
                continue;
            }
            if (ch == L'"' || ch == L'\'' || (language == Language::Code && ch == L'`'))
            {
                const auto quote = ch;
                const bool triple = language == Language::Python && i + 2 < limit && text[i + 1] == quote && text[i + 2] == quote;
                i += triple ? 3 : 1;
                while (i < limit)
                {
                    if (text[i] == L'\\' && i + 1 < limit)
                    {
                        i += 2;
                        continue;
                    }
                    if (text[i] == quote && (!triple || (i + 2 < limit && text[i + 1] == quote && text[i + 2] == quote)))
                    {
                        i += triple ? 3 : 1;
                        break;
                    }
                    ++i;
                }
                add(start, i, Kind::String);
                continue;
            }
            if (ch >= L'0' && ch <= L'9')
            {
                ++i;
                while (i < limit && ((text[i] >= L'0' && text[i] <= L'9') || text[i] == L'.' || text[i] == L'x' ||
                                     (text[i] >= L'a' && text[i] <= L'f') || (text[i] >= L'A' && text[i] <= L'F')))
                {
                    ++i;
                }
                add(start, i, Kind::Number);
                continue;
            }
            if (_IsIdentifierStart(ch))
            {
                ++i;
                while (i < limit && _IsIdentifierContinuation(text[i]))
                {
                    ++i;
                }
                if (_IsKeyword(text.substr(start, i - start), language))
                {
                    add(start, i, Kind::Keyword);
                }
                continue;
            }
            ++i;
        }
        return result;
    }
}

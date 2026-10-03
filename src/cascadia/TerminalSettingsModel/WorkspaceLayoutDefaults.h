// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#pragma once
#include <string_view>

namespace Sansterminal::WorkspaceLayout
{
    inline constexpr std::wstring_view DefaultJson = LR"({"direction":"row","ratio":0.24,"first":"files","second":{"direction":"row","ratio":0.5,"first":"terminal","second":"editor"}})";
}

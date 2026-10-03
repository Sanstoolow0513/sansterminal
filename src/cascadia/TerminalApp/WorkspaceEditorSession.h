// Copyright (c) Sansterminal contributors.
// Licensed under the MIT license.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace WorkspaceEditor
{
    class DocumentSynchronization
    {
    public:
        void Failed() noexcept { _incomplete = true; }
        void Synchronized() noexcept { _incomplete = false; }
        void HostUnavailable() noexcept
        {
            _lost = _lost || _incomplete;
            _incomplete = false;
        }
        bool Incomplete() const noexcept { return _incomplete; }
        bool Lost() const noexcept { return _lost; }
        bool NeedsAttention() const noexcept { return _incomplete || _lost; }

    private:
        bool _incomplete{};
        // Replaying a native snapshot into a new Monaco cannot recover text that
        // never reached native memory. Only explicit discard may forget this loss.
        bool _lost{};
    };

    class EditorSession
    {
    public:
        bool Ready() const noexcept { return _ready; }
        void Connected() noexcept { _ready = true; }
        void Unavailable() noexcept
        {
            _ready = false;
            _lost = _lost || _failed;
            _failed = false;
            _flushId.clear();
        }
        void SynchronizationFailed() noexcept { _failed = true; }
        bool Failed() const noexcept { return _failed; }
        bool Lost() const noexcept { return _lost; }
        // Attribute an unidentifiable transport failure to the documents that
        // existed at failure time, so later documents do not inherit the loss.
        bool TakeLostContent() noexcept { return std::exchange(_lost, false); }
        std::wstring BeginFlush()
        {
            _failed = false;
            _flushId = std::to_wstring(++_sequence);
            return _flushId;
        }
        void Flushed(const std::wstring_view id) noexcept
        {
            if (id == _flushId)
                _flushId.clear();
        }
        bool FlushPending() const noexcept { return !_flushId.empty(); }
        void EndClose() noexcept { _flushId.clear(); }

    private:
        bool _ready{};
        bool _failed{};
        bool _lost{};
        uint64_t _sequence{};
        std::wstring _flushId;
    };
}

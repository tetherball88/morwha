#pragma once

#include <cstdint>
#include <string_view>

namespace MORWHA::State {

    using SessionEpoch = std::uint64_t;

    [[nodiscard]] SessionEpoch GetSessionEpoch() noexcept;
    SessionEpoch BumpSessionEpoch(std::string_view reason) noexcept;

    [[nodiscard]] inline bool IsSessionEpochCurrent(SessionEpoch expectedEpoch) noexcept {
        return expectedEpoch != 0 && GetSessionEpoch() == expectedEpoch;
    }

}  // namespace MORWHA::State

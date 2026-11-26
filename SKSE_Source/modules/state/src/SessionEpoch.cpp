#include "morwha/state/SessionEpoch.h"

#include "morwha/foundation/Logging.h"

#include <atomic>

namespace MORWHA::State {

    namespace {
        std::atomic<SessionEpoch>& EpochCounter() noexcept {
            static std::atomic<SessionEpoch> s_epoch{ 1 };
            return s_epoch;
        }
    }  // namespace

    SessionEpoch GetSessionEpoch() noexcept {
        return EpochCounter().load(std::memory_order_acquire);
    }

    SessionEpoch BumpSessionEpoch(std::string_view reason) noexcept {
        const SessionEpoch next = EpochCounter().fetch_add(1, std::memory_order_acq_rel) + 1;
        Foundation::LogInfo("SessionEpoch bumped to {} (reason='{}')", next, reason);
        return next;
    }

}  // namespace MORWHA::State

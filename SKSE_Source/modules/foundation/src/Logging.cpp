#include "morwha/foundation/Logging.h"

#include <atomic>

namespace MORWHA::Foundation {

    namespace {
        std::atomic<LogSinkFn>& SinkSlot() noexcept {
            static std::atomic<LogSinkFn> s_sink{ nullptr };
            return s_sink;
        }
    }  // namespace

    void SetLogSink(LogSinkFn sink) noexcept {
        SinkSlot().store(sink, std::memory_order_release);
    }

    LogSinkFn GetLogSink() noexcept {
        return SinkSlot().load(std::memory_order_acquire);
    }

    void EmitLog(LogSeverity severity, std::string_view message) noexcept {
        if (const auto sink = GetLogSink()) {
            sink(severity, message);
        }
    }

}  // namespace MORWHA::Foundation

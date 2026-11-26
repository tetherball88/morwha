#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string_view>

namespace MORWHA {

    [[nodiscard]] const std::shared_ptr<spdlog::logger>& GetLogger() noexcept;

    namespace Engine {
        void InitializeFileLogger() noexcept;
        [[nodiscard]] std::string_view GetInstalledModVersion() noexcept;
    }  // namespace Engine

}  // namespace MORWHA

#define MORWHA_LOG_TRACE(...)                                                   \
    do {                                                                        \
        if (const auto& morwhaLog_ = ::MORWHA::GetLogger()) {                   \
            morwhaLog_->trace(__VA_ARGS__);                                     \
        }                                                                       \
    } while (false)

#define MORWHA_LOG_DEBUG(...)                                                   \
    do {                                                                        \
        if (const auto& morwhaLog_ = ::MORWHA::GetLogger()) {                   \
            morwhaLog_->debug(__VA_ARGS__);                                     \
        }                                                                       \
    } while (false)

#define MORWHA_LOG_INFO(...)                                                    \
    do {                                                                        \
        if (const auto& morwhaLog_ = ::MORWHA::GetLogger()) {                   \
            morwhaLog_->info(__VA_ARGS__);                                      \
        }                                                                       \
    } while (false)

#define MORWHA_LOG_WARN(...)                                                    \
    do {                                                                        \
        if (const auto& morwhaLog_ = ::MORWHA::GetLogger()) {                   \
            morwhaLog_->warn(__VA_ARGS__);                                      \
        }                                                                       \
    } while (false)

#define MORWHA_LOG_ERROR(...)                                                   \
    do {                                                                        \
        if (const auto& morwhaLog_ = ::MORWHA::GetLogger()) {                   \
            morwhaLog_->error(__VA_ARGS__);                                     \
        }                                                                       \
    } while (false)

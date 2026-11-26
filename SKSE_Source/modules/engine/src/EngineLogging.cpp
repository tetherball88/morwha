#include "morwha/engine/EngineLogging.h"

#include "morwha/foundation/Logging.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace MORWHA {

    namespace {

        std::shared_ptr<spdlog::logger>& LoggerSlot() noexcept {
            static std::shared_ptr<spdlog::logger> s_logger;
            return s_logger;
        }

        void ForwardFoundationLog(Foundation::LogSeverity severity, std::string_view message) noexcept {
            switch (severity) {
                case Foundation::LogSeverity::Trace:
                    MORWHA_LOG_TRACE("{}", message);
                    break;
                case Foundation::LogSeverity::Debug:
                    MORWHA_LOG_DEBUG("{}", message);
                    break;
                case Foundation::LogSeverity::Info:
                    MORWHA_LOG_INFO("{}", message);
                    break;
                case Foundation::LogSeverity::Warn:
                    MORWHA_LOG_WARN("{}", message);
                    break;
                case Foundation::LogSeverity::Error:
                    MORWHA_LOG_ERROR("{}", message);
                    break;
            }
        }

        [[nodiscard]] std::string TrimCopy(std::string s) {
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
                s.pop_back();
            }
            std::size_t first = 0;
            while (first < s.size() && (s[first] == ' ' || s[first] == '\t')) {
                ++first;
            }
            return s.substr(first);
        }

    }  // namespace

    const std::shared_ptr<spdlog::logger>& GetLogger() noexcept {
        return LoggerSlot();
    }

    namespace Engine {

        void InitializeFileLogger() noexcept {
            auto logDir = SKSE::log::log_directory();
            if (!logDir) {
                if (auto* console = RE::ConsoleLog::GetSingleton()) {
                    console->Print("MORWHA: log directory unavailable");
                }
                return;
            }

            std::filesystem::path logPath = *logDir;
            if (!std::filesystem::is_directory(logPath)) {
                logPath = logPath.parent_path();
            }
            logPath /= "MORWHA.log";

            std::error_code ec;
            std::filesystem::create_directories(logPath.parent_path(), ec);
            if (ec) {
                if (auto* console = RE::ConsoleLog::GetSingleton()) {
                    console->Print("MORWHA: failed to create log folder (%s)", ec.message().c_str());
                }
                return;
            }

            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
            auto logger = std::make_shared<spdlog::logger>("MORWHA", std::move(sink));

            logger->set_level(spdlog::level::info);
            logger->flush_on(spdlog::level::info);
            logger->set_pattern("[%H:%M:%S] [%l] %v");

            spdlog::set_default_logger(logger);
            LoggerSlot() = std::move(logger);
            Foundation::SetLogSink(&ForwardFoundationLog);
            MORWHA_LOG_INFO("MORWHA initialized (version: {})", GetInstalledModVersion());
        }

        std::string_view GetInstalledModVersion() noexcept {
            static const std::string s_cachedVersion = []() -> std::string {
                const std::filesystem::path candidates[] = {
                    "Data/SKSE/Plugins/MORWHA/version.txt",
                    "SKSE/Plugins/MORWHA/version.txt",
                };
                for (const auto& candidate : candidates) {
                    std::error_code ec;
                    if (!std::filesystem::is_regular_file(candidate, ec) || ec) {
                        continue;
                    }
                    std::ifstream in(candidate);
                    std::string line;
                    if (std::getline(in, line)) {
                        line = TrimCopy(std::move(line));
                        if (!line.empty()) {
                            return line;
                        }
                    }
                }
                return "0.1.0";
            }();
            return s_cachedVersion;
        }

    }  // namespace Engine

}  // namespace MORWHA

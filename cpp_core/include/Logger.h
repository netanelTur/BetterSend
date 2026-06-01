#pragma once
#include <string>
#include <string_view>
#include <fstream>
#include <mutex>
#include <chrono>
#include <format>
#include <source_location>
#include <utility>
#include "Constants.h"

// ── Logger ────────────────────────────────────────────────────────────────────
// Thread-safe singleton logger.
// Design pattern: Singleton (Meyer's — thread-safe in C++11+)
//
// Usage:
//   Logger::instance().setLevel(Logger::Level::Debug);
//   Logger::instance().setOutputFile("bettersend.log");
//
//   BS_LOG_DEBUG("Transport", "Connected to {}", ip);
//   BS_LOG_ERROR("Discovery", "mDNS socket failed: {}", errno);
//
// Log format: [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [Component] Message
//             [2024-11-01 14:32:07.412] [ERROR] [Transport] Connection refused

namespace BetterSend {

class Logger {
public:
	// ── Log levels (ordered by severity) ──────────────────────────────────────
	enum class Level : int { Debug = 0, Info = 1, Warning = 2, Error = 3 };

	// ── Singleton access ──────────────────────────────────────────────────────
	static Logger& instance() noexcept {
		static Logger inst;
		return inst;
	}

	// Non-copyable, non-movable
	Logger(const Logger&)            = delete;
	Logger& operator=(const Logger&) = delete;
	Logger(Logger&&)                 = delete;
	Logger& operator=(Logger&&)      = delete;

	// ── Configuration (call before first log) ─────────────────────────────────
	void setLevel(Level minLevel) noexcept { minLevel_ = minLevel; }
	Level level()  const noexcept          { return minLevel_; }

	void setOutputFile(std::string_view path) {
		std::lock_guard lock{mutex_};
		if (file_.is_open()) file_.close();
		file_.open(std::string{path}, std::ios::app);
	}

	void setStderrEnabled(bool enabled) noexcept { stderrEnabled_ = enabled; }

	// ── Core log method ───────────────────────────────────────────────────────
	// Prefer the BS_LOG_* macros below over calling this directly.
	template<typename... Args>
	void log(Level level,
	         std::string_view component,
	         const std::source_location& loc,
	         std::format_string<Args...> fmt,
	         Args&&... args)
	{
		if (level < minLevel_) return;

		const auto message = std::format(fmt, std::forward<Args>(args)...);
		const auto entry   = formatEntry(level, component, message, loc);

		std::lock_guard lock{mutex_};
		if (file_.is_open())  file_ << entry << '\n', file_.flush();
		if (stderrEnabled_)   std::fputs((entry + '\n').c_str(), stderr);
	}

private:
	Logger() = default;

	static std::string_view levelTag(Level l) noexcept {
		switch (l) {
			case Level::Debug:   return "DEBUG";
			case Level::Info:    return "INFO ";
			case Level::Warning: return "WARN ";
			case Level::Error:   return "ERROR";
		}
		return "?????";
	}

	static std::string formatEntry(Level level,
	                               std::string_view component,
	                               std::string_view message,
	                               const std::source_location& loc)
	{
		const auto now  = std::chrono::system_clock::now();
		const auto time = std::chrono::system_clock::to_time_t(now);
		const auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
		                      now.time_since_epoch()) % 1000;

		std::tm tm{};
#if defined(_WIN32)
		localtime_s(&tm, &time);
#else
		localtime_r(&time, &tm);
#endif
		return std::format("[{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] [{}] [{:>12}] {} ({}:{})",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, ms.count(),
			levelTag(level),
			component,
			message,
			loc.file_name(), loc.line());
	}

	Level         minLevel_      = Level::Debug;
	bool          stderrEnabled_ = true;
	std::ofstream file_;
	std::mutex    mutex_;
};

} // namespace BetterSend

// ── Convenience macros ────────────────────────────────────────────────────────
// Use these everywhere — they capture source location automatically.

#define BS_LOG(level, component, ...) \
	::BetterSend::Logger::instance().log( \
		::BetterSend::Logger::Level::level, component, std::source_location::current(), __VA_ARGS__)

#define BS_LOG_DEBUG(component, ...)   BS_LOG(Debug,   component, __VA_ARGS__)
#define BS_LOG_INFO(component, ...)    BS_LOG(Info,    component, __VA_ARGS__)
#define BS_LOG_WARN(component, ...)    BS_LOG(Warning, component, __VA_ARGS__)
#define BS_LOG_ERROR(component, ...)   BS_LOG(Error,   component, __VA_ARGS__)

#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <source_location>
#include <memory>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <print>            // Also includes format somewhere in there
#include <chrono>
#include <queue>
#include <condition_variable>
#include <thread>

#include "glaze/glaze.hpp"

#include "utils/FileUtils.h"
#include "utils/TimeUtils.h"

// The macros we will use henceforth to log ANYTHING. We really did learn from the best
#define LOGGER(type, fmt, ...)  Vulkanised::Logger::getLoggerInstance()->logText(type, std::source_location::current(), fmt, __VA_ARGS__)
#define LOG(fmt, ...)           Vulkanised::Logger::getLoggerInstance()->logText(Vulkanised::Logger::LogType::LOG, std::source_location::current(), fmt __VA_OPT__(,) __VA_ARGS__)
#define LOGH(fmt, ...)          Vulkanised::Logger::getLoggerInstance()->logText(Vulkanised::Logger::LogType::HIGHLIGHT, std::source_location::current(), fmt __VA_OPT__(,) __VA_ARGS__)
#define LOGW(fmt, ...)          Vulkanised::Logger::getLoggerInstance()->logText(Vulkanised::Logger::LogType::WARNING, std::source_location::current(), fmt __VA_OPT__(,) __VA_ARGS__)
#define LOGE(fmt, ...)          Vulkanised::Logger::getLoggerInstance()->logText(Vulkanised::Logger::LogType::ERR, std::source_location::current(), fmt __VA_OPT__(,) __VA_ARGS__)
#define LOGT(fmt, ...)          Vulkanised::Logger::getLoggerInstance()->logText(Vulkanised::Logger::LogType::TIMING, std::source_location::current(), fmt __VA_OPT__(,) __VA_ARGS__)
#define LOGNTHROW(fmt, ...)     Vulkanised::Logger::getLoggerInstance()->logAndThrow(Vulkanised::Logger::LogType::ERR, std::source_location::current(), fmt __VA_OPT__(,) __VA_ARGS__)

namespace Vulkanised
{
    namespace Color
    {
        constexpr const char* Reset{ "\033[0m" };                                   // Resets console output to default color
        constexpr const char* Error{ "\033[48;2;230;0;38;38;2;0;230;192m" };        // Green-ish on red background
        constexpr const char* Warning{ "\033[48;2;230;226;0;38;2;0;4;230m" };       // Blue on yellow background
        constexpr const char* Highlight{ "\033[48;2;0;183;235;38;2;235;52;0m" };    // Red-ish on cyan background
        constexpr const char* FunctionNames{ "\033[48;2;255;255;255;38;2;0;0;0m" }; // Black on white background
        constexpr const char* Timestamp{ "\033[48;2;150;75;0;38;2;7;35;55m" };      // Blue-ish on brown background
        constexpr const char* TimedFunc{ "\033[48;2;0;255;20;38;2;7;35;55m" };      // Pink-ish on green background
    }

/*
* Logger class. No matter what, the logger itself can never use the log macros for itself. Macros are for everyone else.
* Thank you for your attention to this matter...
*/
class Logger
{
public:
    static std::shared_ptr<Logger> getLoggerInstance() noexcept
    {
        static std::shared_ptr<Logger> loggerSingleton{ new Logger() };
        return loggerSingleton;
    }

    ~Logger() noexcept
    {
        //writeSettingsFile();  // For now we will only write the settings when we try to read them (if they dont exist, and once). If we make an editor we can refactor this later.
        logText(LogType::HIGHLIGHT, std::source_location::current(), "Goodnight logger...");

        // We are shifting the logger to ressemble more a channel pattern. Namely, I intend to process every log message in another thread.
        m_Running = false;
        m_ConditionVariable.notify_one(); // Wake up thread to die
        if (m_WorkerThread.joinable()) {
            m_WorkerThread.join();
        }

        m_File << Utilities::File::c_LogSeparation;
    }

    enum class LogType
    {
        LOG = 0,
        HIGHLIGHT,
        TIMING,
        WARNING,
        ERR,
        VALIDATION
    };

    [[nodiscard]] std::string getTimestamp() noexcept;

    // Function that does what I did a THOUSAND times, for throwing shit but also logging, ffs
    template <typename... Args>
    void logAndThrow(LogType type, std::source_location loc, std::string_view fmt, Args&&... args)
    {
        std::string formattedMessage = std::vformat(fmt, std::make_format_args(args...));
        pushToQueue(type, loc, std::move(formattedMessage));
        throw std::runtime_error(formattedMessage);
    }

    // I want to be able to log things with modern formatting abilities. This here function takes care of it.
    template <typename... Args>
    void logText(LogType type, std::source_location loc, std::string_view fmt, Args&&... args)
    {
        std::string formattedMessage = std::vformat(fmt, std::make_format_args(args...));
        //logInternal(formattedMessage, type, loc); // We no longer log when we call logText, we will just push it to a queue.
        pushToQueue(type, loc, std::move(formattedMessage));
    }

private:
    struct LogEntry {
        LogType type;
        std::source_location loc;
        std::string message;
    };

    // Async components
    std::queue<LogEntry> m_Queue;
    std::mutex m_QueueMutex;
    std::condition_variable m_ConditionVariable;
    std::thread m_WorkerThread;
    std::atomic<bool> m_Running{ true };

    void pushToQueue(LogType type, std::source_location loc, std::string&& msg);    // Just push to the damn queue instead of spending time to log!   
    void processQueue();    // The function that loops in the logging thread, doing all the actual printing to console or file (hopefully improving the vulkan thread)

    // One ofstream instance.
    std::ofstream m_File;

    // Settings state, stored in struct (which is also used for json parsing)
    struct LogSettings {
        bool logToFile{ true };
        bool deleteLogFileAtStart{ true };
        bool logToConsole{ true };
    } m_LoggerSettings;

    // Methods for our eyes only...
    void readSettingsFile() noexcept;
    void writeSettingsFile() noexcept;

    // For debugging purposes. Could eventually remove this completely down the line.
    void printBooleans() noexcept;

    [[deprecated]] void logInternal(const std::string& input, LogType inColor, std::source_location callSite) noexcept; // Deprecated function.
    void logInternal(const LogEntry& entry) noexcept;

    // Private constructors to strictly control instantiation of the class.
    Logger() noexcept
    {
        readSettingsFile();
        if (!m_LoggerSettings.deleteLogFileAtStart)
        {
            m_File.open(std::filesystem::path(Utilities::File::c_Path) / Utilities::File::c_LogFileName, std::ios::app);
        }
        else
        {
            m_File.open(std::filesystem::path(Utilities::File::c_Path) / Utilities::File::c_LogFileName);
        }

        // ACTUALLY start the damn thread lmao 
        m_WorkerThread = std::thread(&Logger::processQueue, this);

        //printBooleans();

        logText(LogType::HIGHLIGHT, std::source_location::current(), "Logger initialised (now in async!)...");
    }

    // Delete the copy constructor and assignment operator, since its a static class and shouldn't exist anyways.
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Delete move operations, since its a static class and shouldn't exist anyways.
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;
};
}

#endif // !LOGGER_H

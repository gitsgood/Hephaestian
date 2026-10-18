#include "Vulkanised/Logger.h"

using namespace Vulkanised;

std::string Logger::getTimestamp() noexcept
{
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - Utilities::Time::getStartTime());

    double elapsedSeconds = elapsed.count() / 1000.0;

    std::chrono::system_clock::time_point systemNow = std::chrono::system_clock::now();

    return std::format("[{:%H:%M:%S}] [{:.6f}s] ", systemNow, elapsedSeconds);
}

void Vulkanised::Logger::pushToQueue(LogType type, std::source_location loc, std::string&& msg)
{
    {
        std::scoped_lock<std::mutex> lock(m_QueueMutex);
        m_Queue.push({ type, loc, std::move(msg) });
    }
    m_ConditionVariable.notify_one(); // Wake up the background thread, reawakening its process loop.
}

void Vulkanised::Logger::processQueue()
{
    while (m_Running || !m_Queue.empty())
    {
        std::unique_lock<std::mutex> lock(m_QueueMutex);

        // Wait until there is something to log or we are shutting down
        m_ConditionVariable.wait(lock, [this] {
            return !m_Queue.empty() || !m_Running;
            });

        if (!m_Queue.empty())
        {
            LogEntry entry = std::move(m_Queue.front());
            m_Queue.pop();

            // Unlock while doing I/O so other threads can still push logs
            lock.unlock();
            logInternal(entry);
        }
    }
}

void Logger::readSettingsFile() noexcept
{
    std::filesystem::path settingsPath{ std::filesystem::path(Utilities::File::c_Path) / Utilities::File::c_LogSettingsFileName };
    std::string jsonBuffer{ "" };
    glz::error_ctx settingsReadFailure = glz::read_file_json(m_LoggerSettings, settingsPath.string(), jsonBuffer);
    if (!settingsReadFailure) { logText(LogType::HIGHLIGHT, std::source_location::current(), "Succesfully read the json..."); }
    else 
    { 
        writeSettingsFile(); 
        logText(LogType::WARNING, std::source_location::current(), "Logger settings not found, creating default...");
    }
    //logText(LogType::HIGHLIGHT, std::source_location::current(), "{}", jsonBuffer);
}

void Logger::writeSettingsFile() noexcept
{
    std::filesystem::path settingsPath{ std::filesystem::path(Utilities::File::c_Path) / Utilities::File::c_LogSettingsFileName };
    std::string buffer = glz::write_json(m_LoggerSettings).value_or("error");
    std::ofstream jsonFile;
    jsonFile.open(settingsPath);
    if (!jsonFile) { logText(LogType::ERR, std::source_location::current(), "Couldn't write the json somehow..."); }
    jsonFile << buffer;
    jsonFile.close();
}

void Logger::printBooleans() noexcept
{
    std::stringstream finalMessage;
    finalMessage << "\n";
    finalMessage << (m_LoggerSettings.logToFile ? "logToFile: true\n" : "logToFile: false\n");
    finalMessage << (m_LoggerSettings.deleteLogFileAtStart ? "deleteLogFileAtStart: true\n" : "deleteLogFileAtStart: false\n");
    finalMessage << (m_LoggerSettings.logToConsole ? "logToConsole: true\n" : "logToConsole: false\n");

    logText(LogType::HIGHLIGHT, std::source_location::current(), finalMessage.str());
}

void Logger::logInternal(const std::string& input, LogType inColor, std::source_location callSite) noexcept
{
    // Early exit: If logging is totally disabled, don't even lock the mutex
    if (!m_LoggerSettings.logToConsole && !m_LoggerSettings.logToFile) return;


    //std::scoped_lock<std::mutex> lock(m_Mutex);   // The mutex locking now happens in the pushToQueue function, no need for another here.

    const char* colorCode = Color::Reset;
    const char* prefix = " ";

    // Building the function name string
    std::string functionName = std::format("[ {} ]", callSite.function_name());

    switch (inColor)
    {
    case LogType::LOG:
        colorCode = Color::Reset;
        break;
    case LogType::HIGHLIGHT:
        colorCode = Color::Highlight;
        break;
    case LogType::TIMING:
        colorCode = Color::TimedFunc;
        prefix = " TIMING: ";
        break;
    case LogType::WARNING:
        colorCode = Color::Warning;
        prefix = " WARNING: ";
        break;
    case LogType::ERR:
        colorCode = Color::Error;
        prefix = " ERROR: ";
        break;
    case LogType::VALIDATION:
        colorCode = Color::Reset;
        functionName.clear();       // Validation layers get called by the same function, with a lengthy and useless name. It gets its own case now...
        break;
    default:
        colorCode = Color::Reset;
        break;
    }

    // Building the message
    std::string message = std::format("{}{}", prefix, input);

    // Build the timestamp, once
    std::string timestamp = getTimestamp();

    // Log to console
#ifdef NDEBUG
    // Do nothing, we have no console without debug mode
#else
    if (m_LoggerSettings.logToConsole)
    {
        //std::cout << Color::Timestamp << timestamp << Color::FunctionNames << functionName << Color::Reset << colorCode << message << Color::Reset << "\n";
        std::println("{}{}{}{}{}{}{}{}", Color::Timestamp, timestamp, Color::FunctionNames, functionName, Color::Reset, colorCode, message, Color::Reset);
    }
#endif

    // Output log to a file
    if (m_LoggerSettings.logToFile)
    {
        //m_File << timestamp << functionName << message << "\n";
        std::println(m_File, "{}{}{}", timestamp, functionName, message);
    }
}

void Vulkanised::Logger::logInternal(const LogEntry& entry) noexcept
{
    // Early exit: If logging is totally disabled, don't even lock the mutex
    if (!m_LoggerSettings.logToConsole && !m_LoggerSettings.logToFile) return;


    //std::scoped_lock<std::mutex> lock(m_Mutex);   // The mutex locking now happens in the pushToQueue function, no need for another here.

    const char* colorCode = Color::Reset;
    const char* prefix = " ";

    // Building the function name string
    std::string functionName = std::format("[ {} ]",entry.loc.function_name());

    switch (entry.type)
    {
    case LogType::LOG:
        colorCode = Color::Reset;
        break;
    case LogType::HIGHLIGHT:
        colorCode = Color::Highlight;
        break;
    case LogType::TIMING:
        colorCode = Color::TimedFunc;
        prefix = " TIMING: ";
        break;
    case LogType::WARNING:
        colorCode = Color::Warning;
        prefix = " WARNING: ";
        break;
    case LogType::ERR:
        colorCode = Color::Error;
        prefix = " ERROR: ";
        break;
    case LogType::VALIDATION:
        colorCode = Color::Reset;
        functionName.clear();       // Validation layers get called by the same function, with a lengthy and useless name. It gets its own case now...
        break;
    default:
        colorCode = Color::Reset;
        break;
    }

    // Building the message
    std::string message = std::format("{}{}", prefix, entry.message);

    // Build the timestamp, once
    std::string timestamp = getTimestamp();

    // Log to console
#ifdef NDEBUG
    // Do nothing, we have no console without debug mode
#else
    if (m_LoggerSettings.logToConsole)
    {
        //std::cout << Color::Timestamp << timestamp << Color::FunctionNames << functionName << Color::Reset << colorCode << message << Color::Reset << "\n";
        std::println("{}{}{}{}{}{}{}{}", Color::Timestamp, timestamp, Color::FunctionNames, functionName, Color::Reset, colorCode, message, Color::Reset);
    }
#endif

    // Output log to a file
    if (m_LoggerSettings.logToFile)
    {
        //m_File << timestamp << functionName << message << "\n";
        std::println(m_File, "{}{}{}", timestamp, functionName, message);
    }
}

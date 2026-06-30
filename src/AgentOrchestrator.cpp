#include "AgentOrchestrator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace aine {
namespace {

using json = nlohmann::json;

struct ProcessResult {
    bool launched = false;
    bool timedOut = false;
    int exitCode = -1;
    unsigned long launchErrorCode = 0;
    std::string stdoutText;
    std::string stderrText;
    std::string launchError;
    std::string commandLabel;
};

using ProcessOutputCallback = std::function<void(const std::string& stdoutDelta, const std::string& stderrDelta)>;

struct ProviderCandidate {
    std::string path;
    std::string source;
};

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string Trim(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(),
                value.end());
    return value;
}

std::string JoinStrings(const std::vector<std::string>& values, const std::string& separator) {
    std::ostringstream stream;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            stream << separator;
        }
        stream << values[i];
    }
    return stream.str();
}

std::string TruncateForDisplay(const std::string& value, size_t limit = 900) {
    if (value.size() <= limit) {
        return value;
    }
    if (limit <= 24) {
        return value.substr(0, limit);
    }
    return value.substr(0, limit - 24) + "\n...[truncated]";
}

std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

std::string LogLevelPrefix(LogLevel level) {
    switch (level) {
    case LogLevel::Warning:
        return "Warning";
    case LogLevel::Error:
        return "Error";
    case LogLevel::Info:
    default:
        return "Info";
    }
}

std::string ChatSenderLabel(ChatSender sender) {
    switch (sender) {
    case ChatSender::User:
        return "User";
    case ChatSender::Assistant:
        return "Assistant";
    case ChatSender::System:
    default:
        return "System";
    }
}

std::string Vec3ToText(const std::array<float, 3>& value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value[0] << ", " << value[1] << ", " << value[2];
    return stream.str();
}

std::vector<std::string> LastValues(const std::vector<std::string>& values, size_t limit) {
    if (values.size() <= limit) {
        return values;
    }
    return std::vector<std::string>(values.end() - static_cast<std::ptrdiff_t>(limit), values.end());
}

std::string GetEnvironmentValue(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : Trim(value);
}

constexpr const char* kOpenAIApiKeyEnvironmentVariable = "OPENAI_API_KEY";
constexpr const char* kOpenAIApiModel = "gpt-4.1-mini";
constexpr const char* kOpenAIResponsesEndpoint = "https://api.openai.com/v1/responses";

std::vector<std::string> SplitPathList(const std::string& value) {
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : value) {
#if defined(_WIN32)
        if (ch == ';') {
#else
        if (ch == ':') {
#endif
            if (!current.empty()) {
                parts.push_back(current);
            }
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

bool IsWindowsAppsPath(const std::string& path) {
    std::string normalized = ToLower(path);
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    return normalized.find("\\windowsapps\\") != std::string::npos;
}

std::vector<ProviderCandidate> BuildExecutableCandidates(const std::string& executableName, const char* environmentVariable) {
    std::vector<ProviderCandidate> candidates;
    std::set<std::string> seen;

    auto addCandidate = [&candidates, &seen](const std::filesystem::path& path, const std::string& source) {
        if (path.empty()) {
            return;
        }
        const std::string pathString = path.string();
        const std::string key = ToLower(pathString);
        if (seen.insert(key).second) {
            candidates.push_back({pathString, source});
        }
    };

    const std::string overridePath = GetEnvironmentValue(environmentVariable);
    if (!overridePath.empty()) {
        addCandidate(std::filesystem::path(overridePath), environmentVariable);
        return candidates;
    }

    std::vector<std::string> names;
#if defined(_WIN32)
    names = {executableName + ".exe", executableName + ".cmd", executableName + ".bat", executableName};
#else
    names = {executableName};
#endif

    for (const std::string& pathEntry : SplitPathList(GetEnvironmentValue("PATH"))) {
        for (const std::string& name : names) {
            std::error_code error;
            std::filesystem::path candidate = std::filesystem::path(pathEntry) / name;
            if (std::filesystem::exists(candidate, error) && !error) {
                addCandidate(candidate, "PATH");
            }
        }
    }
    return candidates;
}

std::vector<ProviderCandidate> BuildPathExecutableCandidates(const std::string& executableName) {
    std::vector<ProviderCandidate> candidates;
    std::set<std::string> seen;

    auto addCandidate = [&candidates, &seen](const std::filesystem::path& path, const std::string& source) {
        if (path.empty()) {
            return;
        }
        const std::string pathString = path.string();
        const std::string key = ToLower(pathString);
        if (seen.insert(key).second) {
            candidates.push_back({pathString, source});
        }
    };

    std::vector<std::string> names;
#if defined(_WIN32)
    names = {executableName + ".exe", executableName + ".cmd", executableName + ".bat", executableName};
#else
    names = {executableName};
#endif

    for (const std::string& pathEntry : SplitPathList(GetEnvironmentValue("PATH"))) {
        for (const std::string& name : names) {
            std::error_code error;
            std::filesystem::path candidate = std::filesystem::path(pathEntry) / name;
            if (std::filesystem::exists(candidate, error) && !error) {
                addCandidate(candidate, "PATH");
            }
        }
    }
    return candidates;
}

#if defined(_WIN32)
std::vector<ProviderCandidate> BuildKnownCodexInstallCandidates() {
    std::vector<ProviderCandidate> candidates;
    const std::string localAppData = GetEnvironmentValue("LOCALAPPDATA");
    if (localAppData.empty()) {
        return candidates;
    }

    const std::filesystem::path localRoot(localAppData);
    for (const std::filesystem::path& path : {
             localRoot / "Programs" / "OpenAI" / "Codex" / "bin" / "codex.exe",
             localRoot / "OpenAI" / "Codex" / "bin" / "codex.exe",
         }) {
        std::error_code error;
        if (std::filesystem::exists(path, error) && !error) {
            candidates.push_back({path.string(), "known install"});
        }
    }
    return candidates;
}
#endif

std::string BuildNormalizedAssistantMessage(const std::string& providerLabel, const NormalizedProviderResult& normalized) {
    if (normalized.type == ProviderResultType::ChatResponse) {
        return normalized.message.empty() ? "Provider returned an empty chat response." : normalized.message;
    }

    std::ostringstream message;
    message << providerLabel << " normalized result: " << ProviderResultNormalizer::TypeId(normalized.type) << "\n\n";

    if (!normalized.message.empty()) {
        message << normalized.message << "\n\n";
    }

    if (normalized.type == ProviderResultType::ProposedCommandBatch) {
        message << "The provider proposed a command batch named '" << normalized.commandBatch.name
                << "' with " << normalized.commandBatch.commands.size()
                << " commands. Review Proposed Actions and approve or reject it.";
    } else if (normalized.type == ProviderResultType::ProposedFileChange) {
        message << "The provider proposed a file change for " << normalized.fileChange.path
                << ". File changes are visible but cannot be applied yet.";
    } else if (normalized.type == ProviderResultType::Diagnostic) {
        message << "The provider returned a diagnostic.";
    } else if (normalized.type == ProviderResultType::ProviderError) {
        message << "The provider output could not be used as an action.";
    }

    if (!normalized.diagnostics.empty()) {
        message << "\n\nDiagnostics: " << JoinStrings(normalized.diagnostics, " ");
    }
    return message.str();
}

#if defined(_WIN32)
std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
    if (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

std::string WindowsErrorMessage(DWORD errorCode) {
    LPSTR messageBuffer = nullptr;
    const DWORD size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                      reinterpret_cast<LPSTR>(&messageBuffer), 0, nullptr);
    std::string message;
    if (size > 0 && messageBuffer != nullptr) {
        message.assign(messageBuffer, size);
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == '.')) {
            message.pop_back();
        }
        LocalFree(messageBuffer);
    }

    std::ostringstream stream;
    stream << "Windows error " << errorCode;
    if (!message.empty()) {
        stream << ": " << message;
    }
    return stream.str();
}

std::string QuoteWindowsArgument(const std::string& argument) {
    if (argument.empty()) {
        return "\"\"";
    }

    const bool needsQuotes = argument.find_first_of(" \t\n\v\"") != std::string::npos;
    if (!needsQuotes) {
        return argument;
    }

    std::string result = "\"";
    size_t backslashes = 0;
    for (const char ch : argument) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '"') {
            result.append(backslashes * 2 + 1, '\\');
            result.push_back(ch);
            backslashes = 0;
            continue;
        }
        result.append(backslashes, '\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, '\\');
    result.push_back('"');
    return result;
}

std::string BuildWindowsCommandLine(const std::string& executablePath, const std::vector<std::string>& arguments) {
    std::string commandLine = QuoteWindowsArgument(executablePath);
    for (const std::string& argument : arguments) {
        commandLine.push_back(' ');
        commandLine += QuoteWindowsArgument(argument);
    }
    return commandLine;
}

bool IsWindowsBatchFile(const std::string& executablePath) {
    const std::string extension = ToLower(std::filesystem::path(executablePath).extension().string());
    return extension == ".cmd" || extension == ".bat";
}

std::string BuildBatchCommandLine(const std::string& executablePath, const std::vector<std::string>& arguments) {
    std::string inner = QuoteWindowsArgument(executablePath);
    for (const std::string& argument : arguments) {
        inner.push_back(' ');
        inner += QuoteWindowsArgument(argument);
    }
    return "cmd.exe /S /C \"" + inner + "\"";
}

std::string WindowsSystemExecutable(const std::vector<std::string>& relativeParts, const std::string& fallback) {
    wchar_t systemDirectory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return fallback;
    }

    std::filesystem::path path(systemDirectory);
    for (const std::string& part : relativeParts) {
        path /= part;
    }
    return path.string();
}

std::string WindowsCmdExecutable() {
    return WindowsSystemExecutable({"cmd.exe"}, "cmd.exe");
}

std::string WindowsPowerShellExecutable() {
    return WindowsSystemExecutable({"WindowsPowerShell", "v1.0", "powershell.exe"}, "powershell.exe");
}

std::string BuildCodexCmdCommand(const std::vector<std::string>& arguments) {
    std::string command = "codex";
    for (const std::string& argument : arguments) {
        command.push_back(' ');
        command += QuoteWindowsArgument(argument);
    }
    return command;
}

std::string QuotePowerShellArgument(const std::string& argument) {
    std::string result = "'";
    for (const char ch : argument) {
        if (ch == '\'') {
            result += "''";
        } else {
            result.push_back(ch);
        }
    }
    result.push_back('\'');
    return result;
}

std::string BuildCodexPowerShellCommand(const std::vector<std::string>& arguments) {
    std::string command = "& codex";
    for (const std::string& argument : arguments) {
        command.push_back(' ');
        command += QuotePowerShellArgument(argument);
    }
    return command;
}

void TryRemoveFile(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(path, error);
}

std::filesystem::path MakeTempAgentFilePath(const std::string& prefix, const std::string& extension) {
#if defined(_WIN32)
    const std::string processId = std::to_string(GetCurrentProcessId());
#else
    const std::string processId = std::to_string(getpid());
#endif
    const std::string stamp =
        processId + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / (prefix + "-" + stamp + extension);
}

ProcessResult RunProcess(const std::string& executablePath, const std::vector<std::string>& arguments,
                         const std::filesystem::path& workingDirectory, unsigned long timeoutMs,
                         const std::string* stdinText = nullptr,
                         ProcessOutputCallback outputCallback = {}) {
    ProcessResult result;

    const std::string stamp = std::to_string(GetCurrentProcessId()) + "-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path base = std::filesystem::temp_directory_path() / ("aine-agent-" + stamp);
    const std::filesystem::path stdoutPath = base.string() + ".out";
    const std::filesystem::path stderrPath = base.string() + ".err";
    const std::filesystem::path stdinPath = base.string() + ".in";

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    securityAttributes.bInheritHandle = TRUE;

    const std::wstring stdoutWide = stdoutPath.wstring();
    const std::wstring stderrWide = stderrPath.wstring();
    HANDLE stdoutHandle = CreateFileW(stdoutWide.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      &securityAttributes, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    HANDLE stderrHandle = CreateFileW(stderrWide.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      &securityAttributes, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (stdinText != nullptr) {
        std::ofstream stdinOutput(stdinPath, std::ios::binary);
        stdinOutput << *stdinText;
        stdinOutput.close();
        if (!stdinOutput.good()) {
            result.launchError = "Failed to create subprocess stdin file.";
            if (stdoutHandle != INVALID_HANDLE_VALUE) {
                CloseHandle(stdoutHandle);
                TryRemoveFile(stdoutPath);
            }
            if (stderrHandle != INVALID_HANDLE_VALUE) {
                CloseHandle(stderrHandle);
                TryRemoveFile(stderrPath);
            }
            return result;
        }
    }

    const std::wstring stdinWide = stdinText == nullptr ? std::wstring{} : stdinPath.wstring();
    HANDLE stdinHandle = stdinText == nullptr
                             ? CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ, &securityAttributes, OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL, nullptr)
                             : CreateFileW(stdinWide.c_str(), GENERIC_READ, FILE_SHARE_READ, &securityAttributes,
                                           OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY, nullptr);

    if (stdinHandle == INVALID_HANDLE_VALUE) {
        result.launchError = "Failed to create subprocess stdin handle.";
        if (stdoutHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(stdoutHandle);
        }
        if (stderrHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(stderrHandle);
        }
        TryRemoveFile(stdoutPath);
        TryRemoveFile(stderrPath);
        if (stdinText != nullptr) {
            TryRemoveFile(stdinPath);
        }
        return result;
    }

    if (stdoutHandle == INVALID_HANDLE_VALUE || stderrHandle == INVALID_HANDLE_VALUE) {
        result.launchError = "Failed to create subprocess capture files.";
        if (stdoutHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(stdoutHandle);
        }
        if (stderrHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(stderrHandle);
        }
        if (stdinHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(stdinHandle);
        }
        if (stdinText != nullptr) {
            TryRemoveFile(stdinPath);
        }
        return result;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = stdoutHandle;
    startupInfo.hStdError = stderrHandle;
    startupInfo.hStdInput = stdinHandle == INVALID_HANDLE_VALUE ? nullptr : stdinHandle;

    PROCESS_INFORMATION processInfo{};
    const bool isBatchFile = IsWindowsBatchFile(executablePath);
    std::wstring executableWide = isBatchFile ? std::wstring{} : Utf8ToWide(executablePath);
    std::wstring commandLine = Utf8ToWide(isBatchFile ? BuildBatchCommandLine(executablePath, arguments)
                                                      : BuildWindowsCommandLine(executablePath, arguments));
    std::wstring workingDirectoryWide = workingDirectory.empty() ? std::wstring{} : workingDirectory.wstring();

    HANDLE jobHandle = CreateJobObjectW(nullptr, nullptr);
    if (jobHandle != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
        jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(jobHandle, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo))) {
            CloseHandle(jobHandle);
            jobHandle = nullptr;
        }
    }

    const BOOL created = CreateProcessW(executableWide.empty() ? nullptr : executableWide.c_str(), commandLine.data(), nullptr,
                                        nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        workingDirectoryWide.empty() ? nullptr : workingDirectoryWide.c_str(),
                                        &startupInfo, &processInfo);

    CloseHandle(stdoutHandle);
    CloseHandle(stderrHandle);
    if (stdinHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(stdinHandle);
    }

    if (!created) {
        result.launchErrorCode = GetLastError();
        result.launchError = WindowsErrorMessage(static_cast<DWORD>(result.launchErrorCode));
        result.stdoutText = ReadWholeFile(stdoutPath);
        result.stderrText = ReadWholeFile(stderrPath);
        TryRemoveFile(stdoutPath);
        TryRemoveFile(stderrPath);
        if (stdinText != nullptr) {
            TryRemoveFile(stdinPath);
        }
        if (jobHandle != nullptr) {
            CloseHandle(jobHandle);
        }
        return result;
    }

    result.launched = true;
    if (jobHandle != nullptr && !AssignProcessToJobObject(jobHandle, processInfo.hProcess)) {
        CloseHandle(jobHandle);
        jobHandle = nullptr;
    }

    size_t stdoutReported = 0;
    size_t stderrReported = 0;
    auto reportOutputDeltas = [&]() {
        if (!outputCallback) {
            return;
        }
        const std::string stdoutText = ReadWholeFile(stdoutPath);
        const std::string stderrText = ReadWholeFile(stderrPath);
        std::string stdoutDelta;
        std::string stderrDelta;
        if (stdoutText.size() > stdoutReported) {
            stdoutDelta = stdoutText.substr(stdoutReported);
            stdoutReported = stdoutText.size();
        }
        if (stderrText.size() > stderrReported) {
            stderrDelta = stderrText.substr(stderrReported);
            stderrReported = stderrText.size();
        }
        if (!stdoutDelta.empty() || !stderrDelta.empty()) {
            outputCallback(stdoutDelta, stderrDelta);
        }
    };

    const auto started = std::chrono::steady_clock::now();
    DWORD waitResult = WAIT_TIMEOUT;
    while (true) {
        waitResult = WaitForSingleObject(processInfo.hProcess, outputCallback ? 250 : timeoutMs);
        reportOutputDeltas();
        if (waitResult != WAIT_TIMEOUT) {
            break;
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - started)
                                   .count();
        if (static_cast<unsigned long>(std::max<long long>(0, elapsedMs)) >= timeoutMs) {
            result.timedOut = true;
            if (jobHandle != nullptr) {
                TerminateJobObject(jobHandle, 124);
            } else {
                TerminateProcess(processInfo.hProcess, 124);
            }
            WaitForSingleObject(processInfo.hProcess, 5000);
            reportOutputDeltas();
            break;
        }
    }

    DWORD exitCode = 0;
    if (GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        result.exitCode = static_cast<int>(exitCode);
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    if (jobHandle != nullptr) {
        CloseHandle(jobHandle);
    }

    result.stdoutText = ReadWholeFile(stdoutPath);
    result.stderrText = ReadWholeFile(stderrPath);
    TryRemoveFile(stdoutPath);
    TryRemoveFile(stderrPath);
    if (stdinText != nullptr) {
        TryRemoveFile(stdinPath);
    }
    return result;
}
#else
std::string FindExecutableOnPath(const std::string& executableName) {
    const char* pathValue = std::getenv("PATH");
    if (pathValue == nullptr) {
        return {};
    }

    std::stringstream pathStream(pathValue);
    std::string pathEntry;
    while (std::getline(pathStream, pathEntry, ':')) {
        std::filesystem::path candidate = std::filesystem::path(pathEntry) / executableName;
        if (access(candidate.string().c_str(), X_OK) == 0) {
            return candidate.string();
        }
    }
    return {};
}

std::string QuoteShellArgument(const std::string& argument) {
    std::string result = "'";
    for (char ch : argument) {
        if (ch == '\'') {
            result += "'\\''";
        } else {
            result.push_back(ch);
        }
    }
    result.push_back('\'');
    return result;
}

ProcessResult RunProcess(const std::string& executablePath, const std::vector<std::string>& arguments,
                         const std::filesystem::path& workingDirectory, unsigned long,
                         const std::string* stdinText = nullptr,
                         ProcessOutputCallback outputCallback = {}) {
    ProcessResult result;
    const std::string stamp = std::to_string(getpid()) + "-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path base = std::filesystem::temp_directory_path() / ("aine-agent-" + stamp);
    const std::filesystem::path stdoutPath = base.string() + ".out";
    const std::filesystem::path stderrPath = base.string() + ".err";
    const std::filesystem::path stdinPath = base.string() + ".in";

    if (stdinText != nullptr) {
        std::ofstream stdinOutput(stdinPath, std::ios::binary);
        stdinOutput << *stdinText;
        stdinOutput.close();
        if (!stdinOutput.good()) {
            result.launchError = "Failed to create subprocess stdin file.";
            return result;
        }
    }

    std::string command = "cd " + QuoteShellArgument(workingDirectory.string()) + " && " + QuoteShellArgument(executablePath);
    for (const std::string& argument : arguments) {
        command += " " + QuoteShellArgument(argument);
    }
    if (stdinText != nullptr) {
        command += " < " + QuoteShellArgument(stdinPath.string());
    }
    command += " > " + QuoteShellArgument(stdoutPath.string()) + " 2> " + QuoteShellArgument(stderrPath.string());

    const int code = std::system(command.c_str());
    result.launched = true;
    result.exitCode = WIFEXITED(code) ? WEXITSTATUS(code) : code;
    result.stdoutText = ReadWholeFile(stdoutPath);
    result.stderrText = ReadWholeFile(stderrPath);
    if (outputCallback && (!result.stdoutText.empty() || !result.stderrText.empty())) {
        outputCallback(result.stdoutText, result.stderrText);
    }
    std::filesystem::remove(stdoutPath);
    std::filesystem::remove(stderrPath);
    if (stdinText != nullptr) {
        std::filesystem::remove(stdinPath);
    }
    return result;
}
#endif

bool IsAccessDeniedFailure(const ProcessResult& result) {
#if defined(_WIN32)
    return !result.launched && result.launchErrorCode == ERROR_ACCESS_DENIED;
#else
    return !result.launched && ToLower(result.launchError).find("permission") != std::string::npos;
#endif
}

std::string ProcessFailureSummary(const ProcessResult& result) {
    if (result.timedOut) {
        return result.commandLabel + " timed out.";
    }
    if (!result.launched) {
        return result.commandLabel + " launch failed: " + result.launchError;
    }
    std::ostringstream stream;
    stream << result.commandLabel << " exited with code " << result.exitCode;
    const std::string output = Trim(result.stderrText.empty() ? result.stdoutText : result.stderrText);
    if (!output.empty()) {
        stream << ": " << TruncateForDisplay(output, 260);
    }
    return stream.str();
}

struct CandidateProbe {
    ProviderCandidate candidate;
    ProcessResult result;
    std::string methodLabel;
    std::string status;
    bool ready = false;
    bool notFound = false;
    bool notAuthenticated = false;
    bool accessDenied = false;
    bool timedOut = false;
    bool windowsAppsAlias = false;
    std::string detail;
};

std::string ProbeOutputSummary(const ProcessResult& result) {
    std::ostringstream stream;
    stream << "launched=" << (result.launched ? "true" : "false")
           << " exit=" << result.exitCode
           << " timedOut=" << (result.timedOut ? "true" : "false");
    if (!result.launchError.empty()) {
        stream << " launchError=" << TruncateForDisplay(result.launchError, 220);
    }
    if (!result.stdoutText.empty()) {
        stream << " stdout=" << TruncateForDisplay(Trim(result.stdoutText), 220);
    }
    if (!result.stderrText.empty()) {
        stream << " stderr=" << TruncateForDisplay(Trim(result.stderrText), 220);
    }
    return stream.str();
}

bool OutputLooksLikeCodexNotFound(const ProcessResult& result) {
    const std::string combined = ToLower(result.stdoutText + "\n" + result.stderrText + "\n" + result.launchError);
    return combined.find("codex") != std::string::npos &&
           (combined.find("not recognized") != std::string::npos ||
            combined.find("not found") != std::string::npos ||
            combined.find("could not be found") != std::string::npos ||
            combined.find("is not recognized") != std::string::npos ||
            combined.find("the term 'codex'") != std::string::npos);
}

bool OutputLooksLikeCodexNotAuthenticated(const ProcessResult& result) {
    const std::string combined = ToLower(result.stdoutText + "\n" + result.stderrText + "\n" + result.launchError);
    return combined.find("not authenticated") != std::string::npos ||
           combined.find("not logged in") != std::string::npos ||
           combined.find("login required") != std::string::npos ||
           combined.find("authentication required") != std::string::npos ||
           combined.find("please log in") != std::string::npos ||
           combined.find("run codex login") != std::string::npos;
}

bool OutputLooksLikeCodexLoggedIn(const ProcessResult& result) {
    const std::string combined = ToLower(result.stdoutText + "\n" + result.stderrText);
    return combined.find("logged in") != std::string::npos &&
           combined.find("not logged in") == std::string::npos &&
           combined.find("not authenticated") == std::string::npos;
}

bool OutputLooksLikeOpenAIApiKeyFailure(const ProcessResult& result) {
    const std::string combined = ToLower(result.stdoutText + "\n" + result.stderrText + "\n" + result.launchError);
    return combined.find("api key") != std::string::npos ||
           combined.find("unauthorized") != std::string::npos ||
           combined.find("incorrect api key") != std::string::npos ||
           combined.find("invalid_api_key") != std::string::npos ||
           combined.find("401") != std::string::npos;
}

std::string FormatProbeDetail(const std::string& methodLabel, const std::string& target,
                              const std::string& status, const std::string& summary) {
    std::ostringstream stream;
    stream << methodLabel << " [" << status << "] target=" << target;
    if (!summary.empty()) {
        stream << " | " << summary;
    }
    return stream.str();
}

CandidateProbe ProbeCodexDirectCandidate(const ProviderCandidate& candidate, const std::string& methodLabel) {
    CandidateProbe probe;
    probe.candidate = candidate;
    probe.methodLabel = methodLabel;

    if (IsWindowsAppsPath(candidate.path)) {
        probe.windowsAppsAlias = true;
        probe.status = "WindowsApps alias inaccessible";
        probe.detail = FormatProbeDetail(methodLabel, candidate.path, probe.status,
                                         "skipped package-private WindowsApps path; shell aliases must resolve codex");
        return probe;
    }

    std::error_code error;
    if (!std::filesystem::exists(candidate.path, error) || error) {
        probe.notFound = true;
        probe.status = "Not found";
        probe.detail = FormatProbeDetail(methodLabel, candidate.path, probe.status, "candidate does not exist");
        return probe;
    }
    if (std::filesystem::is_directory(candidate.path, error) && !error) {
        probe.status = "Launch failed";
        probe.detail = FormatProbeDetail(methodLabel, candidate.path, probe.status,
                                         "candidate is a directory, not an executable");
        return probe;
    }

    ProcessResult result = RunProcess(candidate.path, {"--version"}, std::filesystem::current_path(), 5000);
    result.commandLabel = methodLabel + " codex --version";
    probe.result = result;

    if (result.launched && !result.timedOut && result.exitCode == 0) {
        ProcessResult loginResult = RunProcess(candidate.path, {"login", "status"}, std::filesystem::current_path(), 5000);
        loginResult.commandLabel = methodLabel + " codex login status";
        probe.result = loginResult;
        if (loginResult.launched && !loginResult.timedOut && loginResult.exitCode == 0 &&
            OutputLooksLikeCodexLoggedIn(loginResult)) {
            probe.ready = true;
            probe.status = "Connected";
            probe.detail = FormatProbeDetail(methodLabel, candidate.path, probe.status,
                                             "version: " + ProbeOutputSummary(result) +
                                                 " | auth: " + ProbeOutputSummary(loginResult));
            return probe;
        }

        if (loginResult.timedOut) {
            probe.timedOut = true;
            probe.status = "Auth check timed out";
        } else if (OutputLooksLikeCodexNotAuthenticated(loginResult) || loginResult.exitCode != 0) {
            probe.notAuthenticated = true;
            probe.status = "Login required";
        } else {
            probe.status = "Installed, auth unknown";
        }
        probe.detail = FormatProbeDetail(methodLabel, candidate.path, probe.status,
                                         "version: " + ProbeOutputSummary(result) +
                                             " | auth: " + ProbeOutputSummary(loginResult));
        return probe;
    }

    if (result.timedOut) {
        probe.timedOut = true;
        probe.status = "Timed out";
    } else if (OutputLooksLikeCodexNotAuthenticated(result)) {
        probe.notAuthenticated = true;
        probe.status = "Not authenticated";
    } else if (OutputLooksLikeCodexNotFound(result)) {
        probe.notFound = true;
        probe.status = "Not found";
    } else {
        probe.status = "Launch failed";
    }
    probe.detail = FormatProbeDetail(methodLabel, candidate.path, probe.status, ProbeOutputSummary(result));
    return probe;
}

#if defined(_WIN32)
CandidateProbe ProbeCodexCommandShim(const std::string& methodLabel, const std::string& executablePath,
                                     const std::vector<std::string>& arguments) {
    CandidateProbe probe;
    probe.candidate = {executablePath, methodLabel};
    probe.methodLabel = methodLabel;

    ProcessResult result = RunProcess(executablePath, arguments, std::filesystem::current_path(), 5000);
    result.commandLabel = methodLabel + " codex --version";
    probe.result = result;

    if (result.launched && !result.timedOut && result.exitCode == 0) {
        std::vector<std::string> loginArguments;
        if (methodLabel == "cmd shim") {
            loginArguments = {"/d", "/s", "/c", BuildCodexCmdCommand({"login", "status"})};
        } else {
            loginArguments = {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                              BuildCodexPowerShellCommand({"login", "status"})};
        }
        ProcessResult loginResult = RunProcess(executablePath, loginArguments, std::filesystem::current_path(), 5000);
        loginResult.commandLabel = methodLabel + " codex login status";
        probe.result = loginResult;
        if (loginResult.launched && !loginResult.timedOut && loginResult.exitCode == 0 &&
            OutputLooksLikeCodexLoggedIn(loginResult)) {
            probe.ready = true;
            probe.status = "Connected";
            probe.detail = FormatProbeDetail(methodLabel, executablePath, probe.status,
                                             "version: " + ProbeOutputSummary(result) +
                                                 " | auth: " + ProbeOutputSummary(loginResult));
            return probe;
        }

        if (loginResult.timedOut) {
            probe.timedOut = true;
            probe.status = "Auth check timed out";
        } else if (OutputLooksLikeCodexNotAuthenticated(loginResult) || loginResult.exitCode != 0) {
            probe.notAuthenticated = true;
            probe.status = "Login required";
        } else {
            probe.status = "Installed, auth unknown";
        }
        probe.detail = FormatProbeDetail(methodLabel, executablePath, probe.status,
                                         "version: " + ProbeOutputSummary(result) +
                                             " | auth: " + ProbeOutputSummary(loginResult));
        return probe;
    }

    if (result.timedOut) {
        probe.timedOut = true;
        probe.status = "Timed out";
    } else if (!result.launched) {
        probe.status = "Launch failed";
    } else if (OutputLooksLikeCodexNotAuthenticated(result)) {
        probe.notAuthenticated = true;
        probe.status = "Not authenticated";
    } else if (OutputLooksLikeCodexNotFound(result)) {
        probe.notFound = true;
        probe.status = "Not found";
    } else {
        probe.status = "Launch failed";
    }

    probe.detail = FormatProbeDetail(methodLabel, executablePath, probe.status, ProbeOutputSummary(result));
    return probe;
}
#endif

void PopulateCodexProviderHealth(ProviderHealth& health) {
    CandidateProbe firstWindowsApps;
    CandidateProbe firstTimedOut;
    CandidateProbe firstNotAuthenticated;
    CandidateProbe firstLaunchFailed;
    CandidateProbe firstNotFound;
    bool hasWindowsApps = false;
    bool hasTimedOut = false;
    bool hasNotAuthenticated = false;
    bool hasLaunchFailed = false;
    bool hasNonShimLaunchFailed = false;
    bool hasNotFound = false;

    auto rememberFailure = [&](const CandidateProbe& probe) {
        health.probeDetails.push_back(probe.detail);
        if (probe.windowsAppsAlias) {
            if (!hasWindowsApps) {
                firstWindowsApps = probe;
            }
            hasWindowsApps = true;
            return;
        }
        if (probe.timedOut) {
            if (!hasTimedOut) {
                firstTimedOut = probe;
            }
            hasTimedOut = true;
            return;
        }
        if (probe.notAuthenticated) {
            if (!hasNotAuthenticated) {
                firstNotAuthenticated = probe;
            }
            hasNotAuthenticated = true;
            return;
        }
        if (probe.notFound) {
            if (!hasNotFound) {
                firstNotFound = probe;
            }
            hasNotFound = true;
            return;
        }
        if (!hasLaunchFailed) {
            firstLaunchFailed = probe;
        }
        if (probe.methodLabel != "cmd shim" && probe.methodLabel != "PowerShell shim") {
            hasNonShimLaunchFailed = true;
        }
        hasLaunchFailed = true;
    };

    auto acceptReady = [&](const CandidateProbe& probe) {
        health.available = true;
        health.status = probe.status.empty() ? "Connected" : probe.status;
        health.executablePath = probe.candidate.path;
        health.source = probe.methodLabel;
        health.detail = "Codex CLI installed and authenticated via " + probe.methodLabel + ". " + probe.detail;
        health.probeDetails.push_back(probe.detail);
    };

    const std::string overridePath = GetEnvironmentValue("AI_NATIVE_CODEX_PATH");
    if (!overridePath.empty()) {
        CandidateProbe probe = ProbeCodexDirectCandidate({overridePath, "AI_NATIVE_CODEX_PATH"}, "Env override");
        if (probe.ready) {
            acceptReady(probe);
            return;
        }
        rememberFailure(probe);
    }

#if defined(_WIN32)
    for (const ProviderCandidate& candidate : BuildKnownCodexInstallCandidates()) {
        CandidateProbe probe = ProbeCodexDirectCandidate(candidate, "known install");
        if (probe.ready) {
            acceptReady(probe);
            return;
        }
        rememberFailure(probe);
    }

    {
        const std::string cmdPath = WindowsCmdExecutable();
        CandidateProbe probe = ProbeCodexCommandShim("cmd shim", cmdPath,
                                                     {"/d", "/s", "/c", BuildCodexCmdCommand({"--version"})});
        if (probe.ready) {
            acceptReady(probe);
            return;
        }
        rememberFailure(probe);
    }

    {
        const std::string powershellPath = WindowsPowerShellExecutable();
        CandidateProbe probe = ProbeCodexCommandShim(
            "PowerShell shim", powershellPath,
            {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", BuildCodexPowerShellCommand({"--version"})});
        if (probe.ready) {
            acceptReady(probe);
            return;
        }
        rememberFailure(probe);
    }
#else
    const std::string directCodex = FindExecutableOnPath("codex");
    if (!directCodex.empty()) {
        CandidateProbe probe = ProbeCodexDirectCandidate({directCodex, "PATH"}, "direct executable");
        if (probe.ready) {
            acceptReady(probe);
            return;
        }
        rememberFailure(probe);
    }
#endif

    for (const ProviderCandidate& candidate : BuildPathExecutableCandidates("codex")) {
        CandidateProbe probe = ProbeCodexDirectCandidate(candidate, "direct executable");
        if (probe.ready) {
            acceptReady(probe);
            return;
        }
        rememberFailure(probe);
    }

    health.available = false;
    health.executablePath.clear();
    health.source.clear();
    if (hasTimedOut) {
        health.status = "Timed out";
        health.executablePath = firstTimedOut.candidate.path;
        health.source = firstTimedOut.methodLabel;
        health.detail = firstTimedOut.detail;
        return;
    }
    if (hasNotAuthenticated) {
        health.status = "Login required";
        health.executablePath = firstNotAuthenticated.candidate.path;
        health.source = firstNotAuthenticated.methodLabel;
        health.detail = firstNotAuthenticated.detail + ". Run codex login in a terminal, then refresh agent backends.";
        return;
    }
    if (hasWindowsApps && !hasNonShimLaunchFailed) {
        health.status = "WindowsApps alias inaccessible";
        health.executablePath = firstWindowsApps.candidate.path;
        health.source = firstWindowsApps.methodLabel;
        health.detail = firstWindowsApps.detail + ". Tried cmd and PowerShell shims before marking Codex unavailable.";
        return;
    }
    if (hasLaunchFailed) {
        health.status = "Launch failed";
        health.executablePath = firstLaunchFailed.candidate.path;
        health.source = firstLaunchFailed.methodLabel;
        health.detail = firstLaunchFailed.detail;
        return;
    }
    if (hasNotFound) {
        health.status = "Not found";
        health.executablePath = firstNotFound.candidate.path;
        health.source = firstNotFound.methodLabel;
        health.detail = firstNotFound.detail;
        return;
    }

    health.status = "Not found";
    health.detail = "No usable Codex CLI launch strategy was found. Set AI_NATIVE_CODEX_PATH or make codex available in a normal terminal.";
}

void PopulateOpenAIApiProviderHealth(ProviderHealth& health) {
    health.implemented = true;
    health.source = kOpenAIApiKeyEnvironmentVariable;
    if (GetEnvironmentValue(kOpenAIApiKeyEnvironmentVariable).empty()) {
        health.available = false;
        health.status = "Missing API key";
        health.detail = "OPENAI_API_KEY is not set in the process environment. The editor never stores API keys in project files.";
        return;
    }

    health.available = true;
    health.status = "Ready";
    health.detail = std::string("OPENAI_API_KEY is present. Requests use the OpenAI Responses API with model ") +
                    kOpenAIApiModel + "; the key is read from the environment only and is not stored.";
}

CandidateProbe ProbeProviderCandidate(const ProviderCandidate& candidate) {
    CandidateProbe probe;
    probe.candidate = candidate;

    std::error_code error;
    if (!std::filesystem::exists(candidate.path, error) || error) {
        probe.notFound = true;
        probe.detail = candidate.source + " candidate does not exist: " + candidate.path;
        return probe;
    }
    if (std::filesystem::is_directory(candidate.path, error) && !error) {
        probe.detail = candidate.source + " candidate is a directory, not an executable: " + candidate.path;
        return probe;
    }

    for (const std::string& harmlessArgument : {"--version", "--help"}) {
        ProcessResult result = RunProcess(candidate.path, {harmlessArgument}, std::filesystem::current_path(), 5000);
        result.commandLabel = harmlessArgument;
        probe.result = result;

        if (result.launched && !result.timedOut && result.exitCode == 0) {
            probe.ready = true;
            const std::string output = Trim(result.stdoutText.empty() ? result.stderrText : result.stdoutText);
            probe.detail = candidate.source + " candidate passed " + harmlessArgument;
            if (!output.empty()) {
                probe.detail += ": " + TruncateForDisplay(output, 260);
            }
            return probe;
        }

        if (IsAccessDeniedFailure(result)) {
            probe.accessDenied = true;
            probe.detail = candidate.source + " candidate access denied: " + candidate.path + " | " + result.launchError;
            return probe;
        }

        if (result.timedOut) {
            probe.timedOut = true;
            probe.detail = candidate.source + " candidate timed out: " + candidate.path + " | " + ProcessFailureSummary(result);
            return probe;
        }
    }

    probe.detail = candidate.source + " candidate failed health check: " + candidate.path + " | " +
                   ProcessFailureSummary(probe.result);
    return probe;
}

void PopulateCliProviderHealth(ProviderHealth& health, const std::string& executableName, const char* environmentVariable,
                               const std::string& unavailableHint, const std::string& readyDetailSuffix) {
    const std::vector<ProviderCandidate> candidates = BuildExecutableCandidates(executableName, environmentVariable);
    if (candidates.empty()) {
        health.available = false;
        health.status = "Not found";
        health.detail = "No " + executableName + " executable was found on PATH. " + unavailableHint;
        return;
    }

    CandidateProbe firstAccessDenied;
    CandidateProbe firstTimedOut;
    CandidateProbe firstLaunchFailed;
    CandidateProbe firstNotFound;
    bool hasAccessDenied = false;
    bool hasTimedOut = false;
    bool hasLaunchFailed = false;
    bool hasNotFound = false;

    for (const ProviderCandidate& candidate : candidates) {
        CandidateProbe probe = ProbeProviderCandidate(candidate);
        if (probe.ready) {
            health.available = true;
            health.status = "Ready";
            health.executablePath = candidate.path;
            health.source = candidate.source;
            health.detail = probe.detail + readyDetailSuffix;
            return;
        }

        if (probe.accessDenied) {
            if (!hasAccessDenied) {
                firstAccessDenied = probe;
            }
            hasAccessDenied = true;
            continue;
        }

        if (probe.timedOut) {
            if (!hasTimedOut) {
                firstTimedOut = probe;
            }
            hasTimedOut = true;
            continue;
        }

        if (probe.notFound) {
            if (!hasNotFound) {
                firstNotFound = probe;
            }
            hasNotFound = true;
            continue;
        }

        if (!hasLaunchFailed) {
            firstLaunchFailed = probe;
        }
        hasLaunchFailed = true;
    }

    health.available = false;
    if (hasAccessDenied) {
        health.status = "Access denied";
        health.executablePath = firstAccessDenied.candidate.path;
        health.source = firstAccessDenied.candidate.source;
        health.detail = (IsWindowsAppsPath(firstAccessDenied.candidate.path) ? "Skipped inaccessible WindowsApps/app-alias entry. " : "") +
                        firstAccessDenied.detail;
        return;
    }

    if (hasTimedOut) {
        health.status = "Timed out";
        health.executablePath = firstTimedOut.candidate.path;
        health.source = firstTimedOut.candidate.source;
        health.detail = firstTimedOut.detail;
        return;
    }

    if (hasLaunchFailed) {
        health.status = "Launch failed";
        health.executablePath = firstLaunchFailed.candidate.path;
        health.source = firstLaunchFailed.candidate.source;
        health.detail = firstLaunchFailed.detail;
        return;
    }

    health.status = "Not found";
    if (hasNotFound) {
        health.executablePath = firstNotFound.candidate.path;
        health.source = firstNotFound.candidate.source;
        health.detail = firstNotFound.detail + ". " + unavailableHint;
    } else {
        health.detail = "No usable " + executableName + " executable was found. " + unavailableHint;
    }
}

ProcessResult RunCodexProcess(const ProviderHealth& health, const std::vector<std::string>& codexArguments,
                              const std::filesystem::path& workingDirectory, unsigned long timeoutMs,
                              const std::string* stdinText = nullptr,
                              ProcessOutputCallback outputCallback = {}) {
#if defined(_WIN32)
    if (health.source == "cmd shim") {
        ProcessResult result = RunProcess(WindowsCmdExecutable(),
                                          {"/d", "/s", "/c", BuildCodexCmdCommand(codexArguments)},
                                          workingDirectory, timeoutMs, stdinText, outputCallback);
        result.commandLabel = "cmd shim codex";
        return result;
    }
    if (health.source == "PowerShell shim") {
        ProcessResult result = RunProcess(
            WindowsPowerShellExecutable(),
            {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", BuildCodexPowerShellCommand(codexArguments)},
            workingDirectory, timeoutMs, stdinText, outputCallback);
        result.commandLabel = "PowerShell shim codex";
        return result;
    }
#endif

    ProcessResult result =
        RunProcess(health.executablePath, codexArguments, workingDirectory, timeoutMs, stdinText, outputCallback);
    result.commandLabel = health.source.empty() ? "direct executable codex" : health.source + " codex";
    return result;
}

std::string CodexSandboxMode() {
    const char* overrideValue = std::getenv("AI_NATIVE_CODEX_SANDBOX");
    if (overrideValue != nullptr && overrideValue[0] != '\0') {
        return overrideValue;
    }

#if defined(_WIN32)
    return "danger-full-access";
#else
    return "read-only";
#endif
}

bool EnvironmentFlagEnabled(const char* name, bool defaultValue) {
    const std::string value = ToLower(GetEnvironmentValue(name));
    if (value.empty()) {
        return defaultValue;
    }
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

std::string CodexEmbeddedModeLabel() {
    if (EnvironmentFlagEnabled("AI_NATIVE_CODEX_USE_USER_CONFIG", false)) {
        return "user config";
    }
    return "lean embedded config";
}

std::vector<std::string> BuildCodexExecArguments(const std::string& sandboxMode,
                                                 const std::filesystem::path& lastMessagePath = {}) {
    std::vector<std::string> arguments;
    arguments.push_back("exec");

    const bool useUserConfig = EnvironmentFlagEnabled("AI_NATIVE_CODEX_USE_USER_CONFIG", false);
    if (!useUserConfig) {
        arguments.push_back("--ignore-user-config");
    }

    if (EnvironmentFlagEnabled("AI_NATIVE_CODEX_EPHEMERAL", !useUserConfig)) {
        arguments.push_back("--ephemeral");
    }

    const std::string modelOverride = GetEnvironmentValue("AI_NATIVE_CODEX_MODEL");
    if (!modelOverride.empty()) {
        arguments.push_back("--model");
        arguments.push_back(modelOverride);
    }

    const std::string reasoningEffort = GetEnvironmentValue("AI_NATIVE_CODEX_REASONING_EFFORT");
    if (!reasoningEffort.empty()) {
        arguments.push_back("--config");
        arguments.push_back("model_reasoning_effort=\"" + reasoningEffort + "\"");
    }

    arguments.push_back("--color");
    arguments.push_back("never");
    arguments.push_back("--json");
    if (!lastMessagePath.empty()) {
        arguments.push_back("--output-last-message");
        arguments.push_back(lastMessagePath.string());
    }
    arguments.push_back("--sandbox");
    arguments.push_back(sandboxMode);
    arguments.push_back("--skip-git-repo-check");
    arguments.push_back("-");
    return arguments;
}

unsigned long CodexTimeoutMs() {
    const char* overrideValue = std::getenv("AI_NATIVE_CODEX_TIMEOUT_MS");
    if (overrideValue == nullptr || overrideValue[0] == '\0') {
        return EnvironmentFlagEnabled("AI_NATIVE_CODEX_USE_USER_CONFIG", false) ? 300000 : 180000;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(overrideValue, &end, 10);
    if (end == overrideValue || parsed < 30000) {
        return 300000;
    }
    return std::min<unsigned long>(parsed, 900000);
}

void PublishAgentProgress(const AgentProgressCallback& progress, EditorState& state, std::string label,
                          std::string status, std::string detail) {
    state.AddActivity(label, status, detail);
    if (progress) {
        progress(AgentActivity{std::move(label), std::move(status), std::move(detail)});
    }
}

std::string JsonStringValue(const json& document, const char* key) {
    if (!document.contains(key) || !document.at(key).is_string()) {
        return {};
    }
    return document.at(key).get<std::string>();
}

std::string CodexEventDetail(const json& document) {
    const std::string type = JsonStringValue(document, "type");
    if (type == "thread.started") {
        const std::string threadId = JsonStringValue(document, "thread_id");
        return threadId.empty() ? "Codex thread started." : "Thread " + threadId + " started.";
    }
    if (type == "turn.started") {
        return "Codex turn started.";
    }
    if (type == "turn.completed") {
        if (document.contains("usage") && document.at("usage").is_object()) {
            const json& usage = document.at("usage");
            const int inputTokens = usage.value("input_tokens", 0);
            const int outputTokens = usage.value("output_tokens", 0);
            const int reasoningTokens = usage.value("reasoning_output_tokens", 0);
            std::ostringstream stream;
            stream << "Turn completed. input=" << inputTokens << " output=" << outputTokens;
            if (reasoningTokens > 0) {
                stream << " reasoning=" << reasoningTokens;
            }
            return stream.str();
        }
        return "Codex turn completed.";
    }
    if (type == "item.started") {
        if (document.contains("item") && document.at("item").is_object()) {
            const std::string itemType = JsonStringValue(document.at("item"), "type");
            return itemType.empty() ? "Started an item." : "Started " + itemType + ".";
        }
        return "Started an item.";
    }
    if (type == "item.completed") {
        if (document.contains("item") && document.at("item").is_object()) {
            const json& item = document.at("item");
            const std::string itemType = JsonStringValue(item, "type");
            const std::string text = JsonStringValue(item, "text");
            if (!text.empty()) {
                return "Completed " + (itemType.empty() ? std::string("item") : itemType) + ": " +
                       TruncateForDisplay(Trim(text), 220);
            }
            const std::string command = JsonStringValue(item, "command");
            if (!command.empty()) {
                return "Completed command: " + TruncateForDisplay(command, 220);
            }
            return itemType.empty() ? "Completed an item." : "Completed " + itemType + ".";
        }
        return "Completed an item.";
    }
    if (type == "error") {
        const std::string message = JsonStringValue(document, "message");
        return message.empty() ? "Codex reported an error." : message;
    }

    return type.empty() ? "Codex emitted an event." : "Codex emitted " + type + ".";
}

void PublishCodexJsonEvents(const std::string& chunk, std::string& lineBuffer, EditorState& state,
                            const AgentProgressCallback& progress) {
    lineBuffer += chunk;
    size_t newline = std::string::npos;
    while ((newline = lineBuffer.find('\n')) != std::string::npos) {
        std::string line = Trim(lineBuffer.substr(0, newline));
        lineBuffer.erase(0, newline + 1);
        if (line.empty()) {
            continue;
        }
        try {
            json document = json::parse(line);
            const std::string type = JsonStringValue(document, "type");
            PublishAgentProgress(progress, state, "Codex event", type.empty() ? "Event" : type,
                                 CodexEventDetail(document));
        } catch (const std::exception&) {
            PublishAgentProgress(progress, state, "Codex output", "stdout", TruncateForDisplay(line, 240));
        }
    }
}

bool WriteUtf8File(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output << text;
    return output.good();
}

ProcessResult RunOpenAIApiProcess(const std::string& providerPrompt,
                                  const std::filesystem::path& workingDirectory,
                                  unsigned long timeoutMs) {
#if defined(_WIN32)
    ProcessResult result;
    const std::string stamp = std::to_string(GetCurrentProcessId()) + "-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path base = std::filesystem::temp_directory_path() / ("aine-openai-" + stamp);
    const std::filesystem::path promptPath = base.string() + ".prompt.txt";
    const std::filesystem::path scriptPath = base.string() + ".ps1";

    const std::string script = R"PS1(
param(
    [string]$PromptPath,
    [string]$Model,
    [string]$Uri
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$key = $env:OPENAI_API_KEY
if ([string]::IsNullOrWhiteSpace($key)) {
    [Console]::Error.WriteLine('OPENAI_API_KEY is missing from the process environment.')
    exit 2
}
$prompt = Get-Content -LiteralPath $PromptPath -Raw -Encoding UTF8
$body = @{
    model = $Model
    input = @(
        @{
            role = 'user'
            content = $prompt
        }
    )
} | ConvertTo-Json -Depth 20
try {
    $response = Invoke-RestMethod -Method Post -Uri $Uri -Headers @{
        Authorization = "Bearer $key"
        'Content-Type' = 'application/json'
    } -Body $body
    if ($null -ne $response.output_text -and [string]$response.output_text -ne '') {
        [Console]::Out.WriteLine($response.output_text)
        exit 0
    }
    $textParts = @()
    if ($null -ne $response.output) {
        foreach ($item in $response.output) {
            if ($null -ne $item.content) {
                foreach ($content in $item.content) {
                    if ($null -ne $content.text -and [string]$content.text -ne '') {
                        $textParts += [string]$content.text
                    }
                }
            }
        }
    }
    if ($textParts.Count -gt 0) {
        [Console]::Out.WriteLine(($textParts -join "`n"))
        exit 0
    }
    [Console]::Out.WriteLine(($response | ConvertTo-Json -Depth 32))
    exit 0
} catch {
    $message = $_.Exception.Message
    if ($null -ne $_.Exception.Response) {
        try {
            $stream = $_.Exception.Response.GetResponseStream()
            if ($null -ne $stream) {
                $reader = New-Object System.IO.StreamReader($stream)
                $bodyText = $reader.ReadToEnd()
                if (![string]::IsNullOrWhiteSpace($bodyText)) {
                    $message = "$message $bodyText"
                }
            }
        } catch {}
    }
    [Console]::Error.WriteLine($message)
    exit 1
}
)PS1";

    if (!WriteUtf8File(promptPath, providerPrompt) || !WriteUtf8File(scriptPath, script)) {
        result.launchError = "Failed to create temporary OpenAI API request files.";
        TryRemoveFile(promptPath);
        TryRemoveFile(scriptPath);
        return result;
    }

    result = RunProcess(WindowsPowerShellExecutable(),
                        {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", scriptPath.string(),
                         promptPath.string(), kOpenAIApiModel, kOpenAIResponsesEndpoint},
                        workingDirectory, timeoutMs);
    result.commandLabel = "OpenAI API Responses request";
    TryRemoveFile(promptPath);
    TryRemoveFile(scriptPath);
    return result;
#else
    (void)providerPrompt;
    (void)workingDirectory;
    (void)timeoutMs;
    ProcessResult result;
    result.launchError = "OpenAI API backend is currently implemented through Windows PowerShell transport.";
    result.commandLabel = "OpenAI API Responses request";
    return result;
#endif
}

} // namespace

std::string ProjectContextSnapshot::Summary() const {
    std::ostringstream stream;
    stream << "path=" << projectPath << " | scene=" << sceneName << " | saves=" << gameSavePath
           << " | selected=" << selectedEntity
           << " | entities=" << entities.size() << " | chat=" << recentChat.size()
           << " | logs=" << recentLogs.size()
           << " | gatewayDiagnostics=" << toolGatewayDiagnostics.size();
    return stream.str();
}

std::string ProjectContextSnapshot::ToCompactText() const {
    std::ostringstream stream;
    stream << "Project path: " << projectPath << "\n";
    stream << "Project file: " << projectFilePath << "\n";
    stream << "Scene file: " << sceneFilePath << "\n";
    stream << "Game save folder: " << gameSavePath << "\n";
    stream << "Scene name: " << sceneName << "\n";
    stream << "Selected entity: " << selectedEntity << "\n";
    stream << "Entities:\n";
    if (entities.empty()) {
        stream << "- none\n";
    } else {
        for (const std::string& entity : entities) {
            stream << "- " << entity << "\n";
        }
    }
    stream << "Recent conversation:\n";
    if (recentChat.empty()) {
        stream << "- none\n";
    } else {
        for (const std::string& chatTurn : recentChat) {
            stream << "- " << chatTurn << "\n";
        }
    }
    stream << "Recent logs:\n";
    if (recentLogs.empty()) {
        stream << "- none\n";
    } else {
        for (const std::string& log : recentLogs) {
            stream << "- " << log << "\n";
        }
    }
    stream << "Tool gateway diagnostics:\n";
    if (toolGatewayDiagnostics.empty()) {
        stream << "- none\n";
    } else {
        for (const std::string& diagnostic : toolGatewayDiagnostics) {
            stream << "- " << diagnostic << "\n";
        }
    }
    return stream.str();
}

AgentOrchestrator::AgentOrchestrator() {
    RefreshProviderHealth();
}

void AgentOrchestrator::RefreshProviderHealth(EditorState* state) {
    providers_.clear();
    providers_.push_back(BuildProviderHealth(AgentBackend::LocalToolGateway));
    providers_.push_back(BuildProviderHealth(AgentBackend::CodexCli));
    providers_.push_back(BuildProviderHealth(AgentBackend::OpenAIApi));
    providers_.push_back(BuildProviderHealth(AgentBackend::Claude));
    providers_.push_back(BuildProviderHealth(AgentBackend::LocalPromptAssistant));

    if (state != nullptr) {
        for (const ProviderHealth& provider : providers_) {
            state->AddActivity(provider.label, provider.status, provider.detail);
            if (provider.backend == AgentBackend::CodexCli) {
                if (provider.available && !provider.source.empty()) {
                    const std::string target = provider.executablePath.empty() ? provider.detail : provider.executablePath;
                    state->AddActivity("Codex launch method", provider.source, target);
                }
                for (const std::string& probeDetail : provider.probeDetails) {
                    state->AddActivity("Codex probe", "Captured", probeDetail);
                }
            }
        }
    }
}

const ProviderHealth* AgentOrchestrator::FindProvider(AgentBackend backend) const {
    const auto it = std::find_if(providers_.begin(), providers_.end(), [backend](const ProviderHealth& provider) {
        return provider.backend == backend;
    });
    return it == providers_.end() ? nullptr : &(*it);
}

AgentOrchestratorResult AgentOrchestrator::SubmitPrompt(AgentBackend backend, const std::string& prompt, EditorState& state,
                                                        ToolGateway& gateway, const ToolCommandBatch* lastGatewayBatch,
                                                        AgentProgressCallback progress) {
    ProjectContextSnapshot context = CaptureProjectContext(state, lastGatewayBatch);
    state.AddActivity("Project context snapshot", "Captured", context.Summary());

    switch (backend) {
    case AgentBackend::CodexCli:
        return SubmitCodexCli(prompt, context, state, gateway, gateway.SupportedCommands(), std::move(progress));
    case AgentBackend::OpenAIApi:
        return SubmitOpenAIApi(prompt, context, state, gateway, gateway.SupportedCommands());
    case AgentBackend::Claude:
        return SubmitClaudePlaceholder(prompt, context, state, gateway);
    case AgentBackend::LocalPromptAssistant:
        return SubmitLocalPromptAssistant(prompt, context, state);
    case AgentBackend::LocalToolGateway:
    default:
        return SubmitLocalToolGateway(prompt, state, gateway);
    }
}

NormalizedProviderResult AgentOrchestrator::NormalizeProviderOutput(AgentBackend backend, const std::string& output,
                                                                    const ToolGateway& gateway) const {
    ProviderResultNormalizer normalizer;
    return normalizer.Normalize(BackendLabel(backend), output, gateway);
}

ProjectContextSnapshot AgentOrchestrator::CaptureProjectContext(const EditorState& state,
                                                                const ToolCommandBatch* lastGatewayBatch) const {
    ProjectContextSnapshot context;
    context.projectPath = state.ProjectRootPathString();
    context.projectFilePath = state.ProjectFilePathString();
    context.sceneFilePath = state.SceneFilePathString();
    context.gameSavePath = state.GameSaveRootPathString();
    context.sceneName = state.SceneName();

    const Entity* selected = state.FindEntity(state.SelectedEntityId());
    context.selectedEntity = selected == nullptr ? "none" : selected->name;

    for (const Entity& entity : state.Entities()) {
        std::ostringstream stream;
        stream << entity.name << " id=" << entity.id << " pos=(" << Vec3ToText(entity.position) << ")";
        if (!entity.components.empty()) {
            std::vector<std::string> componentNames;
            for (const Component& component : entity.components) {
                componentNames.push_back(component.type);
            }
            stream << " components=[" << JoinStrings(componentNames, ", ") << "]";
        }
        context.entities.push_back(stream.str());
    }

    const std::vector<ChatMessage>& chatHistory = state.ChatHistory();
    const size_t chatStart = chatHistory.size() > 10 ? chatHistory.size() - 10 : 0;
    for (size_t i = chatStart; i < chatHistory.size(); ++i) {
        const ChatMessage& message = chatHistory[i];
        const std::string trimmed = Trim(message.message);
        if (trimmed.empty()) {
            continue;
        }
        context.recentChat.push_back(ChatSenderLabel(message.sender) + ": " + TruncateForDisplay(trimmed, 500));
    }

    const std::vector<LogEntry>& logs = state.Logs();
    const size_t start = logs.size() > 8 ? logs.size() - 8 : 0;
    for (size_t i = start; i < logs.size(); ++i) {
        context.recentLogs.push_back(LogLevelPrefix(logs[i].level) + ": " + logs[i].message);
    }

    if (lastGatewayBatch != nullptr) {
        context.toolGatewayDiagnostics = LastValues(lastGatewayBatch->diagnostics, 8);
    }
    return context;
}

std::vector<std::string> AgentOrchestrator::PromptSuggestions(const std::string& draft, const EditorState& state) const {
    const std::string normalized = ToLower(Trim(draft));
    std::vector<std::string> suggestions;

    auto addSuggestion = [&suggestions](const std::string& suggestion) {
        if (std::find(suggestions.begin(), suggestions.end(), suggestion) == suggestions.end()) {
            suggestions.push_back(suggestion);
        }
    };

    if (normalized.empty()) {
        addSuggestion("review the editor and suggest the top usability improvements");
        addSuggestion("create a starter scene");
        addSuggestion("add a cube named \"Prototype Block\"");
        addSuggestion("validate and save the current scene");
    } else {
        if (normalized.find("suggest") != std::string::npos ||
            normalized.find("improvement") != std::string::npos ||
            normalized.find("improvements") != std::string::npos ||
            normalized.find("review") != std::string::npos ||
            normalized.find("look at") != std::string::npos) {
            addSuggestion("review the editor and suggest the top usability improvements");
        }
        if (normalized.find("scene") != std::string::npos || normalized.find("level") != std::string::npos) {
            addSuggestion("create a starter scene with camera, key light, ground, and player spawn");
        }
        if (normalized.find("cube") != std::string::npos || normalized.find("box") != std::string::npos ||
            normalized.find("object") != std::string::npos) {
            addSuggestion("add a cube named \"Prototype Block\" and save the scene");
        }
        if (normalized.find("save") != std::string::npos || normalized.find("validate") != std::string::npos ||
            normalized.find("check") != std::string::npos) {
            addSuggestion("validate and save the current scene");
        }
        if (normalized.find("codex") != std::string::npos || normalized.find("backend") != std::string::npos) {
            addSuggestion("use Codex CLI to inspect the current project context");
        }
        if (normalized.find("openai") != std::string::npos || normalized.find("api") != std::string::npos ||
            normalized.find("key") != std::string::npos) {
            addSuggestion("use OpenAI API to inspect the current project context");
        }
    }

    const Entity* selected = state.FindEntity(state.SelectedEntityId());
    if (selected != nullptr) {
        addSuggestion("rename selected entity to \"Hero Spawn\"");
    }

    if (suggestions.size() > 4) {
        suggestions.resize(4);
    }
    return suggestions;
}

const char* AgentOrchestrator::BackendLabel(AgentBackend backend) {
    switch (backend) {
    case AgentBackend::CodexCli:
        return "Codex CLI";
    case AgentBackend::OpenAIApi:
        return "OpenAI API";
    case AgentBackend::Claude:
        return "Claude";
    case AgentBackend::LocalPromptAssistant:
        return "Local Prompt Assistant";
    case AgentBackend::LocalToolGateway:
    default:
        return "Local Tool Gateway";
    }
}

const char* AgentOrchestrator::BackendId(AgentBackend backend) {
    switch (backend) {
    case AgentBackend::CodexCli:
        return "codex-cli";
    case AgentBackend::OpenAIApi:
        return "openai-api";
    case AgentBackend::Claude:
        return "claude";
    case AgentBackend::LocalPromptAssistant:
        return "local-prompt-assistant";
    case AgentBackend::LocalToolGateway:
    default:
        return "local-tool-gateway";
    }
}

AgentOrchestratorResult AgentOrchestrator::SubmitLocalToolGateway(const std::string& prompt, EditorState& state,
                                                                  ToolGateway& gateway) {
    AgentOrchestratorResult result;
    result.backend = AgentBackend::LocalToolGateway;
    result.gatewayResult = gateway.SubmitPrompt(prompt, state);
    result.assistantMessage = result.gatewayResult.assistantMessage;
    result.hasGatewayResult = true;
    result.ok = result.gatewayResult.batch.ok;
    return result;
}

AgentOrchestratorResult AgentOrchestrator::SubmitCodexCli(const std::string& prompt, const ProjectContextSnapshot& context,
                                                          EditorState& state, ToolGateway& gateway,
                                                          const std::vector<std::string>& supportedCommands,
                                                          AgentProgressCallback progress) {
    RefreshProviderHealth();
    const ProviderHealth* health = FindProvider(AgentBackend::CodexCli);
    if (health == nullptr || !health->available) {
        std::string reason = "Codex CLI Not found";
        if (health != nullptr) {
            reason = "Codex CLI " + health->status + ": " + health->detail;
            if (!health->executablePath.empty()) {
                reason += " | path=" + health->executablePath;
            }
        }
        (void)gateway;
        (void)supportedCommands;
        return SubmitProviderFailure(AgentBackend::CodexCli, health == nullptr ? "Not found" : health->status, reason,
                                     state);
    }

    AgentOrchestratorResult result;
    result.backend = AgentBackend::CodexCli;
    state.SetAgentStatus("Running Codex CLI");
    const std::string codexSandboxMode = CodexSandboxMode();
    const unsigned long codexTimeoutMs = CodexTimeoutMs();
    const std::filesystem::path codexWorkingDirectory = std::filesystem::current_path();
    PublishAgentProgress(progress, state, "Codex CLI", "Running",
                         "Launch method: " + health->source + " | workspace root: " +
                             codexWorkingDirectory.string() + " | project root: " + context.projectPath +
                             " | sandbox: " + codexSandboxMode +
                             " | mode: " + CodexEmbeddedModeLabel() +
                             " | timeout: " + std::to_string(codexTimeoutMs / 1000) + "s");

    const std::string codexPrompt = BuildCodexPrompt(prompt, context, supportedCommands);
    const std::filesystem::path lastMessagePath = MakeTempAgentFilePath("aine-codex-last-message", ".txt");
    std::string codexJsonLineBuffer;
    ProcessResult process = RunCodexProcess(
        *health, BuildCodexExecArguments(codexSandboxMode, lastMessagePath), codexWorkingDirectory, codexTimeoutMs,
        &codexPrompt, [&](const std::string& stdoutDelta, const std::string& stderrDelta) {
            if (!stdoutDelta.empty()) {
                PublishCodexJsonEvents(stdoutDelta, codexJsonLineBuffer, state, progress);
            }
            if (!stderrDelta.empty()) {
                PublishAgentProgress(progress, state, "Codex stderr", "Captured",
                                     TruncateForDisplay(Trim(stderrDelta), 360));
            }
        });
    if (!Trim(codexJsonLineBuffer).empty()) {
        PublishCodexJsonEvents("\n", codexJsonLineBuffer, state, progress);
    }
    const std::string codexLastMessage = Trim(ReadWholeFile(lastMessagePath));
    TryRemoveFile(lastMessagePath);

    result.stdoutText = process.stdoutText;
    result.stderrText = process.stderrText;
    result.exitCode = process.exitCode;
    result.ok = process.launched && !process.timedOut && process.exitCode == 0;

    if (!process.launched) {
        const std::string status = "Launch failed";
        state.SetAgentStatus("Codex CLI launch failed");
        return SubmitProviderFailure(AgentBackend::CodexCli, status,
                                     "Codex CLI " + status + ": " + process.launchError +
                                         " | method=" + health->source + " | path=" + health->executablePath,
                                     state);
    }

    if (process.timedOut) {
        PublishAgentProgress(progress, state, "Codex CLI", "Timed out",
                             "Process exceeded " + std::to_string(codexTimeoutMs / 1000) + " seconds.");
        state.SetAgentStatus("Codex CLI timed out");
        return SubmitProviderFailure(AgentBackend::CodexCli, "Timed out",
                                     "Codex CLI timed out after " + std::to_string(codexTimeoutMs / 1000) +
                                         " seconds | method=" + health->source +
                                         " | path=" + health->executablePath,
                                     state);
    }

    std::ostringstream detail;
    detail << "exit=" << process.exitCode;
    if (!process.stderrText.empty()) {
        detail << " stderr=" << TruncateForDisplay(process.stderrText, 240);
    }
    PublishAgentProgress(progress, state, "Codex CLI", process.exitCode == 0 ? "Complete" : "Failed", detail.str());
    if (!process.stdoutText.empty()) {
        PublishAgentProgress(progress, state, "Codex event stream", "Captured", TruncateForDisplay(process.stdoutText));
    }
    if (!process.stderrText.empty()) {
        PublishAgentProgress(progress, state, "Codex stderr", process.exitCode == 0 ? "Captured" : "Failed",
                             TruncateForDisplay(process.stderrText));
    }

    const std::string normalizedOutput = codexLastMessage.empty() ? process.stdoutText : codexLastMessage;
    if (process.exitCode != 0) {
        result.normalizedResult = NormalizeProviderOutput(AgentBackend::CodexCli, normalizedOutput, gateway);
        result.hasNormalizedResult = result.normalizedResult.type != ProviderResultType::ProviderError;
        if (result.hasNormalizedResult && !OutputLooksLikeCodexNotAuthenticated(process)) {
            result.ok = true;
            result.diagnostics.push_back("Codex CLI exited with code " + std::to_string(process.exitCode) +
                                         " after returning usable output.");
            state.AddActivity("Codex CLI", "Completed with warnings", result.diagnostics.back());
            state.AddActivity("Provider output normalized", ProviderResultNormalizer::TypeId(result.normalizedResult.type),
                              result.normalizedResult.title);
            result.assistantMessage = BuildNormalizedAssistantMessage("Codex CLI", result.normalizedResult);
            state.SetAgentStatus("Idle");
            return result;
        }

        state.SetAgentStatus(OutputLooksLikeCodexNotAuthenticated(process) ? "Codex CLI not authenticated"
                                                                           : "Codex CLI failed");
        std::ostringstream reason;
        if (OutputLooksLikeCodexNotAuthenticated(process)) {
            reason << "Codex CLI Not authenticated. Run codex login in a terminal, then refresh agent backends. ";
        }
        reason << "Codex CLI exited with code " << process.exitCode;
        if (!process.stderrText.empty()) {
            reason << ": " << TruncateForDisplay(process.stderrText, 360);
        }
        reason << " | method=" << health->source << " | path=" << health->executablePath;
        return SubmitProviderFailure(AgentBackend::CodexCli, "Failed", reason.str(), state);
    }

    result.normalizedResult = NormalizeProviderOutput(AgentBackend::CodexCli, normalizedOutput, gateway);
    result.hasNormalizedResult = true;
    result.ok = result.normalizedResult.type != ProviderResultType::ProviderError;
    state.AddActivity("Provider output normalized", ProviderResultNormalizer::TypeId(result.normalizedResult.type),
                      result.normalizedResult.title);
    result.assistantMessage = BuildNormalizedAssistantMessage("Codex CLI", result.normalizedResult);
    state.SetAgentStatus("Idle");
    return result;
}

AgentOrchestratorResult AgentOrchestrator::SubmitOpenAIApi(const std::string& prompt, const ProjectContextSnapshot& context,
                                                           EditorState& state, ToolGateway& gateway,
                                                           const std::vector<std::string>& supportedCommands) {
    RefreshProviderHealth();
    const ProviderHealth* health = FindProvider(AgentBackend::OpenAIApi);
    if (health == nullptr || !health->available) {
        std::string reason = "OpenAI API Missing API key";
        if (health != nullptr) {
            reason = "OpenAI API " + health->status + ": " + health->detail;
        }
        (void)gateway;
        (void)supportedCommands;
        return SubmitProviderFailure(AgentBackend::OpenAIApi, health == nullptr ? "Missing API key" : health->status,
                                     reason, state);
    }

    AgentOrchestratorResult result;
    result.backend = AgentBackend::OpenAIApi;
    state.SetAgentStatus("Running OpenAI API");
    state.AddActivity("OpenAI API", "Running",
                      std::string("Responses API model=") + kOpenAIApiModel +
                          " | project root: " + context.projectPath);

    const std::string providerPrompt = BuildProviderPrompt("OpenAI API", prompt, context, supportedCommands);
    ProcessResult process = RunOpenAIApiProcess(providerPrompt, std::filesystem::path(context.projectPath), 120000);

    result.stdoutText = process.stdoutText;
    result.stderrText = process.stderrText;
    result.exitCode = process.exitCode;
    result.ok = process.launched && !process.timedOut && process.exitCode == 0;

    if (!process.launched) {
        state.SetAgentStatus("OpenAI API launch failed");
        return SubmitProviderFailure(AgentBackend::OpenAIApi, "Launch failed",
                                     "OpenAI API Launch failed: " + process.launchError, state);
    }

    if (process.timedOut) {
        state.AddActivity("OpenAI API", "Timed out", "Request exceeded 120 seconds.");
        state.SetAgentStatus("OpenAI API timed out");
        return SubmitProviderFailure(AgentBackend::OpenAIApi, "Timed out",
                                     "OpenAI API timed out after 120 seconds.", state);
    }

    std::ostringstream detail;
    detail << "exit=" << process.exitCode << " model=" << kOpenAIApiModel;
    if (!process.stderrText.empty()) {
        detail << " stderr=" << TruncateForDisplay(process.stderrText, 240);
    }
    state.AddActivity("OpenAI API", process.exitCode == 0 ? "Complete" : "Failed", detail.str());
    if (!process.stdoutText.empty()) {
        state.AddActivity("OpenAI API stdout", "Captured", TruncateForDisplay(process.stdoutText));
    }
    if (!process.stderrText.empty()) {
        state.AddActivity("OpenAI API stderr", process.exitCode == 0 ? "Captured" : "Failed",
                          TruncateForDisplay(process.stderrText));
    }

    if (process.exitCode != 0) {
        state.SetAgentStatus(OutputLooksLikeOpenAIApiKeyFailure(process) ? "OpenAI API key rejected" : "OpenAI API failed");
        std::ostringstream reason;
        reason << "OpenAI API exited with code " << process.exitCode;
        if (!process.stderrText.empty()) {
            reason << ": " << TruncateForDisplay(process.stderrText, 360);
        }
        return SubmitProviderFailure(AgentBackend::OpenAIApi, "Failed", reason.str(), state);
    }

    result.normalizedResult = NormalizeProviderOutput(AgentBackend::OpenAIApi, process.stdoutText, gateway);
    result.hasNormalizedResult = true;
    result.ok = result.normalizedResult.type != ProviderResultType::ProviderError;
    state.AddActivity("Provider output normalized", ProviderResultNormalizer::TypeId(result.normalizedResult.type),
                      result.normalizedResult.title);
    result.assistantMessage = BuildNormalizedAssistantMessage("OpenAI API", result.normalizedResult);
    if (!process.stderrText.empty()) {
        result.assistantMessage += "\n\nOpenAI API stderr:\n" + Trim(process.stderrText);
    }
    state.SetAgentStatus("Idle");
    return result;
}

AgentOrchestratorResult AgentOrchestrator::SubmitClaudePlaceholder(const std::string& prompt,
                                                                   const ProjectContextSnapshot& context,
                                                                   EditorState& state, ToolGateway& gateway) {
    RefreshProviderHealth();
    const ProviderHealth* health = FindProvider(AgentBackend::Claude);
    std::string reason = "Claude provider placeholder";
    if (health != nullptr) {
        reason = health->status + ": " + health->detail;
    }

    state.AddActivity("Claude", "Placeholder", reason);
    (void)context;
    (void)prompt;
    (void)gateway;
    return SubmitProviderFailure(AgentBackend::Claude, "Placeholder", "Claude backend unavailable: " + reason, state);
}

AgentOrchestratorResult AgentOrchestrator::SubmitLocalPromptAssistant(const std::string& prompt,
                                                                      const ProjectContextSnapshot& context,
                                                                      EditorState& state) {
    AgentOrchestratorResult result;
    result.backend = AgentBackend::LocalPromptAssistant;
    state.SetAgentStatus("Local Prompt Assistant");
    state.AddActivity("Local Prompt Assistant", "Read-only", "Generated prompt suggestions without applying scene commands.");

    const std::vector<std::string> suggestions = PromptSuggestions(prompt, state);
    std::ostringstream message;
    message << "Local Prompt Assistant is read-only.\n\n";
    message << "Project context: " << context.Summary() << "\n\n";
    if (suggestions.empty()) {
        message << "Suggestion: add a concrete target, expected scene change, and validation request.";
    } else {
        message << "Suggestions:";
        for (const std::string& suggestion : suggestions) {
            message << "\n- " << suggestion;
        }
    }

    result.assistantMessage = message.str();
    result.ok = true;
    state.SetAgentStatus("Idle");
    return result;
}

AgentOrchestratorResult AgentOrchestrator::SubmitProviderFailure(AgentBackend backend, const std::string& status,
                                                                 const std::string& reason, EditorState& state) {
    AgentOrchestratorResult result;
    result.backend = backend;
    result.ok = false;
    result.diagnostics.push_back(reason);

    std::ostringstream message;
    message << BackendLabel(backend) << " " << status << ".\n\n";
    message << reason << "\n\n";
    message << "No local scene fallback was applied. Select Local Tool Gateway explicitly for deterministic scene commands.";
    result.assistantMessage = message.str();

    state.AddActivity(BackendLabel(backend), status, reason);
    state.SetAgentStatus("Idle");
    return result;
}

AgentOrchestratorResult AgentOrchestrator::RunGatewayFallback(const std::string& prompt, const std::string& reason,
                                                             EditorState& state, ToolGateway& gateway) {
    AgentOrchestratorResult result;
    result.backend = AgentBackend::LocalToolGateway;
    result.usedFallback = true;
    result.diagnostics.push_back(reason);
    state.AddActivity("Local Tool Gateway fallback", "Running", reason);

    result.gatewayResult = gateway.SubmitPrompt(prompt, state);
    result.hasGatewayResult = true;
    result.ok = result.gatewayResult.batch.ok;

    std::ostringstream message;
    message << reason << ".\n\nUsed Local Tool Gateway fallback.\n\n";
    message << result.gatewayResult.assistantMessage;
    result.assistantMessage = message.str();
    state.SetAgentStatus("Idle");
    return result;
}

ProviderHealth AgentOrchestrator::BuildProviderHealth(AgentBackend backend) const {
    ProviderHealth health;
    health.backend = backend;
    health.id = BackendId(backend);
    health.label = BackendLabel(backend);

    if (backend == AgentBackend::LocalToolGateway) {
        health.available = true;
        health.implemented = true;
        health.status = "Ready";
        health.detail = "Deterministic offline command batches through ToolGateway.";
        return health;
    }

    if (backend == AgentBackend::LocalPromptAssistant) {
        health.available = false;
        health.implemented = true;
        health.status = "Disabled";
        health.detail = "Temporarily disabled while Codex in-editor chat is the primary workflow.";
        return health;
    }

    if (backend == AgentBackend::CodexCli) {
        health.implemented = true;
        PopulateCodexProviderHealth(health);
        return health;
    }

    if (backend == AgentBackend::OpenAIApi) {
        PopulateOpenAIApiProviderHealth(health);
        return health;
    }

    if (backend == AgentBackend::Claude) {
        health.implemented = false;
        PopulateCliProviderHealth(health, "claude", "AI_NATIVE_CLAUDE_PATH",
                                  "Set AI_NATIVE_CLAUDE_PATH to a launchable Claude CLI executable.",
                                  " (provider adapter is still placeholder-only)");
        return health;
    }

    return health;
}

std::string AgentOrchestrator::BuildCodexPrompt(const std::string& prompt, const ProjectContextSnapshot& context,
                                                const std::vector<std::string>& supportedCommands) const {
    const std::string trimmed = Trim(prompt);
    if (ToLower(trimmed).rfind("# codex implementation brief", 0) == 0) {
        const std::string lowerBrief = ToLower(trimmed);
        const bool improvementReviewBrief =
            lowerBrief.find("## task mode\neditor improvement review") != std::string::npos;
        const bool conversationBrief = lowerBrief.find("## task mode\nconversation") != std::string::npos;
        const bool sceneCommandBrief = lowerBrief.find("## task mode\nscene command") != std::string::npos;
        std::ostringstream stream;
        stream << "You are the Codex CLI development backend for AI Native Editor.\n";
        if (improvementReviewBrief) {
            stream << "This is an editor improvement review. Inspect the repository and current editor context as "
                      "needed, then return prioritized recommendations. Do not edit files, run broad builds, or mutate "
                      "scene state unless the user explicitly asks for implementation.\n";
        } else if (conversationBrief) {
            stream << "This is a conversation task. Answer the user directly using the recent conversation and editor "
                      "context. Do not edit files or run builds unless the user explicitly asks for implementation.\n";
        } else if (sceneCommandBrief) {
            stream << "This is a scene-command task. Prefer ToolGateway-compatible scene/project commands or concise "
                      "guidance. Do not edit repository source files unless the user explicitly asks for editor code changes.\n";
        } else {
            stream << "This is a repository implementation task. You may inspect and edit source files, run focused "
                      "build/tests, and report what changed.\n";
        }
        stream << "Do not mutate the live editor scene state directly. Scene/project runtime edits should still go "
                  "through durable project files or ToolGateway-compatible changes.\n";
        stream << "Keep the work narrow, preserve existing behavior, and give a concise final summary with the proof actually run.\n";
        stream << "Current compact editor context follows for orientation only; do not treat it as the full task boundary.\n\n";
        stream << context.ToCompactText() << "\n";
        stream << "Supported ToolGateway commands for scene-only proposals: "
               << JoinStrings(supportedCommands, ", ") << "\n\n";
        stream << trimmed << "\n";
        return stream.str();
    }
    return BuildProviderPrompt("Codex CLI", prompt, context, supportedCommands);
}

std::string AgentOrchestrator::BuildProviderPrompt(const std::string& providerLabel, const std::string& prompt,
                                                   const ProjectContextSnapshot& context,
                                                   const std::vector<std::string>& supportedCommands) const {
    std::ostringstream stream;
    stream << "You are the " << providerLabel << " backend for AI Native Editor.\n";
    stream << "You are continuing the conversation shown in Recent conversation. Use it to resolve follow-ups, pronouns, "
              "and references like 'that', 'the last thing', or 'why did it fail'.\n";
    stream << "For ordinary questions, answer naturally in plain text. Do not force JSON for normal conversation.\n";
    stream << "If the current prompt is broad or underspecified, reply with a concise scoped plan or one clarifying "
              "question before proposing implementation work.\n";
    stream << "Do not mutate the live editor scene state. If scene edits are needed, describe ToolGateway commands only.\n";
    stream << "The editor will validate and require approval before applying provider command batches.\n";
    stream << "Only propose actions when the user explicitly asks to modify the scene or project. "
              "If you propose actions, return one strict JSON object and no markdown fences. Otherwise return plain text.\n";
    stream << "Allowed result_type values: chat_response, proposed_command_batch, proposed_file_change, diagnostic, provider_error.\n";
    stream << "Strict command batch JSON format:\n";
    stream << "{\n";
    stream << "  \"result_type\": \"proposed_command_batch\",\n";
    stream << "  \"message\": \"Short explanation\",\n";
    stream << "  \"command_batch\": {\n";
    stream << "    \"version\": 1,\n";
    stream << "    \"name\": \"Batch name\",\n";
    stream << "    \"commands\": [\n";
    stream << "      {\"type\":\"project.setSaveLocation\",\"path\":\"SavedGames/ProfileA\"},\n";
    stream << "      {\"type\":\"scene.createEntity\",\"name\":\"Entity Name\",\"components\":[{\"type\":\"Transform\",\"properties\":{}}]},\n";
    stream << "      {\"type\":\"scene.setTransform\",\"targetName\":\"Entity Name\",\"transform\":{\"position\":[0,0,0],\"rotation\":[0,0,0],\"scale\":[1,1,1]}},\n";
    stream << "      {\"type\":\"validate.scene\"}\n";
    stream << "    ]\n";
    stream << "  }\n";
    stream << "}\n";
    stream << "Use the compact project and conversation context below.\n\n";
    stream << context.ToCompactText() << "\n";
    stream << "Supported ToolGateway commands: " << JoinStrings(supportedCommands, ", ") << "\n\n";
    stream << "Current user prompt:\n" << prompt << "\n";
    return stream.str();
}

} // namespace aine

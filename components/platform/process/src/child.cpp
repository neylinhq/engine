#include <cerrno>
#include <engine/platform/process/process.hpp>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace engine::platform::process {
namespace {
#ifdef _WIN32
std::wstring quoted(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto character : value) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        result.append(slashes * (character == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (character == L'"') {
            result += L'\\';
        }
        result += character;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
struct Child {
    Handle process;
    Handle thread;
};
Handle child_job() {
    Handle job(CreateJobObjectW(nullptr, nullptr), &CloseHandle);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits,
                                         sizeof(limits))) {
        throw std::runtime_error("Cannot supervise child process lifetime");
    }
    return job;
}
Child start(const Command& command, HANDLE job) {
    auto line = quoted(command.executable.native());
    for (const auto& argument : command.arguments) {
        line += L' ' + quoted(text_path(argument).native());
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(command.executable.c_str(), line.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup,
                        &process)) {
        throw std::runtime_error("Cannot start child process");
    }
    Child child{{process.hProcess, &CloseHandle}, {process.hThread, &CloseHandle}};
    if (!AssignProcessToJobObject(job, child.process.get()) ||
        ResumeThread(child.thread.get()) == static_cast<DWORD>(-1)) {
        TerminateProcess(child.process.get(), 126);
        throw std::runtime_error("Cannot start supervised child process");
    }
    return child;
}
#endif
} // namespace
#ifdef _WIN32
int run(const Command& command) {
    auto job = child_job();
    auto child = start(command, job.get());
    const auto waited = WaitForSingleObject(child.process.get(), INFINITE);
    DWORD code = 0;
    const auto read = GetExitCodeProcess(child.process.get(), &code);
    if (waited != WAIT_OBJECT_0 || !read) {
        throw std::runtime_error("Cannot wait for child process");
    }
    return static_cast<int>(code);
}
#else
int run(const Command& command) {
    std::vector<std::string> strings{path_text(command.executable)};
    strings.insert(strings.end(), command.arguments.begin(), command.arguments.end());
    std::vector<char*> arguments;
    for (auto& value : strings) {
        arguments.push_back(value.data());
    }
    arguments.push_back(nullptr);
    const auto parent = getpid();
    const auto child = fork();
    if (child < 0) {
        throw std::runtime_error("Cannot start child process");
    }
    if (child == 0) {
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || getppid() != parent) {
            _exit(126);
        }
        execv(command.executable.c_str(), arguments.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            throw std::runtime_error("Cannot wait for child process");
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
#endif
} // namespace engine::platform::process

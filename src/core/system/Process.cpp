#include "core/system/Process.h"

#include "base/Log.h"
#include "core/system/Handle.h"
#include "core/tasks/Task.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace wl::core {

namespace {

// Limits what the child inherits to one handle (audit ENG-07). Without it CreateProcess copies
// every inheritable handle of the process: two tools started at the same moment from two runners
// held each other's pipe, and the one that ended first looked stuck until the other was done.
class InheritOnly {
public:
    explicit InheritOnly(HANDLE handle) : m_handle(handle) {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size); // fails by design, reports the size
        if (size == 0) {
            return;
        }
        m_storage.resize(size);
        const auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_storage.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) {
            return;
        }
        // The list keeps a pointer to the value, not a copy: m_handle lives as long as the list.
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &m_handle, sizeof(m_handle), nullptr,
                                       nullptr)) {
            DeleteProcThreadAttributeList(list);
            return;
        }
        m_list = list;
    }
    ~InheritOnly() {
        if (m_list) {
            DeleteProcThreadAttributeList(m_list);
        }
    }
    InheritOnly(const InheritOnly&) = delete;
    InheritOnly& operator=(const InheritOnly&) = delete;

    // Null when the list could not be built: the caller then starts the child the old way.
    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return m_list; }

private:
    HANDLE m_handle;
    std::vector<std::byte> m_storage;
    LPPROC_THREAD_ATTRIBUTE_LIST m_list = nullptr;
};

// A job whose processes die when its last handle closes — ours, so also when WinLove is killed
// (D-130, audit ENG-02): a dism.exe left behind kept the mounted image's hives loaded.
UniqueHandle killOnCloseJob() {
    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) {
        return UniqueHandle();
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        return UniqueHandle();
    }
    return job;
}

// The tool ended by itself: whatever it left running on purpose keeps running, as it did before
// there was a job. Only a WinLove that dies while the tool runs takes the tool with it.
void keepWhatIsLeft(HANDLE job) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
}

} // namespace

Result<std::uint32_t> runProcess(std::wstring commandLine, const std::function<void(std::string_view)>& onOutput,
                                 const CancelToken* cancel, ProcessLife life) {
    if (cancel && life == ProcessLife::Outlives) {
        // A tool that must outlive us must not be cut by a cancel either (Process.h).
        log::warn("process", L"a cancel token was passed with ProcessLife::Outlives and is ignored: " + commandLine);
        cancel = nullptr;
    }
    if (cancel && cancel->cancelled()) {
        return fail(ErrorCode::Cancelled, L"operation cancelled", commandLine);
    }
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, nullptr, 0)) {
        return fail(ErrorCode::IoError, L"could not create a pipe", {}, static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }
    SetHandleInformation(writeEnd, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT); // only the write end goes to the child
    const InheritOnly only(writeEnd);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = static_cast<DWORD>(only.get() ? sizeof(STARTUPINFOEXW) : sizeof(STARTUPINFOW));
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = writeEnd;
    startup.StartupInfo.hStdError = writeEnd;
    startup.StartupInfo.hStdInput = nullptr; // a tool that asks something reads end-of-file instead of waiting
    startup.lpAttributeList = only.get();
    // Suspended: the child must be in the job before it can start anything of its own.
    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    if (only.get()) {
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr,
                                        &startup.StartupInfo, &process);
    const DWORD startError = GetLastError();
    CloseHandle(writeEnd); // ours: the read below ends when the child's copy closes
    if (!started) {
        CloseHandle(readEnd);
        return fail(ErrorCode::IoError, L"could not start the program", commandLine,
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(startError)));
    }
    UniqueHandle job = life == ProcessLife::EndsWithUs ? killOnCloseJob() : UniqueHandle();
    if (job && !AssignProcessToJobObject(job.get(), process.hProcess)) {
        // We are in a job ourselves that does not take nested ones: the tool runs without, as before.
        job = UniqueHandle();
    }
    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        const DWORD resumeError = GetLastError();
        TerminateProcess(process.hProcess, 1);
        CloseHandle(readEnd);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return fail(ErrorCode::IoError, L"could not start the program", commandLine,
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(resumeError)));
    }
    char buffer[4096];
    DWORD read = 0;
    bool cancelled = false;
    if (!cancel) {
        while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
            if (onOutput) {
                onOutput(std::string_view(buffer, read));
            }
        }
    } else {
        // A blocking ReadFile cannot be told to stop, so with a token the pipe is polled: read
        // whatever is there (the child never waits on a full pipe), otherwise wait a moment.
        bool exited = false;
        for (;;) {
            if (cancel->cancelled()) {
                cancelled = true;
                break;
            }
            DWORD available = 0;
            if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr)) {
                break; // every write end is closed and the pipe is empty
            }
            if (available > 0) {
                const DWORD wanted = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
                if (!ReadFile(readEnd, buffer, wanted, &read, nullptr) || read == 0) {
                    break;
                }
                if (onOutput) {
                    onOutput(std::string_view(buffer, read));
                }
                continue;
            }
            if (!exited) {
                exited = WaitForSingleObject(process.hProcess, 25) == WAIT_OBJECT_0; // ends the wait early
            } else {
                Sleep(25); // the tool is gone, something it started still holds the pipe
            }
        }
    }
    if (cancelled) {
        // The whole job: what the tool started goes with it. Without a job, the tool alone.
        if (job) {
            TerminateJobObject(job.get(), 1);
        } else {
            TerminateProcess(process.hProcess, 1);
        }
        CloseHandle(readEnd);
        WaitForSingleObject(process.hProcess, 5000); // termination is asynchronous; never wait forever on a refusal
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return fail(ErrorCode::Cancelled, L"operation cancelled", commandLine);
    }
    CloseHandle(readEnd);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (job) {
        keepWhatIsLeft(job.get());
    }
    return static_cast<std::uint32_t>(exitCode);
}

Result<std::wstring> systemTool(std::wstring_view name) {
    wchar_t system[MAX_PATH];
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return fail(ErrorCode::IoError, L"could not find the system folder");
    }
    return std::wstring(system) + L"\\" + std::wstring(name);
}

} // namespace wl::core

// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pal_config.h"
#include "pal_process.h"
#include "pal_utilities.h"

#include <assert.h>
#include <errno.h>
#include <limits>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>
#if HAVE_PIPE2
#include <fcntl.h>
#endif

#if HAVE_SCHED_SETAFFINITY || HAVE_SCHED_GETAFFINITY
#include <sched.h>
#endif

// Validate that our Signals enum values are correct for the platform
static_assert(PAL_SIGKILL == SIGKILL, "");

// Validate that our WaitPidOptions enum values are correct for the platform
static_assert(PAL_WNOHANG == WNOHANG, "");
static_assert(PAL_WUNTRACED == WUNTRACED, "");

// Validate that our SysLogPriority values are correct for the platform
static_assert(PAL_LOG_EMERG == LOG_EMERG, "");
static_assert(PAL_LOG_ALERT == LOG_ALERT, "");
static_assert(PAL_LOG_CRIT == LOG_CRIT, "");
static_assert(PAL_LOG_ERR == LOG_ERR, "");
static_assert(PAL_LOG_WARNING == LOG_WARNING, "");
static_assert(PAL_LOG_NOTICE == LOG_NOTICE, "");
static_assert(PAL_LOG_INFO == LOG_INFO, "");
static_assert(PAL_LOG_DEBUG == LOG_DEBUG, "");
static_assert(PAL_LOG_KERN == LOG_KERN, "");
static_assert(PAL_LOG_USER == LOG_USER, "");
static_assert(PAL_LOG_MAIL == LOG_MAIL, "");
static_assert(PAL_LOG_DAEMON == LOG_DAEMON, "");
static_assert(PAL_LOG_AUTH == LOG_AUTH, "");
static_assert(PAL_LOG_SYSLOG == LOG_SYSLOG, "");
static_assert(PAL_LOG_LPR == LOG_LPR, "");
static_assert(PAL_LOG_NEWS == LOG_NEWS, "");
static_assert(PAL_LOG_UUCP == LOG_UUCP, "");
static_assert(PAL_LOG_CRON == LOG_CRON, "");
static_assert(PAL_LOG_AUTHPRIV == LOG_AUTHPRIV, "");
static_assert(PAL_LOG_FTP == LOG_FTP, "");
static_assert(PAL_LOG_LOCAL0 == LOG_LOCAL0, "");
static_assert(PAL_LOG_LOCAL1 == LOG_LOCAL1, "");
static_assert(PAL_LOG_LOCAL2 == LOG_LOCAL2, "");
static_assert(PAL_LOG_LOCAL3 == LOG_LOCAL3, "");
static_assert(PAL_LOG_LOCAL4 == LOG_LOCAL4, "");
static_assert(PAL_LOG_LOCAL5 == LOG_LOCAL5, "");
static_assert(PAL_LOG_LOCAL6 == LOG_LOCAL6, "");
static_assert(PAL_LOG_LOCAL7 == LOG_LOCAL7, "");

// Validate that out PriorityWhich values are correct for the platform
static_assert(PAL_PRIO_PROCESS == static_cast<int>(PRIO_PROCESS), "");
static_assert(PAL_PRIO_PGRP == static_cast<int>(PRIO_PGRP), "");
static_assert(PAL_PRIO_USER == static_cast<int>(PRIO_USER), "");

enum
{
    READ_END_OF_PIPE = 0,
    WRITE_END_OF_PIPE = 1,
};

static void CloseIfOpen(int fd)
{
    // Ignoring errors from close is a deliberate choice and we musn't
    // let close during cleanup on a failure path disturb errno.
    if (fd >= 0)
    {
        int priorErrno = errno;
        close(fd);
        errno = priorErrno;
    }
}

extern "C" int32_t SystemNative_ForkAndExecProcess(const char* filename,
                                      char* const argv[],
                                      char* const envp[],
                                      const char* cwd,
                                      int32_t redirectStdin,
                                      int32_t redirectStdout,
                                      int32_t redirectStderr,
                                      int32_t* childPid,
                                      int32_t* stdinFd,
                                      int32_t* stdoutFd,
                                      int32_t* stderrFd)
{
    int success = true;
    int stdinFds[2] = {-1, -1}, stdoutFds[2] = {-1, -1}, stderrFds[2] = {-1, -1}, waitForChildToExecPipe[2] = {-1, -1};
    int processId = -1;

    // Validate arguments
    if (nullptr == filename || nullptr == argv || nullptr == envp || nullptr == stdinFd || nullptr == stdoutFd ||
        nullptr == stderrFd || nullptr == childPid)
    {
        assert(false && "null argument.");
        errno = EINVAL;
        success = false;
        goto done;
    }

    if ((redirectStdin & ~1) != 0 || (redirectStdout & ~1) != 0 || (redirectStderr & ~1) != 0)
    {
        assert(false && "Boolean redirect* inputs must be 0 or 1.");
        errno = EINVAL;
        success = false;
        goto done;
    }

    // Open pipes for any requests to redirect stdin/stdout/stderr
    if ((redirectStdin && pipe(stdinFds) != 0) || (redirectStdout && pipe(stdoutFds) != 0) ||
        (redirectStderr && pipe(stderrFds) != 0))
    {
        assert(false && "pipe() failed.");
        success = false;
        goto done;
    }

    // If we have pipe2 and can use O_CLOEXEC, we create a pipe purely for the benefit
    // of knowing when the child process has called exec.  We can use that to block waiting
    // on the pipe to be closed, which lets us block the parent from returning until the
    // child process is actually transitioned to the target program.  This avoids problems
    // where the parent process uses members of Process, like ProcessName, when the Process
    // is still the clone of this one. This is a best-effort attempt, so ignore any errors.
#if HAVE_PIPE2
    pipe2(waitForChildToExecPipe, O_CLOEXEC);
#endif

    // Fork the child process
    if ((processId = fork()) == -1)
    {
        assert(false && "fork() failed.");
        success = false;
        goto done;
    }

    if (processId == 0) // processId == 0 if this is child process
    {
        // Close the child's copy of the parent end of any open pipes
        CloseIfOpen(stdinFds[WRITE_END_OF_PIPE]);
        CloseIfOpen(stdoutFds[READ_END_OF_PIPE]);
        CloseIfOpen(stderrFds[READ_END_OF_PIPE]);

        // For any redirections that should happen, dup the pipe descriptors onto stdin/out/err.
        // Then close out the old pipe descriptrs, which we no longer need.
        if ((redirectStdin && dup2(stdinFds[READ_END_OF_PIPE], STDIN_FILENO) == -1) ||
            (redirectStdout && dup2(stdoutFds[WRITE_END_OF_PIPE], STDOUT_FILENO) == -1) ||
            (redirectStderr && dup2(stderrFds[WRITE_END_OF_PIPE], STDERR_FILENO) == -1))
        {
            _exit(errno != 0 ? errno : EXIT_FAILURE);
        }
        CloseIfOpen(stdinFds[READ_END_OF_PIPE]);
        CloseIfOpen(stdoutFds[WRITE_END_OF_PIPE]);
        CloseIfOpen(stderrFds[WRITE_END_OF_PIPE]);

        // Change to the designated working directory, if one was specified
        if (nullptr != cwd && chdir(cwd) == -1)
        {
            _exit(errno != 0 ? errno : EXIT_FAILURE);
        }

        // Finally, execute the new process.  execve will not return if it's successful.
        execve(filename, argv, envp);
        _exit(errno != 0 ? errno : EXIT_FAILURE); // execve failed
    }

    // This is the parent process. processId == pid of the child
    *childPid = processId;
    *stdinFd = stdinFds[WRITE_END_OF_PIPE];
    *stdoutFd = stdoutFds[READ_END_OF_PIPE];
    *stderrFd = stderrFds[READ_END_OF_PIPE];

done:
    // Regardless of success or failure, close the parent's copy of the child's end of
    // any opened pipes.  The parent doesn't need them anymore.
    CloseIfOpen(stdinFds[READ_END_OF_PIPE]);
    CloseIfOpen(stdoutFds[WRITE_END_OF_PIPE]);
    CloseIfOpen(stderrFds[WRITE_END_OF_PIPE]);

    // Also close the write end of the exec waiting pipe, and wait for the pipe to be closed
    // by trying to read from it (the read will wake up when the pipe is closed and broken).
    // Ignore any errors... this is a best-effort attempt.
    CloseIfOpen(waitForChildToExecPipe[WRITE_END_OF_PIPE]);
    if (waitForChildToExecPipe[READ_END_OF_PIPE] != -1)
    {
        int ignored;
        ssize_t bytesRead;
        if (success)
        {
            do
            {
                bytesRead = read(waitForChildToExecPipe[READ_END_OF_PIPE], &ignored, 1);
            } 
            while (bytesRead == -1 && errno == EINTR);
        }
        CloseIfOpen(waitForChildToExecPipe[READ_END_OF_PIPE]);
    }

    // If we failed, close everything else and give back error values in all out arguments.
    if (!success)
    {
        CloseIfOpen(stdinFds[WRITE_END_OF_PIPE]);
        CloseIfOpen(stdoutFds[READ_END_OF_PIPE]);
        CloseIfOpen(stderrFds[READ_END_OF_PIPE]);

        *stdinFd = -1;
        *stdoutFd = -1;
        *stderrFd = -1;
        *childPid = -1;
    }

    return success ? 0 : -1;
}

// Each platform type has it's own RLIMIT values but the same name, so we need
// to convert our standard types into the platform specific ones.
static int32_t ConvertRLimitResourcesPalToPlatform(RLimitResources value)
{
    switch (value)
    {
        case PAL_RLIMIT_CPU:
            return RLIMIT_CPU;
        case PAL_RLIMIT_FSIZE:
            return RLIMIT_FSIZE;
        case PAL_RLIMIT_DATA:
            return RLIMIT_DATA;
        case PAL_RLIMIT_STACK:
            return RLIMIT_STACK;
        case PAL_RLIMIT_CORE:
            return RLIMIT_CORE;
        case PAL_RLIMIT_AS:
            return RLIMIT_AS;
        case PAL_RLIMIT_RSS:
            return RLIMIT_RSS;
        case PAL_RLIMIT_MEMLOCK:
            return RLIMIT_MEMLOCK;
        case PAL_RLIMIT_NPROC:
            return RLIMIT_NPROC;
        case PAL_RLIMIT_NOFILE:
            return RLIMIT_NOFILE;
    }

    assert(false && "Unknown RLIMIT value");
    return -1;
}

// Because RLIM_INFINITY is different per-platform, use the max value of a uint64 (which is RLIM_INFINITY on Ubuntu)
// to signify RLIM_INIFINITY; on OS X, where RLIM_INFINITY is slightly lower, we'll translate it to the correct value
// here.
static rlim_t ConvertFromManagedRLimitInfinityToPalIfNecessary(uint64_t value)
{
    // rlim_t type can vary per platform, so we also treat anything outside its range as infinite.
    if (value == UINT64_MAX || value > std::numeric_limits<rlim_t>::max())
        return RLIM_INFINITY;

    return static_cast<rlim_t>(value);
}

// Because RLIM_INFINITY is different per-platform, use the max value of a uint64 (which is RLIM_INFINITY on Ubuntu)
// to signify RLIM_INIFINITY; on OS X, where RLIM_INFINITY is slightly lower, we'll translate it to the correct value
// here.
static uint64_t ConvertFromNativeRLimitInfinityToManagedIfNecessary(rlim_t value)
{
    if (value == RLIM_INFINITY)
        return UINT64_MAX;

    return UnsignedCast(value);
}

static void ConvertFromRLimitManagedToPal(const RLimit& pal, rlimit& native)
{
    native.rlim_cur = ConvertFromManagedRLimitInfinityToPalIfNecessary(pal.CurrentLimit);
    native.rlim_max = ConvertFromManagedRLimitInfinityToPalIfNecessary(pal.MaximumLimit);
}

static void ConvertFromPalRLimitToManaged(const rlimit& native, RLimit& pal)
{
    pal.CurrentLimit = ConvertFromNativeRLimitInfinityToManagedIfNecessary(native.rlim_cur);
    pal.MaximumLimit = ConvertFromNativeRLimitInfinityToManagedIfNecessary(native.rlim_max);
}

extern "C" int32_t SystemNative_GetRLimit(RLimitResources resourceType, RLimit* limits)
{
    assert(limits != nullptr);

    int32_t platformLimit = ConvertRLimitResourcesPalToPlatform(resourceType);
    rlimit internalLimit;
    int result = getrlimit(platformLimit, &internalLimit);
    if (result == 0)
    {
        ConvertFromPalRLimitToManaged(internalLimit, *limits);
    }
    else
    {
        *limits = {};
    }

    return result;
}

extern "C" int32_t SystemNative_SetRLimit(RLimitResources resourceType, const RLimit* limits)
{
    assert(limits != nullptr);

    int32_t platformLimit = ConvertRLimitResourcesPalToPlatform(resourceType);
    rlimit internalLimit;
    ConvertFromRLimitManagedToPal(*limits, internalLimit);
    return setrlimit(platformLimit, &internalLimit);
}

extern "C" int32_t SystemNative_Kill(int32_t pid, int32_t signal)
{
    return kill(pid, signal);
}

extern "C" int32_t SystemNative_GetPid()
{
    return getpid();
}

extern "C" int32_t SystemNative_GetSid(int32_t pid)
{
    return getsid(pid);
}

extern "C" void SystemNative_SysLog(SysLogPriority priority, const char* message, const char* arg1)
{
    syslog(static_cast<int>(priority), message, arg1);
}

extern "C" int32_t SystemNative_WaitPid(int32_t pid, int32_t* status, WaitPidOptions options)
{
    assert(status != nullptr);

    int32_t result;
    while (CheckInterrupted(result = waitpid(pid, status, static_cast<int>(options))));
    return result;
}

extern "C" int32_t SystemNative_WExitStatus(int32_t status)
{
    return WEXITSTATUS(status);
}

extern "C" int32_t SystemNative_WIfExited(int32_t status)
{
    return WIFEXITED(status);
}

extern "C" int32_t SystemNative_WIfSignaled(int32_t status)
{
    return WIFSIGNALED(status);
}

extern "C" int32_t SystemNative_WTermSig(int32_t status)
{
    return WTERMSIG(status);
}

extern "C" int64_t SystemNative_PathConf(const char* path, PathConfName name)
{
    int32_t confValue = -1;
    switch (name)
    {
        case PAL_PC_LINK_MAX:
            confValue = _PC_LINK_MAX;
            break;
        case PAL_PC_MAX_CANON:
            confValue = _PC_MAX_CANON;
            break;
        case PAL_PC_MAX_INPUT:
            confValue = _PC_MAX_INPUT;
            break;
        case PAL_PC_NAME_MAX:
            confValue = _PC_NAME_MAX;
            break;
        case PAL_PC_PATH_MAX:
            confValue = _PC_PATH_MAX;
            break;
        case PAL_PC_PIPE_BUF:
            confValue = _PC_PIPE_BUF;
            break;
        case PAL_PC_CHOWN_RESTRICTED:
            confValue = _PC_CHOWN_RESTRICTED;
            break;
        case PAL_PC_NO_TRUNC:
            confValue = _PC_NO_TRUNC;
            break;
        case PAL_PC_VDISABLE:
            confValue = _PC_VDISABLE;
            break;
    }

    if (confValue == -1)
    {
        assert(false && "Unknown PathConfName");
        errno = EINVAL;
        return -1;
    }

    return pathconf(path, confValue);
}

extern "C" int64_t SystemNative_GetMaximumPath()
{
    int64_t result = pathconf("/", _PC_PATH_MAX);
    if (result == -1)
    {
        result = PATH_MAX;
    }

    return result;
}

extern "C" int32_t SystemNative_GetPriority(PriorityWhich which, int32_t who)
{
    // GetPriority uses errno 0 to show succes to make sure we don't have a stale value
    errno = 0;
#if PRIORITY_REQUIRES_INT_WHO
    return getpriority(which, who);
#else
    return getpriority(which, static_cast<id_t>(who));
#endif
}

extern "C" int32_t SystemNative_SetPriority(PriorityWhich which, int32_t who, int32_t nice)
{
#if PRIORITY_REQUIRES_INT_WHO
    return setpriority(which, who, nice);
#else
    return setpriority(which, static_cast<id_t>(who), nice);
#endif
}

extern "C" char* SystemNative_GetCwd(char* buffer, int32_t bufferSize)
{
    assert(bufferSize >= 0);

    if (bufferSize < 0)
    {
        errno = EINVAL;
        return nullptr;
    }

    return getcwd(buffer, UnsignedCast(bufferSize));
}

#if HAVE_SCHED_SETAFFINITY
extern "C" int32_t SystemNative_SchedSetAffinity(int32_t pid, intptr_t* mask)
{
    assert(mask != nullptr);

    int maxCpu = sizeof(intptr_t) * 8;
    assert(maxCpu <= CPU_SETSIZE);

    cpu_set_t set;
    CPU_ZERO(&set);

    intptr_t bits = *mask; 
    for (int cpu = 0; cpu < maxCpu; cpu++)
    {
        if ((bits & static_cast<intptr_t>(1u << cpu)) != 0)
        {
            CPU_SET(cpu, &set);
        }
    }
 
    return sched_setaffinity(pid, sizeof(cpu_set_t), &set);
}
#endif

#if HAVE_SCHED_GETAFFINITY
extern "C" int32_t SystemNative_SchedGetAffinity(int32_t pid, intptr_t* mask)
{
    assert(mask != nullptr);

    cpu_set_t set;
    int32_t result = sched_getaffinity(pid, sizeof(cpu_set_t), &set);
    if (result == 0)
    {
        int maxCpu = sizeof(intptr_t) * 8;
        assert(maxCpu <= CPU_SETSIZE);

        intptr_t bits = 0;
        for (int cpu = 0; cpu < maxCpu; cpu++)
        {
            if (CPU_ISSET(cpu, &set))
            {
                bits |= (1u << cpu);
            }
        }

        *mask = bits;
    }
    else
    {
        *mask = 0;
    }

    return result;
}
#endif

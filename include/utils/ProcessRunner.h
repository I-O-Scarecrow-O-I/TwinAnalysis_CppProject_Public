#pragma once
// Cross-platform utility to run a subprocess and capture its stdout/stderr.
// Usage:
//   ProcessRunner::Result r = ProcessRunner::run({"python", "script.py", "arg"});
//   if (r.exitCode == 0) { /* r.stdOut contains output */ }

#include <string>
#include <vector>
#include <sstream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/wait.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <cstring>
#endif

class ProcessRunner {
public:
    struct Result {
        int    exitCode = -1;
        std::string stdOut;
        std::string stdErr;
        std::string errorMsg; // non-empty if we couldn't launch the process at all
    };

    // Run a command given as a list of tokens (argv[0] is the executable).
    // Returns Result. On launch failure, exitCode=-1 and errorMsg is set.
    static Result run(const std::vector<std::string>& args) {
        Result res;
        if (args.empty()) {
            res.errorMsg = "ProcessRunner::run: empty args";
            return res;
        }

#ifdef _WIN32
        res = runWindows(args);
#else
        res = runPosix(args);
#endif
        return res;
    }

    // Convenience: build a single quoted command string for logging purposes.
    static std::string argsToString(const std::vector<std::string>& args) {
        std::ostringstream oss;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) oss << ' ';
            // Simple quoting: wrap in double quotes if the token contains spaces
            bool needsQuote = args[i].find(' ') != std::string::npos;
            if (needsQuote) oss << '"';
            oss << args[i];
            if (needsQuote) oss << '"';
        }
        return oss.str();
    }

private:

#ifdef _WIN32
    static Result runWindows(const std::vector<std::string>& args) {
        Result res;

        // Build command line string
        std::string cmdLine;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) cmdLine += ' ';
            bool needsQuote = args[i].find(' ') != std::string::npos ||
                              args[i].find('\t') != std::string::npos ||
                              args[i].empty();
            if (needsQuote) cmdLine += '"';
            // Escape backslashes and quotes inside the argument
            for (size_t j = 0; j < args[i].size(); ++j) {
                char c = args[i][j];
                if (c == '"') cmdLine += "\\\"";
                else cmdLine += c;
            }
            if (needsQuote) cmdLine += '"';
        }

        // Create pipes for stdout and stderr
        HANDLE hStdOutRead = NULL, hStdOutWrite = NULL;
        HANDLE hStdErrRead = NULL, hStdErrWrite = NULL;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0) ||
            !SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
            res.errorMsg = "CreatePipe stdout failed";
            return res;
        }
        if (!CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0) ||
            !SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(hStdOutRead); CloseHandle(hStdOutWrite);
            res.errorMsg = "CreatePipe stderr failed";
            return res;
        }

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.hStdOutput = hStdOutWrite;
        si.hStdError  = hStdErrWrite;
        si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
        si.dwFlags   |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi{};
        std::string cmdLineMut = cmdLine;
        BOOL ok = CreateProcessA(
            NULL, &cmdLineMut[0], NULL, NULL, TRUE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

        CloseHandle(hStdOutWrite);
        CloseHandle(hStdErrWrite);

        if (!ok) {
            CloseHandle(hStdOutRead); CloseHandle(hStdErrRead);
            res.errorMsg = "CreateProcess failed for: " + cmdLine;
            return res;
        }

        // Read stdout
        res.stdOut = readPipe(hStdOutRead);
        res.stdErr = readPipe(hStdErrRead);

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        res.exitCode = static_cast<int>(exitCode);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hStdOutRead);
        CloseHandle(hStdErrRead);
        return res;
    }

    static std::string readPipe(HANDLE h) {
        std::string out;
        char buf[4096];
        DWORD read;
        while (ReadFile(h, buf, sizeof(buf), &read, NULL) && read > 0)
            out.append(buf, read);
        return out;
    }

#else  // POSIX

    static Result runPosix(const std::vector<std::string>& args) {
        Result res;

        // Build argv
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);

        // Create pipes
        int pipeOut[2], pipeErr[2];
        if (pipe(pipeOut) != 0 || pipe(pipeErr) != 0) {
            res.errorMsg = std::string("pipe() failed: ") + strerror(errno);
            return res;
        }

        pid_t pid = fork();
        if (pid < 0) {
            res.errorMsg = std::string("fork() failed: ") + strerror(errno);
            close(pipeOut[0]); close(pipeOut[1]);
            close(pipeErr[0]); close(pipeErr[1]);
            return res;
        }

        if (pid == 0) {
            // Child
            dup2(pipeOut[1], STDOUT_FILENO);
            dup2(pipeErr[1], STDERR_FILENO);
            close(pipeOut[0]); close(pipeOut[1]);
            close(pipeErr[0]); close(pipeErr[1]);
            execvp(argv[0], argv.data());
            // exec failed
            _exit(127);
        }

        // Parent: close write ends
        close(pipeOut[1]);
        close(pipeErr[1]);

        // Read both pipes to avoid deadlock: read them in a simple loop
        res.stdOut = drainFd(pipeOut[0]);
        res.stdErr = drainFd(pipeErr[0]);

        close(pipeOut[0]);
        close(pipeErr[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
            res.exitCode = WEXITSTATUS(status);
        else
            res.exitCode = -1;

        return res;
    }

    static std::string drainFd(int fd) {
        std::string out;
        char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            out.append(buf, static_cast<size_t>(n));
        return out;
    }
#endif
};

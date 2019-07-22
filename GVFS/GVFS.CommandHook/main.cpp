#include "stdafx.h"
#include "common.h"

using std::string;

enum PostIndexChangedErrorReturnCode
{
    ErrorPostIndexChangedProtocol = ReturnCode::LastError + 1,
};

enum class HookType
{
    Invalid = 0,
    PreCommand = 1,
    PostCommand = 2,
};

static const std::string PreCommandHook = "pre-command";
static const std::string PostCommandHook = "post-command";
static const std::string GitPidArg = "--git-pid=";
static const int InvalidProcessId = -1;

static const std::string AcquireRequest = "AcquireLock";
static const std::string DenyGVFSResult = "LockDeniedGVFS";
static const std::string DenyGitResult = "LockDeniedGit";
static const std::string AcceptResult = "LockAcquired";
static const std::string AvailableResult = "LockAvailable";
static const std::string MountNotReadyResult = "MountNotReady";
static const std::string UnmountInProgressResult = "UnmountInProgress";

const int PIPE_BUFFER_SIZE = 1024;

HookType GetHookType(const char* string)
{
    if (string == PreCommandHook)
    {
        return HookType::PreCommand;
    }
    else if (string == PostCommandHook)
    {
        return HookType::PostCommand;
    }

    return HookType::Invalid;
}

int GetParentPid(int argc, char* argv[])
{
    char** beginArgs = argv;
    char** endArgs = beginArgs + argc;

    char** pidArg = std::find_if(
        beginArgs, 
        endArgs, 
        [](char* argString) 
        {
            if (strlen(argString) < GitPidArg.length())
            {
                return false;
            }

            return 0 == strncmp(GitPidArg.c_str(), argString, GitPidArg.length());
        });

    if (pidArg == endArgs)
    {
        die(InvalidCommand, "Git did not supply the process Id.\nEnsure you are using the correct version of the git client.");
    }

    // TODO: Error on duplicates
    std::string pidString(*pidArg);
    if (!pidString.empty())
    {
        pidString = pidString.substr(GitPidArg.length());

        // TODO: Ensure string is value int value
        return std::atoi(pidString.c_str());
    }

    die(InvalidCommand, "Git did not supply the process Id.\nEnsure you are using the correct version of the git client.");

    return InvalidProcessId;
}

static std::string GetGitCommand(int argc, char* argv[])
{
    UNREFERENCED_PARAMETER(argc);

    std::string command(argv[2]);
    std::transform(command.begin(), command.end(), command.begin(), [](unsigned char c) -> unsigned char { return static_cast<unsigned char>(std::tolower(c)); });
    
    if(command.length() >= 4 && command.substr(0, 4) == "git-")
    { 
        command = command.substr(4);
    }

    return command;
}

static bool IsProcessActive(int pid)
{
    // TODO Implement
    UNREFERENCED_PARAMETER(pid);
    return true;
}

static bool CheckGVFSLockAvailabilityOnly(int argc, char *argv[])
{
    /*try
    {
        // Don't acquire the GVFS lock if the git command is not acquiring locks.
        // This enables tools to run status commands without to the index and
        // blocking other commands from running. The git argument
        // "--no-optional-locks" results in a 'negative'
        // value GIT_OPTIONAL_LOCKS environment variable.
        return GetGitCommand(args).Equals("status", StringComparison.OrdinalIgnoreCase) &&
            (args.Any(arg = > arg.Equals("--no-lock-index", StringComparison.OrdinalIgnoreCase)) ||
                IsGitEnvVarDisabled("GIT_OPTIONAL_LOCKS"));
    }
    catch (Exception e)
    {
        ExitWithError("Failed to determine if GVFS should aquire GVFS lock: " + e.ToString());
    }*/
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    return false;
}

static bool ShouldLock(int argc, char* argv[])
{
    std::string gitCommand(GetGitCommand(argc, argv));

    switch (gitCommand[0])
    {
        // Keep these alphabetically sorted
    case 'b':
        if (gitCommand == "blame" ||
            gitCommand == "branch")
        {
            return false;
        }

        break;

    case 'c':
        if (gitCommand == "cat-file" ||
            gitCommand == "check-attr" ||
            gitCommand == "check-ignore" ||
            gitCommand == "check-mailmap" ||
            gitCommand == "commit-graph" ||
            gitCommand == "config" ||
            gitCommand == "credential")
        {
            return false;
        }
        break;

    case 'd':
        if (gitCommand == "diff" ||
            gitCommand == "diff-files" ||
            gitCommand == "diff-index" ||
            gitCommand == "diff-tree" ||
            gitCommand == "difftool")
        {
            return false;
        }

        break;

    case 'f':
        if (gitCommand == "fetch" ||
            gitCommand == "for-each-ref")
        {
            return false;
        }

        break;

    case 'h':
        if (gitCommand == "help" ||
            gitCommand == "hash-object")
        {
            return false;
        }

        break;

    case 'i':
        if (gitCommand == "index-pack")
        {
            return false;
        }

        break;

    case 'l':
        if (gitCommand == "log" ||
            gitCommand == "ls-files" ||
            gitCommand == "ls-tree")
        {
            return false;
        }

        break;

    case 'm':
        if (gitCommand == "merge-base" ||
            gitCommand == "multi-pack-index")
        {
            return false;
        }

        break;

    case 'n':
        if (gitCommand == "name-rev")
        {
            return false;
        }

        break;

    case 'p':
        if (gitCommand == "push")
        {
            return false;
        }

        break;

    case 'r':
        if (gitCommand == "remote" ||
            gitCommand == "rev-list" ||
            gitCommand == "rev-parse")
        {
            return false;
        }

        break;

    case 's':
        /*
         * There are several git commands that are "unsupoorted" in virtualized (VFS4G)
         * enlistments that are blocked by git. Usually, these are blocked before they acquire
         * a GVFSLock, but the submodule command is different, and is blocked after acquiring the
         * GVFS lock. This can cause issues if another action is attempting to create placeholders.
         * As we know the submodule command is a no-op, allow it to proceed without acquiring the
         * GVFSLock. I have filed issue #1164 to track having git block all unsupported commands
         * before calling the pre-command hook.
         */
        if (gitCommand == "show" ||
            gitCommand == "show-ref" ||
            gitCommand == "symbolic-ref" || 
            gitCommand == "submodule")
        {
            return false;
        }

        break;

    case 't':
        if (gitCommand == "tag")
        {
            return false;
        }

        break;

    case 'u':
        if (gitCommand == "unpack-objects" ||
            gitCommand == "update-ref")
        {
            return false;
        }

        break;

    case 'v':
        if (gitCommand == "version")
        {
            return false;
        }

        break;

    case 'w':
        if (gitCommand == "web--browse")
        {
            return false;
        }

        break;

    default:
        break;
    }

    char** beginArgs = argv;
    char** endArgs = beginArgs + argc;

    if (gitCommand == "reset")
    {
        if (endArgs != std::find_if(beginArgs, endArgs, [](char* argString) { return (0 == strcmp(argString, "--soft")); }))
        {
            return false;
        }
    }

    // TODO: Support these checks
    /*if (!KnownGitCommands.Contains(gitCommand) && IsAlias(gitCommand))
    {
        return false;
    }*/

    return true;
}

void CheckForLegalCommands(int argc, char* argv[])
{
    std::string command = GetGitCommand(argc, argv);
    if (command == "gui")
    {
        die(ReturnCode::InvalidCommand, "To access the 'git gui' in a GVFS repo, please invoke 'git-gui.exe' instead.");
    }
}

static std::string GenerateFullCommand(int argc, char* argv[])
{
    std::string fullGitCommand("git ");
    for (int i = 2; i < argc; ++i)
    {
        if (strlen(argv[i]) < GitPidArg.length())
        {
            fullGitCommand += " ";
            fullGitCommand += argv[i];
        }
        else if (0 != strncmp(argv[i], GitPidArg.c_str(), GitPidArg.length()))
        {
            fullGitCommand += " ";
            fullGitCommand += argv[i];
        }
    }

    return fullGitCommand;
}

static std::string GetGitCommandSessionId()
{
    /*try
    {
        return Environment.GetEnvironmentVariable("GIT_TR2_PARENT_SID", EnvironmentVariableTarget.Process) ? ? string.Empty;
    }
    catch (Exception)
    {
        return string.Empty;
    }*/

    return "";
}

static bool CheckAcceptResponse(const string& responseHeader, bool checkAvailabilityOnly, string& message)
{
    if (responseHeader == AcceptResult)
    {
        if (!checkAvailabilityOnly)
        {
            message = "";
            return true;
        }
        else
        {
            message = "Error when acquiring the lock. Unexpected response: "; // +response.CreateMessage();
            return false;
        }
    }
    else if (responseHeader == AvailableResult)
    {
        if (checkAvailabilityOnly)
        {
            message = "";
            return true;
        }
        else
        {
            message = "Error when acquiring the lock. Unexpected response: "; // +response.CreateMessage();
            return false;
        }
    }

    message = "Error when acquiring the lock. Not an Accept result: "; // +response.CreateMessage();
    return false;
}

static bool TryAcquireGVFSLockForProcess(
    bool unattended,
    PIPE_HANDLE pipeClient,
    const std::string& fullCommand,
    int pid,
    bool isElevated,
    bool isConsoleOutputRedirectedToFile,
    bool checkAvailabilityOnly,
    const std::string& gitCommandSessionId,
    std::string& result)
{
    // Format:
    // "AcquireLock|<pid>|<is elevated>|<checkAvailabilityOnly>|<parsed command length>|<parsed command>|<gitcommndsessionid length>|<gitcommand sessionid>"

    std::ostringstream requestMessageStream;
    requestMessageStream
        << "AcquireLock" << "|"
        << pid << "|"
        << (isElevated ? "true" : "false") << "|"
        << (checkAvailabilityOnly ? "true" : "false") << "|"
        << fullCommand.length() << "|"
        << fullCommand << "|"
        << gitCommandSessionId.length() << "|"
        << gitCommandSessionId
        << static_cast<char>(0x03);

    std::string requestMessage = requestMessageStream.str();

    unsigned long bytesWritten;
    unsigned long messageLength = static_cast<unsigned long>(requestMessage.length() + 1);
    int error = 0;
    bool success = WriteToPipe(
        pipeClient,
        requestMessage.c_str(),
        messageLength,
        &bytesWritten,
        &error);

    if (!success || bytesWritten != messageLength)
    {
        die(ReturnCode::PipeWriteFailed, "Failed to write to pipe (%d)\n", error);
    }

    char message[PIPE_BUFFER_SIZE + 1];
    unsigned long bytesRead;
    int lastError;
    success = ReadFromPipe(
        pipeClient,
        message,
        PIPE_BUFFER_SIZE,
        &bytesRead,
        &lastError);

    if (!success)
    {
        result = "Failed to read response";
        return false;
    }

    std::string response(message);
    size_t headerSeparator = response.find('|');
    std::string responseHeader;
    if (headerSeparator != string::npos)
    {
        responseHeader = response.substr(0, headerSeparator);
    }
    else
    {
        responseHeader = response;
    }

    if (responseHeader == AcceptResult || responseHeader == AvailableResult)
    {
        return CheckAcceptResponse(responseHeader, checkAvailabilityOnly, result);
    }
    else if (responseHeader == MountNotReadyResult)
    {
        result = "GVFS has not finished initializing, please wait a few seconds and try again.";
        return false;
    }
    else if (responseHeader == UnmountInProgressResult)
    {
        result = "GVFS is unmounting.";
        return false;
    }
    else if (responseHeader == DenyGVFSResult)
    {
        // TODO
        // message = response.DenyGVFSMessage;
    }
    else if (responseHeader == DenyGitResult)
    {
        // TODO
        // message = string.Format("Waiting for '{0}' to release the lock", response.ResponseData.ParsedCommand);
    }
    else
    {
        result = "Error when acquiring the lock. Unrecognized response: "; // +response.CreateMessage();
        return false;
    }

    auto waitForLock = [pipeClient, &requestMessage, &message, &bytesRead, &lastError, &result, checkAvailabilityOnly]() -> bool
    {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));

            unsigned long bytesWritten;
            unsigned long messageLength = static_cast<unsigned long>(requestMessage.length() + 1);
            int error = 0;
            bool success = WriteToPipe(
                pipeClient,
                requestMessage.c_str(),
                messageLength,
                &bytesWritten,
                &error);

            if (!success || bytesWritten != messageLength)
            {
                die(ReturnCode::PipeWriteFailed, "Failed to write to pipe (%d)\n", error);
            }

            success = ReadFromPipe(
                pipeClient,
                message,
                PIPE_BUFFER_SIZE,
                &bytesRead,
                &lastError);

            if (!success)
            {
                result = "Failed to read response";
                return false;
            }

            std::string response(message);
            size_t headerSeparator = response.find('|');
            std::string responseHeader;
            if (headerSeparator != string::npos)
            {
                responseHeader = response.substr(0, headerSeparator);
            }
            else
            {
                responseHeader = response;
            }

            if (responseHeader == AcceptResult || responseHeader == AvailableResult)
            {
                return CheckAcceptResponse(responseHeader, checkAvailabilityOnly, result);
            }
            else if (responseHeader == UnmountInProgressResult)
            {
                return false;
            }
        }
    };

    UNREFERENCED_PARAMETER(unattended);
    bool isSuccessfulLockResult;
    // if (unattended)
    {
        isSuccessfulLockResult = waitForLock();
    }

    UNREFERENCED_PARAMETER(isConsoleOutputRedirectedToFile);
    /*else
    {
        isSuccessfulLockResult = ConsoleHelper.ShowStatusWhileRunning(
            waitForLock,
            message,
            output: Console.Out,
            showSpinner : !isConsoleOutputRedirectedToFile,
            gvfsLogEnlistmentRoot : gvfsEnlistmentRoot);
    }*/

    result = "";
    return isSuccessfulLockResult;
}

void AcquireGVFSLockForProcess(bool unattended, int argc, char* argv[], int pid, PIPE_HANDLE pipeClient)
{
    std::string result;
    bool checkGvfsLockAvailabilityOnly = CheckGVFSLockAvailabilityOnly(argc, argv);
    std::string fullCommand = GenerateFullCommand(argc, argv);
    std::string gitCommandSessionId = GetGitCommandSessionId();

    if (!TryAcquireGVFSLockForProcess(
        unattended,
        pipeClient,
        fullCommand,
        pid,
        false, // GVFSHooksPlatform.IsElevated(),
        false, // isConsoleOutputRedirectedToFile: GVFSHooksPlatform.IsConsoleOutputRedirectedToFile(),
        checkGvfsLockAvailabilityOnly, // checkAvailabilityOnly
        gitCommandSessionId,
        result))
    {
        die(InvalidCommand, result.c_str());
    }
}

void SendReleaseLock(
    bool unattended,
    PIPE_HANDLE pipeClient,
    const std::string& fullCommand,
    int pid,
    bool isElevated,
    bool isConsoleOutputRedirectedToFile)
{
    // Format:
    // "ReleaseLock|<pid>|<is elevated>|<checkAvailabilityOnly>|<parsed command length>|<parsed command>|<gitcommndsessionid length>|<gitcommand sessionid>"

    std::ostringstream requestMessageStream;
    requestMessageStream
        << "ReleaseLock" << "|"
        << pid << "|"
        << (isElevated ? "true" : "false") << "|"
        << "false" << "|" // checkAvailabilityOnly
        << fullCommand.length() << "|"
        << fullCommand << "|"
        << 0 << "|" // gitCommandSessionId length
        << ""; // gitCommandSessionId

    std::string requestMessage = requestMessageStream.str();

    unsigned long bytesWritten;
    unsigned long messageLength = static_cast<unsigned long>(requestMessage.length() + 1);
    int error = 0;
    bool success = WriteToPipe(
        pipeClient,
        requestMessage.c_str(),
        messageLength,
        &bytesWritten,
        &error);

    if (!success || bytesWritten != messageLength)
    {
        die(ReturnCode::PipeWriteFailed, "Failed to write to pipe (%d)\n", error);
    }

    char message[PIPE_BUFFER_SIZE + 1];
    unsigned long bytesRead;
    int lastError;
    success = ReadFromPipe(
        pipeClient,
        message,
        PIPE_BUFFER_SIZE,
        &bytesRead,
        &lastError);

    // TODO: Fancy response handling
    UNREFERENCED_PARAMETER(unattended);
    UNREFERENCED_PARAMETER(isConsoleOutputRedirectedToFile);
    if (!success)
    {
        die(PipeReadFailed, "\nError communicating with GVFS: Run 'git status' to check the status of your repo");
    }

}

void ReleaseGVFSLock(bool unattended, int argc, char* argv[], int pid, PIPE_HANDLE pipeClient)
{
    string fullCommand = GenerateFullCommand(argc, argv);

    SendReleaseLock(
        unattended,
        pipeClient,
        fullCommand,
        pid,
        false, // GVFSHooksPlatform.IsElevated(),
        false); // GVFSHooksPlatform.IsConsoleOutputRedirectedToFile(),
}

void RunLockRequest(int argc, char *argv[], bool unattended, std::function<void(bool, int, char*[], int, PIPE_HANDLE)> requestToRun)
{
    if (ShouldLock(argc, argv))
    {
        PATH_STRING pipeName(GetGVFSPipeName(argv[0]));
        PIPE_HANDLE pipeHandle = CreatePipeToGVFS(pipeName);

        int pid = GetParentPid(argc, argv);
        if (pid == InvalidProcessId || !IsProcessActive(pid))
        {
            die(InvalidCommand, "GVFS.Hooks: Unable to find parent git.exe process (PID: %d).", pid);
        }

        requestToRun(unattended, argc, argv, pid, pipeHandle);
    }
}

void RunPreCommands(int argc, char *argv[])
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    /*string command = GetGitCommand(args);
    switch (command)
    {
    case "fetch":
    case "pull":
        ProcessHelper.Run("gvfs", "prefetch --commits", redirectOutput: false);
        break;
    }*/
}

void RunPostCommands()
{
    /*if (!unattended)
    {
        RemindUpgradeAvailable();
    }*/
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        die(ReturnCode::InvalidArgCount, "Usage: gvfs.hooks.exe --git-pid=<pid> <hook> <git verb> [<other arguments>]");
    }

    // TODO: Check if unattended
    bool unattended = false;

    // TODO: Exit with success if outside VFS4G repo

    DisableCRLFTranslationOnStdPipes();

    HookType hookType = GetHookType(argv[1]);
    switch (hookType)
    {
    case HookType::PreCommand:
        CheckForLegalCommands(argc, argv);
        RunLockRequest(argc, argv, unattended, AcquireGVFSLockForProcess);
        RunPreCommands(argc, argv);
        break;

    case HookType::PostCommand:
        // Do not release the lock if this request was only run to see if it could acquire the GVFSLock,
        // but did not actually acquire it.
        if (!CheckGVFSLockAvailabilityOnly(argc, argv))
        {
            RunLockRequest(argc, argv, unattended, ReleaseGVFSLock);
        }

        RunPostCommands();
        break;
        break;

    default:
        die(ReturnCode::InvalidArgCount, "Unrecognized hook: %s", argv[1]);
        break;
    }

    return 0;
}


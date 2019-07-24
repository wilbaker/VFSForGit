#include "stdafx.h"
#include "common.h"
#include "Shlobj.h"

using std::string;
using std::vector;

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
static const int PostCommandSpinnerDelayMs = 500;

static const std::string AcquireRequest = "AcquireLock";
static const std::string DenyGVFSResult = "LockDeniedGVFS";
static const std::string DenyGitResult = "LockDeniedGit";
static const std::string AcceptResult = "LockAcquired";
static const std::string AvailableResult = "LockAvailable";
static const std::string MountNotReadyResult = "MountNotReady";
static const std::string UnmountInProgressResult = "UnmountInProgress";

static const std::string UnattendedEnvironmentVariable = "GVFS_UNATTENDED";

static const std::string ReminderNotification = "\nA new version of GVFS is available. Run `gvfs upgrade --confirm` from an elevated command prompt to upgrade.\n";

static PATH_STRING s_pipeName;

static const std::unordered_set<std::string> KnownGitCommands(
    {
        "add",
        "am",
        "annotate",
        "apply",
        "archive",
        "bisect--helper",
        "blame",
        "branch",
        "bundle",
        "cat-file",
        "check-attr",
        "check-ignore",
        "check-mailmap",
        "check-ref-format",
        "checkout",
        "checkout-index",
        "cherry",
        "cherry-pick",
        "clean",
        "clone",
        "column",
        "commit",
        "commit-tree",
        "config",
        "count-objects",
        "credential",
        "describe",
        "diff",
        "diff-files",
        "diff-index",
        "diff-tree",
        "fast-export",
        "fetch",
        "fetch-pack",
        "fmt-merge-msg",
        "for-each-ref",
        "format-patch",
        "fsck",
        "fsck-objects",
        "gc",
        "get-tar-commit-id",
        "grep",
        "hash-object",
        "help",
        "index-pack",
        "init",
        "init-db",
        "interpret-trailers",
        "log",
        "ls-files",
        "ls-remote",
        "ls-tree",
        "mailinfo",
        "mailsplit",
        "merge",
        "merge-base",
        "merge-file",
        "merge-index",
        "merge-ours",
        "merge-recursive",
        "merge-recursive-ours",
        "merge-recursive-theirs",
        "merge-subtree",
        "merge-tree",
        "mktag",
        "mktree",
        "mv",
        "name-rev",
        "notes",
        "pack-objects",
        "pack-redundant",
        "pack-refs",
        "patch-id",
        "pickaxe",
        "prune",
        "prune-packed",
        "pull",
        "push",
        "read-tree",
        "rebase",
        "rebase--helper",
        "receive-pack",
        "reflog",
        "remote",
        "remote-ext",
        "remote-fd",
        "repack",
        "replace",
        "rerere",
        "reset",
        "rev-list",
        "rev-parse",
        "revert",
        "rm",
        "send-pack",
        "shortlog",
        "show",
        "show-branch",
        "show-ref",
        "stage",
        "status",
        "stripspace",
        "symbolic-ref",
        "tag",
        "unpack-file",
        "unpack-objects",
        "update-index",
        "update-ref",
        "update-server-info",
        "upload-archive",
        "upload-archive--writer",
        "var",
        "verify-commit",
        "verify-pack",
        "verify-tag",
        "version",
        "whatchanged",
        "worktree",
        "write-tree",

        // Externals
        "bisect",
        "filter-branch",
        "gui",
        "merge-octopus",
        "merge-one-file",
        "merge-resolve",
        "mergetool",
        "parse-remote",
        "quiltimport",
        "rebase",
        "submodule",
    });

const int PIPE_BUFFER_SIZE = 1024;

string RunProcess(const string& processName, const string& args, bool redirectOutput)
{
    // https://docs.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output
    HANDLE g_hChildStd_OUT_Rd = NULL;
    HANDLE g_hChildStd_OUT_Wr = NULL;

    if (redirectOutput)
    {
        // Set the bInheritHandle flag so pipe handles are inherited.
        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        // Create a pipe for the child process's STDOUT.
        if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0))
        {
            die(ReturnCode::PipeConnectError, "StdoutRd CreatePipe");
        }

        // Ensure the read handle to the pipe for STDOUT is not inherited.
        if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
        {
            die(ReturnCode::PipeConnectError, "Stdout SetHandleInformation");
        }
    }

    PROCESS_INFORMATION piProcInfo;
    STARTUPINFOW siStartInfo;
    BOOL bSuccess = FALSE;

    // Set up members of the PROCESS_INFORMATION structure. 
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    // Set up members of the STARTUPINFO structure. 
    // This structure specifies the STDIN and STDOUT handles for redirection.
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);

    if (redirectOutput)
    {
        siStartInfo.hStdError = INVALID_HANDLE_VALUE;
        siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
        siStartInfo.hStdInput = INVALID_HANDLE_VALUE;
        siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
    }

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> utf16conv;
    std::wstring command = utf16conv.from_bytes(processName) + L" " + utf16conv.from_bytes(args);
    wchar_t buffer[4096];

    // TODO: Handle buffer too small
    wcscpy_s(buffer, command.c_str());

    // Create the child process. 
    bSuccess = CreateProcessW(
        NULL,
        buffer,     // command line 
        NULL,          // process security attributes 
        NULL,          // primary thread security attributes 
        TRUE,          // handles are inherited 
        0,             // creation flags 
        NULL,          // use parent's environment 
        NULL,          // use parent's current directory 
        &siStartInfo,  // STARTUPINFO pointer 
        &piProcInfo);  // receives PROCESS_INFORMATION

    // If an error occurs, exit the application. 
    if (!bSuccess)
        die(ReturnCode::LastError, "CreateProcess");

    string output;
    if (redirectOutput)
    {
        CloseHandle(g_hChildStd_OUT_Wr);

        DWORD dwRead;
        CHAR chBuf[4096] = {};
        bSuccess = FALSE;
        for (;;)
        {
            bSuccess = ReadFile(g_hChildStd_OUT_Rd, chBuf, 4096, &dwRead, NULL);
            if (!bSuccess || dwRead == 0)
            {
                break;
            }
            else
            {
                output += chBuf;
            }
        }
    }

    // Wait until child process exits
    WaitForSingleObject(piProcInfo.hProcess, INFINITE);

    CloseHandle(piProcInfo.hProcess);
    CloseHandle(piProcInfo.hThread);

    return output;
}

// needs cross-plat
static PATH_STRING GetHighestAvailableVersionFilePath()
{
    PWSTR programDataPath;
    HRESULT getPathResult = SHGetKnownFolderPath(
        FOLDERID_ProgramData,  // rfid
        KF_FLAG_CREATE,        // dwFlags
        NULL,                  // hToken
        &programDataPath);

    if (SUCCEEDED(getPathResult))
    {
        wchar_t upgradeDirectory[MAX_PATH];
        _snwprintf_s(upgradeDirectory, _TRUNCATE, L"%s\\GVFS\\GVFS.Upgrade\\HighestAvailableVersion", programDataPath);
        CoTaskMemFree(programDataPath);
        return PATH_STRING(upgradeDirectory);
    }

    return PATH_STRING();
}

// needs cross-plat
bool FileExists(const PATH_STRING& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return (attributes != INVALID_FILE_ATTRIBUTES) && ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

static bool IsLocalUpgradeAvailable()
{
    PATH_STRING highestVersionFilePath(GetHighestAvailableVersionFilePath());
    if (!highestVersionFilePath.empty())
    {
        return FileExists(highestVersionFilePath);
    }

    return false;
}

void RemindUpgradeAvailable()
{
    // The idea is to generate a random number between 0 and 99. To make
    // sure that the reminder is displayed only 10% of the times a git
    // command is run, check that the random number is between 0 and 10,
    // which will have a probability of 10/100 == 10%.
    std::mt19937 gen(static_cast<unsigned int>(std::time(nullptr) % UINT_MAX)); //Standard mersenne_twister_engine seeded with the current time
    int reminderFrequency = 10;
    int randomValue = gen() % 100;

    if (randomValue <= reminderFrequency && IsLocalUpgradeAvailable())
    {
        printf(ReminderNotification.c_str());
    }
}

bool GitCommandIsKnown(const std::string& gitCommand)
{
    return KnownGitCommands.find(gitCommand) != KnownGitCommands.end();
}

bool IsAlias(const std::string& gitCommand)
{
    string output = RunProcess("git", "config --get alias." + gitCommand, /*redirectOutput*/ true);
    return !output.empty();
}

static bool IsUnattended()
{
    char unattendedEnvVariable[2056];
    size_t requiredSize;
    if (getenv_s(&requiredSize, unattendedEnvVariable, UnattendedEnvironmentVariable.c_str()) == 0)
    {
        return 0 == strcmp(unattendedEnvVariable, "1");
    }
    
    return false;
}

static std::string GetGitCommandSessionId()
{
    char gitEnvVariable[2056];
    size_t requiredSize;

    // TOOD: handle error codes
    if (getenv_s(&requiredSize, gitEnvVariable, "GIT_TR2_PARENT_SID") == 0)
    {
        return gitEnvVariable;
    }

    return "";
}

static bool IsElevated()
{
    // https://docs.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-checktokenmembership
    BOOL b;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSID AdministratorsGroup;
    b = AllocateAndInitializeSid(
        &NtAuthority,
        2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &AdministratorsGroup);
    if (b)
    {
        if (!CheckTokenMembership(NULL, AdministratorsGroup, &b))
        {
            b = FALSE;
        }

        FreeSid(AdministratorsGroup);
    }

    return(b);
}

static bool IsConsoleOutputRedirectedToFile()
{
    // Windows specific
    return FILE_TYPE_DISK == GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
}

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

    // TODO: Error on duplicates?
    std::string pidString(*pidArg);
    if (!pidString.empty())
    {
        pidString = pidString.substr(GitPidArg.length());

        // TODO: Ensure string is value int value?
        return std::atoi(pidString.c_str());
    }

    die(InvalidCommand, "Git did not supply the process Id.\nEnsure you are using the correct version of the git client.");

    return InvalidProcessId;
}

static string ParseCommandFromLockResponse(const string& responseBody)
{
    if (!responseBody.empty())
    {
        // This mesage is stored using the MessageSeperator delimiter for performance reasons
        // Format of the body uses length prefixed string so that the strings can have the delimiter in them
        // Examples:
        // "123|true|false|13|parsedCommand|9|sessionId"
        // "321|false|true|30|parsedCommand with | delimiter|26|sessionId with | delimiter"
        vector<string> dataParts;
        size_t offset = 0;
        size_t delimPos = responseBody.find('|');
        while (delimPos != string::npos)
        {
            dataParts.emplace_back(responseBody.substr(offset, delimPos - offset));
            offset = delimPos + 1;
            delimPos = responseBody.find('|', offset);
        }

        dataParts.emplace_back(responseBody.substr(offset));
       
        if (dataParts.size() < 7)
        {
            die(ReturnCode::InvalidResponse, "Invalid lock message. Expected at least 7 parts, got: %zu from message: '%s'", dataParts.size(), responseBody.c_str());
        }

        int parsedCommandLength = 0;
        try
        {
            parsedCommandLength = std::stoi(dataParts[3]);
        }
        catch (...)
        {
            die(ReturnCode::InvalidResponse, "Invalid lock message. Failed to parse command length: '%s'", dataParts[3].c_str());
        }

        // ParsedCommandLength should be the length of the string at the end of the message
        // Add the length of the previous parts, plus delimiters
        size_t commandStartingSpot = dataParts[0].length() + dataParts[1].length() + dataParts[2].length() + dataParts[3].length() + 4;
        if ((commandStartingSpot + parsedCommandLength) >= responseBody.length())
        {
            die(ReturnCode::InvalidResponse, "Invalid lock message. The parsedCommand is an unexpected length, got: {0} from message: '{1}'", parsedCommandLength, responseBody.c_str());
        }

        return responseBody.substr(commandStartingSpot, parsedCommandLength);
    }

    return "";
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

static bool ReadTerminatedMessageFromGVFS(PIPE_HANDLE pipeHandle, std::string& responseMessage)
{
    // Allow for 1 extra character in case we need to
    // null terminate the message, and the message
    // is PIPE_BUFFER_SIZE chars long.
    char message[PIPE_BUFFER_SIZE + 1];
    unsigned long bytesRead;
    unsigned long messageLength;
    int lastError;
    bool finishedReading = false;
    bool success;
    std::ostringstream response;

    do
    {
        success = ReadFromPipe(
            pipeHandle,
            message,
            PIPE_BUFFER_SIZE,
            &bytesRead,
            &lastError);

        if (!success)
        {
            break;
        }

        messageLength = bytesRead;

        if (message[messageLength - 1] == '\x3')
        {
            finishedReading = true;
            messageLength -= 1;
        }

        message[messageLength] = '\0';
        response << message;

    } while (success && !finishedReading);

    if (!success)
    {
        return false;
    }

    responseMessage = response.str();
    return true;
}

static bool IsProcessActive(int pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid);
    if (process != NULL)
    {
        DWORD exitCode;
        if (GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE)
        {
            CloseHandle(process);
            return true;
        }

        CloseHandle(process);
    }
     
    return false;
}

static bool IsGitEnvVarDisabled(string envVar)
{
    char gitEnvVariable[2056];
    size_t requiredSize;

    // TOOD: handle error codes
    if (getenv_s(&requiredSize, gitEnvVariable, envVar.c_str()) == 0)
    {
        if (_stricmp(gitEnvVariable, "false") == 0 ||
            _stricmp(gitEnvVariable, "no") == 0 ||
            _stricmp(gitEnvVariable, "off") == 0 ||
            _stricmp(gitEnvVariable, "0") == 0)
        {
            return true;
        }
    }

    return false;
}

static bool ShowStatusWhileRunning(
    std::function<bool()> action,
    const string& message,
    bool showSpinner,
    int initialDelayMs)
{
    bool result = false;
    std::atomic<bool> initialMessageWritten = false;

    if (!showSpinner)
    {
        printf("%s...", message.c_str());
        fflush(stdout);
        initialMessageWritten = true;
        result = action();
    }
    else
    {
        std::promise<void> actionIsDonePromise;
        std::future<void> actionIsDoneFuture(actionIsDonePromise.get_future());
        std::future<void> spinnerThread = std::async(
            std::launch::async, 
            [&actionIsDoneFuture, initialDelayMs, &initialMessageWritten, &message]()
        {
            int retries = 0;

            // TODO: use mdash instead of ndash
            std::array<char, 4> waiting = { '-', '\\', '|', '/' };

            bool isComplete = false;
            while (!isComplete)
            {
                if (retries == 0)
                {
                    isComplete = actionIsDoneFuture.wait_for(std::chrono::milliseconds(initialDelayMs)) == std::future_status::ready;
                }
                else
                {
                    printf("\r%s...%c", message.c_str(), waiting[(retries / 2) % waiting.size()]);
                    fflush(stdout);
                    initialMessageWritten = true;
                    isComplete = actionIsDoneFuture.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
                }

                retries++;
            }

            if (initialMessageWritten)
            {
                // Clear out any trailing waiting character
                printf("\r%s...", message.c_str());
                fflush(stdout);
            }
        });

        result = action();
        actionIsDonePromise.set_value();
        spinnerThread.wait();
    }

    if (result)
    {
        if (initialMessageWritten)
        {
            printf("Succeeded\n");
        }
    }
    else
    {
        if (!initialMessageWritten)
        {
            printf("\r%s...", message.c_str());
            fflush(stdout);
        }

        printf("Failed.\n");
    }

    return result;
}

static bool CheckGVFSLockAvailabilityOnly(int argc, char *argv[])
{
    // Don't acquire the GVFS lock if the git command is not acquiring locks.
    // This enables tools to run status commands without to the index and
    // blocking other commands from running. The git argument
    // "--no-optional-locks" results in a 'negative'
    // value GIT_OPTIONAL_LOCKS environment variable.

    if (_stricmp(GetGitCommand(argc, argv).c_str(), "status") != 0)
    {
        return false;
    }

    char** beginArgs = argv;
    char** endArgs = beginArgs + argc;
    if (endArgs != std::find_if(beginArgs, endArgs, [](char* argString) { return (0 == _stricmp(argString, "--no-lock-index")); }))
    {
        return true;
    }

    return IsGitEnvVarDisabled("GIT_OPTIONAL_LOCKS");
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

    if (!GitCommandIsKnown(gitCommand) && IsAlias(gitCommand))
    {
        return false;
    }

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
    unsigned long messageLength = static_cast<unsigned long>(requestMessage.length());
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

    std::string response;
    success = ReadTerminatedMessageFromGVFS(pipeClient, /* out */ response);

    if (!success)
    {
        result = "Failed to read response";
        return false;
    }

    size_t headerSeparator = response.find('|');
    string responseHeader;
    string message;
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
        message = response.substr(headerSeparator + 1);
    }
    else if (responseHeader == DenyGitResult)
    {
        message = "Waiting for '" + ParseCommandFromLockResponse(response.substr(headerSeparator + 1)) + "' to release the lock";
    }
    else
    {
        result = "Error when acquiring the lock. Unrecognized response: " + response;
        return false;
    }

    auto waitForLock = [pipeClient, &requestMessage, &result, checkAvailabilityOnly]() -> bool
    {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));

            unsigned long bytesWritten;
            unsigned long messageLength = static_cast<unsigned long>(requestMessage.length());
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

            std::string response;
            success = ReadTerminatedMessageFromGVFS(pipeClient, /* out */ response);

            if (!success)
            {
                result = "Failed to read response";
                return false;
            }

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

    bool isSuccessfulLockResult;
    if (unattended)
    {
        isSuccessfulLockResult = waitForLock();
    }
    else
    {
        isSuccessfulLockResult = ShowStatusWhileRunning(
            waitForLock,
            message,
            !isConsoleOutputRedirectedToFile, // showSpinner
            0); // initialDelayMs
    }

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
        IsElevated(),
        IsConsoleOutputRedirectedToFile(),
        checkGvfsLockAvailabilityOnly,
        gitCommandSessionId,
        result))
    {
        die(InvalidCommand, result.c_str());
    }
}

void ReleaseReponseHandler(const std::string& rawResponse)
{
    size_t headerSeparator = rawResponse.find('|');
    string responseHeader;
    string responseBody;
    if (headerSeparator != string::npos)
    {
        responseHeader = rawResponse.substr(0, headerSeparator);
        responseBody = rawResponse.substr(headerSeparator + 1);
    }
    else
    {
        responseHeader = rawResponse;
    }

    if (!responseBody.empty())
    {
        vector<string> releaseLockSections;
        size_t offset = 0;
        size_t delimPos = responseBody.find('<');
        while (delimPos != string::npos)
        {
            releaseLockSections.emplace_back(responseBody.substr(offset, delimPos - offset));
            offset = delimPos + 1;
            delimPos = responseBody.find('|', offset);
        }

        releaseLockSections.emplace_back(responseBody.substr(offset));

        if (releaseLockSections.size() != 4)
        {
            printf("\nError communicating with GVFS: Run 'git status' to check the status of your repo\n");
            printf("    DEBUG: releaseLockSections.size() %zu, %s\n", releaseLockSections.size(), rawResponse.c_str());
            return;
        }

        int failedToUpdateCount = 0;
        try
        {
            failedToUpdateCount = std::stoi(releaseLockSections[0]);
        }
        catch (...)
        {
            printf("\nError communicating with GVFS: Run 'git status' to check the status of your repo\n");
            return;
        }

        int failedToDeleteCount = 0;
        try
        {
            failedToDeleteCount = std::stoi(releaseLockSections[1]);
        }
        catch (...)
        {
            printf("\nError communicating with GVFS: Run 'git status' to check the status of your repo\n");
            return;
        }

        if (failedToUpdateCount > 0 || failedToDeleteCount > 0)
        {
            if (failedToUpdateCount + failedToDeleteCount > 100)
            {
                printf("\nGVFS failed to update %d files, run 'git status' to check the status of files in the repo", failedToDeleteCount + failedToUpdateCount);
            }
            else
            {
                // TODO: Build failed to delete/update list
            }
        }
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
        << "" // gitCommandSessionId
        << static_cast<char>(0x03);

    std::string requestMessage = requestMessageStream.str();

    unsigned long bytesWritten;
    unsigned long messageLength = static_cast<unsigned long>(requestMessage.length());
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

    auto releaseLock = [&pipeClient]() -> bool
    {
        std::string response;
        if (!ReadTerminatedMessageFromGVFS(pipeClient, /* out */ response))
        {
            printf("\nError communicating with GVFS: Run 'git status' to check the status of your repo\n");
            printf("    DEBUG: %s\n", response.c_str());
            return true;
        }

        ReleaseReponseHandler(response);
        return true;
    };

    if (unattended || isConsoleOutputRedirectedToFile)
    {
        releaseLock();
    }
    else
    {
        ShowStatusWhileRunning(
            releaseLock,
            "Waiting for GVFS to parse index and update placeholder files",
            !isConsoleOutputRedirectedToFile, // showSpinner
            PostCommandSpinnerDelayMs);
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
        IsElevated(),
        IsConsoleOutputRedirectedToFile());
}

void RunLockRequest(int argc, char *argv[], bool unattended, std::function<void(bool, int, char*[], int, PIPE_HANDLE)> requestToRun)
{
    if (ShouldLock(argc, argv))
    {
        PIPE_HANDLE pipeHandle = CreatePipeToGVFS(s_pipeName);

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
    string command = GetGitCommand(argc, argv);
    if (command == "fetch" || command == "pull")
    {
        RunProcess("gvfs", "prefetch --commits", /*redirectOutput*/ false);
    }
}

void RunPostCommands(bool unattended)
{
    if (!unattended)
    {
        RemindUpgradeAvailable();
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        die(ReturnCode::InvalidArgCount, "Usage: gvfs.hooks.exe --git-pid=<pid> <hook> <git verb> [<other arguments>]");
    }

    bool unattended = IsUnattended();

    if (!GetPipeNameIfInsideGVFSRepo(/*out*/ s_pipeName))
    {
        // Nothing to hook when being run outside of a GVFS repo.
        // This is also the path when run with --git-dir outside of a GVFS directory, see Story #949665
        die(ReturnCode::Success, "");
    }

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

        RunPostCommands(unattended);
        break;
        break;

    default:
        die(ReturnCode::InvalidArgCount, "Unrecognized hook: %s", argv[1]);
        break;
    }

    return 0;
}


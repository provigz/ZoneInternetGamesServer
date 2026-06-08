#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <Lmcons.h>
#include <conio.h>
#include <stdio.h>
#include <string>
#include <TlHelp32.h>
#include <wchar.h>

#define DLL_FILE_PATH "InternetGamesClientDLL.dll"
#define DLL_FILE_PATH_XP "InternetGamesClientDLL_XP.dll"

/** Functions */
#ifdef WIN_XP
int EnableDebugPrivileges();
DWORD* FindAllProcessIDs(int& outCount);
#else
#ifdef _WIN64
DWORD* FindAllProcessIDs(int& outCount);
#else
DWORD* FindAllProcessIDs(bool procXP, int& outCount);
#endif
#endif
void WaitForCloseInput();

int wmain(int argc, wchar_t* argv[])
{
    system("cls"); // Clear the console screen

    printf("===================================================\n");
#ifdef WIN_XP
    printf("  WINDOWS XP/ME INTERNET GAMES DLL INJECTOR [x86]  \n");
#else
#ifdef _WIN64
    printf("     WINDOWS INTERNET GAMES DLL INJECTOR [x64]     \n");
#else
    printf("     WINDOWS INTERNET GAMES DLL INJECTOR [x86]     \n");
#endif
#endif
    printf("===================================================\n\n");

#ifdef WIN_XP
    // Enable debug privileges (required for Windows XP/2000)
    const int dbgPrivResult = EnableDebugPrivileges();
    if (dbgPrivResult < 0)
    {
        WaitForCloseInput();
        return dbgPrivResult;
    }
#endif

    bool errorOccurred = false;

    // Determine full DLL file path and get process IDs of target executables
    int procCount;
    CHAR currentDir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, currentDir);
#ifdef WIN_XP
    const std::string dllPath = std::string(currentDir) + '\\' + DLL_FILE_PATH_XP;
    const size_t dllPathSize = dllPath.length();

    const DWORD* procIDs = FindAllProcessIDs(procCount);
    if (!procIDs)
    {
        printf("ERROR: Couldn't find any running Windows XP/ME Internet Games!\n");
        WaitForCloseInput();
        return 1;
    }
#else
#ifdef _WIN64
    const std::string dllPath = std::string(currentDir) + '\\' + DLL_FILE_PATH;
    const size_t dllPathSize = dllPath.length();

    const DWORD* procIDs = FindAllProcessIDs(procCount);
    if (!procIDs)
    {
        printf("ERROR: Couldn't find any running Windows 7 Internet Games!\n");
        WaitForCloseInput();
        return 1;
    }
#else
    const std::string dllPath = std::string(currentDir) + '\\' + DLL_FILE_PATH;
    const std::string dllPathXP = std::string(currentDir) + '\\' + DLL_FILE_PATH_XP;
    const size_t dllPathSize = dllPath.length();
    const size_t dllPathXPSize = dllPathXP.length();

    const DWORD* procIDs = FindAllProcessIDs(false, procCount);
    int procCountXP;
    const DWORD* procIDsXP = FindAllProcessIDs(true, procCountXP);
    if (!procIDs && !procIDsXP)
    {
        printf("ERROR: Couldn't find any running Windows 7 or XP/ME Internet Games!\n");
        WaitForCloseInput();
        return 1;
    }

    /* Go through all XP processes */
    for (int procIdx = 0; procIdx < procCountXP; ++procIdx)
    {
        const DWORD procID = *(procIDsXP + procIdx);

        // Get a handle to the process
        HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, TRUE, procID);

        // Write full DLL file path to target process memory
        LPVOID filePathAddress = VirtualAllocEx(hProcess, NULL, dllPathXPSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!filePathAddress)
        {
            printf("ERROR: Process %d: Couldn't allocate memory for DLL file path in target process memory: %X\n", procID, GetLastError());
            errorOccurred = true;
            continue;
        }
        if (!WriteProcessMemory(hProcess, filePathAddress, dllPathXP.c_str(), dllPathXPSize, NULL))
        {
            printf("ERROR: Process %d: Couldn't write DLL file path in target process memory: %X\n", procID, GetLastError());
            errorOccurred = true;
            continue;
        }

        // Get the address of the "LoadLibraryA" function
        HMODULE hKernel32DLL = GetModuleHandle(L"kernel32.dll");
        if (!hKernel32DLL)
        {
            printf("ERROR: Process %d: Couldn't get handle to \"kernel32.dll\": %X\n", procID, GetLastError());
            errorOccurred = true;
            continue;
        }
        LPVOID hLoadLibraryA = GetProcAddress(hKernel32DLL, "LoadLibraryA");

        // Create a thread in the target process to load the DLL
        if (!CreateRemoteThread(hProcess, NULL, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(hLoadLibraryA), filePathAddress, 0, NULL))
        {
            printf("ERROR: Process %d: Creating a thread in the target process to load the DLL failed: %X\n", procID, GetLastError());
            errorOccurred = true;
            continue;
        }
    }
    delete[] procIDsXP;
#endif
#endif

    /* Go through all processes */
    for (int procIdx = 0; procIdx < procCount; ++procIdx)
    {
        const DWORD procID = *(procIDs + procIdx);

        // Get a handle to the process
        HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, TRUE, procID);

        // Write full DLL file path to target process memory
        LPVOID filePathAddress = VirtualAllocEx(hProcess, NULL, dllPathSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!filePathAddress)
        {
            printf("ERROR: Process %d: Couldn't allocate memory for DLL file path in target process memory: %X\n", procID, GetLastError());
            errorOccurred = true;
            continue;
        }
        if (!WriteProcessMemory(hProcess, filePathAddress, dllPath.c_str(), dllPathSize, NULL))
        {
            printf("ERROR: Process %d: Couldn't write DLL file path in target process memory: %X\n", procID, GetLastError());
            errorOccurred = true;
            continue;
        }

        // Get the address of the "LoadLibraryA" function
        HMODULE hKernel32DLL = GetModuleHandle(L"kernel32.dll");
        if (!hKernel32DLL)
        {
            printf("ERROR: Process %d: Couldn't get handle to \"kernel32.dll\": %X\n", procID, GetLastError());
            errorOccurred = true;
            continue;
        }
        LPVOID hLoadLibraryA = GetProcAddress(hKernel32DLL, "LoadLibraryA");

        // Create a thread in the target process to load the DLL
        if (!CreateRemoteThread(hProcess, NULL, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(hLoadLibraryA), filePathAddress, 0, NULL))
        {
            printf("ERROR: Process %d: Creating a thread in the target process to load the DLL failed: %X\n", procID, GetLastError());
#ifndef WIN_XP
#ifdef _WIN64
            printf("CHECK: Does the architecture of this injector (x64/64-bit) match the architecture of the target game?\n\n");
#else
            printf("CHECK: Does the architecture of this injector (x86/32-bit) match the architecture of the target game?\n\n");
#endif
#endif
            errorOccurred = true;
            continue;
        }
    }
    delete[] procIDs;

    if (errorOccurred)
        WaitForCloseInput();
    return 0;
}

/** Functions */

#ifdef WIN_XP
int EnableDebugPrivileges()
{
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
    {
        printf("ERROR: Couldn't open token for current process!");
        return -2;
    }

    TOKEN_PRIVILEGES tokenPriv;
    LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tokenPriv.Privileges[0].Luid);
    tokenPriv.PrivilegeCount = 1;
    tokenPriv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    const BOOL result = AdjustTokenPrivileges(hToken, FALSE, &tokenPriv, 0, NULL, 0);
    CloseHandle(hToken);
    if (!result)
    {
        printf("ERROR: Couldn't adjust privileges of process token!");
        return -3;
    }
    return 0;
}
#endif

#ifdef WIN_XP
DWORD* FindAllProcessIDs(int& outCount)
#else
#ifdef _WIN64
DWORD* FindAllProcessIDs(int& outCount)
#else
DWORD* FindAllProcessIDs(bool procXP, int& outCount)
#endif
#endif
{
    outCount = 0; // Initialize count to 0

    HANDLE processSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processSnap == INVALID_HANDLE_VALUE)
        return nullptr;

    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(processSnap, &processEntry))
    {
        do
        {
#ifdef WIN_XP
            if (!wcscmp(processEntry.szExeFile, L"zClientm.exe"))
#else
#ifdef _WIN64
            if (!wcscmp(processEntry.szExeFile, L"bckgzm.exe") ||
                !wcscmp(processEntry.szExeFile, L"chkrzm.exe") ||
                !wcscmp(processEntry.szExeFile, L"shvlzm.exe"))
#else
            if (procXP ? (!wcscmp(processEntry.szExeFile, L"zClientm.exe"))
                    : (!wcscmp(processEntry.szExeFile, L"bckgzm.exe") ||
                        !wcscmp(processEntry.szExeFile, L"chkrzm.exe") ||
                        !wcscmp(processEntry.szExeFile, L"shvlzm.exe")))
#endif
#endif
            {
                ++outCount;
            }
        }
        while (Process32Next(processSnap, &processEntry));
    }
    CloseHandle(processSnap);

    if (outCount == 0)
        return nullptr;

    DWORD* pidArray = new DWORD[outCount];
    int pidIdx = -1;

    processSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Process32First(processSnap, &processEntry))
    {
        do
        {
#ifdef WIN_XP
            if (!wcscmp(processEntry.szExeFile, L"zClientm.exe"))
#else
#ifdef _WIN64
            if (!wcscmp(processEntry.szExeFile, L"bckgzm.exe") ||
                !wcscmp(processEntry.szExeFile, L"chkrzm.exe") ||
                !wcscmp(processEntry.szExeFile, L"shvlzm.exe"))
#else
            if (procXP ? (!wcscmp(processEntry.szExeFile, L"zClientm.exe"))
                : (!wcscmp(processEntry.szExeFile, L"bckgzm.exe") ||
                    !wcscmp(processEntry.szExeFile, L"chkrzm.exe") ||
                    !wcscmp(processEntry.szExeFile, L"shvlzm.exe")))
#endif
#endif
            {
                pidArray[++pidIdx] = processEntry.th32ProcessID;
            }
        }
        while (Process32Next(processSnap, &processEntry));
    }
    CloseHandle(processSnap);

    return pidArray;
}

void WaitForCloseInput()
{
    printf("\nPress any key to close...");
    _getch();
}

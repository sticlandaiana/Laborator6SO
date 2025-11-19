// file: SharedCounter.cpp
// Compile in Visual Studio (x86 or x64) - single executable.
// Run the exe once; it creates a child process automatically.

#include <windows.h>
#include <iostream>
#include <string>
#include <ctime>
#include <cstdlib>

const char* MAP_NAME = "Local\\MySharedCounterMap_Example_v1";
const char* SEM_NAME = "Local\\MySharedCounterSemaphore_Example_v1";
const int TARGET = 1000;

struct SharedData {
    volatile LONG current; // current counter (0..TARGET)
    volatile LONG pad;     // padding/reserved
};

void log(const char* who, const char* msg) {
    std::cout << "[" << who << "] " << msg << std::endl;
}

int main(int argc, char** argv) {
    bool isChild = false;
    if (argc >= 2) {
        std::string a = argv[1];
        if (a == "child") isChild = true;
    }

    // Seed RNG differently for parent/child
    srand((unsigned int)(GetTickCount() ^ GetCurrentProcessId() ^ (isChild ? 0xABC : 0x123)));

    // Create or open file mapping
    HANDLE hMap = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(SharedData),
        MAP_NAME
    );
    if (!hMap) {
        std::cerr << "CreateFileMapping failed: " << GetLastError() << std::endl;
        return 1;
    }

    bool createdMapping = (GetLastError() != ERROR_ALREADY_EXISTS);

    // Map view
    SharedData* pShared = reinterpret_cast<SharedData*>(
        MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData))
        );
    if (!pShared) {
        std::cerr << "MapViewOfFile failed: " << GetLastError() << std::endl;
        CloseHandle(hMap);
        return 1;
    }

    // Initialize memory if we created it
    if (createdMapping) {
        pShared->current = 0;
        pShared->pad = 0;
    }

    // Create or open named semaphore with initial count 1 (acts like mutex)
    HANDLE hSem = CreateSemaphoreA(nullptr, 1, 1, SEM_NAME);
    if (!hSem) {
        std::cerr << "CreateSemaphore failed: " << GetLastError() << std::endl;
        UnmapViewOfFile(pShared);
        CloseHandle(hMap);
        return 1;
    }

    HANDLE hChildProcess = nullptr;
    if (!isChild) {
        // Create child process (same exe) with argument "child"
        char cmdLine[MAX_PATH * 2];
        // Get full path to current executable
        DWORD len = GetModuleFileNameA(nullptr, cmdLine, sizeof(cmdLine));
        if (len == 0 || len == sizeof(cmdLine)) {
            std::cerr << "GetModuleFileName failed" << std::endl;
            // but continue: fallback to argv[0]
            if (argc >= 1) strncpy_s(cmdLine, argv[0], sizeof(cmdLine) - 1);
        }
        // append child argument
        std::string cmd = std::string("\"") + cmdLine + "\" child";

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        if (!CreateProcessA(
            nullptr,
            const_cast<char*>(cmd.c_str()),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi
        )) {
            std::cerr << "CreateProcess failed: " << GetLastError() << std::endl;
            // continue anyway — this would mean only parent runs
        }
        else {
            hChildProcess = pi.hProcess;
            CloseHandle(pi.hThread); // keep process handle only
            std::cout << "[parent] Child created (pid=" << pi.dwProcessId << ")\n";
        }
    }

    const char* who = isChild ? "child" : "parent";
    std::cout << "[" << who << "] started. Press Ctrl+C to abort.\n";

    // Main loop
    while (true) {
        DWORD wait = WaitForSingleObject(hSem, INFINITE);
        if (wait != WAIT_OBJECT_0) {
            std::cerr << "[" << who << "] WaitForSingleObject on semaphore failed: " << GetLastError() << std::endl;
            break;
        }

        // Inside critical section
        LONG cur = InterlockedCompareExchange(&pShared->current, 0, 0); // read atomically
        if (cur >= TARGET) {
            // release and exit
            ReleaseSemaphore(hSem, 1, nullptr);
            break;
        }

        // Toss coin and while coin==2 write next number
        int coin;
        do {
            coin = (rand() % 2) + 1; // 1 or 2
            if (coin == 2) {
                // increment and write
                LONG newval = InterlockedIncrement(&pShared->current);
                std::cout << "[" << who << "] wrote " << newval << std::endl;
                if (newval >= TARGET) break;
                // small short pause (not required) to make output readable
                Sleep(1);
            }
        } while (coin == 2 && InterlockedCompareExchange(&pShared->current, 0, 0) < TARGET);

        // Leave critical section
        ReleaseSemaphore(hSem, 1, nullptr);

        // If reached target, break
        if (InterlockedCompareExchange(&pShared->current, 0, 0) >= TARGET) break;

        // small random sleep to let the other process run
        Sleep((rand() % 5) + 1);
    }

    std::cout << "[" << who << "] exiting. Final counter=" << pShared->current << std::endl;

    // If parent, wait for child and cleanup
    if (!isChild) {
        if (hChildProcess) {
            WaitForSingleObject(hChildProcess, INFINITE);
            CloseHandle(hChildProcess);
        }
        // cleanup named objects (mapping/semaphore). Other processes may still have handles mapped — but here both done.
        UnmapViewOfFile(pShared);
        CloseHandle(hMap);
        CloseHandle(hSem);
    }
    else {
        // child: unmap only; do not close mapping/semaphore names (handles)
        UnmapViewOfFile(pShared);
        CloseHandle(hMap);
        CloseHandle(hSem);
    }

    return 0;
}

#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>

#define TARGET_PROCESS_EXE L"sv6c.exe"
#define TARGET_PROCESS_URL "https://p.eagate.573.jp/game/konasteapp/API/login/login.html?game_id=sdvx"
#define TARGET_PROCESS_NAME "SOUND VOLTEX EXCEED GEAR"

std::filesystem::path get_kshook_path()
{
    auto cwd = std::filesystem::current_path();

    if (std::filesystem::exists(cwd / "kshook.dll"))
        return (cwd / "kshook.dll");

    if (std::filesystem::exists(cwd / "kshook_debug.dll"))
        return (cwd / "kshook_debug.dll");

    return {};
}

int main(int argc, char *argv[])
{
    SetConsoleTitle(L"kshook");

    auto const path = get_kshook_path();

    if (path.empty())
    {
        fprintf(stderr, "ERROR: kshook library not found in '%s'.\n\n",
                std::filesystem::current_path().string().c_str());
        system("pause");
        return EXIT_FAILURE;
    }

    ShellExecuteA(GetConsoleWindow(), "open", TARGET_PROCESS_URL, "", "", 0);

    fprintf(stdout, "Waiting for %s to open...\n", TARGET_PROCESS_NAME);

    auto process = INVALID_HANDLE_VALUE;

    while (process == INVALID_HANDLE_VALUE)
    {
        auto const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

        if (snapshot == INVALID_HANDLE_VALUE)
        {
            fprintf(stderr, "ERROR: Failed to get list of running processes. (%lu)\n\n", GetLastError());
            system("pause");
            return EXIT_FAILURE;
        }

        auto entry = PROCESSENTRY32{.dwSize = sizeof(PROCESSENTRY32)};

        while (Process32Next(snapshot, &entry))
        {
            if (std::wstring_view(entry.szExeFile) != TARGET_PROCESS_EXE)
                continue;

            process = OpenProcess(PROCESS_ALL_ACCESS, false, entry.th32ProcessID);

            if (process == nullptr)
            {
                fprintf(stderr, "ERROR: Failed to open game process. (%lu)\n\n", GetLastError());
                system("pause");
                return EXIT_FAILURE;
            }

            break;
        }

        CloseHandle(snapshot);
        Sleep(1000);
    }

    auto const memory =
        VirtualAllocEx(process, nullptr, path.string().size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    if (memory == nullptr)
    {
        fprintf(stderr, "ERROR: Failed to allocate memory for library path. (%lu)\n\n", GetLastError());
        system("pause");
        return EXIT_FAILURE;
    }

    if (WriteProcessMemory(process, memory, path.string().data(), path.string().size(), nullptr) == 0)
    {
        fprintf(stderr, "ERROR: Failed to write library path to game process. (%lu)\n\n", GetLastError());
        system("pause");
        return EXIT_FAILURE;
    }

    if (CreateRemoteThread(process, nullptr, 0, LPTHREAD_START_ROUTINE(LoadLibraryA), memory, 0, nullptr) == nullptr)
    {
        fprintf(stderr, "ERROR: Failed to create remote thread. (%lu)\n\n", GetLastError());
        system("pause");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

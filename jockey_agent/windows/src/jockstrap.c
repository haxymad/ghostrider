#include <windows.h>
#include <stdio.h>

static const char *USAGE =
    "jockstrap.exe — Jockey VM stager\n"
    "\n"
    "usage:\n"
    "  jockstrap.exe --run <file.jkb> [--vm <path>]\n"
    "  jockstrap.exe --help\n"
    "\n"
    "options:\n"
    "  --run <file>   execute a Jockey bytecode file\n"
    "  --vm <path>    path to jockey_runner.exe (default: same directory)\n"
    "  --help         show this help\n";

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "/?") == 0) {
        fputs(USAGE, stderr);
        return 0;
    }

    if (strcmp(argv[1], "--run") != 0 || argc < 3) {
        fputs(USAGE, stderr);
        return 1;
    }

    const char *bytecode_path = argv[2];
    char exe_dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        fprintf(stderr, "cannot resolve executable path\n");
        return 1;
    }
    char *slash = strrchr(exe_dir, '\\');
    if (slash) *slash = '\0';

    char vm_path[MAX_PATH];
    snprintf(vm_path, MAX_PATH, "%s\\jockey_runner.exe", exe_dir);

    for (int i = 3; i < argc - 1; i++) {
        if (strcmp(argv[i], "--vm") == 0) {
            snprintf(vm_path, MAX_PATH, "%s", argv[++i]);
            break;
        }
    }

    char cmdline[MAX_PATH * 2];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" \"%s\"", vm_path, bytecode_path);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessA(
            NULL, cmdline, NULL, NULL, FALSE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "failed to launch VM: %lu\n", GetLastError());
        return 1;
    }

    DWORD rc = 0;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)rc;
}

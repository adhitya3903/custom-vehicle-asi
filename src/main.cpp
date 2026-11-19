// CustomVehicles - TAHAP 0: tes pipeline build + ASI Loader.
// Plugin ini TIDAK menyentuh memori GTA sama sekali, jadi aman (tidak bikin crash).
// Tugasnya cuma menulis log ke CustomVehicles.log di folder GTA:
//   - bukti plugin ter-load oleh ASI Loader
//   - ukuran gta_sa.exe (untuk cek varian exe)
//   - status samp.dll
//   - deteksi tombol F9 (dipakai nanti untuk tes tukar model)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

static char g_exePath[MAX_PATH] = {0};
static char g_logPath[MAX_PATH] = {0};

static void Log(const char* fmt, ...)
{
    if (!g_logPath[0]) return;

    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "a") != 0 || !f) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fprintf(f, "\n");
    fclose(f);
}

static void InitPaths()
{
    GetModuleFileNameA(NULL, g_exePath, MAX_PATH);

    char dir[MAX_PATH];
    strcpy_s(dir, g_exePath);
    char* slash = strrchr(dir, '\\');
    if (slash) *slash = 0;

    sprintf_s(g_logPath, "%s\\CustomVehicles.log", dir);
}

static unsigned long long GetExeSize()
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(g_exePath, GetFileExInfoStandard, &data)) return 0;
    return ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}

static DWORD WINAPI Worker(LPVOID)
{
    InitPaths();

    Log("=== CustomVehicles (tahap 0) ter-load ===");
    Log("Proses: %s", g_exePath);
    Log("Ukuran exe: %llu byte", GetExeSize());

    DWORD start = GetTickCount();
    bool sampChecked = false;
    bool wasDown = false;

    for (;;)
    {
        Sleep(50);

        // samp.dll biasanya baru ke-load beberapa detik setelah game mulai
        if (!sampChecked && (GetTickCount() - start) > 10000)
        {
            sampChecked = true;
            Log("samp.dll ter-load: %s", GetModuleHandleA("samp.dll") ? "ya" : "tidak");
        }

        // Hanya baca tombol kalau jendela game yang aktif
        DWORD fgPid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
        if (fgPid != GetCurrentProcessId())
        {
            wasDown = false;
            continue;
        }

        bool down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (down && !wasDown)
            Log("F9 ditekan");
        wasDown = down;
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        HANDLE h = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
    return TRUE;
}

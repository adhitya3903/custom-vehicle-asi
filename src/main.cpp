// CustomVehicles - TAHAP 1 (versi diagnostik)
//
//  F10 = 1a (BACA SAJA, aman): catat model kendaraan yang sedang kamu naiki ke log.
//  F11 = 1b (TES TUKAR MODEL): tukar model kendaraan yang kamu naiki ke Banshee (429).
//        Tekan F11 lagi di kendaraan yang sama untuk mengembalikan ke model asli.
//
// (F1, Tab, F4, F5, F7, F8, F9, T/F6, G sudah dipakai SA-MP, jadi dihindari.)
//
// Tombol dibaca oleh thread terpisah (cara ini terbukti jalan di tahap 0).
// Aksi sebenarnya (baca/tukar model) dijalankan di thread GAME lewat event
// Plugin SDK, karena memanggil fungsi game dari thread lain itu berbahaya.
//
// Log "diag" membuktikan apakah event game benar-benar dipanggil di SA-MP.

#include "plugin.h"
#include "game_sa/common.h"
#include "game_sa/CVehicle.h"
#include "game_sa/CStreaming.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

static const int TEST_MODEL = 429;     // Banshee (model bawaan GTA, hanya untuk tes tukar)
static const int KEY_READ   = VK_F10;
static const int KEY_SWAP   = VK_F11;

static char g_logPath[MAX_PATH] = {0};

static void Log(const char* fmt, ...)
{
    if (!g_logPath[0])
    {
        char exe[MAX_PATH] = {0};
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        char* slash = strrchr(exe, '\\');
        if (slash) *slash = 0;
        sprintf_s(g_logPath, "%s\\CustomVehicles.log", exe);
    }

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

// ---------------------------------------------------------------- 1a (baca saja)
static void OnRead()
{
    CVehicle* veh = FindPlayerVehicle(-1, false);
    if (!veh)
    {
        Log("BACA: kamu tidak sedang di dalam kendaraan.");
        return;
    }

    int model = (int)veh->m_nModelIndex;
    Log("BACA: model kendaraan = %d, alamat objek = %p", model, (void*)veh);

    // Diagnostik 0.3.DL: tabel model info asli GTA (alamat 1.0 US).
    if (model >= 0 && model < 20000)
    {
        unsigned int info = *(unsigned int*)(0xA9B0C8 + model * 4);
        Log("BACA: modelInfoPtrs[%d] (tabel asli) = 0x%08X", model, info);
    }
}

// ---------------------------------------------------------------- 1b (tukar model)
static CVehicle* g_swapVeh = nullptr;
static int g_swapOriginal = 0;

static void OnSwap()
{
    CVehicle* veh = FindPlayerVehicle(-1, false);
    if (!veh)
    {
        Log("TUKAR: kamu tidak sedang di dalam kendaraan.");
        return;
    }

    int current = (int)veh->m_nModelIndex;
    if (current < 400 || current > 611)
    {
        Log("TUKAR: model %d bukan kendaraan standar (400-611), dibatalkan.", current);
        return;
    }

    bool restoring = (g_swapVeh == veh && g_swapOriginal != 0);
    int target = restoring ? g_swapOriginal : ((current == TEST_MODEL) ? 411 : TEST_MODEL);

    Log("TUKAR: %s: model %d -> %d", restoring ? "KEMBALIKAN" : "TUKAR", current, target);

    Log("TUKAR: langkah 1/3: minta model %d ke streaming...", target);
    CStreaming::RequestModel(target, 2);
    CStreaming::LoadAllRequestedModels(false);

    Log("TUKAR: langkah 2/3: model %d dimuat, memanggil SetModelIndex...", target);
    veh->SetModelIndex(target);

    Log("TUKAR: langkah 3/3: selesai. m_nModelIndex sekarang = %d", (int)veh->m_nModelIndex);

    if (restoring)
    {
        g_swapVeh = nullptr;
        g_swapOriginal = 0;
    }
    else
    {
        g_swapVeh = veh;
        g_swapOriginal = current;
    }
}

// ---------------------------------------------------------------- jembatan thread tombol -> thread game
static volatile LONG g_pendRead = 0;
static volatile LONG g_pendSwap = 0;
static volatile LONG g_cntProcess = 0;
static volatile LONG g_cntDraw = 0;

static void Pump(volatile LONG* counter, const char* name)
{
    if (InterlockedIncrement(counter) == 1)
        Log("diag: event %s AKTIF (dipanggil pertama kali)", name);

    if (InterlockedExchange(&g_pendRead, 0)) OnRead();
    if (InterlockedExchange(&g_pendSwap, 0)) OnSwap();
}

static void OnGameProcess() { Pump(&g_cntProcess, "gameProcessEvent"); }
static void OnDrawing()     { Pump(&g_cntDraw,    "drawingEvent"); }

// ---------------------------------------------------------------- thread tombol
static DWORD WINAPI KeyThread(LPVOID)
{
    bool readWas = false;
    bool swapWas = false;
    int reports = 0;
    DWORD lastReport = GetTickCount();

    for (;;)
    {
        Sleep(30);

        DWORD now = GetTickCount();
        if (reports < 6 && (now - lastReport) >= 10000)
        {
            lastReport = now;
            reports++;
            Log("diag #%d: gameProcessEvent=%ld drawingEvent=%ld", reports,
                (long)g_cntProcess, (long)g_cntDraw);
        }

        DWORD fgPid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
        if (fgPid != GetCurrentProcessId())
        {
            readWas = swapWas = false;
            continue;
        }

        bool r = (GetAsyncKeyState(KEY_READ) & 0x8000) != 0;
        bool s = (GetAsyncKeyState(KEY_SWAP) & 0x8000) != 0;

        if (r && !readWas)
        {
            Log("tombol BACA (F10) terdeteksi");
            InterlockedExchange(&g_pendRead, 1);
        }
        if (s && !swapWas)
        {
            Log("tombol TUKAR (F11) terdeteksi");
            InterlockedExchange(&g_pendSwap, 1);
        }

        readWas = r;
        swapWas = s;
    }
    return 0;
}

class CustomVehiclesPlugin
{
public:
    CustomVehiclesPlugin()
    {
        Log("=== CustomVehicles (tahap 1 diagnostik: F10 baca, F11 tukar) ter-load ===");
        plugin::Events::gameProcessEvent.before += OnGameProcess;
        plugin::Events::drawingEvent += OnDrawing;

        HANDLE h = CreateThread(NULL, 0, KeyThread, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
} g_customVehiclesPlugin;

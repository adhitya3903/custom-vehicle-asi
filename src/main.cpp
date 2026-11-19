// CustomVehicles - TAHAP 1 (1a + 1b)
//
//  F9  = 1a (BACA SAJA, aman): catat model kendaraan yang sedang kamu naiki ke log.
//  F10 = 1b (TES TUKAR MODEL): tukar model kendaraan yang kamu naiki ke Banshee (429).
//        Tekan F10 lagi di kendaraan yang sama untuk mengembalikan ke model asli.
//
// Setiap langkah ditulis ke CustomVehicles.log SEBELUM dijalankan, jadi kalau
// game crash, baris terakhir di log menunjukkan crash terjadi di langkah mana.

#include "plugin.h"
#include "game_sa/common.h"
#include "game_sa/CVehicle.h"
#include "game_sa/CStreaming.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

static const int TEST_MODEL = 429;   // Banshee (model bawaan GTA, hanya untuk tes tukar)

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
static void OnF9()
{
    CVehicle* veh = FindPlayerVehicle(-1, false);
    if (!veh)
    {
        Log("F9: kamu tidak sedang di dalam kendaraan.");
        return;
    }

    int model = (int)veh->m_nModelIndex;
    Log("F9: model kendaraan = %d, alamat objek = %p", model, (void*)veh);

    // Diagnostik 0.3.DL: tabel model info asli GTA (alamat 1.0 US).
    // Kalau nilainya 0 untuk model yang jelas-jelas ada, berarti tabelnya dipindah.
    if (model >= 0 && model < 20000)
    {
        unsigned int info = *(unsigned int*)(0xA9B0C8 + model * 4);
        Log("F9: modelInfoPtrs[%d] (tabel asli) = 0x%08X", model, info);
    }
}

// ---------------------------------------------------------------- 1b (tukar model)
static CVehicle* g_swapVeh = nullptr;
static int g_swapOriginal = 0;

static void OnF10()
{
    CVehicle* veh = FindPlayerVehicle(-1, false);
    if (!veh)
    {
        Log("F10: kamu tidak sedang di dalam kendaraan.");
        return;
    }

    int current = (int)veh->m_nModelIndex;
    if (current < 400 || current > 611)
    {
        Log("F10: model %d bukan kendaraan standar (400-611), tukar dibatalkan.", current);
        return;
    }

    bool restoring = (g_swapVeh == veh && g_swapOriginal != 0);
    int target = restoring ? g_swapOriginal : ((current == TEST_MODEL) ? 411 : TEST_MODEL);

    Log("F10: %s: model %d -> %d", restoring ? "KEMBALIKAN" : "TUKAR", current, target);

    Log("F10: langkah 1/3: minta model %d ke streaming...", target);
    CStreaming::RequestModel(target, 2);
    CStreaming::LoadAllRequestedModels(false);

    Log("F10: langkah 2/3: model %d dimuat, memanggil SetModelIndex...", target);
    veh->SetModelIndex(target);

    Log("F10: langkah 3/3: selesai. m_nModelIndex sekarang = %d", (int)veh->m_nModelIndex);

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

// ---------------------------------------------------------------- loop game
static void OnGameProcess()
{
    static bool f9Was = false;
    static bool f10Was = false;

    // Hanya baca tombol kalau jendela game yang sedang aktif
    DWORD fgPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
    if (fgPid != GetCurrentProcessId())
    {
        f9Was = f10Was = false;
        return;
    }

    bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    bool f10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;

    if (f9 && !f9Was) OnF9();
    if (f10 && !f10Was) OnF10();

    f9Was = f9;
    f10Was = f10;
}

class CustomVehiclesPlugin
{
public:
    CustomVehiclesPlugin()
    {
        Log("=== CustomVehicles (tahap 1: F9 baca, F10 tukar) ter-load ===");
        plugin::Events::gameProcessEvent.before += OnGameProcess;
    }
} g_customVehiclesPlugin;

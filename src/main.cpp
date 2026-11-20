// CustomVehicles - TAHAP 1, PERCOBAAN 2 (diagnostik rinci)
//
//  F10 = BACA SAJA (aman): catat model kendaraan yang sedang kamu naiki.
//  F11 = TES TUKAR MODEL: tukar model kendaraan yang kamu naiki ke Banshee (429).
//        Tekan F11 lagi di kendaraan yang sama untuk mengembalikan ke model asli.
//
// (F1, Tab, F4, F5, F7, F8, F9, T/F6, G sudah dipakai SA-MP, jadi dihindari.)
//
// Perubahan dari percobaan 1:
//  - Cek status streaming model target; kalau BELUM ter-load, tukar dibatalkan (aman).
//  - Model lama dibuang dulu (DeleteRwObject) sebelum SetModelIndex, meniru cara game
//    me-recreate kendaraan.
//  - Dump pointer "node" bodi kendaraan sebelum/sesudah (untuk membuktikan apakah
//    node jadi kosong setelah tukar, dugaan penyebab crash percobaan 1).
//  - Log 5 frame setelah tukar: kalau muncul "frame ... selesai", game selamat.

#include "plugin.h"
#include "game_sa/common.h"
#include "game_sa/CVehicle.h"
#include "game_sa/CStreaming.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

static const int TEST_MODEL = 429;     // Banshee (model bawaan GTA, hanya untuk tes tukar)
static const int HOTKEY_READ = VK_F10;
static const int HOTKEY_SWAP = VK_F11;

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

// Status streaming model: array CStreamingInfo di 0x8E4CC0 (GTA SA 1.0 US), tiap elemen
// 20 byte, status load di offset 0x10. Nilai 1 = LOADED.
static unsigned int GetLoadState(int modelId)
{
    return *(unsigned char*)(0x8E4CC0 + modelId * 20 + 0x10);
}

// Dump mentah pointer node bodi (m_aCarNodes). Offset 0x648 adalah tebakan dari
// struktur CAutomobile; kalau salah, yang tercatat cuma angka acak (tidak berbahaya).
static void DumpNodes(CVehicle* veh, const char* label)
{
    unsigned int* n = (unsigned int*)((unsigned char*)veh + 0x648);
    Log("%s: node[1..8] = %08X %08X %08X %08X %08X %08X %08X %08X",
        label, n[1], n[2], n[3], n[4], n[5], n[6], n[7], n[8]);
}

// ---------------------------------------------------------------- baca saja
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

    if (model >= 0 && model < 20000)
    {
        unsigned int info = *(unsigned int*)(0xA9B0C8 + model * 4);
        Log("BACA: modelInfoPtrs[%d] (tabel asli) = 0x%08X", model, info);
    }
    if (model >= 400 && model <= 611)
        Log("BACA: status streaming model %d = %u (1 = loaded)", model, GetLoadState(model));

    DumpNodes(veh, "BACA");
}

// ---------------------------------------------------------------- tukar model
static CVehicle* g_swapVeh = nullptr;
static int g_swapOriginal = 0;
static int g_framesToWatch = 0;

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

    Log("TUKAR: %s model %d -> %d", restoring ? "KEMBALIKAN" : "TUKAR", current, target);

    // 1. pastikan model target ter-load. Kalau tidak, batalkan (tidak menyentuh kendaraan).
    Log("TUKAR: [1/6] status model %d sebelum request = %u", target, GetLoadState(target));
    CStreaming::RequestModel(target, 2);
    for (int i = 0; i < 10 && GetLoadState(target) != 1; i++)
        CStreaming::LoadAllRequestedModels(false);

    unsigned int st = GetLoadState(target);
    Log("TUKAR: [2/6] status model %d setelah load = %u (harus 1)", target, st);
    if (st != 1)
    {
        Log("TUKAR: model belum ter-load, DIBATALKAN. Kendaraan tidak disentuh.");
        return;
    }

    // 2. tukar
    DumpNodes(veh, "TUKAR node SEBELUM");

    Log("TUKAR: [3/6] memanggil DeleteRwObject...");
    veh->DeleteRwObject();

    Log("TUKAR: [4/6] DeleteRwObject selesai. Memanggil SetModelIndex(%d)...", target);
    veh->SetModelIndex(target);

    Log("TUKAR: [5/6] SetModelIndex selesai. m_nModelIndex = %d", (int)veh->m_nModelIndex);
    DumpNodes(veh, "TUKAR node SESUDAH");

    Log("TUKAR: [6/6] selesai. Menunggu 5 frame untuk memastikan game selamat...");
    g_framesToWatch = 5;

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

    // pengawas pasca-tukar: dicatat SEBELUM aksi baru, jadi baris ini hanya muncul
    // kalau game sudah melewati frame berikutnya tanpa crash.
    if (g_framesToWatch > 0)
    {
        Log("pasca-tukar: frame selesai tanpa crash (sisa pantau %d)", g_framesToWatch - 1);
        g_framesToWatch--;
    }

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

    for (;;)
    {
        Sleep(30);

        DWORD fgPid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
        if (fgPid != GetCurrentProcessId())
        {
            readWas = swapWas = false;
            continue;
        }

        bool r = (GetAsyncKeyState(HOTKEY_READ) & 0x8000) != 0;
        bool s = (GetAsyncKeyState(HOTKEY_SWAP) & 0x8000) != 0;

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
        Log("=== CustomVehicles (tahap 1, percobaan 2: F10 baca, F11 tukar) ter-load ===");
        plugin::Events::gameProcessEvent.before += OnGameProcess;
        plugin::Events::drawingEvent += OnDrawing;

        HANDLE h = CreateThread(NULL, 0, KeyThread, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
} g_customVehiclesPlugin;

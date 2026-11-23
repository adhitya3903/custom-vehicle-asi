// CustomVehicles - TAHAP B1b (muat model custom dari FILE ke slot donor, tanpa modloader)
//
//  F10 = BACA SAJA (aman): catat model kendaraan yang sedang kamu naiki.
//  F11 = TUKAR MODEL: tukar model kendaraan yang kamu naiki ke model DONOR.
//        ID donor dibaca dari CustomVehicles.ini di folder GTA (lihat README).
//        Tekan F11 lagi di kendaraan yang sama untuk mengembalikan ke model asli.
//  INSERT = MUAT model custom dari file (CustomVehicles\\<File>.dff/.txd) ke slot donor.
//        F11 juga memuatnya otomatis kalau belum dimuat.
//  F12 = TES ISOLASI: jalankan urutan tukar yang SAMA, tapi ke model yang SAMA
//        (misal Infernus -> Infernus). Kalau ini juga crash, masalahnya ada di
//        urutan tukarnya; kalau selamat, masalahnya ada di data khusus model lain.
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
#include "game_sa/CAutomobile.h"
#include "game_sa/CBaseModelInfo.h"
#include "game_sa/CTxdStore.h"
#include "game_sa/CFileLoader.h"
#include "game_sa/CStreaming.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

static const int DEFAULT_DONOR = 503;  // dipakai kalau CustomVehicles.ini tidak ada / tidak valid

// Baca ID donor dari CustomVehicles.ini (folder GTA):
//   [CustomVehicles]
//   Donor=503
static int GetDonorModel()
{
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = 0;

    char ini[MAX_PATH] = {0};
    sprintf_s(ini, "%s\\CustomVehicles.ini", exe);

    int v = (int)GetPrivateProfileIntA("CustomVehicles", "Donor", DEFAULT_DONOR, ini);
    if (v < 400 || v > 611) v = DEFAULT_DONOR;
    return v;
}

static void GetGameDir(char* out, size_t size)
{
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = 0;
    strcpy_s(out, size, exe);
}

// Nama dasar file model custom dari INI: File=infernus  ->  CustomVehicles\infernus.dff/.txd
static void GetCustomFileName(char* out, size_t size)
{
    char dir[MAX_PATH] = {0};
    GetGameDir(dir, sizeof(dir));
    char ini[MAX_PATH] = {0};
    sprintf_s(ini, "%s\\CustomVehicles.ini", dir);
    GetPrivateProfileStringA("CustomVehicles", "File", "", out, (DWORD)size, ini);
}

static bool FileExistsA(const char* path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static const int HOTKEY_READ = VK_F10;
static const int HOTKEY_SWAP = VK_F11;
static const int HOTKEY_SELF = VK_F12;
static const int HOTKEY_LOAD = VK_INSERT;

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

// Data collision model: dari tabel model info asli (0xA9B0C8) -> CBaseModelInfo::pColModel
// (offset 0x14) -> CColModel::m_data (offset 0x2C) berdasarkan struktur CColModelSAInterface
// di kode MTA. "data" = 0 artinya data collision (termasuk suspension lines) BELUM ada.
struct ColInfo
{
    unsigned int mi;
    unsigned int col;
    unsigned int data;
    unsigned int numLines;   // jumlah suspension lines (CColData offset 0x06)
    unsigned int linesPtr;   // pointer array suspension lines (CColData offset 0x10)
};

static ColInfo GetColInfo(int modelId)
{
    ColInfo c = {0, 0, 0, 0, 0};
    if (modelId < 0 || modelId >= 20000) return c;
    c.mi = *(unsigned int*)(0xA9B0C8 + modelId * 4);
    if (c.mi)
    {
        c.col = *(unsigned int*)(c.mi + 0x14);
        if (c.col)
            c.data = *(unsigned int*)(c.col + 0x2C);
    }
    if (c.data)
    {
        c.numLines = *(unsigned char*)(c.data + 0x06);
        c.linesPtr = *(unsigned int*)(c.data + 0x10);
    }
    return c;
}

static void LogColInfo(const char* label, int modelId)
{
    ColInfo c = GetColInfo(modelId);
    Log("%s: model %d: modelInfo=%08X colModel=%08X colData=%08X %s | suspensi: jumlah=%u ptr=%08X %s",
        label, modelId, c.mi, c.col, c.data, c.data ? "(ada)" : "(KOSONG!)",
        c.numLines, c.linesPtr, c.linesPtr ? "(ada)" : "(KOSONG!)");
}

// Dump mentah pointer node bodi (m_aCarNodes). Offset 0x648 adalah tebakan dari
// struktur CAutomobile; kalau salah, yang tercatat cuma angka acak (tidak berbahaya).
static void DumpNodes(CVehicle* veh, const char* label)
{
    unsigned int* n = (unsigned int*)((unsigned char*)veh + 0x648);
    Log("%s: node[1..11]  = %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X",
        label, n[1], n[2], n[3], n[4], n[5], n[6], n[7], n[8], n[9], n[10], n[11]);
    Log("%s: node[12..22] = %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X",
        label, n[12], n[13], n[14], n[15], n[16], n[17], n[18], n[19], n[20], n[21], n[22]);
}

// Dump data handling kendaraan (struktur tHandlingData, 0xE0 byte) ke log, hanya BACA.
// Pointer handling ada di CVehicle offset 0x384 (900 desimal, sesuai struktur CVehicle
// di MTA). Tujuannya mencocokkan isi dump dengan baris kendaraan itu di data\handling.cfg
// supaya offset field suspensi bisa dipastikan sebelum kita pernah MENULIS ke handling.
static void DumpHandling(CVehicle* veh, const char* label)
{
    unsigned int hp = *(unsigned int*)((unsigned char*)veh + 0x384);
    Log("%s: pointer handling = %08X", label, hp);
    if (hp == 0 || IsBadReadPtr((const void*)hp, 0xE0))
    {
        Log("%s: pointer handling tidak valid, dump dilewati.", label);
        return;
    }

    unsigned int* d = (unsigned int*)hp;
    for (int i = 0; i < 0xE0 / 4; i += 4)
    {
        float f0 = *(float*)&d[i + 0];
        float f1 = *(float*)&d[i + 1];
        float f2 = *(float*)&d[i + 2];
        float f3 = *(float*)&d[i + 3];
        Log("%s: +%02X: %08X(%g) %08X(%g) %08X(%g) %08X(%g)", label, i * 4,
            d[i + 0], f0, d[i + 1], f1, d[i + 2], f2, d[i + 3], f3);
    }
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
    LogColInfo("BACA", model);

    DumpNodes(veh, "BACA");
    DumpHandling(veh, "BACA handling");
}

// ---------------------------------------------------------------- B1b: muat model dari file
static unsigned int g_customClump = 0;   // pointer clump custom yang sedang terpasang di donor
static bool g_donorBroken = false;       // true kalau muat gagal di tengah jalan (donor tidak aman dipakai)
static CVehicle* g_swapVeh = nullptr;    // kendaraan yang sedang ditukar (dideklarasi di sini supaya bisa dicek)

// Mengganti isi model donor dengan model dari file, memakai fungsi loader bawaan game:
//   1) pastikan model donor (versi asli) sudah ter-load
//   2) buang clump lama, buang TXD lama
//   3) muat TXD custom ke slot TXD donor, lalu muat DFF custom ke model donor
// force=false: lewati kalau model custom sudah terpasang.
static bool EnsureCustomDonor(int donor, bool force)
{
    if (g_donorBroken)
    {
        Log("MUAT: donor RUSAK dari percobaan sebelumnya, tidak aman dipakai. Restart game.");
        return false;
    }

    char file[64] = {0};
    GetCustomFileName(file, sizeof(file));
    if (!file[0])
    {
        Log("MUAT: INI File= kosong, memakai model donor apa adanya (modloader / bawaan).");
        return true;
    }

    char dir[MAX_PATH] = {0};
    GetGameDir(dir, sizeof(dir));

    char dffRel[MAX_PATH], txdRel[MAX_PATH], dffFull[MAX_PATH], txdFull[MAX_PATH];
    sprintf_s(dffRel, "CustomVehicles\\%s.dff", file);
    sprintf_s(txdRel, "CustomVehicles\\%s.txd", file);
    sprintf_s(dffFull, "%s\\%s", dir, dffRel);
    sprintf_s(txdFull, "%s\\%s", dir, txdRel);

    Log("MUAT: [1/8] cek file: %s dan %s", dffFull, txdFull);
    if (!FileExistsA(dffFull) || !FileExistsA(txdFull))
    {
        Log("MUAT: file tidak ditemukan. Buat folder CustomVehicles di folder GTA dan taruh %s.dff / %s.txd di sana.", file, file);
        return false;
    }

    Log("MUAT: [2/8] pastikan model donor %d (versi asli) ter-load...", donor);
    CStreaming::RequestModel(donor, 2);
    for (int i = 0; i < 10 && GetLoadState(donor) != 1; i++)
        CStreaming::LoadAllRequestedModels(false);
    if (GetLoadState(donor) != 1)
    {
        Log("MUAT: model donor tidak mau ter-load, dibatalkan (belum ada yang diubah).");
        return false;
    }

    unsigned int miPtr = *(unsigned int*)(0xA9B0C8 + donor * 4);
    if (!miPtr)
    {
        Log("MUAT: modelInfo donor kosong, dibatalkan (belum ada yang diubah).");
        return false;
    }
    CBaseModelInfo* mi = (CBaseModelInfo*)miPtr;
    short txdSlot = *(short*)(miPtr + 0x0A);       // CBaseModelInfo::m_nTxdIndex
    unsigned int rwObj = *(unsigned int*)(miPtr + 0x1C);   // CBaseModelInfo::m_pRwObject

    Log("MUAT: modelInfo=%08X txdSlot=%d clumpSekarang=%08X clumpCustom=%08X", miPtr, (int)txdSlot, rwObj, g_customClump);
    if (!force && g_customClump != 0 && rwObj == g_customClump)
    {
        Log("MUAT: model custom sudah terpasang, dilewati.");
        return true;
    }

    // mulai dari sini kondisi donor berubah. Kalau gagal, donor ditandai rusak.
    Log("MUAT: [3/8] DeleteRwObject (buang clump lama)...");
    mi->DeleteRwObject();

    Log("MUAT: [4/8] RemoveTxd(%d) (buang TXD lama)...", (int)txdSlot);
    CTxdStore::RemoveTxd(txdSlot);

    Log("MUAT: [5/8] LoadTxd(%d, %s)...", (int)txdSlot, txdRel);
    bool okTxd = CTxdStore::LoadTxd(txdSlot, txdRel);
    Log("MUAT: LoadTxd hasil = %d", okTxd ? 1 : 0);
    if (!okTxd)
    {
        g_donorBroken = true;
        Log("MUAT: GAGAL memuat TXD. Donor ditandai rusak (jangan dipakai, restart game).");
        return false;
    }

    // Selaraskan status streaming TXD slot ini jadi "LOADED" (id TXD = 20000 + slot), karena
    // kita sudah memuatnya sendiri. Kalau tidak, game bisa memuat ulang TXD asli dari IMG.
    {
        unsigned char* st = (unsigned char*)(0x8E4CC0 + (20000 + (int)txdSlot) * 20 + 0x10);
        Log("MUAT: status streaming TXD %d sebelum = %u", 20000 + (int)txdSlot, (unsigned)*st);
        *st = 1;
    }

    Log("MUAT: [6/8] LoadClumpFile(%s, %d)...", dffRel, donor);
    // Di Plugin SDK, LoadClumpFile hanya menerima stream. Stream dibuka sendiri; TIDAK kita
    // tutup dari sini (kalau game sudah menutupnya, menutup dua kali akan crash).
    RwStream* dffStream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, dffRel);
    Log("MUAT: stream DFF = %p", (void*)dffStream);
    if (!dffStream)
    {
        g_donorBroken = true;
        Log("MUAT: GAGAL membuka stream DFF. Donor ditandai rusak (restart game).");
        return false;
    }

    CTxdStore::PushCurrentTxd();
    CTxdStore::SetCurrentTxd(txdSlot);
    bool okDff = CFileLoader::LoadClumpFile(dffStream, (unsigned int)donor);
    CTxdStore::PopCurrentTxd();
    Log("MUAT: LoadClumpFile hasil = %d", okDff ? 1 : 0);

    rwObj = *(unsigned int*)(miPtr + 0x1C);
    Log("MUAT: [7/8] LoadClumpFile selesai. clump baru = %08X", rwObj);
    if (rwObj == 0)
    {
        g_donorBroken = true;
        Log("MUAT: GAGAL memuat DFF (clump kosong). Donor ditandai rusak (restart game).");
        return false;
    }

    CTxdStore::AddRef(txdSlot);   // menyeimbangkan RemoveRef yang dilakukan game saat model dibuang
    g_customClump = rwObj;
    Log("MUAT: [8/8] BERHASIL. Model custom terpasang di donor %d.", donor);
    return true;
}

static void OnLoadCustom()
{
    int donor = GetDonorModel();
    if (g_swapVeh != nullptr)
    {
        Log("MUAT: ada kendaraan yang sedang ditukar. Kembalikan dulu dengan F11, lalu tekan INSERT lagi.");
        return;
    }
    Log("MUAT: INSERT ditekan, donor = %d", donor);
    EnsureCustomDonor(donor, true);
}

// ---------------------------------------------------------------- tukar model
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
    int donor = GetDonorModel();
    int target = restoring ? g_swapOriginal : ((current == donor) ? 411 : donor);
    if (!restoring) Log("TUKAR: donor dari INI = %d", donor);

    // B1b: pastikan model custom dari file sudah terpasang di donor (hanya kalau mau menukar KE donor)
    if (!restoring && target == donor)
    {
        if (!EnsureCustomDonor(donor, false))
        {
            Log("TUKAR: model custom donor belum siap, tukar DIBATALKAN. Kendaraan tidak disentuh.");
            return;
        }
    }

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

    // 2. cek data collision (dugaan penyebab crash: colData model target kosong)
    LogColInfo("TUKAR asal  ", current);
    LogColInfo("TUKAR target", target);
    if (GetColInfo(target).data == 0)
    {
        Log("TUKAR: colData model target KOSONG. DIBATALKAN supaya game tidak crash.");
        return;
    }

    // 3. tukar
    DumpNodes(veh, "TUKAR node SEBELUM");

    Log("TUKAR: [3/6] memanggil DeleteRwObject...");
    veh->DeleteRwObject();

    Log("TUKAR: [4/6] DeleteRwObject selesai. Memanggil SetModelIndex(%d)...", target);
    veh->SetModelIndex(target);

    Log("TUKAR: [5/7] SetModelIndex selesai. m_nModelIndex = %d", (int)veh->m_nModelIndex);

    // Dugaan penyebab crash: suspension lines belum disiapkan untuk model baru.
    // Game memanggil ini sendiri saat membuat mobil baru; kita meniru urutan itu.
    Log("TUKAR: [6/7] memanggil SetupSuspensionLines...");
    static_cast<CAutomobile*>(veh)->SetupSuspensionLines();
    Log("TUKAR: SetupSuspensionLines selesai");
    LogColInfo("TUKAR target SESUDAH", target);
    DumpNodes(veh, "TUKAR node SESUDAH");

    Log("TUKAR: [7/7] selesai. Menunggu 5 frame untuk memastikan game selamat...");
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

// ---------------------------------------------------------------- tes isolasi (model yang sama)
static void OnSelfSwap()
{
    CVehicle* veh = FindPlayerVehicle(-1, false);
    if (!veh)
    {
        Log("ISOLASI: kamu tidak sedang di dalam kendaraan.");
        return;
    }

    int current = (int)veh->m_nModelIndex;
    if (current < 400 || current > 611)
    {
        Log("ISOLASI: model %d bukan kendaraan standar (400-611), dibatalkan.", current);
        return;
    }

    Log("ISOLASI: tukar ke model YANG SAMA (%d) dengan urutan yang sama", current);
    DumpNodes(veh, "ISOLASI node SEBELUM");

    Log("ISOLASI: [1/3] DeleteRwObject...");
    veh->DeleteRwObject();

    Log("ISOLASI: [2/3] SetModelIndex(%d)...", current);
    veh->SetModelIndex(current);

    DumpNodes(veh, "ISOLASI node SESUDAH");
    Log("ISOLASI: [3/3] selesai. Menunggu 5 frame untuk memastikan game selamat...");
    g_framesToWatch = 5;
}

// ---------------------------------------------------------------- jembatan thread tombol -> thread game
static volatile LONG g_pendRead = 0;
static volatile LONG g_pendSwap = 0;
static volatile LONG g_pendSelf = 0;
static volatile LONG g_pendLoad = 0;
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
    if (InterlockedExchange(&g_pendSelf, 0)) OnSelfSwap();
    if (InterlockedExchange(&g_pendLoad, 0)) OnLoadCustom();
}

static void OnGameProcess() { Pump(&g_cntProcess, "gameProcessEvent"); }
static void OnDrawing()     { Pump(&g_cntDraw,    "drawingEvent"); }

// ---------------------------------------------------------------- thread tombol
static DWORD WINAPI KeyThread(LPVOID)
{
    bool readWas = false;
    bool swapWas = false;
    bool selfWas = false;
    bool loadWas = false;

    for (;;)
    {
        Sleep(30);

        DWORD fgPid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
        if (fgPid != GetCurrentProcessId())
        {
            readWas = swapWas = selfWas = loadWas = false;
            continue;
        }

        bool r = (GetAsyncKeyState(HOTKEY_READ) & 0x8000) != 0;
        bool s = (GetAsyncKeyState(HOTKEY_SWAP) & 0x8000) != 0;
        bool q = (GetAsyncKeyState(HOTKEY_SELF) & 0x8000) != 0;
        bool l = (GetAsyncKeyState(HOTKEY_LOAD) & 0x8000) != 0;

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

        if (q && !selfWas)
        {
            Log("tombol ISOLASI (F12) terdeteksi");
            InterlockedExchange(&g_pendSelf, 1);
        }

        if (l && !loadWas)
        {
            Log("tombol MUAT (INSERT) terdeteksi");
            InterlockedExchange(&g_pendLoad, 1);
        }

        readWas = r;
        swapWas = s;
        selfWas = q;
        loadWas = l;
    }
    return 0;
}

class CustomVehiclesPlugin
{
public:
    CustomVehiclesPlugin()
    {
        Log("=== CustomVehicles (tahap 1, B1b: INSERT muat dari file, F10 baca, F11 tukar ke donor, F12 isolasi) ter-load ===");
        plugin::Events::gameProcessEvent.before += OnGameProcess;
        plugin::Events::drawingEvent += OnDrawing;

        HANDLE h = CreateThread(NULL, 0, KeyThread, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
} g_customVehiclesPlugin;

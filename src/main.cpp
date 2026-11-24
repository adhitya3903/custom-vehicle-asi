// CustomVehicles - TAHAP B1c (kolam donor: banyak model custom, dimuat dari FILE, tanpa modloader)
//
//  INSERT = MUAT semua model di daftar Models= ke kolam donor (diagnostik / preload).
//  F10    = BACA SAJA (aman): catat model kendaraan yang sedang kamu naiki + data handling.
//  F11    = TUKAR: ganti kendaraan yang kamu naiki ke model custom berikutnya di daftar.
//           Tekan lagi untuk model custom berikutnya; setelah model terakhir, kembali ke model asli.
//  F12    = TES ISOLASI: tukar ke model yang SAMA (untuk membedakan masalah urutan vs masalah data).
//
// Konfigurasi di CustomVehicles.ini (folder GTA):
//   Donors=503,502,494,504     <- slot kendaraan yang TIDAK dipakai servermu (400-611)
//   Models=infernus,r32        <- file CustomVehicles\<nama>.dff dan .txd
//
// (F1, Tab, F4, F5, F7, F8, F9, T/F6, G sudah dipakai SA-MP, jadi dihindari.)
//
// Batasan versi ini: model yang sudah dimuat ke sebuah donor tidak dibuang lagi selama game
// berjalan (belum ada "eviction"). Jumlah model berbeda yang bisa dipakai = jumlah donor.

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
#include <cstdlib>

static void GetGameDir(char* out, size_t size)
{
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = 0;
    strcpy_s(out, size, exe);
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

// ---------------------------------------------------------------- konfigurasi (INI)
static const int MAX_DONORS = 16;
static const int MAX_MODELS = 32;

struct DonorSlot
{
    int id;                 // ID model kendaraan yang dipakai sebagai wadah (400-611)
    int modelIdx;           // indeks di g_models yang sedang dimuat di slot ini, -1 = kosong
    unsigned int clump;     // pointer clump custom yang terpasang (untuk mendeteksi kalau dibuang game)
    bool broken;            // true kalau muat gagal di tengah jalan (slot tidak aman)
};

static DonorSlot g_donors[MAX_DONORS];
static int g_donorCount = 0;
static char g_models[MAX_MODELS][64];
static int g_modelCount = 0;
static bool g_configLoaded = false;

static void ReadIni(const char* key, char* out, size_t size)
{
    char dir[MAX_PATH] = {0};
    GetGameDir(dir, sizeof(dir));
    char ini[MAX_PATH] = {0};
    sprintf_s(ini, "%s\\CustomVehicles.ini", dir);
    GetPrivateProfileStringA("CustomVehicles", key, "", out, (DWORD)size, ini);
}

static void ParseCsv(const char* src, char out[][64], int maxItems, int& count)
{
    char buf[512] = {0};
    strncpy_s(buf, sizeof(buf), src, _TRUNCATE);
    char* ctx = nullptr;
    for (char* tok = strtok_s(buf, ",", &ctx); tok && count < maxItems; tok = strtok_s(nullptr, ",", &ctx))
    {
        while (*tok == ' ' || *tok == '\t') tok++;
        char* end = tok + strlen(tok);
        while (end > tok && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
        if (*tok)
        {
            strncpy_s(out[count], 64, tok, _TRUNCATE);
            count++;
        }
    }
}

static void EnsureConfig()
{
    if (g_configLoaded) return;
    g_configLoaded = true;

    char buf[512] = {0};

    // donor: Donors=503,502 (atau format lama Donor=503)
    ReadIni("Donors", buf, sizeof(buf));
    if (!buf[0])
    {
        char one[32] = {0};
        ReadIni("Donor", one, sizeof(one));
        if (one[0]) strncpy_s(buf, sizeof(buf), one, _TRUNCATE);
        else strcpy_s(buf, "503");
    }
    char tmp[MAX_DONORS][64];
    int n = 0;
    ParseCsv(buf, tmp, MAX_DONORS, n);
    for (int i = 0; i < n; i++)
    {
        int id = atoi(tmp[i]);
        if (id < 400 || id > 611) { Log("KONFIG: donor '%s' tidak valid (harus 400-611), dilewati.", tmp[i]); continue; }
        bool dup = false;
        for (int j = 0; j < g_donorCount; j++) if (g_donors[j].id == id) dup = true;
        if (dup) continue;
        g_donors[g_donorCount].id = id;
        g_donors[g_donorCount].modelIdx = -1;
        g_donors[g_donorCount].clump = 0;
        g_donors[g_donorCount].broken = false;
        g_donorCount++;
    }

    // model: Models=infernus,r32 (atau format lama File=infernus)
    memset(buf, 0, sizeof(buf));
    ReadIni("Models", buf, sizeof(buf));
    if (!buf[0]) ReadIni("File", buf, sizeof(buf));
    ParseCsv(buf, g_models, MAX_MODELS, g_modelCount);

    Log("KONFIG: %d donor, %d model custom.", g_donorCount, g_modelCount);
    for (int i = 0; i < g_donorCount; i++) Log("KONFIG: donor[%d] = model %d", i, g_donors[i].id);
    for (int i = 0; i < g_modelCount; i++) Log("KONFIG: model[%d] = %s", i, g_models[i]);
}

// ---------------------------------------------------------------- muat model dari file ke sebuah donor
static bool EnsureModelLoaded(int modelId)
{
    CStreaming::RequestModel(modelId, 2);
    for (int i = 0; i < 10 && GetLoadState(modelId) != 1; i++)
        CStreaming::LoadAllRequestedModels(false);
    return GetLoadState(modelId) == 1;
}

// Mengganti isi model donor dengan model dari file memakai loader bawaan game.
// Kalau gagal SEBELUM ada yang diubah: return false, slot tetap aman.
// Kalau gagal SETELAH ada yang diubah: slot ditandai rusak.
static bool LoadModelIntoDonor(DonorSlot& slot, int modelIdx)
{
    const char* file = g_models[modelIdx];
    int donor = slot.id;

    char dir[MAX_PATH] = {0};
    GetGameDir(dir, sizeof(dir));

    char dffRel[MAX_PATH], txdRel[MAX_PATH], dffFull[MAX_PATH], txdFull[MAX_PATH];
    sprintf_s(dffRel, "CustomVehicles\\%s.dff", file);
    sprintf_s(txdRel, "CustomVehicles\\%s.txd", file);
    sprintf_s(dffFull, "%s\\%s", dir, dffRel);
    sprintf_s(txdFull, "%s\\%s", dir, txdRel);

    Log("MUAT[%s -> donor %d]: [1/8] cek file...", file, donor);
    if (!FileExistsA(dffFull) || !FileExistsA(txdFull))
    {
        Log("MUAT: file tidak ditemukan: %s / %s", dffFull, txdFull);
        return false;
    }

    Log("MUAT: [2/8] pastikan model donor %d (versi asli) ter-load...", donor);
    if (!EnsureModelLoaded(donor))
    {
        Log("MUAT: model donor %d tidak mau ter-load, dibatalkan (belum ada yang diubah).", donor);
        return false;
    }

    unsigned int miPtr = *(unsigned int*)(0xA9B0C8 + donor * 4);
    if (!miPtr)
    {
        Log("MUAT: modelInfo donor %d kosong, dibatalkan (belum ada yang diubah).", donor);
        return false;
    }
    CBaseModelInfo* mi = (CBaseModelInfo*)miPtr;
    short txdSlot = *(short*)(miPtr + 0x0A);               // CBaseModelInfo::m_nTxdIndex
    unsigned int rwObj = *(unsigned int*)(miPtr + 0x1C);   // CBaseModelInfo::m_pRwObject
    Log("MUAT: modelInfo=%08X txdSlot=%d clumpSekarang=%08X", miPtr, (int)txdSlot, rwObj);

    // ---- mulai dari sini kondisi donor berubah
    Log("MUAT: [3/8] DeleteRwObject (buang clump lama)...");
    mi->DeleteRwObject();

    Log("MUAT: [4/8] RemoveTxd(%d) (buang TXD lama)...", (int)txdSlot);
    CTxdStore::RemoveTxd(txdSlot);

    Log("MUAT: [5/8] LoadTxd(%d, %s)...", (int)txdSlot, txdRel);
    bool okTxd = CTxdStore::LoadTxd(txdSlot, txdRel);
    Log("MUAT: LoadTxd hasil = %d", okTxd ? 1 : 0);
    if (!okTxd)
    {
        slot.broken = true;
        Log("MUAT: GAGAL memuat TXD. Donor %d ditandai rusak.", donor);
        return false;
    }

    // Selaraskan status streaming TXD slot ini jadi "LOADED" (id TXD = 20000 + slot).
    {
        unsigned char* st = (unsigned char*)(0x8E4CC0 + (20000 + (int)txdSlot) * 20 + 0x10);
        Log("MUAT: status streaming TXD %d sebelum = %u", 20000 + (int)txdSlot, (unsigned)*st);
        *st = 1;
    }

    Log("MUAT: [6/8] LoadClumpFile(%s, %d)...", dffRel, donor);
    // Stream dibuka sendiri dan TIDAK ditutup dari sini (kalau game sudah menutupnya,
    // menutup dua kali akan crash).
    RwStream* dffStream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, dffRel);
    Log("MUAT: stream DFF = %p", (void*)dffStream);
    if (!dffStream)
    {
        slot.broken = true;
        Log("MUAT: GAGAL membuka stream DFF. Donor %d ditandai rusak.", donor);
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
        slot.broken = true;
        Log("MUAT: GAGAL memuat DFF (clump kosong). Donor %d ditandai rusak.", donor);
        return false;
    }

    CTxdStore::AddRef(txdSlot);   // menyeimbangkan RemoveRef yang dilakukan game saat model dibuang
    slot.clump = rwObj;
    slot.modelIdx = modelIdx;
    Log("MUAT: [8/8] BERHASIL. Model '%s' terpasang di donor %d.", file, donor);
    return true;
}

// Cari (atau siapkan) donor yang memegang model custom ke-modelIdx. Return indeks donor, -1 kalau gagal.
static int AcquireDonorFor(int modelIdx)
{
    // 1) sudah ada donor yang memegang model ini?
    for (int i = 0; i < g_donorCount; i++)
    {
        DonorSlot& s = g_donors[i];
        if (s.broken || s.modelIdx != modelIdx) continue;

        unsigned int miPtr = *(unsigned int*)(0xA9B0C8 + s.id * 4);
        unsigned int rwObj = miPtr ? *(unsigned int*)(miPtr + 0x1C) : 0;
        if (rwObj != 0 && rwObj == s.clump && GetLoadState(s.id) == 1)
        {
            Log("DONOR: model '%s' sudah ada di donor %d.", g_models[modelIdx], s.id);
            return i;
        }
        // game membuang / memuat ulang model donor ini -> muat ulang file kita
        Log("DONOR: donor %d berubah (dibuang/dimuat ulang oleh game), memuat ulang '%s'.", s.id, g_models[modelIdx]);
        if (LoadModelIntoDonor(s, modelIdx)) return i;
        return -1;
    }

    // 2) pakai donor kosong
    for (int i = 0; i < g_donorCount; i++)
    {
        DonorSlot& s = g_donors[i];
        if (s.broken || s.modelIdx != -1) continue;
        Log("DONOR: memakai donor kosong %d untuk '%s'.", s.id, g_models[modelIdx]);
        if (LoadModelIntoDonor(s, modelIdx)) return i;
        // gagal: kalau slot tidak rusak, coba donor kosong berikutnya
        if (s.broken) continue;
        return -1;
    }

    Log("DONOR: kolam donor PENUH atau rusak semua. Tambahkan ID di Donors= pada INI.");
    return -1;
}

static void LogPool()
{
    for (int i = 0; i < g_donorCount; i++)
    {
        DonorSlot& s = g_donors[i];
        if (s.broken) Log("KOLAM: donor %d = RUSAK", s.id);
        else if (s.modelIdx < 0) Log("KOLAM: donor %d = kosong", s.id);
        else Log("KOLAM: donor %d = '%s'", s.id, g_models[s.modelIdx]);
    }
}

static void OnLoadCustom()
{
    EnsureConfig();
    Log("MUAT: INSERT ditekan: memuat semua model ke kolam donor.");
    for (int m = 0; m < g_modelCount; m++)
        AcquireDonorFor(m);
    LogPool();
}

// ---------------------------------------------------------------- tukar model
struct SwapEntry
{
    CVehicle* veh;
    int original;    // model asli kendaraan sebelum ditukar
    int donorId;     // ID donor yang sedang dipakai
    int modelIdx;    // model custom yang sedang dipakai
};
static SwapEntry g_swaps[16];
static int g_swapCount = 0;
static int g_framesToWatch = 0;

// Cari entri untuk kendaraan ini. Kalau entri ada tapi modelnya tidak cocok lagi (kendaraan lama
// sudah dihancurkan dan alamatnya dipakai ulang), entri dianggap basi dan dibuang.
static int FindSwap(CVehicle* veh)
{
    for (int i = 0; i < g_swapCount; i++)
    {
        if (g_swaps[i].veh != veh) continue;
        if ((int)veh->m_nModelIndex == g_swaps[i].donorId) return i;

        Log("TUKAR: entri basi untuk kendaraan %p dibuang.", (void*)veh);
        g_swaps[i] = g_swaps[g_swapCount - 1];
        g_swapCount--;
        return -1;
    }
    return -1;
}

static void RemoveSwapAt(int i)
{
    g_swaps[i] = g_swaps[g_swapCount - 1];
    g_swapCount--;
}

// Urutan tukar yang sudah terbukti: DeleteRwObject -> SetModelIndex -> SetupSuspensionLines.
static bool PerformSwap(CVehicle* veh, int target)
{
    Log("SWAP: [1/5] pastikan model %d ter-load (status sebelum = %u)...", target, GetLoadState(target));
    if (!EnsureModelLoaded(target))
    {
        Log("SWAP: model %d tidak ter-load, DIBATALKAN. Kendaraan tidak disentuh.", target);
        return false;
    }

    LogColInfo("SWAP target", target);
    if (GetColInfo(target).data == 0)
    {
        Log("SWAP: colData model target KOSONG, DIBATALKAN.");
        return false;
    }

    DumpNodes(veh, "SWAP node SEBELUM");

    Log("SWAP: [2/5] DeleteRwObject...");
    veh->DeleteRwObject();

    Log("SWAP: [3/5] SetModelIndex(%d)...", target);
    veh->SetModelIndex(target);

    Log("SWAP: [4/5] SetupSuspensionLines...");
    static_cast<CAutomobile*>(veh)->SetupSuspensionLines();

    LogColInfo("SWAP target SESUDAH", target);
    DumpNodes(veh, "SWAP node SESUDAH");
    Log("SWAP: [5/5] selesai. m_nModelIndex = %d. Menunggu 5 frame...", (int)veh->m_nModelIndex);
    g_framesToWatch = 5;
    return true;
}

static void OnSwap()
{
    EnsureConfig();

    CVehicle* veh = FindPlayerVehicle(-1, false);
    if (!veh)
    {
        Log("TUKAR: kamu tidak sedang di dalam kendaraan.");
        return;
    }
    if (g_modelCount == 0)
    {
        Log("TUKAR: daftar Models= di INI kosong. Isi dulu, contoh: Models=infernus");
        return;
    }
    if (g_donorCount == 0)
    {
        Log("TUKAR: daftar Donors= di INI kosong / tidak valid.");
        return;
    }

    int current = (int)veh->m_nModelIndex;
    if (current < 400 || current > 611)
    {
        Log("TUKAR: model %d bukan kendaraan standar (400-611), dibatalkan.", current);
        return;
    }

    int e = FindSwap(veh);
    int original = (e >= 0) ? g_swaps[e].original : current;
    int nextIdx = (e >= 0) ? g_swaps[e].modelIdx + 1 : 0;

    if (nextIdx >= g_modelCount)
    {
        // putaran habis -> kembalikan ke model asli
        Log("TUKAR: KEMBALIKAN ke model asli %d", original);
        if (PerformSwap(veh, original) && e >= 0)
            RemoveSwapAt(e);
        return;
    }

    Log("TUKAR: model custom #%d '%s' (model asli kendaraan = %d)", nextIdx, g_models[nextIdx], original);
    int slotIdx = AcquireDonorFor(nextIdx);
    if (slotIdx < 0)
    {
        Log("TUKAR: model custom belum siap, tukar DIBATALKAN. Kendaraan tidak disentuh.");
        return;
    }
    int target = g_donors[slotIdx].id;

    if (!PerformSwap(veh, target))
        return;

    if (e >= 0)
    {
        g_swaps[e].donorId = target;
        g_swaps[e].modelIdx = nextIdx;
    }
    else if (g_swapCount < 16)
    {
        g_swaps[g_swapCount].veh = veh;
        g_swaps[g_swapCount].original = original;
        g_swaps[g_swapCount].donorId = target;
        g_swaps[g_swapCount].modelIdx = nextIdx;
        g_swapCount++;
    }
    else
    {
        Log("TUKAR: catatan penukaran penuh (16), kendaraan ini tidak bisa dikembalikan otomatis.");
    }
    LogPool();
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
        Log("=== CustomVehicles (B1c: kolam donor; INSERT muat, F10 baca, F11 tukar/siklus, F12 isolasi) ter-load ===");
        plugin::Events::gameProcessEvent.before += OnGameProcess;
        plugin::Events::drawingEvent += OnDrawing;

        HANDLE h = CreateThread(NULL, 0, KeyThread, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
} g_customVehiclesPlugin;

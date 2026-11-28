// CustomVehicles - TAHAP B2 (kelas donor + unduh model dari server ke cache bersama)
//
//  INSERT = MUAT semua model yang sudah siap ke kolam donor (diagnostik / preload).
//  F10    = BACA SAJA (aman): catat model kendaraan yang sedang kamu naiki + data handling.
//  F11    = TUKAR: ganti kendaraan yang kamu naiki ke model custom berikutnya di daftar.
//           Tekan lagi untuk model berikutnya; setelah model terakhir, kembali ke model asli.
//  F12    = TES ISOLASI: tukar ke model yang SAMA (untuk membedakan masalah urutan vs masalah data).
//
// Konfigurasi di CustomVehicles.ini (folder GTA). Dua mode:
//   MODE LOKAL (tanpa BaseUrl): model dibaca dari folder GTA\CustomVehicles\<nama>.dff/.txd
//   MODE UNDUH (BaseUrl diisi): plugin mengunduh BaseUrl/manifest.txt, lalu file model yang
//     belum ada di cache. Cache bersama (satu tempat untuk semua server), nama file berbentuk
//     0x<CRC32>.dff / .txd, default: Dokumen\GTA San Andreas User Files\SAMP\cache\CustomVehicles
//
// Donor punya kelas (Donors=503:sport,504:sedan). Model custom hanya dipasang di donor yang
// sekelas, supaya hitbox cocok. Kelas kosong = cocok dengan semua.
//
// (F1, Tab, F4, F5, F7, F8, F9, T/F6, G sudah dipakai SA-MP, jadi dihindari.)
//
// Batasan versi ini: model yang sudah dimuat ke sebuah donor tidak dibuang lagi selama game
// berjalan (belum ada "eviction"). Jumlah model berbeda yang bisa dipakai = jumlah donor.
// Verifikasi unduhan memakai CRC32 (cek kerusakan file, bukan perlindungan dari pemalsuan).

// Mode unduh dari server (WinHTTP + cache bersama).
//   0 = NONAKTIF (default): bagian unduh tidak ikut dikompilasi. Hanya mode lokal.
//   1 = AKTIF: ubah angka ini jadi 1 kalau mau mencoba mode unduh (belum teruji).
#define CV_ENABLE_DOWNLOAD 0

#include "plugin.h"
#include "game_sa/common.h"
#include "game_sa/CVehicle.h"
#include "game_sa/CAutomobile.h"
#include "game_sa/CBaseModelInfo.h"
#include "game_sa/CTxdStore.h"
#include "game_sa/CFileLoader.h"
#include "game_sa/CStreaming.h"

#include <windows.h>
#if CV_ENABLE_DOWNLOAD
#include <winhttp.h>
#include <shlobj.h>
#endif
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <vector>

#if CV_ENABLE_DOWNLOAD
#pragma comment(lib, "winhttp.lib")
#endif

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
    char cls[16];           // kelas donor (sport, sedan, ...), kosong = cocok dengan semua
    int modelIdx;           // indeks di g_models yang sedang dimuat di slot ini, -1 = kosong
    unsigned int clump;     // pointer clump custom yang terpasang (untuk mendeteksi kalau dibuang game)
    bool broken;            // true kalau muat gagal di tengah jalan (slot tidak aman)
};

struct ModelDef
{
    char name[64];
    char cls[16];           // kelas model, kosong = boleh di donor mana saja
    char dffPath[MAX_PATH]; // relatif ke folder GTA (mode lokal) atau absolut (cache unduhan)
    char txdPath[MAX_PATH];
};

static DonorSlot g_donors[MAX_DONORS];
static int g_donorCount = 0;
static ModelDef g_models[MAX_MODELS];
static volatile LONG g_modelCount = 0;     // hanya model yang SUDAH SIAP yang dihitung
static volatile LONG g_configReady = 0;
static char g_baseUrl[512] = {0};
static char g_cacheDir[MAX_PATH] = {0};

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

// "503:sport" -> tok jadi "503", cls = "sport"
static void SplitClass(char* tok, char* cls, size_t clsSize)
{
    cls[0] = 0;
    char* colon = strchr(tok, ':');
    if (colon)
    {
        *colon = 0;
        strncpy_s(cls, clsSize, colon + 1, _TRUNCATE);
        _strlwr_s(cls, clsSize);
    }
}

static bool ClassMatches(const char* a, const char* b)
{
    if (!a[0] || !b[0]) return true;
    return _stricmp(a, b) == 0;
}

#if CV_ENABLE_DOWNLOAD
// ---------------------------------------------------------------- unduh (WinHTTP) + CRC32 + cache
static unsigned int Crc32(const unsigned char* d, size_t n)
{
    static unsigned int table[256];
    static bool init = false;
    if (!init)
    {
        for (unsigned int i = 0; i < 256; i++)
        {
            unsigned int c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    unsigned int c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static bool ReadFileAll(const char* path, std::vector<unsigned char>& out)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return false; }
    out.resize((size_t)size);
    size_t got = size ? fread(out.data(), 1, (size_t)size, f) : 0;
    fclose(f);
    return got == (size_t)size;
}

static bool WriteFileAll(const char* path, const std::vector<unsigned char>& data)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) return false;
    size_t put = data.empty() ? 0 : fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return put == data.size();
}

// GET sederhana lewat WinHTTP. Dipanggil HANYA dari thread kerja (tidak boleh dari thread game).
static bool HttpGet(const char* url, std::vector<unsigned char>& out)
{
    wchar_t wurl[2048];
    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 2048)) return false;

    wchar_t host[256] = {0};
    wchar_t path[1792] = {0};
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 1792;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) { Log("HTTP: URL tidak valid: %s", url); return false; }

    HINTERNET session = WinHttpOpen(L"CustomVehicles/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 10000, 10000, 30000, 60000);

    bool ok = false;
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (conn)
    {
        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (req)
        {
            if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, NULL))
            {
                DWORD status = 0, size = sizeof(status);
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
                if (status == 200)
                {
                    ok = true;
                    for (;;)
                    {
                        DWORD avail = 0;
                        if (!WinHttpQueryDataAvailable(req, &avail)) { ok = false; break; }
                        if (avail == 0) break;
                        size_t old = out.size();
                        if (old + avail > 64u * 1024u * 1024u) { Log("HTTP: file terlalu besar, dibatalkan."); ok = false; break; }
                        out.resize(old + avail);
                        DWORD got = 0;
                        if (!WinHttpReadData(req, out.data() + old, avail, &got)) { ok = false; break; }
                        out.resize(old + got);
                    }
                }
                else
                {
                    Log("HTTP: status %lu untuk %s", (unsigned long)status, url);
                }
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
    return ok;
}

// Pastikan file ada di cache dengan CRC yang benar. Unduh kalau belum / rusak.
static bool EnsureCached(const char* url, unsigned int expectedCrc, const char* dest)
{
    std::vector<unsigned char> buf;
    if (ReadFileAll(dest, buf) && Crc32(buf.data(), buf.size()) == expectedCrc)
    {
        Log("CACHE: sudah ada & CRC cocok: %s", dest);
        return true;
    }

    Log("UNDUH: %s", url);
    buf.clear();
    if (!HttpGet(url, buf))
    {
        Log("UNDUH: GAGAL mengunduh %s", url);
        return false;
    }
    unsigned int crc = Crc32(buf.data(), buf.size());
    if (crc != expectedCrc)
    {
        Log("UNDUH: CRC TIDAK COCOK (%08X, seharusnya %08X) untuk %s. File dibuang.", crc, expectedCrc, url);
        return false;
    }

    char tmp[MAX_PATH];
    sprintf_s(tmp, "%s.part", dest);
    if (!WriteFileAll(tmp, buf) || !MoveFileExA(tmp, dest, MOVEFILE_REPLACE_EXISTING))
    {
        Log("UNDUH: gagal menyimpan ke cache: %s", dest);
        DeleteFileA(tmp);
        return false;
    }
    Log("UNDUH: OK (%u byte) -> %s", (unsigned)buf.size(), dest);
    return true;
}

// Baca manifest.txt dan unduh semua model di dalamnya. Format tiap baris:
//   nama,kelas,crcDff,crcTxd      (kelas "-" = bebas; CRC heksadesimal, contoh 1A2B3C4D)
static void SyncRemote()
{
    char url[1024];
    sprintf_s(url, "%s/manifest.txt", g_baseUrl);
    Log("UNDUH: mengambil manifest: %s", url);

    std::vector<unsigned char> data;
    if (!HttpGet(url, data))
    {
        Log("UNDUH: gagal mengambil manifest. Tidak ada model dari server.");
        return;
    }
    data.push_back(0);

    SHCreateDirectoryExA(NULL, g_cacheDir, NULL);

    char* ctxLine = nullptr;
    int lineNo = 0;
    for (char* line = strtok_s((char*)data.data(), "\n", &ctxLine); line; line = strtok_s(nullptr, "\n", &ctxLine))
    {
        lineNo++;
        while (*line == ' ' || *line == '\t') line++;
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')) line[--len] = 0;
        if (!*line || *line == '#') continue;

        if (g_modelCount >= MAX_MODELS) { Log("UNDUH: daftar model penuh (%d).", MAX_MODELS); break; }

        char* ctx = nullptr;
        char* name = strtok_s(line, ",", &ctx);
        char* cls  = strtok_s(nullptr, ",", &ctx);
        char* sd   = strtok_s(nullptr, ",", &ctx);
        char* st   = strtok_s(nullptr, ",", &ctx);
        if (!name || !cls || !sd || !st)
        {
            Log("MANIFEST: baris %d tidak valid, dilewati.", lineNo);
            continue;
        }
        unsigned int dffCrc = (unsigned int)strtoul(sd, nullptr, 16);
        unsigned int txdCrc = (unsigned int)strtoul(st, nullptr, 16);

        ModelDef& m = g_models[g_modelCount];     // hanya thread ini yang menulis; diterbitkan di akhir
        memset(&m, 0, sizeof(m));
        strncpy_s(m.name, sizeof(m.name), name, _TRUNCATE);
        if (strcmp(cls, "-") != 0) { strncpy_s(m.cls, sizeof(m.cls), cls, _TRUNCATE); _strlwr_s(m.cls, sizeof(m.cls)); }
        sprintf_s(m.dffPath, "%s\\0x%08X.dff", g_cacheDir, dffCrc);
        sprintf_s(m.txdPath, "%s\\0x%08X.txd", g_cacheDir, txdCrc);

        char urlDff[1024], urlTxd[1024];
        sprintf_s(urlDff, "%s/%s.dff", g_baseUrl, name);
        sprintf_s(urlTxd, "%s/%s.txd", g_baseUrl, name);

        Log("MANIFEST: model '%s' kelas '%s'", m.name, m.cls[0] ? m.cls : "(bebas)");
        if (!EnsureCached(urlDff, dffCrc, m.dffPath)) continue;
        if (!EnsureCached(urlTxd, txdCrc, m.txdPath)) continue;

        InterlockedIncrement(&g_modelCount);       // terbitkan: model ini siap dipakai thread game
        Log("MANIFEST: model '%s' SIAP.", m.name);
    }
    Log("UNDUH: selesai. %ld model siap.", (long)g_modelCount);
}

#endif  // CV_ENABLE_DOWNLOAD

// ---------------------------------------------------------------- konfigurasi
// Dijalankan SEKALI oleh thread kerja (bukan dari DllMain / thread game).
static void EnsureConfig()
{
    char buf[512] = {0};

    // donor: Donors=503:sport,504:sedan   (atau tanpa kelas: 503,504; format lama Donor=503)
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
        char cls[16];
        SplitClass(tmp[i], cls, sizeof(cls));
        int id = atoi(tmp[i]);
        if (id < 400 || id > 611) { Log("KONFIG: donor '%s' tidak valid (harus 400-611), dilewati.", tmp[i]); continue; }
        bool dup = false;
        for (int j = 0; j < g_donorCount; j++) if (g_donors[j].id == id) dup = true;
        if (dup) continue;
        g_donors[g_donorCount].id = id;
        strncpy_s(g_donors[g_donorCount].cls, sizeof(g_donors[g_donorCount].cls), cls, _TRUNCATE);
        g_donors[g_donorCount].modelIdx = -1;
        g_donors[g_donorCount].clump = 0;
        g_donors[g_donorCount].broken = false;
        g_donorCount++;
    }

#if CV_ENABLE_DOWNLOAD
    ReadIni("BaseUrl", g_baseUrl, sizeof(g_baseUrl));
    size_t bl = strlen(g_baseUrl);
    while (bl > 0 && (g_baseUrl[bl - 1] == '/' || g_baseUrl[bl - 1] == '\\' || g_baseUrl[bl - 1] == ' ')) g_baseUrl[--bl] = 0;
#else
    {
        char probe[8] = {0};
        ReadIni("BaseUrl", probe, sizeof(probe));
        if (probe[0])
            Log("KONFIG: BaseUrl diisi, tetapi build ini TANPA fitur unduh (CV_ENABLE_DOWNLOAD=0). Memakai mode lokal.");
    }
#endif

    if (g_baseUrl[0])
    {
#if CV_ENABLE_DOWNLOAD
        // MODE UNDUH: cache bersama
        ReadIni("CacheDir", g_cacheDir, sizeof(g_cacheDir));
        if (!g_cacheDir[0])
        {
            char docs[MAX_PATH] = {0};
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, docs)))
                sprintf_s(g_cacheDir, "%s\\GTA San Andreas User Files\\SAMP\\cache\\CustomVehicles", docs);
            else
                strcpy_s(g_cacheDir, "CustomVehicles\\cache");
        }
        Log("KONFIG: MODE UNDUH. BaseUrl = %s", g_baseUrl);
        Log("KONFIG: folder cache = %s", g_cacheDir);
#endif
    }
    else
    {
        // MODE LOKAL: Models=infernus:sport,Skyline:sport  (atau format lama File=infernus)
        memset(buf, 0, sizeof(buf));
        ReadIni("Models", buf, sizeof(buf));
        if (!buf[0]) ReadIni("File", buf, sizeof(buf));
        char names[MAX_MODELS][64];
        int mc = 0;
        ParseCsv(buf, names, MAX_MODELS, mc);

        char dir[MAX_PATH] = {0};
        GetGameDir(dir, sizeof(dir));
        for (int i = 0; i < mc && g_modelCount < MAX_MODELS; i++)
        {
            char cls[16];
            SplitClass(names[i], cls, sizeof(cls));
            ModelDef& m = g_models[g_modelCount];
            memset(&m, 0, sizeof(m));
            strncpy_s(m.name, sizeof(m.name), names[i], _TRUNCATE);
            strncpy_s(m.cls, sizeof(m.cls), cls, _TRUNCATE);
            sprintf_s(m.dffPath, "CustomVehicles\\%s.dff", m.name);
            sprintf_s(m.txdPath, "CustomVehicles\\%s.txd", m.name);

            char fullDff[MAX_PATH], fullTxd[MAX_PATH];
            sprintf_s(fullDff, "%s\\%s", dir, m.dffPath);
            sprintf_s(fullTxd, "%s\\%s", dir, m.txdPath);
            if (!FileExistsA(fullDff) || !FileExistsA(fullTxd))
            {
                Log("KONFIG: file model '%s' tidak lengkap (%s / %s), dilewati.", m.name, fullDff, fullTxd);
                continue;
            }
            InterlockedIncrement(&g_modelCount);
        }
        Log("KONFIG: MODE LOKAL.");
    }

    Log("KONFIG: %d donor, %ld model siap.", g_donorCount, (long)g_modelCount);
    for (int i = 0; i < g_donorCount; i++)
        Log("KONFIG: donor[%d] = model %d kelas '%s'", i, g_donors[i].id, g_donors[i].cls[0] ? g_donors[i].cls : "(bebas)");
    for (int i = 0; i < (int)g_modelCount; i++)
        Log("KONFIG: model[%d] = %s kelas '%s'", i, g_models[i].name, g_models[i].cls[0] ? g_models[i].cls : "(bebas)");

    InterlockedExchange(&g_configReady, 1);
}

// ---------------------------------------------------------------- muat model dari file ke sebuah donor
static bool EnsureModelLoaded(int modelId)
{
    CStreaming::RequestModel(modelId, 2);
    for (int i = 0; i < 10 && GetLoadState(modelId) != 1; i++)
        CStreaming::LoadAllRequestedModels(false);
    return GetLoadState(modelId) == 1;
}

// Menyiapkan path yang bisa dibuka loader game. Path relatif (mode lokal) dipakai apa adanya.
// Path absolut (cache) dicoba dibuka lewat RenderWare; kalau tidak bisa, file disalin ke
// folder game (CustomVehicles\cache) dan path relatif dipakai.
static bool ResolveLoadPath(const char* path, char* out, size_t outSize, const char* gameDir)
{
    bool absolute = (path[0] && path[1] == ':') || (path[0] == '\\' && path[1] == '\\');
    if (!absolute)
    {
        char full[MAX_PATH];
        sprintf_s(full, "%s\\%s", gameDir, path);
        if (!FileExistsA(full)) { Log("MUAT: file tidak ditemukan: %s", full); return false; }
        strncpy_s(out, outSize, path, _TRUNCATE);
        return true;
    }

#if CV_ENABLE_DOWNLOAD
    if (!FileExistsA(path)) { Log("MUAT: file cache tidak ditemukan: %s", path); return false; }

    RwStream* probe = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, path);
    if (probe)
    {
        RwStreamClose(probe, nullptr);
        strncpy_s(out, outSize, path, _TRUNCATE);
        return true;
    }

    Log("MUAT: path absolut tidak bisa dibuka RenderWare, menyalin ke folder game...");
    char dirAbs[MAX_PATH];
    sprintf_s(dirAbs, "%s\\CustomVehicles\\cache", gameDir);
    SHCreateDirectoryExA(NULL, dirAbs, NULL);
    const char* base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    char rel[MAX_PATH], dst[MAX_PATH];
    sprintf_s(rel, "CustomVehicles\\cache\\%s", base);
    sprintf_s(dst, "%s\\%s", gameDir, rel);
    if (!CopyFileA(path, dst, FALSE))
    {
        Log("MUAT: gagal menyalin %s -> %s", path, dst);
        return false;
    }
    strncpy_s(out, outSize, rel, _TRUNCATE);
    return true;
#else
    Log("MUAT: path absolut tidak didukung di build ini (CV_ENABLE_DOWNLOAD=0): %s", path);
    return false;
#endif
}

// Mengganti isi model donor dengan model dari file memakai loader bawaan game.
// Kalau gagal SEBELUM ada yang diubah: return false, slot tetap aman.
// Kalau gagal SETELAH ada yang diubah: slot ditandai rusak.
static bool LoadModelIntoDonor(DonorSlot& slot, int modelIdx)
{
    const ModelDef& model = g_models[modelIdx];
    int donor = slot.id;

    char dir[MAX_PATH] = {0};
    GetGameDir(dir, sizeof(dir));

    Log("MUAT[%s -> donor %d]: [1/8] siapkan path file...", model.name, donor);
    char dffUse[MAX_PATH], txdUse[MAX_PATH];
    if (!ResolveLoadPath(model.dffPath, dffUse, sizeof(dffUse), dir)) return false;
    if (!ResolveLoadPath(model.txdPath, txdUse, sizeof(txdUse), dir)) return false;
    Log("MUAT: DFF = %s", dffUse);
    Log("MUAT: TXD = %s", txdUse);

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

    Log("MUAT: [5/8] LoadTxd(%d)...", (int)txdSlot);
    bool okTxd = CTxdStore::LoadTxd(txdSlot, txdUse);
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

    Log("MUAT: [6/8] LoadClumpFile(%d)...", donor);
    // Stream dibuka sendiri dan TIDAK ditutup dari sini (kalau game sudah menutupnya,
    // menutup dua kali akan crash).
    RwStream* dffStream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, dffUse);
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
    Log("MUAT: [8/8] BERHASIL. Model '%s' terpasang di donor %d.", model.name, donor);
    return true;
}

// Cari (atau siapkan) donor yang memegang model custom ke-modelIdx. Return indeks donor, -1 kalau gagal.
static int AcquireDonorFor(int modelIdx)
{
    const ModelDef& model = g_models[modelIdx];

    // 1) sudah ada donor yang memegang model ini?
    for (int i = 0; i < g_donorCount; i++)
    {
        DonorSlot& s = g_donors[i];
        if (s.broken || s.modelIdx != modelIdx) continue;

        unsigned int miPtr = *(unsigned int*)(0xA9B0C8 + s.id * 4);
        unsigned int rwObj = miPtr ? *(unsigned int*)(miPtr + 0x1C) : 0;
        if (rwObj != 0 && rwObj == s.clump && GetLoadState(s.id) == 1)
        {
            Log("DONOR: model '%s' sudah ada di donor %d.", model.name, s.id);
            return i;
        }
        // game membuang / memuat ulang model donor ini -> muat ulang file kita
        Log("DONOR: donor %d berubah (dibuang/dimuat ulang oleh game), memuat ulang '%s'.", s.id, model.name);
        if (LoadModelIntoDonor(s, modelIdx)) return i;
        return -1;
    }

    // 2) pakai donor kosong yang SEKELAS
    bool classFull = true;
    for (int i = 0; i < g_donorCount; i++)
    {
        DonorSlot& s = g_donors[i];
        if (s.broken || s.modelIdx != -1) continue;
        if (!ClassMatches(model.cls, s.cls)) continue;
        classFull = false;
        Log("DONOR: memakai donor kosong %d (kelas '%s') untuk '%s' (kelas '%s').",
            s.id, s.cls[0] ? s.cls : "(bebas)", model.name, model.cls[0] ? model.cls : "(bebas)");
        if (LoadModelIntoDonor(s, modelIdx)) return i;
        if (s.broken) continue;    // rusak -> coba donor sekelas berikutnya
        return -1;
    }

    if (classFull)
        Log("DONOR: tidak ada donor kosong yang sekelas dengan '%s' (kelas '%s'). Tambahkan donor kelas itu di Donors=.",
            model.name, model.cls[0] ? model.cls : "(bebas)");
    else
        Log("DONOR: semua donor sekelas untuk '%s' gagal / rusak.", model.name);
    return -1;
}

static void LogPool()
{
    for (int i = 0; i < g_donorCount; i++)
    {
        DonorSlot& s = g_donors[i];
        const char* c = s.cls[0] ? s.cls : "(bebas)";
        if (s.broken) Log("KOLAM: donor %d [%s] = RUSAK", s.id, c);
        else if (s.modelIdx < 0) Log("KOLAM: donor %d [%s] = kosong", s.id, c);
        else Log("KOLAM: donor %d [%s] = '%s'", s.id, c, g_models[s.modelIdx].name);
    }
}

static bool ConfigOk(const char* who)
{
    if (!g_configReady)
    {
        Log("%s: konfigurasi belum siap (thread kerja masih berjalan). Coba lagi sebentar.", who);
        return false;
    }
    return true;
}

static void OnLoadCustom()
{
    if (!ConfigOk("MUAT")) return;
    Log("MUAT: INSERT ditekan: memuat semua model yang siap ke kolam donor (%ld model).", (long)g_modelCount);
    for (int m = 0; m < (int)g_modelCount; m++)
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
    if (!ConfigOk("TUKAR")) return;

    CVehicle* veh = FindPlayerVehicle(-1, false);
    if (!veh)
    {
        Log("TUKAR: kamu tidak sedang di dalam kendaraan.");
        return;
    }
    int modelCount = (int)g_modelCount;
    if (modelCount == 0)
    {
        Log("TUKAR: belum ada model custom yang siap (isi Models= di INI, atau tunggu / cek unduhan dari BaseUrl).");
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

    if (nextIdx >= modelCount)
    {
        // putaran habis -> kembalikan ke model asli
        Log("TUKAR: KEMBALIKAN ke model asli %d", original);
        if (PerformSwap(veh, original) && e >= 0)
            RemoveSwapAt(e);
        return;
    }

    Log("TUKAR: model custom #%d '%s' (model asli kendaraan = %d)", nextIdx, g_models[nextIdx].name, original);
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

// Thread kerja: baca konfigurasi lalu (kalau BaseUrl diisi) unduh manifest + model ke cache.
// Berjalan terpisah dari thread game supaya unduhan tidak membuat game patah-patah.
static DWORD WINAPI WorkerThread(LPVOID)
{
    EnsureConfig();
#if CV_ENABLE_DOWNLOAD
    if (g_baseUrl[0]) SyncRemote();
#endif
    return 0;
}

class CustomVehiclesPlugin
{
public:
    CustomVehiclesPlugin()
    {
        Log("=== CustomVehicles (B2: kelas donor + unduh; INSERT muat, F10 baca, F11 tukar/siklus, F12 isolasi) ter-load ===");
        plugin::Events::gameProcessEvent.before += OnGameProcess;
        plugin::Events::drawingEvent += OnDrawing;

        HANDLE h = CreateThread(NULL, 0, KeyThread, NULL, 0, NULL);
        if (h) CloseHandle(h);

        HANDLE w = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
        if (w) CloseHandle(w);
    }
} g_customVehiclesPlugin;

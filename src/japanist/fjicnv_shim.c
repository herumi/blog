/*
fjicnv.dll shim (64bit)
Japanistの実fjicnv.dll(優先ベース 0x15000000, 2GB 未満必須)をClaude Desktopのように低位が埋まったプロセスでも動かすための差し込みDLL

動作:
- launcher.exeがCREATE_SUSPENDEDでclaude.exeを起こし0x15000000にMEM_RESERVEで領域を確保してからResumeThreadしている前提。
- このshimを実fjicnv.dllの場所(x64\CMD\fjicnv.dll)に置き、実物は同じディレクトリにfjicnv_real.dll (DYNAMICBASEを落としたコピー)として置く。
- oakfjitip100が唯一のexport otwc0003_5000gTopを初めて呼んだとき(=変換開始時) に、0x15000000の予約を解放し、直後にfjicnv_real.dllをLoadLibraryする。
DYNAMICBASE を落としてあるので空いていれば優先ベース0x15000000に入り、以降はそのまま実物へ転送する。
- 予約が無い(launcher 経由でない、他アプリ)場合は他アプリのIMEを壊さないよう実物をロードして転送する(パススルー)。
*/

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

/* 1にすると %TEMP%\fjicnv_shim.log と OutputDebugString にログを出す */
#define SHIM_LOG 0

#define RESERVE_ADDR ((void*)0x15000000ULL)

typedef unsigned short (*otwc_fn)(int func, void *block);

static HINSTANCE g_self = NULL;
static otwc_fn g_real = NULL;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
#if SHIM_LOG
static long g_call = 0;  /* ログ用の呼び出し通し番号 */
#endif

static void logline(const char *fmt, ...) {
#if !SHIM_LOG
    (void)fmt;
    return;
#else
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);

    OutputDebugStringA(buf);

    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("TEMP", path, sizeof(path));
    if (n == 0 || n >= sizeof(path)) return;
    strcat_s(path, sizeof(path), "\\fjicnv_shim.log");

    FILE *fp = NULL;
    if (fopen_s(&fp, path, "a") == 0 && fp) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(fp, "[%02d:%02d:%02d.%03d pid=%lu] %s",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                GetCurrentProcessId(), buf);
        fclose(fp);
    }
#endif
}

/* g_self のフルパスの末尾ファイル名を fjicnv_real.dll に差し替える */
static BOOL build_real_path(wchar_t *out, size_t cch) {
    wchar_t self[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return FALSE;

    wchar_t *slash = wcsrchr(self, L'\\');
    if (!slash) return FALSE;
    *(slash + 1) = 0;

    if (wcslen(self) + wcslen(L"fjicnv_real.dll") >= cch) return FALSE;
    wcscpy_s(out, cch, self);
    wcscat_s(out, cch, L"fjicnv_real.dll");
    return TRUE;
}

static BOOL CALLBACK init_cb(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)param; (void)ctx;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(RESERVE_ADDR, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_RESERVE && mbi.AllocationBase == RESERVE_ADDR) {
            logline("reservation found at %p size=0x%zx, releasing\n", mbi.AllocationBase, mbi.RegionSize);
            if (!VirtualFree(RESERVE_ADDR, 0, MEM_RELEASE)) {
                logline("VirtualFree failed err=%lu\n", GetLastError());
            } else {
                logline("VirtualFree ok\n");
            }
        } else {
            logline("no launcher reservation (state=0x%lx base=%p) -> passthrough\n", mbi.State, mbi.AllocationBase);
        }
    } else {
        logline("VirtualQuery(0x15000000) failed err=%lu\n", GetLastError());
    }

    wchar_t real[MAX_PATH];
    if (!build_real_path(real, MAX_PATH)) {
        logline("build_real_path failed\n");
        return TRUE;
    }
    logline("loading real: %ls\n", real);

    HMODULE h = LoadLibraryW(real);
    if (!h) {
        logline("LoadLibrary(fjicnv_real.dll) failed err=%lu\n", GetLastError());
        return TRUE;
    }
    logline("real loaded at %p (below 2GB: %s)\n", (void*)h, ((ULONG_PTR)h < 0x80000000ULL) ? "YES" : "NO");

    g_real = (otwc_fn)GetProcAddress(h, "otwc0003_5000gTop");
    logline("GetProcAddress(otwc0003_5000gTop) = %p\n", (void*)g_real);
    return TRUE;
}

unsigned short otwc0003_5000gTop(int func, void *block) {
    InitOnceExecuteOnce(&g_once, init_cb, NULL, NULL);

    if (!g_real) {
        logline("g_real is NULL, cannot forward (func=%d)\n", func);
        return 0;
    }

#if SHIM_LOG
    long n = InterlockedIncrement(&g_call);
    if (n <= 40) {
        logline("call#%ld func=%d block=%p\n", n, func, block);
    }
#endif

    unsigned short r = g_real(func, block);

#if SHIM_LOG
    if (n <= 40) {
        logline("call#%ld func=%d -> %u\n", n, func, r);
    }
#endif
    return r;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}

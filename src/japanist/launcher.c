/*
launcher.exe (64bit)
対象のbrowserプロセス(既定はClaude Desktopのclaude.exe)をCREATE_SUSPENDEDで起こし、
Chromium/Electronのコードが低位2GiBを埋める前に、fjicnv.dllの優先ベース0x15000000をMEM_RESERVEで確保してからResumeThreadする。
以降はfjicnv shimがその場所を使う (fjicnv_shim.c を参照)。

使い方:
 launcher.exe                          ... Claude Desktop (app-*から最新のclaude.exeを選択)
 launcher.exe claude|edge|chrome [args] ... 名前で指定 (edge/chromeはApp Pathsレジストリから探す)
 launcher.exe <exe path> [args]         ... 任意のexe
argsは対象にそのまま渡す (URLなど)。
対象が既に起動している場合は中断する (単一インスタンスのアプリでは既存プロセスに渡されるだけで場所取りが効かないため)。
*/

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <shlobj.h>
#include <stdint.h>

/* コンソールを出さない */
#pragma comment(linker, "/SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup")

#define RESERVE_ADDR ((LPVOID)0x15000000ULL)
#define RESERVE_SIZE (0x4000000ULL)   /* 64MiB */
#define CMDLINE_MAX 32768

static void msg(const wchar_t *text, UINT icon) {
    MessageBoxW(NULL, text, L"launcher (Japanist fix)", MB_OK | icon);
}

/* "app-1.52386.6"のような名前から最大4個の整数を取り出して比較する */
static void parse_ver(const wchar_t *name, uint32_t v[4]) {
    v[0] = v[1] = v[2] = v[3] = 0;
    const wchar_t *p = wcschr(name, L'-');
    if (!p) return;
    p++;
    for (int i = 0; i < 4 && *p; i++) {
        v[i] = (uint32_t)wcstoul(p, NULL, 10);
        p = wcschr(p, L'.');
        if (!p) break;
        p++;
    }
}

static int ver_gt(const uint32_t a[4], const uint32_t b[4]) {
    for (int i = 0; i < 4; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return 0;
}

static BOOL find_claude(wchar_t *out, size_t cch) {
    wchar_t base[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, base))) return FALSE;
    wcscat_s(base, MAX_PATH, L"\\AnthropicClaude");

    wchar_t pattern[MAX_PATH];
    swprintf_s(pattern, MAX_PATH, L"%s\\app-*", base);

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return FALSE;

    wchar_t best[MAX_PATH] = L"";
    uint32_t bestv[4] = {0,0,0,0};
    BOOL found = FALSE;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcsncmp(fd.cFileName, L"app-", 4) != 0) continue;
        uint32_t v[4];
        parse_ver(fd.cFileName, v);
        if (!found || ver_gt(v, bestv)) {
            memcpy(bestv, v, sizeof(bestv));
            wcscpy_s(best, MAX_PATH, fd.cFileName);
            found = TRUE;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (!found) return FALSE;
    swprintf_s(out, cch, L"%s\\%s\\claude.exe", base, best);
    return TRUE;
}

/*
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\<exe> の既定値からフルパスを得る。
msedge.exe, chrome.exe はインストーラがここに登録する。
*/
static BOOL find_app_path(const wchar_t *exename, wchar_t *out, size_t cch) {
    wchar_t key[MAX_PATH];
    swprintf_s(key, MAX_PATH, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%s", exename);
    DWORD size = (DWORD)(cch * sizeof(wchar_t));
    LSTATUS st = RegGetValueW(HKEY_LOCAL_MACHINE, key, NULL, RRF_RT_REG_SZ, NULL, out, &size);
    return st == ERROR_SUCCESS && out[0] != 0;
}

/* フルパスの末尾ファイル名 */
static const wchar_t *basename_of(const wchar_t *path) {
    const wchar_t *p = wcsrchr(path, L'\\');
    return p ? p + 1 : path;
}

/*
exenameという名前のプロセスを数える。
pathsubが非NULLなら実行イメージのパスにそれを含むものだけ数える
(Claude Desktopのclaude.exeをClaude Code CLIの~\.local\bin\claude.exeと区別するため)。
*/
static int count_procs(const wchar_t *exename, const wchar_t *pathsub) {
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exename) != 0) continue;
            if (!pathsub) { n++; continue; }
            HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!hp) continue;
            wchar_t img[MAX_PATH]; DWORD cch = MAX_PATH;
            if (QueryFullProcessImageNameW(hp, 0, img, &cch)) {
                if (wcsstr(img, pathsub) != NULL) n++;
            }
            CloseHandle(hp);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return n;
}

/* 引数にスペースや引用符があれば引用符で囲んで追加する */
static void append_arg(wchar_t *cmd, size_t cch, const wchar_t *arg) {
    BOOL quote = (arg[0] == 0) || wcschr(arg, L' ') || wcschr(arg, L'\t') || wcschr(arg, L'"');
    wcscat_s(cmd, cch, L" ");
    if (!quote) {
        wcscat_s(cmd, cch, arg);
        return;
    }
    wcscat_s(cmd, cch, L"\"");
    for (const wchar_t *p = arg; *p; p++) {
        if (*p == L'"') wcscat_s(cmd, cch, L"\\\"");
        else {
            wchar_t s[2] = { *p, 0 };
            wcscat_s(cmd, cch, s);
        }
    }
    wcscat_s(cmd, cch, L"\"");
}

int wmain(int argc, wchar_t **argv) {
    const wchar_t *target = (argc > 1) ? argv[1] : NULL;
    int first_arg = (argc > 1) ? 2 : argc;

    wchar_t exe[MAX_PATH];
    const wchar_t *pathsub = NULL;   /* 二重起動チェックでパスに要求する部分文字列 */
    const wchar_t *label = NULL;     /* メッセージ用 */
    if (target == NULL || _wcsicmp(target, L"claude") == 0) {
        label = L"Claude Desktop";
        pathsub = L"AnthropicClaude";
        if (!find_claude(exe, MAX_PATH)) {
            msg(L"claude.exe が見つかりません (%LOCALAPPDATA%\\AnthropicClaude\\app-*)。", MB_ICONERROR);
            return 1;
        }
    } else if (_wcsicmp(target, L"edge") == 0) {
        label = L"Microsoft Edge";
        if (!find_app_path(L"msedge.exe", exe, MAX_PATH)) {
            msg(L"msedge.exe が App Paths に見つかりません。", MB_ICONERROR);
            return 1;
        }
    } else if (_wcsicmp(target, L"chrome") == 0) {
        label = L"Google Chrome";
        if (!find_app_path(L"chrome.exe", exe, MAX_PATH)) {
            msg(L"chrome.exe が App Paths に見つかりません。", MB_ICONERROR);
            return 1;
        }
    } else {
        label = target;
        wcscpy_s(exe, MAX_PATH, target);
    }

    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
        wchar_t m[MAX_PATH + 64];
        swprintf_s(m, _countof(m), L"ファイルがありません:\n%s", exe);
        msg(m, MB_ICONERROR);
        return 1;
    }

    int running = count_procs(basename_of(exe), pathsub);
    if (running > 0) {
        wchar_t m[256];
        swprintf_s(m, _countof(m),
            L"%s は既に起動しています。\n"
            L"一度終了してから、このアイコンで起動し直してください。\n"
            L"(低位の場所取りは起動時にしか行えません)", label);
        msg(m, MB_ICONWARNING);
        return 1;
    }

    /* CreateProcess はコマンドラインを書き換え可能なバッファで要求する */
    static wchar_t cmd[CMDLINE_MAX];
    swprintf_s(cmd, CMDLINE_MAX, L"\"%s\"", exe);
    for (int k = first_arg; k < argc; k++) {
        append_arg(cmd, CMDLINE_MAX, argv[k]);
    }

    STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        wchar_t m[128];
        swprintf_s(m, _countof(m), L"CreateProcess 失敗 err=%lu", GetLastError());
        msg(m, MB_ICONERROR);
        return 1;
    }

    LPVOID got = VirtualAllocEx(pi.hProcess, RESERVE_ADDR, (SIZE_T)RESERVE_SIZE, MEM_RESERVE, PAGE_NOACCESS);
    if (got != RESERVE_ADDR) {
        wchar_t m[192];
        swprintf_s(m, _countof(m), L"0x15000000 の場所取りに失敗しました (got=%p err=%lu)。中断します。", got, GetLastError());
        msg(m, MB_ICONERROR);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 1;
    }

    if (ResumeThread(pi.hThread) == (DWORD)-1) {
        msg(L"ResumeThread に失敗しました。", MB_ICONERROR);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

/*
launcher.exe (64bit)
Claude Desktopのbrowserプロセス(claude.exe)をCREATE_SUSPENDEDで起こし、
Electronのコードが低位2GiBを埋める前に、fjicnv.dllの優先ベース0x15000000をMEM_RESERVEで確保してからResumeThreadする。
以降はfjicnv shimがその場所を使う (fjicnv_shim.c を参照)。

使い方:
 launcher.exe                  ... app-*から最新のclaude.exeを選択
 launcher.exe <claude.exe path>
 launcher.exe --force ...       ... 既存のclaude.exeがあっても続行
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

static void msg(const wchar_t *text, UINT icon) {
    MessageBoxW(NULL, text, L"Claude launcher (Japanist fix)", MB_OK | icon);
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
Claude Desktopのclaude.exeだけ数える。
Claude Code CLI(~\.local\bin\claude.exe)と区別するため、実行イメージのパスに"AnthropicClaude"を含むものだけをカウントする。
*/
static int count_claude_procs(void) {
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"claude.exe") != 0) continue;
            HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!hp) continue;
            wchar_t img[MAX_PATH]; DWORD cch = MAX_PATH;
            if (QueryFullProcessImageNameW(hp, 0, img, &cch)) {
                if (wcsstr(img, L"AnthropicClaude") != NULL) n++;
            }
            CloseHandle(hp);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return n;
}

int wmain(int argc, wchar_t **argv) {
    BOOL force = FALSE;
    const wchar_t *given = NULL;
    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"--force") == 0) force = TRUE;
        else given = argv[i];
    }

    wchar_t exe[MAX_PATH];
    if (given) {
        wcscpy_s(exe, MAX_PATH, given);
    } else if (!find_claude(exe, MAX_PATH)) {
        msg(L"claude.exe が見つかりません (%LOCALAPPDATA%\\AnthropicClaude\\app-*)。", MB_ICONERROR);
        return 1;
    }

    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
        wchar_t m[MAX_PATH + 64];
        swprintf_s(m, _countof(m), L"ファイルがありません:\n%s", exe);
        msg(m, MB_ICONERROR);
        return 1;
    }

    int running = count_claude_procs();
    if (running > 0 && !force) {
        msg(L"Claude Desktop は既に起動しています。\n"
            L"一度終了してから、このアイコンで起動し直してください。\n"
            L"(低位の場所取りは起動時にしか行えません)", MB_ICONWARNING);
        return 1;
    }

    /* CreateProcess はコマンドラインを書き換え可能なバッファで要求する */
    wchar_t cmd[MAX_PATH + 4];
    swprintf_s(cmd, MAX_PATH + 4, L"\"%s\"", exe);

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

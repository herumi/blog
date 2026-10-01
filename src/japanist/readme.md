# Windows Claude DesktopでJapanist 10を使うためのファイル群
- [Edge/Chrome/Claude DesktopでJapanistを使う](../../articles/japanist-with-chrome.md)参照

## ソース

- launcher.c
launcher.exeのソース。claude.exeをCREATE_SUSPENDEDで起こし、Electronが低位2GiBを埋める前に0x15000000をMEM_RESERVEで
64MiBだけ確保してからResumeThreadする。
%LOCALAPPDATA%\AnthropicClaude\app-* から最新版の claude.exeを自動選択し、Claude Desktop が既に起動していれば警告して終了する(Claude Code CLI の claude.exe は区別)。
--forceで既存起動があっても続行。

- fjicnv_shim.c
差し替え用fjicnv.dll(shim)のソース。唯一のexportであるotwc0003_5000gTopが初めて呼ばれたとき(変換開始時)に、
launcherが取った0x15000000の予約を解放し、fjicnv_real.dllをLoadLibraryして優先ベースに入れる。以降はそのまま実物に転送。
launcher経由でない場合(他アプリ)は予約が無いので素通しで実物をロードして転送するだけで、他アプリの IME を壊さない。

- fjicnv.def
shim のエクスポート定義。otwc0003_5000gTop@1だけを公開する。build.batの/DEF:で使用。

- build.bat
ビルドスクリプト。fjicnv.dllとlauncher.exeを生成する。

## 配置・運用スクリプト(PowerShell、管理者で実行)
事前に`C:\Program Files\Japanist10\x64\CMD`に書き込み権限を付与しておく。

- patch_dll.ps1
現行の実`C:\Program Files\Japanist10\x64\CMD\fjicnv.dll`を読み、
PEヘッダのDllCharacteristicsからDYNAMIC_BASE(0x40) を落としたコピーfjicnv_real.dll をこのフォルダに作る。
ASLRを切ることで、空いていれば優先ベース 0x15000000に固定にする。

- install.ps1
Japanist10のx64\CMD\fjicnv.dllをshimに差し替え、同じフォルダにfjicnv_real.dll(パッチ済み)を置く。
元のDLLはfjicnv.dll.suspendbakに退避する。

- uninstall.ps1
install.ps1の差し替えを元に戻す。fjicnv.dll.suspendbakからfjicnv.dllを復元し、shim、fjicnv_real.dll、退避ファイルを削除する。

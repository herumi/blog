# x64\CMD\fjicnv.dll を shim に差し替え、fjicnv_real.dll (パッチ済み) を隣に置く。
# 元の fjicnv.dll は fjicnv.dll.suspendbak として退避 (既存 .old/.bak/.patched は触らない)。
#
# Program Files への書き込みが必要。あらかじめ管理者コマンドプロンプトで
#   icacls "C:\Program Files\Japanist10\x64\CMD" /grant "%USERNAME%:(M)"
# として書き込み権限を付けておくか、本スクリプトを管理者 PowerShell で実行する。
# 起動: powershell -ExecutionPolicy Bypass -File install.ps1
$ErrorActionPreference = 'Stop'

$cmd  = 'C:\Program Files\Japanist10\x64\CMD'
$here = $PSScriptRoot
$shim = Join-Path $here 'fjicnv.dll'
$real = Join-Path $here 'fjicnv_real.dll'

if (!(Test-Path $shim)) { throw "shim 未ビルド: $shim (build.bat を実行)" }
if (!(Test-Path $real)) { throw "real 未生成: $real (patch_dll.ps1 を実行)" }

$target = Join-Path $cmd 'fjicnv.dll'
$bak    = Join-Path $cmd 'fjicnv.dll.suspendbak'

if (!(Test-Path $bak)) {
    Copy-Item $target $bak
    "backed up original -> $bak"
} else {
    "backup already exists (keeping): $bak"
}

# ロックされていても rename は通るので、rename してから shim を書く
$tmp = "$target.replacing"
if (Test-Path $tmp) { Remove-Item $tmp -Force -ErrorAction SilentlyContinue }
Move-Item $target $tmp -Force
Copy-Item $shim $target -Force
try { Remove-Item $tmp -Force } catch { "note: 旧 fjicnv.dll がロック中。$tmp を後で削除してください" }

Copy-Item $real (Join-Path $cmd 'fjicnv_real.dll') -Force

"installed:"
"  $target            (shim)"
"  $(Join-Path $cmd 'fjicnv_real.dll') (patched real)"
"確認: dumpbin -exports `"$target`" が otwc0003_5000gTop 1 個なら shim。"

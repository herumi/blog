# install.ps1 の差し替えを元に戻す。
# fjicnv.dll.suspendbak から fjicnv.dll を復元し、shim/fjicnv_real.dll/退避を削除する。
# install.ps1 と同様に x64\CMD への書き込み権限 (icacls か管理者) が必要。
# 起動: powershell -ExecutionPolicy Bypass -File uninstall.ps1
$ErrorActionPreference = 'Stop'

$cmd    = 'C:\Program Files\Japanist10\x64\CMD'
$target = Join-Path $cmd 'fjicnv.dll'
$bak    = Join-Path $cmd 'fjicnv.dll.suspendbak'
$real   = Join-Path $cmd 'fjicnv_real.dll'

if (!(Test-Path $bak)) { throw "退避が無い: $bak 。手動で確認してください" }

$tmp = "$target.replacing"
if (Test-Path $tmp) { Remove-Item $tmp -Force -ErrorAction SilentlyContinue }
if (Test-Path $target) {
    Move-Item $target $tmp -Force
}
Copy-Item $bak $target -Force
try { Remove-Item $tmp -Force } catch { "note: 旧 shim がロック中。$tmp を後で削除してください" }

Remove-Item $real -Force -ErrorAction SilentlyContinue
Remove-Item $bak  -Force

"restored: $target を元の fjicnv.dll に戻し、shim/fjicnv_real.dll/退避を削除しました"
"確認: dumpbin -headers `"$target`" の DllCharacteristics が 0x160 なら原状復帰。"

# 実 fjicnv.dll (x64) から DYNAMIC_BASE を落としたコピー fjicnv_real.dll を作る。
# editbin と checksum を除いて同じ操作:
#   copy "C:\Program Files\Japanist10\x64\CMD\fjicnv.dll" fjicnv_real.dll
#   editbin /DYNAMICBASE:NO fjicnv_real.dll
#
# install.ps1 適用後は x64\CMD\fjicnv.dll が shim に置き換わっているので、
# 退避 fjicnv.dll.suspendbak があればそちらを元にする (shim をパッチしないため)。
# 起動: powershell -ExecutionPolicy Bypass -File patch_dll.ps1 (管理者不要)
#
# DllCharacteristics (PE + 24 + 0x46) の低位バイトを -band 0xBF して bit 0x40 を消す。
$ErrorActionPreference = 'Stop'

$cmd = 'C:\Program Files\Japanist10\x64\CMD'
$src = Join-Path $cmd 'fjicnv.dll.suspendbak'
if (!(Test-Path $src)) { $src = Join-Path $cmd 'fjicnv.dll' }
$dst = Join-Path $PSScriptRoot 'fjicnv_real.dll'

$b = [IO.File]::ReadAllBytes($src)

# shim (約 100KB) を取り違えていないかのガード。実物は約 570KB。
if ($b.Length -lt 200KB) {
    throw "元ファイルが小さすぎる ($($b.Length) bytes): $src は shim の可能性。fjicnv.dll.suspendbak を確認してください"
}

$pe = [BitConverter]::ToUInt32($b, 0x3C)
$off = $pe + 24 + 0x46
$before = $b[$off]
$b[$off] = $b[$off] -band 0xBF
[IO.File]::WriteAllBytes($dst, $b)

"src : $src"
"dst : $dst ($($b.Length) bytes)"
"DllCharacteristics low byte: 0x{0:X2} -> 0x{1:X2}" -f $before, $b[$off]
"(DYNAMIC_BASE 0x40 を落とした。0x60 -> 0x20 なら HighEntropy のみ残り固定ベース化)"
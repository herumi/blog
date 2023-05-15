---
title: "Xbyakライクなx64用静的ASM生成ツールs_xbyak"
emoji: "🛠"
type: "tech"
topics: ["x64", "ASM", "Python", "Xbyak"]
published: true
---
## 初めに
これはx64用JITアセンブラ[Xbyak](https://github.com/herumi/xbyak)に慣れてしまい、JITでなく静的なアセンブリ言語（以下ASM）もXbyakライクに書きたいという人（つまり私）がPython上で似た開発体験を求めて作ったツールです。
`s_xbyak`の"s_"は静的(static)からつけました。

## [s_xbyak](https://github.com/herumi/s_xbyak)の特徴
- Pythonで作られたASMコードジェネレータ
- gas (GNU Assembler), [Netwide Assembler (NASM)](https://www.nasm.us/), [Microsoft Macro Assembler](https://learn.microsoft.com/vi-vn/cpp/assembler/masm/microsoft-macro-assembler-reference)に対応
- Win64 ABIとAMD64 (Linux)に（一部）対応
- XbyakライクなDSL

## 背景
私はC++上でJITコードを書きたくてXbyakを作りました。するとJIT機能だけでなく、ASMをC++の文法で記述できるのはとても便利なことが分かりました。既存のアセンブラの文法は制約が多かったり、擬似命令を覚えるのが面倒だったりするからです。Xbyakに慣れてしまうと通常のアセンブラは使いたくなくなりました。
しかしXbyakはJITコード生成なので静的なASMを書きたいときは、JIT生成したコードをバイナリダンプしてdb命令などで埋め込んで使うといった無理やりな手段しかありませんでした。
そこで静的なASM出力ツール`s_xbyak`を作りました。お気軽さを求めて言語はC++ではなくPythonを選びました。
最初はNASMにだけ対応するつもりだったのですが、GASやMASMも対応してるのはgccやVisual Studioをインストールするだけで使えて何かと便利だったからです。その代わり差を吸収するのにえらく苦労してるのですがその話は次回。

## 例1. 足し算の関数
どんなことができるのか、まず足し算関数のサンプル[add.py](https://github.com/herumi/s_xbyak/blob/main/sample/add.py)を紹介しましょう。

```python
# add.py
from s_xbyak import *

parser = getDefaultParser()
param = parser.parse_args()

init(param)
segment('text')
with FuncProc('add2'):
  with StackFrame(2) as sf:
    x = sf.p[0]
    y = sf.p[1]
    lea(rax, ptr(x + y))

term()
```

説明
- `from s_xbyak import *` : `s_xbyak`をインポートします。
- `getDefaultParser()` : デフォルトのコマンドライン引数を設定します。
  - `-win` : Win64 ABIを使う（デフォルトはAMD64）。
  - `-m (gas|nasm|masm)` : 出力ASMを設定する（デフォルトはnasm）。
- `parser.parse_args()` : コマンドライン引数を解析して結果を辞書`param`に返す。
  - `param`は`win : bool`と`mode : str`を持つ。
- `init(param)` : `param`を元に初期化する。
- `segment('text')` : `text`セグメント開始。
- `with FuncProc('add2'):` `add2`という関数を宣言する。
- `with StackFrame(2) as sf` : `int`型2個の引数を持つ関数のスタックフレームを用意する。
  - 注意 : 現状では0~4を指定可能。関数の引数は整数型かポインタ型のみ対応。
- `x = sf.p[0]` : 関数の第一引数レジスタを`x`という名前にする。
- `y = sf.p[1]` : 関数の第二引数レジスタを`y`という名前にする。
- `lea(rax, ptr(x + y))` : `x+y`の結果を`rax`に代入する。
  - メモリ参照は`Xbyak`では`ptr[...]`を使いましたが、`s_xbyak`では`ptr(...)`を使います（`Xbyak`との違い）。
  - `ret`は`StackFrame`のスコープが終わるところで自動挿入されます。
- `term()` : 出力を終わる。

## `add.py`の使い方
```
python3 add.py [-win][-m mode]
```

### Linuxのgas向け出力
```
python3 add.py -m gas > add_s.S
gcc -c add_s.S
```

```asm
# for gas
#ifdef __linux__
  #define PRE(x) x
  #define TYPE(x) .type x, @function
  #define SIZE(x) .size x, .-x
#else
  #ifdef _WIN32
    #define PRE(x) x
  #else
    #define PRE(x) _ ## x
  #endif
  #define TYPE(x)
  #define SIZE(x)
#endif
.text
.global PRE(add2)
PRE(add2):
TYPE(add2)
lea (%rdi,%rsi,1), %rax
ret
SIZE(add2)
```

`PRE`, `TYPE`, `SIZE`マクロはLinux/Intel macOS/Windowsの差を吸収するためのものです。
gccにマクロを認識させるために拡張子は`*.s`ではなく大文字の`*.S`を使ってください。
`s_xbyak`の`lea(rax, ptr(x + y))`に対応する部分が`lea (%rdi,%rsi,1), %rax`となっています。

### WindowsのMASM向け出力
```
python3 add.py -m masm > add_s.asm
ml64 /c add_s.asm
```

```nasm
; for masm (ml64.exe)
_text segment
add2 proc export
lea rax, [rcx+rdx]
ret
add2 endp
_text ends
end
```

Win64 ABIに合わせて`lea(rax, ptr(x + y))`が`lea rax, [rcx+rdx]`に展開されています。

### NASM向け出力
AMD64 (Linux)用なら
```
python3 add.py -m nasm > add_s.nasm
nasm -f elf64 add_s.nasm
```
Windows用なら
```
python3 add.py -m nasm -win > add_s.nasm
nasm -f win64_s.nasm
```
としてください。

## 例2. AVXを使う例
```c
void add_avx(float *z, const float *x, const float *y, size_t n)
{
  assert(n > 0 && (n % 4) == 0);
  for (size_t i = 0; i < n; i++) z[i] = x[i] + y[i];
}
```
という関数をAVXを使って実装してみましょう。説明を簡単にするため端数処理は省きます（nは正の4の倍数とする）。
関数本体部分だけ抜き出します。

```python
  with FuncProc('add_avx'):
     with StackFrame(4, vNum=1, vType=T_XMM) as sf:
      pz = sf.p[0]
      px = sf.p[1]
      py = sf.p[2]
      n = sf.p[3]
      lpL = Label()

      L(lpL)
      vmovups(xmm0, ptr(px))
      vaddps(xmm0, xmm0, ptr(py))
      vmovups(ptr(pz), xmm0)
      add(px, 16)
      add(py, 16)
      add(pz, 16)
      sub(n, 4)
      jnz(lpL)
```

説明
- `with StackFrame(4, vNum=1, vType=T_XMM) as sf:`
  - `vNum=1` : SIMDレジスタを1個使う。
  - `vType=T_XMM` : XMMレジスタを使う。
    - これらの値はSIMDレジスタの退避・復元に影響します。

足し算関数と同様にこのコードからASM出力してC++から呼び出すとちゃんと動作します。なおこの例はAVXの説明のためで高速ではありません。

## 例3. メモリ参照
[mem.py](https://github.com/herumi/s_xbyak/blob/main/sample/mem.py)

```python
  init(param)
  segment('data')
  global_('g_x')
  dd_(123)
  segment('text')

  with FuncProc('inc_and_add'):
    with StackFrame(1) as sf:
      inc(dword(rip+'g_x'))
      y = sf.p[0]
      mov(eax, ptr(rip+'g_x'))
      add(rax, y)

  term()
```

説明
- `segment('data')` : `data`領域を設定します。
- `global_('g_x')` : `g_x`という名前を他のファイルから参照できるようにします。
- dd_(123)` : 32ビット整数を配置します。
  - `db_` : 8ビット
  - `dw_` : 16ビット
  - `dd_` : 32ビット
  - `dq_` : 64ビット
`inc(dword(rip+'g_x'))` : メモリに対するincはサイズを指定するため`ptr`ではなく`dword`を指定してください。
  - `byte` : 1バイト
  - `word` : 2バイト
  - `dword` : 4バイト
  - `qword` : 8バイト
- 変数`g_x`へのアクセスは`rip+名前`の形にしてください。これはRIP参照することでLinux/Windows/Intel macOSで動作できるからです。

## 例4. AVX-512

### マージマスキング
マスクレジスタは`k1`, ..., `k7`を使います。

- `vaddps(xmm1 | k1, xmm2, xmm3)`
- `vmovups(ptr(rax+rcx*4+123)|k1, zmm0)`

### ゼロマスキング

- `vsubps(ymm0 | k4 | T_z, ymm1, ymm2)`

### ブロードキャスト

- `vmulps(zmm0, zmm1, ptr_b(rax))`
  - `Xbyak`と同じく`ptr_b`で自動的に`{1toX}`のXが決定されます。

### 丸め制御

- `vdivps(zmm0, zmm1, zmm2|T_rz_sae)`

### 例外の抑制

- `vmaxss(xmm1, xmm2, xmm3|T_sae)`

### `m128`と`m256`の区別

メモリサイズを指定したい場合は`ptr`や`ptr_b`の代わりに`xword` (128-bit), `yword` (256-bit), `zword` (512-bit)を使ってください。
```python
vcvtpd2dq(xmm16, xword (eax+32)) # m128
vcvtpd2dq(xmm0, yword (eax+32)) # m256
vcvtpd2dq(xmm21, ptr_b (eax+32)) # m128 + broadcast
vcvtpd2dq(xmm19, yword_b (eax+32)) # m256 + broadcast
```

## その他の例
細かい使い方は[test/misc.py](https://github.com/herumi/s_xbyak/blob/main/test/misc.py)や[test/string.py](https://github.com/herumi/s_xbyak/blob/main/test/string.py)などを参照してください。

## まとめ
ASM生成コードツール`s_xbyak`を紹介しました。既に自分のプロジェクト[mcl](https://github.com/herumi/mcl)や[fmath](https://github.com/herumi/fmath)などの既存のコードの一部を`s_xbyak`を使って書き直しています。
まだしばらく安定するまでは文法の破壊的変更があると思いますが、興味ある方は試して感想を頂けると幸いです。

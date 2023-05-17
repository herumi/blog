---
title: "x64用主要アセンブラの構文差異クイズ"
emoji: "❓"
type: "tech"
topics: ["x64", "ASM", "Python", "Xbyak"]
published: true
---
## 初めに
これはx64用JITアセンブラ[Xbyak](https://github.com/herumi/xbyak)や静的アセンブラ[s_xbyak](https://github.com/herumi/s_xbyak)を開発するときに、各種アセンブラの差異についてはまったり調べたりしたことをまとめるにあたり、せっかくなのでクイズ形式にしたものです。
中級以降は主にAVX-512に関するかなりマニアックで瑣末な知識です。何を聞かれてるのか分からなくても殆どの場合、何の問題もありません。

## 前置き
ここで扱うアセンブラは
- GAS (GNU Assembler) 2.38
- [Netwide Assembler (NASM)](https://www.nasm.us/) 2.16
- [Microsoft Macro Assembler](https://learn.microsoft.com/vi-vn/cpp/assembler/masm/microsoft-macro-assembler-reference) 14.35.32217.1

です。MASMはWindows専用、GASは主にLinuxで扱われますが、Windowsでもmingwなどで使えます。NASMはwin64, elf64, macho64 (Intel macOS)用のオブジェクトコードを生成できます。
問題は概ねIntelの[SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)か[XED](https://intelxed.github.io/)の表記にしたがいます。GAS/NASM/MASMでどう書くかを答えてください。自分が慣れてるアセンブラのみの回答でもOKです。

s_xbyakでは、アドレス参照についてXbyakの`ptr[...]`が`ptr(...)`になる以外は概ね同じです。またアセンブラの中で`{}`を使う部分はC++やPythonでは扱えないので`|`を使い、特殊なシンボルの先頭には`T_`をつけています。

問題 : 以下の文章に相当するASMを書いてください。

## 初級編

### Q1
movでraxにrbxの値をコピーする。[答えA1](#a1)

### Q2
raxの値に0x123をaddする。[答えA2](#a2)

### Q3
raxで指定されたアドレスの64bit整数をincする。[答えA3](#a3)

### Q4
ラベル`buf`という名前がつけられた8byteの整数データが定義されているとき、それをRIP相対でraxに読み込む。[答えA4](#a4)

## 中級編

### Q5
アドレスraxにあるfloat値をブロードキャストしてzmm1にvaddpsしてzmm0に代入する。[答えA5](#a5)

### Q6
zmm1のfloat値16個を負の無限大方向(rd-sae)に丸めてzmm0にvcvtps2dqする。[答えA6](#a6)

### Q7
zmm1のfloat値16個を例外抑制しつつ整数値に銀行丸め(.5は偶数に丸める)でzmm0にvrndscalepsする。[答えA7](#a7)

## 上級編

### Q8
アドレスraxにあるdouble値2個をxmm0にcvtpd2dqする。[答えA8](#a8)

### Q9
アドレスraxにあるdouble値をm256としてブロードキャストしてxmm0にcvtpd2dqする。[答えA9](#a9)

### Q10
第2世代Xeon SPで使えるようになったAVX-512 VNNI命令vpdpbusdをAVX-512をサポートしていないAlder Lakeで使えるようにAVX-VNNIとしてエンコードする。[答えA10](#a10)

## 回答

### A1
- Xbyak : `mov(rax, rbx)`
- MASM : `mov rax, rbx`
- NASM : `mov rax, rbx`
- GAS : `mov %rbx, %rax`

GASは引数の順序がIntelのマニュアルと逆順になり、レジスタには`%`をつける必要があります。[問題1](#q1)

### A2
- Xbyak : `add(rax, 0x123)`
- MASM : `add rax, 123h`
- NASM : `add rax, 0x123`
- GAS : `add $0x123, %rax`

MASMは`0x...`という16進数表記はエラーで、`...h`という表記でなければなりません。
GASは数値の先頭に`$`をつける必要があります。[問題2](#q2)

### A3
- Xbyak : `inc(qword[rax])`
- MASM : `inc qword ptr[rax]`
- NASM : `inc qword[rax]`
- GAS : `incq (%rax)`

サイズを明示するときはGAS以外はメモリ側につけるのですがGASはニーモニックの後ろに添え字をつけます。[問題3](#q3)

### A4
- s_xbyak : `mov(rax, ptr(rip+'buf'))`
- MASM : `mov rax, qword ptr buf`
- NASM : `mov rax, [rel buf]`
- GAS : `mov buf(%rip), %rax`

RIP相対はx64で現在の場所から32bit相対でアドレスを指定する方法です。共有ライブラリではPIC（位置独立コード）のためにRIP相対を使うのが望ましいです。
WindowsでRIP相対を使わないと[/LARGEADDRESSAWARE:NO](https://learn.microsoft.com/ja-jp/cpp/build/reference/largeaddressaware-handle-large-addresses?view=msvc-170)が必要になったり、macOSではエラーになったりします。

MASMのmovは`[]`を無視するので注意してください。

機能|NASM | MASM
-|-|-
bufの64bit絶対アドレスをraxに入れる| `mov rax, buf` | `mov rax, buf` or `mov rax, [buf]`
bufの下位32bitアドレスにある値をraxに入れる| `mov rax, [buf]` | 無い?
bufへのRIP相対アドレスにある値をraxに入れる| `mov rax, [rel buf]` | `mov rax, qword ptr [buf]` or `mov rax, qword ptr buf`

[問題4](#q4)

### A5
- Xbyak : `vaddps(zmm0, zmm1, ptr_b[rax])`
- MASM : `vaddps zmm0, zmm1, dword bcst [rax]`
- NASM : `vaddps zmm0, zmm1, [rax]{1to16}`
- GAS : `vaddps (%rax){1to16}, %zmm1, %zmm0`

1個の要素をレジスタ全体にブロードキャストするにはIntelの表記では`{1toX}`を使います。
今回はfloat(32bit)をzmmレジスタ(512bit)にブロードキャストするので`X = 512/32=16`となります。GASとNASMは`{1to16}`という属性が付与されています。
しかし、本来ニーモニックとレジスタから`X`の値は一意に決まります。そこでXbyakでは`ptr_b`を採用し、自動的に設定されるようにしました（`b`はbroadcastから）。
MASMも恐らく同様の考えにより`bcst`を使います。ただしMASMは元のデータサイズ（今回は`dword`）を指定する必要があります。
s_xbyakでは`ptr_b`から`{1toX}`を求めるために、Intel SDMからニーモニックのパターンを全て抜き出してテーブルを作って対応しました。なかなか面倒でした。[問題5](#q5)

### A6
- Xbyak : `vcvtps2dq(zmm0 | T_rd_sae, zmm1)` または `vcvtps2dq(zmm0, zmm1 | T_rd_sae)`
- MASM : `vcvtps2dq zmm0, zmm1{rd-sae}`
- NASM : `vcvtps2dq zmm0, zmm1, {rd-sae}`
- GAS : `vcvtps2dq {rd-sae}, %zmm1, %zmm0`

Intel XEDによる逆アセンブラは`vcvtps2dq zmm0{rd-sae}, zmm1`を出力しました。アセンブラによって`{rd-sae}`の指定位置が異なります。
Xbyakでは場所に悩まないように、どちらのレジスタにつけてもOKにしています。
MASMはIntelの表記に似てレジスタと`{rd-sae}`の間にコンマはないのですが、NASMは何故かコンマが必要です。`{1toX}`はコンマが要らないので不思議ですね。
GASは`{rd-sae}`はレジスタじゃないのにそれを含めて逆順にしているので違和感があります。[問題6](#q6)

### A7
- Xbyak: `vrndscaleps(zmm0|T_sae, zmm1, 0)` または `vrndscaleps(zmm0, zmm1|T_sae, 0)`
- MASM : `vrndscaleps zmm0, zmm1, 0{sae}`
- NASM : `vrndscaleps zmm0, zmm1, {sae}, 0`
- GAS : `vrndscaleps $0, {sae}, %zmm1, %zmm0`

前問の類題で即値を取る場合です。Intel XEDによる逆アセンブラは`vrndscaleps zmm0{sae}, zmm1, 0`を出力しました。やはりアセンブラごとに`{sae}`の場所が違います。NASMやGASは`{sae}`が端にあるというわけではないのですね。[問題7](#q7)

### A8
- Xbyak : `vcvtpd2dq(xmm0, ptr[rax])` または `vcvtpd2dq(xmm0, xword[rax])`
- MASM : `vcvtpd2dq xmm0, xmmword ptr [rax]`
- NASM : `vcvtpd2dq xmm0, oword [rax]`
- GAS : `vcvtpd2dqx (%rax), %xmm0`

`vcvtpd2dq(xmm, mem)`は`m128`と`m256`によって挙動が変わります。ニーモニックとレジスタだけでメモリのサイズを特定できない例外パターンの一つです。そのためメモリサイズを指定する必要があります。
Xbyakでは`ptr`を使うと`xword`として扱います。`m256`にしたい場合は`yword`を使います。
NASMは`xword`ではなく`oword`を指定しなければなりません。`yword`や`zword`はそのままなので`xword`にしてほしかったところです。GASはニーモニックに`x`をつけます（m256なら`y`、m512なら`z`）。以前objdumpのバグを見つけて[報告した](https://lists.gnu.org/archive/html/bug-binutils/2018-04/msg00002.html)ことがあります。

サイズ指定子一覧

asm|8|16|32|64|128|256|512
-|-|-|-|-|-|-|-
Xbyak|byte|word|dword|qword|xword|yword|zword
MASM|byte ptr|word ptr|dword ptr|qword ptr|xmmword ptr|ymmword ptr|zmmword ptr
NASM|byte|word|dword|qword|oword|yword|zword
GAS|b|w|l|q|x|y|z

GASの32bitが`d`ではなく`l`なのに注意。`l`だとlongで64bitかと勘違いしそうです。[問題8](#q8)

### A9
- Xbyak : `vcvtpd2dq(xmm0, yword_b[rax])`
- MASM : 未対応
- NASM : `vcvtpd2dq xmm0, [rax]{1to4}`
- GAS : `vcvtpd2dq (%rax){1to4}, %xmm0` または`vcvtpd2dqy (%rax){1to4}, %xmm0`

Xbyakではywordとしてブロードキャストするために`yword_b`を使います（ここでは`ptr_b`を使うと`xword_b`となります）。
GASやNASMでは`{1to4}`があるのでm256とわかるためにサイズを指定する必要はありません。
MASMは`vcvtpd2dq xmm0, qword bcst ymmword ptr [rax]`と書いてもm128のブロードキャスとして扱われたので[バグ報告](https://developercommunity.visualstudio.com/t/ml64exe-cant-deal-with-vcvtpd2dq-xmm0/10352105)しました。そのうち改善されると思います。[問題9](#q9)

### A10
- Xbyak : `vpdpbusd(xmm0, xmm1, xmm2, VexEncoding)`
- MASM : `vex vpdpbusd xmm0, xmm1, xmm2`
- NASM : 未対応?
- GAS : `{vex} vpdpbusd %xmm2, %xmm1, %xmm0`

デフォルトでは`{evex}`や`EvexEncoding`を指定したのと同じです。
NASMでは`{evex}`は指定できてAVX-512としてエンコードされましたが、`{vex}`はエラーになりました。現時点で未対応なのかもしれません（マニュアルには`{vex2}`や`{vec3}`という表記があったのですがどれもエラー）。[問題10](#q10)

## まとめ
s_xbyakのAVX-512関係の実装のために他のアセンブラでの記法を調べたのですが、改めて構文に細かな違いがあるのに気付きました。
とりあえずs_xbyakでこれらの差異は吸収できたので、今後（私は）開発が多少楽になるでしょう。
バグや未対応な機能もちらほらありますね。回答に書いた以外ではMASMでAVX-512 FP16周りのvcvttsh2usiなどで`{sae}`を指定できない[バグ](https://developercommunity.visualstudio.com/t/ml64exe-does-not-assemble-vcvttsh2usi-w/10351750)を見つけたりもしました。

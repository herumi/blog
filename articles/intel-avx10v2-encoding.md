---
title: "Intel AVX10.2の複雑怪奇な命令体系とエンコーディング事情"
emoji: "📖"
type: "tech"
topics: ["EVEX", "AVX512", "AVX10v2", "Xbyak"]
published: true
---
## 初めに

AVX10は256ビットSIMDしか搭載しない非AVX-512 CPUでもAVX-512の機能や命令を扱えるようにした命令セットです。AVX-512は新しく命令が追加されることはなく、今後はAVX10で256/512ビットSIMDを統一的に扱えるようにすると宣言されています。
2024年7月に登場した[Intel AVX10.2](https://www.intel.com/content/www/us/en/content-details/836199/intel-advanced-vector-extensions-10-2-intel-avx10-2-architecture-specification.html)ではいろいろな命令が追加されています。
概要はmod_poppoさんの[AVX10.2の新機能](https://zenn.dev/mod_poppo/articles/new-features-of-avx10-2)などを参照していただくとして、ここでは[Xbyak](https://github.com/herumi/xbyak)をAVX10.2に対応させるときに戸惑った部分を紹介します。

## AVX-512 VNNIとAVX-VNNI

2019年に登場したIce Lake でサポートされた AVX-512 VNNI には `vpdpbusd` という `uint8_t` と `int8_t` の積和命令があります。名前はの途中の `us` は `Unsigned` × `Signed` の意味。
その後、AVX-512がサーバ用途向けに舵を切り、コンシューマ向けCPUには256ビットSIMD（YMMレジスタ）までしか載せなくなってから、YMM向けのAVX-VNNIが登場しました。
例えば `vpdpbusd(ymm1, ymm2, ymm3)` はAVX-512 VNNIでは `62F26D2850CB` に、AVX-VNNIでは `C4E26D50CB` にエンコードにしなくてはなりません。
Xbyakではそれらを指定するために、Encodingパラメータを指定できるようにしています。

```cpp
vpdpbusd(ymm1, ymm2, ymm3); // AVX-512 VNNI
vpdpbusd(ymm1, ymm2, ymm3, EvexEncoding); // 明示的な指定も可能（AVX-512 VNNI）
vpdpbusd(ymm1, ymm2, ymm3, VexEncoding); // AVX-VNNI
```

生成コード

```
XDIS 0: AVX512    AVX512EVEX 62F26D2850CB             vpdpbusd ymm1, ymm2, ymm3
XDIS 6: AVX512    AVX512EVEX 62F26D2850CB             vpdpbusd ymm1, ymm2, ymm3
XDIS c: VEX       AVX_VNNI   C4E26D50CB               vpdpbusd ymm1, ymm2, ymm3
```

Encodingを省略したときのデフォルトエンコーディング設定は `setDefaultEncoding` で指定できます。以前のブログ記事 [encodingの選択](https://zenn.dev/herumi/articles/granite-rapids-sierra-forest#encoding%E3%81%AE%E9%81%B8%E6%8A%9E) や Xbyakのマニュアル [Selecting AVX512-VNNI, AVX-VNNI, AVX-VNNI-INT8, AVX10.2](https://github.com/herumi/xbyak/blob/master/doc/usage.md#selecting-avx512-vnni-avx-vnni-avx-vnni-int8-avx102) 参照。

実はgasでもアセンブラのニーモニックの前に `{vex}` という表記をつけられます（豆知識）。

```bash
% cat a.s
vpdpbusd %ymm3, %ymm2, %ymm1
{vex} vpdpbusd %ymm3, %ymm2, %ymm1
```

```bash
gcc -c a.s
% objdump -d a.o -M intel

a.o:     file format elf64-x86-64


Disassembly of section .text:

0000000000000000 <.text>:
   0:   62 f2 6d 28 50 cb       vpdpbusd ymm1,ymm2,ymm3
   6:   c4 e2 6d 50 cb          {vex} vpdpbusd ymm1,ymm2,ymm3
```

## AVX10.2の整数とFP16のVNNIやメディア新命令

さて、AVX10.2ではFP16（16ビット浮動小数点数）や8ビット整数の積和演算のための新たな命令が追加されています。
たとえば前節の `vpdpbusd` に非常によく似た名前の `vpdpbsud` があります。こちらは  `Signed` × `Unsigned`。引数をひっくり返せば `us` と同じ挙動になる（と思う。多分）。
`vpdpbsud` は AVX-VNNI-INT8 のカテゴリーで2022年9月の [Intel Architecture Instruction Set Extensions and Future Features](https://cdrdv2-public.intel.com/671368/architecture-instruction-set-extensions-programming-reference.pdf) で登場しました。しかし、Intel SDMには先日2024年10月14日に公開されたバージョンになるまで載っていなかったのでうっかり忘れやすいです。対応CPUはSierra Forestなどから。
今回、これらの命令がAVX10.2で対応しました。今までがAVX-512 (EVEX) ではなくAVX-VNNI-INT8 (VEX) エンコーディングだったのが、AVX10のEVEXエンコーディング版が追加されたわけです。

```cpp
vpdpbsud(xmm1, xmm2, xmm3);
```

```bash
XDIS 5: VEX       AVX_VNNI_INT8 C4E26E50CB               vpdpbsud ymm1, ymm2, ymm3 # AVX-VNNI-INT8
XDIS a: AVX512    AVX512EVEX 62F26E2850CB             vpdpbsud ymm1, ymm2, ymm3 # AVX10.2
```

同様に16ビット整数の積和演算のための命令 AVX-VNNI-INT16 も拡張されています。

```cpp
vpdpwsud(xmm1, xmm2, xmm3);
```

```bash
XDIS 5: VEX       AVX_VNNI_INT16 C4E26ED2CB               vpdpwsud ymm1, ymm2, ymm3 # AVX-VNNI-INT8
XDIS a: AVX512    AVX512EVEX 62F26E28D2CB             vpdpwsud ymm1, ymm2, ymm3 # AVX10.2
```

# Xbyakでの扱いを決めるときの試行錯誤

さて、従来のAVX-VNNI-INT8（や16）がVEXエンコーディングで、今回追加されたAVX10.2はEVEXエンコーディングなので、最初は素直に

```cpp
vpdpbsud(xmm1, xmm2, xmm3);
vpdpbsud(xmm1, xmm2, xmm3, VexEncoding);
vpdpbsud(xmm1, xmm2, xmm3, EvexEncoding);
```

と指定できるように設計しました。デフォルトはVEXにしました。すると、前節の`vpdpbusd`のテストが失敗しました。
`setDefaultEncoding`のデフォルト値を変更してしまったので、おかしくなってしまったのです。

表にすると次のようになります。

命令|最初に登場したもの|次に追加されたもの|あるべきデフォルト値
-|-|-|-
vpdpbusd|AVX-512 VNNI (EVEX)|AVX-VNNI (VEX)|EVEX
vpdpbsud|AVX-VNNI-INT8 (VEX)|AVX10.2 (EVEX)|VEX
vpdpwsud|AVX-VNNI-INT16 (VEX)|AVX10.2 (EVEX)|VEX

当たり前といえば当たり前なのですが、`vpdpbusd`と`vpdpbsud`のデフォルト値は独立に設定できないといけないのです。
同じ`setDefaultEncoding`を使っては駄目なのですね。仕方がないので`setDefaultEncodingAVX10`という新たなデフォルト設定関数を追加しました。名前がダサいがそんな風に拡張されるとは思ってなかったので後方互換性のためには仕方がないです。

`vpdpbusd` のカテゴリーは `vpdpbusd`, `vpdpbusds`, `vpdpwssd`, `vpdpwssds` の4個。
`vpdpbsud` のカテゴリーは `vmpsadbw`, `vpdpbssd`, `vpdpbssds`, `vpdpbsud`, `vpdpbsuds`, `vpdpbuud`, `vpdpbuuds`, `vpdpwsud`, `vpdpwsuds`, `vpdpwusd`, `vpdpwusds`, `vpdpwuud`, `vpdpwuuds` の13個です。

目がちかちかしますね。

# 最後の親玉 `vmovd` と `vmovw`
AVX10.2の新規命令は100種類近くあります。ちまちまと対応していて、仕様書の最後の `vmovd` でやられました（421ページ目）。
`vmovd` は32ビット値をSIMDレジスタに0拡張する命令なのですが、既にAVX (VEX),  AVX512かAVX10.1 (EVEX) のエンコーディングが定義されています。

命令|昔|現状|今回登場
-|-|-|-
vmovd|AVX|AVX512F/AVX10.1|AVX10.2
vmovw|-|AVX512-FP16/AVX10.1|AVX10.2

しかし、今回AVX10.2で新たな EVEX エンコーディングが追加されているのです。つまり、`VexEncoding` と `EvexEncoding` では区別がつかないのです。
こいつのためには AVX10.2 かそれより以前かで区別するしかありません。

`vmovd` と `vmovw` だけ特別扱いするかどうか悩んだのですが、`AVX10v2Encoding` と `PreAVX10v2Encoding` （AVX10.2以前）というフラグを用意して、AVX10.2で追加された命令は一律これらのフラグで指定するように決めました。今後は `AVX10v3Encoding` みたいなのが増えていくだけだろうという楽観的な予測です。
デフォルト値は `setDefaultEncodingAVX10` を使います。

```cpp
vpdpbsud(xmm1, xmm2, xmm3);
vpdpbsud(xmm1, xmm2, xmm3, PreAVX20v2Encoding);
vpdpbsud(xmm1, xmm2, xmm3, AVX20v2Encoding);

vmovd(xmm1, xmm2, PreAVX20v2Encoding);
vmovd(xmm1, xmm2, AVX20v2Encoding);
```

この仕様がよかったのかは今後のIntelの拡張方法によりますが……。

ちなみに

```cpp
movd(ptr[rax+128], xmm1); // SSE
vmovd(ptr[rax+128], xmm1); // AVX
vmovd(ptr[rax+128], xmm30, PreAVX10v2Encoding); // AVX512 or AVX10.1
vmovd(ptr[rax+128], xmm30, AVX10v2Encoding); // AVX10.2
```

は

```bash
XDIS 0: DATAXFER  SSE2       660F7E8880000000         movd dword ptr [rax+0x80], xmm1
XDIS 8: DATAXFER  AVX        C5F97E8880000000         vmovd dword ptr [rax+0x80], xmm1
XDIS 10: DATAXFER  AVX512EVEX 62617D087E7020           vmovd dword ptr [rax+0x80], xmm30
XDIS 17: DATAXFER  AVX512EVEX 62617D08D67020           vmovd dword ptr [rax+0x80], xmm30
```

となります。`movd`のオペコードはSSE時代から `0x7E` だったのですが、何故か `0xD6` に移動しました（mem-to-regは`0x7E`）。ややこしいですね。

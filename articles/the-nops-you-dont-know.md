---
title: "あなたの知らないNOPたち"
emoji: "📖"
type: "tech"
topics: ["x86", "nop", "xed"]
published: true
---
## 初めに
NOPとはNo Operationというx86における何もしない命令です。
ここでは多種多様なNOPを紹介します。

## 1バイトの何もしない命令
太古の8086時代からxchg r1, r2というレジスタr1とr2の中身を交換する命令 (exchange) がありました。xchgはr1とr2が同じレジスタのときは同じレジスタを入れ換えるので実質何もしません（フラグも変更しない）。
そしてxchg ax, axの機械語が90h（16進数）という1バイトだったのでnopはxchg ax, axのエイリアスと扱われるようになります。nopはプログラムの特定の命令を無効化（必要なだけ90hで埋める）やデータのパディングに利用されていました。またCPUのデコーダ処理の効率化のためにジャンプ先をアライメント（2のべきの倍数）するときにも使われるようになります。

```x86asm
# .lpのアドレスが16バイトアライメントされるようにnopを挿入する
nop
nop
...
nop
.lp:
    add eax, [edx] # 適当なループのサンプル
    add edx, 4
    dec ecx
    jnz .lp
```

## 複数バイトの何もしない命令
nopは何も影響がないとはいえ、1バイトnopは実質xchg命令なので実行コストがかかります。複数のnopはそれだけ時間がかかり無駄なので、たとえば4個のnopを使う代わりに1個の何もしない命令を入れられるとうれしいです。
x86にはオペランドサイズプレフィクス66hというものがあり、これをつけるとオペランドサイズが16bitモードなら32bit, 32/64bitモードなら16bitになります。nopにつけても何もしない挙動は変わりませんが、66h 90hとして2バイトnopとして利用できました。

私は他に次のような命令をnop代わりに使ったことがあります。

長さ|asm|バイト
-|-|-
3|lea eax, [eax]|8D 04 20h
4|lea eax, [eax+0x00]|8D 44 20 00h
6|lea eax, [eax+0x00000000]|8D 80 00 00 00 00h

## マルチバイトNOP
実は複数バイトで表現できるnop（マルチバイトNOP）がPentiumPro (1995) から導入されていました（PentiumPro以前で実行すると未定義例外が発生）。
しかしIntelのマニュアルに正式に掲載されたのは2006年3月 (IA-32 Intel Architecture Software Developer's Manual 253667-019) からのようです（253667-018には載ってない）。

*IntelのSDMより引用して表記の改変*

長さ|asm|バイト
-|-|-
2|66 nop|66 90h
3|nop [eax]|0F 1F 00h
4|nop [eax + 00h]|0F 1F 40 00h
5|nop [eax + eax*1 + 00h]|0F 1F 44 00 00h
6|66 nop [eax + eax*1 + 00h]|66 0F 1F 44 00 00h
7|nop [eax + 00000000h]|0F 1F 80 00 00 00 00h
8|nop [eax + eax*1 + 00000000h]|0F 1F 84 00 00 00 00 00h
9|66 nop [eax + eax*1 + 00000000h]|66 0F 1F 84 00 00 00 00 00h

実はAMDのSoftware Optimization Guide for AMD Athlon 64 and AMD Opteron Processors 25112 Rev 3.04（2004年3月）には前節の66hプレフィクスを沢山つける方法が載っていました。

*AMDのAhtlon64用マニュアルより引用して表記の改変*
長さ|バイト
-|-
1| 90h
2| 66 90h
3| 66 66 90h
4| 66 66 66 90h
5| 66 66 90 66 90h
6| 66 66 90 66 66 90h
7| 66 66 66 90 66 66 90h
8| 66 66 66 90 66 66 66 90h
9| 66 66 90 66 66 90 66 66 90h

しかし、IntelとAMDで異なるNOPが推奨されていては扱いづらいです。Intelのマニュアルで公式化され、多分どこかのタイミング（未調査）でAMDもそれに追従することで安心して使えるようになります。

少なくとも2016年頃のAMDのマニュアルには載ってました（今手元に無いので確認できず）。
2023年のSoftware Optimization Guide for the AMD Zen4 Microarchitectureでは9バイトまではIntelと同一のマルチバイトNOPが掲載されています。
AMDのマニュアルでは更に10~15バイトでは66hプレフィクスをその分だけ追加する方法が掲載されています。

*AMDのZen4用マニュアルより引用して表記の改変*
長さ|バイト
-|-
10|66 66 0F 1F 84 00 00 00 00 00h
11|66 66 66 0F 1F 84 00 00 00 00 00h
12|66 66 66 66 0F 1F 84 00 00 00 00 00h
13|66 66 66 66 66 0F 1F 84 00 00 00 00 00h
14|66 66 66 66 66 66 0F 1F 84 00 00 00 00 00h
15|66 66 66 66 66 66 66 0F 1F 84 00 00 00 00 00h

マルチバイトNOPは32bit/64bit両環境で使え、マニュアルに掲載されることで安心して使えるようになります。

10バイト以上のマルチバイトNOPはIntelのマニュアルには載っていません。しかし、Intel xedで-chip-check PENTIUMPROを指定して逆アセンブルできたので大丈夫でしょう。
手元のいくつかのIntel CPUで試しても大丈夫でした。また9バイト+6バイトマルチバイトNOPを使うより15バイトマルチバイトNOPを使う方が速かったです。
ただしZen4のマニュアルにもありますが、古いCPUでは66プレフィクスのつけすぎ（4個以上）はペナルティを食らう場合があります。

## x64で本当に何もしない命令
話は2003年にさかのぼり、x86で最初に64bit化 (x86-64) したときnopが本当のnopになりました。

何を言ってるのかというと、nopがxchg ax, axのエイリアスだったことを思い出しましょう。32bit化したときにこれはxchg eax, eaxの意味となりましたが何もしないのは変わりませんでした。
しかし、64bit化したときxchg eax, eaxは「何もしない命令」ではなくなったのです。

x86-64ではxchgに限らず一般的に32bitレジスタの操作は対応する64bitレジスタの上位32bitを0クリアするという仕様が入りました。
「64bitレジスタの上位32bitを触らない」という仕様にすることもできましたが、そうすると余計な依存関係が発生したり、パーシャルレジスタストールを起こすなど扱いづらかったから避けたと思われます。
そのためxchg eax, eaxは「何もしない命令」ではなく「raxの上位32bitをクリアする命令 (movzx rax, eax)」に変わってしまったのです。
しかたなくxchg eax, eaxとnopを別の命令とし、90hをnop専用に割り当てることで対応しました。xchg eax, eaxは異なるバイトコードにエンコードされるようになりました。

*xchgの機械語*
bit|xchg ax, ax|xchg eax, eax|xchg rax, rax
-|-|-|-
16|90h|66 90h|-
32|66 90h|90h|-
64|66 90h| 87 C0h （nopではない）|48 90h

## 古いCPUでは何もしない新しい命令
新しいCPUが登場する度に、新しい命令が追加されています。基本的に新しい命令を使うときはcpuidなどを利用してその命令が使えるときのみ使えるようにします。しかし、その方法は条件分岐が増えて面倒なことが多いです。

そこで一部の命令は古いCPUでは何もしない命令として動作するように拡張していることがあります。それらの例をいくつか紹介しましょう。

### prefetch
prefetch*命令はメモリの先読みを指示する命令でPentium3で導入されました。キャッシュ階層に応じて4種類あります。これらの命令は非対応CPUではnopとして扱われます。

命令|対象キャッシュレベル
-|-
prefetcht0|L1, L2, L3すべて
prefetcht1|L2, L3
prefetcht2|L3
prefetchnta|non-temporal （キャッシュ汚染を最小化）

### pause
pause命令はスピンロック（OSに処理を譲らずにローカルに処理する排他制御）におけるCPUのリソースを減らすための命令でPentium4で導入されました。リソースを減らすだけで演算自体には影響しません。ループ内で毎回pause命令自体が使えるか分岐が入っていると本末転倒になってしまいます。pause命令はF3 90hでやはり非対応CPUではnopとして扱われます。

```x86asm
.lp:
   cmp byte [lock], 0
   je .exit
   pause
   jmp .lp
```

### endbr32/endbr64
endbr32/endbr64命令はControl-flow Enforcement Technology (CET)というセキュリティ機構の一部で2020年のTiger Lakeから導入されました。CET非対応な環境ではnopとして扱われます。

```c
int f(int x) { return x + 3; }
```
みたいなコードをコンパイルしたら、

```x86asm
f:
    endbr64
    lea eax, 3[rdi]
    ret
```

というコードが生成されてendbr64って何?と思われた方もいらっしゃるのではないでしょうか。

命令|バイトコード
-|-
endbr32|F3 0F 1E FBh
endbr64|F3 0F 1E FAh

CETが有効なときにcall/jmpの間接ジャンプ先がこの命令でないと#CP(control protection exception)が発生します。CET非対応CPUではendbr64はnopとして扱われるので、CETが有効なときはセキュリティ機構が働き、無効なときは何もしないという動作になります。

Intel MPX(Memory Protection Extensions)という配列境界を実行時にチェックする仕組みでbndldkといった命令があり、非対応CPUではnopになるのですが、そもそもあまり普及しなかったためdeprecatedになってしまいました。

### 最近のprefetch系
命令の先読みをより細かく制御するためのprefetchit0, prefetchit1がGranite Rapids (2024)で対応されました。
2026年末に登場予定のDiamond RapidsやNova Lakeは、自分だけでなく他のコアも読むかもしれないときに先読みするprefetchrst2に対応するそうです。

これらの命令は新しいため、Intel xed(v2026.02.17)で逆アセンブルしてもデフォルトではnopと表示されます。

```cpp
struct Code : Xbyak::CodeGenerator {
    Code()
    {
        prefetchit0(ptr[rip+8]);
        prefetchit1(ptr[rip+8]);
        prefetchrst2(ptr[rax]);
    }
};
```

生成コードを逆アセンブルすると全部nopと表示されます。

```sh
% xed -64 -ir bin
XDIS 0: WIDENOP   BASE       0F183D08000000           nop dword ptr [rip+0x8]
XDIS 7: WIDENOP   BASE       0F183508000000           nop dword ptr [rip+0x8]
XDIS e: WIDENOP   BASE       0F1820                   nop dword ptr [rax]
```

逆アセンブルオプションでGranite Rapidsを明示するとprefetch{it0,it1}が表示されました。
```sh
% xed -64 -ir bin -chip-check GRANITE_RAPIDS
Setting chip to GRANITE_RAPIDS
XDIS 0: PREFETCH  ICACHE_PREFETCH 0F183D08000000           prefetchit0 ptr [rip+0x8]
XDIS 7: PREFETCH  ICACHE_PREFETCH 0F183508000000           prefetchit1 ptr [rip+0x8]
XDIS e: WIDENOP   BASE       0F1820                   nop dword ptr [rax]
```

Ubuntu 24.04.4 LTSのobjdump 2.42でもprefetchrst2はnopでした。

```sh
% objdump -M x86-64,intel -D -b binary -m i386 bin

bin:     file format binary
Disassembly of section .data:
00000000 <.data>:
   0:   0f 18 3d 08 00 00 00    prefetchit0 BYTE PTR [rip+0x8]        # 0xf
   7:   0f 18 35 08 00 00 00    prefetchit1 BYTE PTR [rip+0x8]        # 0x16
   e:   0f 18 20                nop    DWORD PTR [rax]
```

xedでNova Lakeを明示するとprefetchrst2も表示されました。

```sh
% xed -64 -ir bin -chip-check NOVA_LAKE
Setting chip to NOVA_LAKE
XDIS 0: WIDENOP   BASE       0F183D08000000           nop dword ptr [rip+0x8]
XDIS 7: WIDENOP   BASE       0F183508000000           nop dword ptr [rip+0x8]
XDIS e: PREFETCH  MOVRS      0F1820                   prefetchrst2 ptr [rax]
```

ちなみにUnified Acceleration (UXL) Foundationの[oneDNN](https://github.com/uxlfoundation/oneDNN)で（恐らく）Intelの人がXbyakにprefetchrst2を独自に追加していたのですが、間違えてprefetcht2になっていたのでそれを指摘したら[直してもらえました](https://github.com/uxlfoundation/oneDNN/pull/4737#issuecomment-3987163249)。最新版のXbyakでは対応してるのでそのうち本家のを使ってもらえるでしょう。
フェッチ系は間違えてても動作はするので気がつきづらいですね。
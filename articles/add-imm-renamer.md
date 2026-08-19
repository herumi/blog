---
title: "最近のIntel CPUは複数の連続する加減算命令を畳み込む"
emoji: "📖"
type: "tech"
topics: ["x64", "renamer", "optimizer", "add"]
published: false
---
## はじめに
一見、タイトルはなんのこと? と思われるかもしれませんが、そう呼びたくなる現象があるのをつい最近知りました。面白かったのでその紹介をします。なお、かなり専門的な話となりますのであしからず。

## 命令のレイテンシを測定する
x64のタイムスタンプカンターを取得する命令rdtscを使ってマイクロベンチマークをいろいろとっていました。
現在、rdtscはCPUの定格ベース周波数を基準としたクロックサイクル（以下cycと略記）を測定します。
私はCPUをフルで回したときの周波数に対するcycを測定したかったので、次の方法をとりました。

まず、add(rax, rax)みたいな1cycかかると分かっている命令を並べてそれにかかったcycを測定します。

*関数f0()*
```cpp
    static const uint64_t N = 1000000000;
    static const int UNROLL = 8;
    align(16);
    Label lpL;
    mov(ecx, N); // 10億回ループ
L(lpL);
    for (int i = 0; i < UNROLL; i++) add(rax, rax); // 8回アンロール
    dec(ecx);
    jnz(lpL);
    ret();
```

*f0()のadd 1回あたりにかかったcycを計測する*
```cpp
double measureBase(Func f)
{
  Clock clk; // rdtscを読み出すラッパｰクラス
  clk.begin();
  f();
  clk.end();
  return clk.getClock() / double(N);
}
double rate = measureBase(f0) / UNROLL;
```

連続するadd(rax, rax)は依存関係があるので必ず1cycかかります。それをUNROLL(=8)回並べてdec(ecx); / jnz(lpL);でループします。
dec+jnzはCPU内部でμオペコードに変換された後マクロフュージョンされます。addとは同時実行されるのでカウンターをみるときは無視できます。
ループ全体でかかったcycをループ回数とアンロール回数で割れば、結局measureBaseで返される値はadd 1命令にかかった定格ベースでのcycです。

たとえば、定格周波数が2GHzで実行時に4GHzで動いていればrate = 0.5となります。逆に言えば、その後rdtscで測定したcycをrateで割れば稼働時の周波数でのcycをえられます。
このようにしてimul(rax, rax);をN回ループするcycを測定すると1命令あたり2.97とでました（多少変動します）。仕様上は3cycなので正しく取得できていることが分かりました([cyc-mul-addi.cpp](https://github.com/herumi/misc/blob/main/cyc-mul-addi.cpp))。

## 現象に遭遇したきっかけ
さて、本題です。いろいろなベンチマークをとっているときに

```cpp
for (int i = 0; i < 8; i++) add(rax, 1);
```
の結果が1.57cycと出たのです。最初に述べたように連続するadd(rax, 1)は一つあたり1cycかかるので8cycとなるはずです。実際add(rax, rax)は8cycです。それが1.57cycとは変です。
ループを伸ばして16個のaddにしても3.18cycとなりました。1命令あたり0.2cycとなってしまってます。

## 他の命令でも試す
そうすると気になるのはadd(rax, 1)だけなのかということです。そこで定数値を他のにしたり、add(rax, 1); add(rax, 2); ...のように変えたり、大きな値にしたりして試しました([add-imm-renamer.cpp](https://github.com/herumi/misc/blob/main/add-imm-renamer.cpp)）。

###
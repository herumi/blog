---
title: "Intel CPUの乗算と連続する加算のフュージョン"
emoji: "📖"
type: "tech"
topics: ["x64", "asm", "optimizer", "fusion"]
published: false
---
## はじめに
[前回の記事](https://zenn.dev/herumi/articles/add-imm-fusion)で、最近のIntel CPUが連続する即値加算命令（add imm）をAllocate/Renamerの段階(RAT)で処理して実行ポートに渡すμopの数を減らす現象（ここではフュージョンと呼びます）を紹介しました。
今回はその続きです。加算の個数を1個ずつ変えたときの挙動と、レイテンシ3の乗算命令imulに続けて加算の連鎖を置いたときの挙動をSapphire Rapids（Xeon w9-3495X、Golden Coveアーキテクチャ）で調べます。
フュージョンされたaddが依存連鎖のレイテンシにほとんど寄与しないことを直接確認でき、全体の速度がレイテンシとAllocate帯域(6μops/cyc)に強く依存しているのが分かりました。

## 測定方法
前回と同じ[cyc-mul-addi.cpp](https://github.com/herumi/misc/blob/main/cyc-mul-addi.cpp)を使います（コンパイルには[Xbyak](https://github.com/herumi/xbyak)が必要です）。次の形のループでadd(rax, 1)の個数lpNを0から24まで変えて測定します。

```cpp
    mov(ecx, N); // 10億回ループ
L(lpL);
    if (mul) imul(rax, rax); // -mulオプションをつけたときだけ
    for (int i = 0; i < lpN; i++) add(rax, 1);
    dec(ecx);
    jnz(lpL);
```

cycの計測は前回と同じで、add(rax, rax) x 8のループでrdtscとの比率rateを校正してからループ1回あたりのcycを求めています。

perfのカウンタは次のオプションで取得しました。

```
perf stat -e '{instructions,uops_issued.any,uops_executed.thread,uops_retired.slots,inst_retired.macro_fused}:u' ./a.out [-mul] -lp <lpN>
```

プログラムは校正のためにadd(rax, rax) x 8のループを6回実行してから本来の測定を1回するので、前回同様、mode 0単独実行時の各カウンタの6/7を引いてループ1回（10億イテレーション）あたりに換算しています。
なお全測定を通してuops_retired.slotsはuops_issued.anyとほぼ同じ値、inst_retired.macro_fusedは1.00（dec+jnzのペア）だったので、以下の表ではこの2つを省略します。

## addの連鎖
まずimul無し、add(rax, 1) x lpNだけの結果です（詳しいデータは[fusion-plot.py](https://github.com/herumi/blog/blob/main/src/fusion-plot.py)参照）。

*add(rax, 1) x lpNの1イテレーションあたりの測定結果*

![](/images/fusion-add.png)

1ループあたりaddがlpN回、decとjnzが1回ずつで命令数instructionsはlpN+2です。uops_issued（発行されたμops）はdecとjnzがマクロフュージョンされて1μopになるのでlpN+1とほぼ線形です（グラフの青丸の破線・縦軸は左の目盛り）。

しかし、uops_executed（実行ポートに渡ったμops）の増加は緩やかでlpN=24でも約6と、addのほとんどが実行ポートに渡っていません。前回見たフュージョンです。

増え方を詳しくみると階段的です。uops_executedから1（dec+jnz分）を引いた値は、lpN=1〜5で約1μop, 6〜11で約2μops, 12〜17で約3μops, 18〜24で約4〜5μopsとAllocateのグループ数ceil((lpN+1)/6)におおよそ一致します。
つまりAllocate幅に対応した6μopごとにaddがおおむね1個だけ実行ポートに渡っています。

cycの方（縦軸は右側の目盛り）はいくつかの領域に分かれます。

- lpN=0〜5はほぼ1.0cycです。dec+jnzのみのループも1cycかかっているので、これはAllocateに関係なくループ自体の下限です。
- lpN=6, 7は1.17, 1.34cycで、これは(lpN+1)/6 = 1.17, 1.33と一致します。Golden CoveのAllocate幅は6μops/cyc（Intel最適化マニュアルの「Wider machine: 5→6 wide allocation」）なので、lpN+1個のμopをRATに通すだけで(lpN+1)/6 cycかかります。ここは純粋にAllocate帯域律速です。
- lpN=8〜11はほぼ2.0cycで一定です。上記のAllocate帯域律速なら1.50〜1.83のはずなので、2cycに量子化される別の制約がありそうです（原因は分かりません）。
- lpN=12以降は再び増加し、lpN=24で実測4.15に対しモデルは4.17なので終端ではあっていますが途中の傾きが一定ではありません。

## imulに続くaddの連鎖
次が今回の本題でループの先頭にimul(rax, rax)を1個入れます。

```cpp
L(lpL);
    imul(rax, rax);
    for (int i = 0; i < lpN; i++) add(rax, 1);
    dec(ecx);
    jnz(lpL);
```

imulとlpN個のaddはraxを介した依存連鎖があるので、フュージョンが無ければ1ループは合計(3 + lpN) cycかかるはずです。

*imul(rax, rax) + add(rax, 1) x lpNの1イテレーションあたりの測定結果*

![](/images/fusion-add-mul.png)

instructionsとuops_issuedはmulが1個増えた分それぞれ1増えてinstructions=lpN+3, uops_issued=lpN+2です。uops_executedはaddのみのときより約1μop（imulの分）多いだけで、階段状に増える形はそのままです。つまり実体化の振る舞いはaddのみのときとほぼ同じです。

それに比べてcycはかなり異なる挙動をしています。詳しく見るためにcycのみを抜き出したグラフで見てみます。

*cycの比較。(lpN+2)/6はAllocate帯域のモデル値*

![](/images/fusion-cycle.png)

cycは3本描いています。青の上三角の実線がaddのみ、オレンジ下三角の実線がimul + addで、淡い青の上三角の破線はaddのみの値を1つ左にずらした（横軸lpNの位置にaddのみのlpN+1の値を置いた）ものです。imulありのlpNとaddのみのlpN+1はuops_issuedのμop数が同じ（どちらもlpN+2個）なので、淡い青の破線とオレンジを縦に見比べると同じμop数同士の比較になります。黒の破線(lpN+2)/6もそのμop数に対するAllocate帯域のモデル値です。

cycはlpN=0から16まで一貫してほぼ3.0cycです。addのみのグラフではlpN=16はaddを16個並べて2.98cycかかっていたのにimul 1個だけのときと同じ時間しかかかっていません。その約3cyc分の処理がimul 1個のレイテンシの陰にまるごと隠れています。

コードを目で見るとその特異性が実感できるかもしれません。`imul(rax, rax);`1個と

```cpp
imul(rax, rax);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
add(rax, 1);
```

が同じ3cycで動いています。つまりこの条件ではaddによる依存連鎖の遅延が観測されません。前回の「リネーム段で基準となる物理レジスタ+小さなオフセットにフュージョンする」というモデルの正当性を補強します。

増加に転じる位置をAllocate幅6から予想するとuops_issued=lpN+2なので帯域だけで(lpN+2)/6 cycかかります。これがimulのレイテンシ3に等しくなるのはlpN=16であり、それ以降は帯域律速に切り替わります。

lpN=17以降、imul + addのlpNの値はaddのみのlpN+1の値（淡い青の破線）とよく一致しています（3.17/3.15、3.39/3.40、3.74/3.75など）。グラフでもこの領域では両者がほぼ重なり、lpN=16以前ではオレンジだけが3.0cycの床に張り付いているのが見えます。
帯域律速の領域ではimulは「μopを1個増やす」以外の寄与がなく、レイテンシ3は完全に隠れてしまうということです。
結局、全域を通して

```
cyc = max(imulのレイテンシ3, addのみで同じμop数のときの値)
```

というシンプルな形で説明できます。後者はおおむね(lpN+2)/6ですが、addのみのlpN=8〜11のように帯域モデルからずれる領域もあるので、実測値そのものと考えた方が正確です。

## まとめ
- フュージョンされたadd immは実行ポートに渡らないだけでなく、imulに対する依存連鎖への追加遅延が観測されませんでした。レイテンシ3のimul 1個の陰に、単独なら3cycかかる16個のaddの連鎖がまるごと隠れます。
- ループ全体の速度はレイテンシとAllocate帯域6μops/cycのmaxで説明できます。imulありの場合、増加に転じる位置（lpN=17）も帯域モデルの予想通りでした。
- 実行ポートに渡るaddはAllocateの6μopグループあたり1個で、フュージョンの単位はAllocateグループと考えると辻褄が合います。
- addのみのlpN=8〜11が2cycになる量子化と、lpN=12以降の帯域モデルからの振れについては機会があれば調べたいです。

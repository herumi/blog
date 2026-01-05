---
title: "OpenMPによる気軽な並列計算"
emoji: "📖"
type: "tech"
topics: ["OpenMP", "reduction", "並列計算"]
published: true
---
## 動機
最適化アルゴリズムの実験をしていると、変数のよい値を探したり、正当性チェックを確認したいことがしばしばあります。
全数探索が数分から小一時間でできるなら、状況に応じたよい探索アルゴリズムを考えるよりも100コアCPUの力業で楽したいです。

というわけで、ここではごく初歩的なOpenMPの使い方を説明します。

## parallel for
まずは一番簡単なループの並列実行です。

```cpp
const int MAX = 0x7fffffff;
void check1()
{
    for (int i = 0; i < MAX; i++) {
        if (!check(i)) {
            printf("i=%d\n", i); exit(1);
        }
    }
    puts("ok");
}
```

ここで `check(i)` は変数 `i` のみに依存する副作用のない関数でブール値を返すものとします。
`i` に関するすべてのパターンが正しく動くかをチェックするものと想定してください。

この作業は各`i`は独立で、どの順序で実行してもよいので並列実行できます（エラーになったときのiはどれか一つが分かれば十分とする）。

この場合、`for` の前にプラグマ `#pragma omp parallel for` を追加するだけでOKです。

```cpp
void check2()
{
    #pragma omp parallel for // この行を追加
    for (int i = 0; i < MAX; i++) {
        if (!check(i)) {
            printf("i=%d\n", i); exit(1);
        }
    }
    puts("ok");
}
```

## ビルド方法と実行結果
コンパイルオプションは次の通り

```sh
# Linux
% g++ sample.cpp -O3 -fopenmp

# Mac
% clang++ sample.cpp -O3 -Xpreprocessor -fopenmp -lomp -L /opt/homebrew/opt/libomp/lib/

# Windows
% cl /Ox /openmp:llvm sample.cpp
```

Linuxは `-fopenmp` のみでOKですが、Macは `-Xpreprocessor -fopenmp` としなければなりません。順序を変えて `-fopenmp -Xpreprocessor` とするとエラーになります。またリンカオプションも必要です。
Windowsはデフォルトの `/openmp` だけだと古いバージョンになるので `/openmp:llvm` をつけます（それでも古いのですが）。

手元のXeon w9-3495Xで `check1` と `check2` を実行したところ次のようになりました。

**check1, check2のベンチマーク**

プログラム|実時間|高速化率|CPU使用率|ユーザ時間
-|-|-|-|-
check1|1:18.26|-|99.9%|78.254u
check2|0:01.74|44.0|10386.7%|180.726u

元の `check1` が78秒（1:18.26）掛かっていたのが1.74秒と44倍速くなりました。CPU使用率は103倍になってます。
ユーザ時間が78秒から180秒と2.3倍に増えてるのはCPUの排他制御などに掛かった時間でしょう。`check(i)` がもっと重たい処理だったら高速化率が100倍に速くなることも多いです。

簡単で効果が絶大ですね。

## parallel for reduction
今回は `get(i)` の最大値を求める次のコードを対象にします。前節と同様 `get(i)` は変数 `i` のみに依存する副作用のない関数で、int値を返すものとします。

```cpp
void test0()
{
    int m = 0;
    for (int i = 0; i < MAX; i++) {
        int v = get(i);
        if (v > m) {
            m = v;
        }
    }
    printf("m=%d\n", m);
}
```
C++的には `if (v > m) { m = v; }` を `std::mutex` で囲んで`#pragma omp parallel for`したくなりますが、そうするとCPUが互いにロックを取り合って激遅になります。

こういったコードは次のpragmaを追加するとよいです。OpenMPが各スレッドごとに変数を用意してロックをとらずに計算し、最後に（排他制御をしながら）それらのmaxをとってくれます。

```cpp
void test1()
{
    int m = 0;
    #pragma omp parallel for reduction(max:m)
    for (int i = 0; i < MAX; i++) {
        int v = get(i);
        if (v > m) {
            m = v;
        }
    }
    printf("m=%d\n", m);
}
```

プログラム|実時間|高速化率|CPU使用率|ユーザ時間
-|-|-|-|-
test0|1:17.58|-|100.0%|77.582u
test1|0:01.76|44.0|10346.5%|181.652u

全体の傾向は前節の結果とほぼ同じです。

## maxとargmaxを同時に求める
前節の最大値を求めるときに、同時にその値を与える `i` も取得したいとします。

```cpp
void test2()
{
    int m = 0;
    int mi = 0;
    for (int i = 0; i < MAX; i++) {
        int v = get(i);
        if (v > m) {
            m = v;
            mi = i;
        }
    }
    printf("m=%d mi=%d v=%d\n", m, mi, get(mi));
}
```

この場合、更新したい変数が2個となるため `#pragma omp parallel for reduction(max:m)` は使えません。
ループを分割して、それぞれに割り当てられたスレッド上で実行する部分 (parallel) とそれぞれの結果をくっつけて一つにする操作 (critical) の二段階を明示的に記述する必要があります。

```cpp
void test3()
{
    int m = 0;
    int mi = 0;
    #pragma omp parallel
    {
        int local_m = 0; // スレッドごとのローカル変数
        int local_mi = 0;
        #pragma omp for
        for (int i = 0; i < MAX; i++) {
            int v = get(i);
            if (v > local_m) {
                local_m = v;
                local_mi = i;
            }
        }
        // スレッドの結果を元にm, miを更新
        #pragma omp critical
        if (local_m > m) {
            m = local_m;
            mi = local_mi;
        }
    }
    printf("m=%d mi=%d v=%d\n", m, mi, get(mi));
}
```

`omp parallel` と `omp critical` と似た処理をかかないといけないので面倒ですね。どちらも与えられた値と現状の値を比較して更新するという処理なので関数にしてしまいましょう。

```cpp
struct Stat {
    int m = 0;
    int mi = 0;
    void combine(const Stat& rhs)
    {
        if (rhs.m > m) {
            m = rhs.m;
            mi = rhs.mi;
        }
    }
};

void test5()
{
    Stat stat;
    #pragma omp parallel
    {
        Stat local;
        #pragma omp for
        for (int i = 0; i < MAX; i++) {
            int v = get(i);
            local.combine(Stat{v, i});
        }
        #pragma omp critical
        stat.combine(local);
    }
    int m = stat.m;
    int mi = stat.mi;
    printf("m=%d mi=%d v=%d\n", m, mi, get(mi));
}
```

## reductionをカスタマイズするdeclare reduction
OpenMP 4.0からはreductionをカスタマイズする機能があります。

```cpp
#pragma omp declare reduction(識別子: 型名: 結合器) initializer(初期設定)
#pragma omp parallel for reduction(識別子: 最終結果の変数)
```
という形で使います。先程の例では

```cpp
void test6()
{
    Stat stat;
    #pragma omp declare reduction(stat_red: Stat: omp_out.combine(omp_in)) initializer(omp_priv = Stat())
    #pragma omp parallel for reduction(stat_red:stat)
    for (int i = 0; i < MAX; i++) {
        int v = get(i);
        stat.combine(Stat{v, i});
    }
    int m = stat.m;
    int mi = stat.mi;
    printf("m=%d mi=%d v=%d\n", m, mi, get(mi));
}
```
となります。`#pragma`が長いですが、本体はすっきりしました。こちらの方が書きやすいですね。
ここで

- `stat_red`: 適当に決めた識別子
- `Stat`: ループ内で更新される変数の型
- `omp_out.combine(omp_in)`: 各スレッドごとの結果を結合する方法（今の場合は `Stat::combine` を呼び出す）
  - `omp_out`: 結合される変数
  - `omp_in`: 結合する前の変数
- `omp_priv = Stat()`: 各スレッドの初期値
- `stat`: 最終結果の変数

です。`omp_in`, `omp_out`, `omp_priv` はOpenMPで予約された名前で変更できません。
ほかにループに突入する前の変数の値 `omp_orig` があります。それを使いたい場合は `omp_priv = omp_orig` とします（今の場合はどちらも同じ）。

なお、MSVCは残念ながらVisual Studio 2026でもまだdeclare reductionに対応していません。
clang-cl.exeは対応しているのでそちらを使いましょう。

```bat
clang-cl sample.cpp -O3 -openmp
```

## （おまけ）最小のargmaxを得る
前節のコードで得られるargmaxは、どのスレッドがどういう順序で実行されるか不定なので実行するたびに変わり得ます。
最小のargmaxがほしい場合は、Statを次のように修正すればよいです。

```cpp
struct Stat {
    int m = 0;
    int mi = 0;
    void combine(const Stat& rhs)
    {
        if (rhs.m > m) {
            m = rhs.m;
            mi = rhs.mi;
        }
        if (rhs.m == m && rhs.mi < mi) { // 同じmを与えるならより小さいmiを取る
            mi = rhs.mi;
        }
    }
};
```

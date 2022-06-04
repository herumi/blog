---
title: "多倍長整数の実装5（乗算とmulx）"
emoji: "🧮"
type: "tech"
topics: ["int","mul", "mulx", "cpp", "intrinsic"]
published: true
---
## 初めに

今回はN桁x1桁の固定多倍長整数の乗算の実装の改善をします。
記事全体の一覧は[多倍長整数の実装1（C/C++）](https://zenn.dev/herumi/articles/bitint-01-cpp)参照。

## 乗算と加算の順序を入れ換える

前回の実装は筆算の通り、「掛け算してから足し算」を繰り返しました（図の左側）。

```
  2 4 7         2 4 7
x     9       x     9
-------       -------
    6 3  =>     3 6 3
  3 6         1 8 6
1 8           -------
-------       2 2 2 3
2 2 2 3
```

しかし、`x[i] * y = [H[i]:L[i]]`で求めた64bitのペアを全て一次保存しておき、掛け算が全部終わってから加算すると最初に求めた多倍長加算のコードが使えます（図の右側）。
そのやり方を実装すると次のようになります。


```cpp
template<size_t N>
Unit mulUnitT(Unit *z, const Unit *x, const Unit y)
{
  // N個の要素の配列L[N]とH[N]を用意する
  Unit L[N];
  Unit H[N];
  // 先にx[i] * yを求めてL[i]とH[i]に保存する
  for (size_t i = 0; i < N; i++) {
    L[i] = mulUnit1(&H[i], x[i], y);
  }
  z[0] = L[0]; // 一番下の桁はL[0]
  // 途中はL[]とH[]が一つずれた状態でN桁分加算する
  // 最後はH[N-1]に繰り上がりを加算する
  return H[N - 1] + addT<N - 1>(z + 1, L + 1, H);
}
```

`clang++-12 -O2 -masm=intel`でN = 4のときをbuildしてみました。

```nasm
mulUnit4:
    push    r14
    push    rbx
    mov rcx, rdx
    mov rax, rdx
    mul qword ptr [rsi]
    mov r8, rdx
    mov r9, rax
    mov rax, rcx
    mul qword ptr [rsi + 8]
    mov r10, rdx
    mov r11, rax
    mov rax, rcx
    mul qword ptr [rsi + 16]
    mov r14, rdx
    mov rbx, rax
    mov rax, rcx
    mul qword ptr [rsi + 24]
    mov qword ptr [rdi], r9
    add r11, r8
    mov qword ptr [rdi + 8], r11
    adc rbx, r10
    adc rax, r14
    mov qword ptr [rdi + 16], rbx
    mov qword ptr [rdi + 24], rax
    adc rdx, 0
    mov rax, rdx
    pop rbx
    pop r14
    ret
```

mulが4回、addが1回、繰り上がりのためのadcが3回です。
前回のコードではmulが4回、addが3回、adcが3回、setbが1回だったので大幅な改善です。演算回数としては最小ですね。
一次変数用の配列L[4]とH[4]もスタックに取らず、レジスタでやりくりしています。すばらしい。

## mulxを使う

前節のコードは演算回数最小のコードになり、文句無しなのですが一つ欠点があります。
それはNが大きくなると途中結果がレジスタに入りきらず、スタック保存しないといけなくなる点です。

本当は前回実装したように「掛け算してから足し算」を繰り返す方が一次メモリは少なくてよいのです。
しかし、x86/x64ではそれができません。なぜなら、mul命令はCFを変更する仕様だからです。
add+adcによる多倍長の繰り上がりはCFを次々と変更しながら加算するので、途中で別の命令によってCFが変わると困るのです。

```nasm
# うまく動く例
mul ...
mul ...
...
add ...
adc ...
adc ...
```

```nasm
# CFが壊されてうまくいかない例
add ... # CFがセットされる
mul ... # CFが変更される
adc ... # 間違ったCF処理をしてしまう
mul ...
adc ...
```

そこでIntelはHaswellからmulxという命令を追加しました。

```nasm
mulx H, L, x # [H:L] = x * rdx
```
mulxは3個のレジスタ(H, L, x)と暗黙のrdxを引数にとり、`x*rdx`の結果128bitを`[H:L]`に入れます。
そのときフラグを変更しません。したがってaddやadcと混在して利用できます。
コンパイラのintrinsic命令は

```cpp
// [*hi:return] = a * b
uint64_t _mulx_u64(uint64_t a, uint64_t b, uint64_t *hi);
```
です。

## adcとmulxのintrinsicによる実装

adcとmulxのintrinsic関数`_addcarry_u64`と`_mulx_u64`を組み合わせます。
最初はmulxして下位64bitはそのまま`z[0]`に書きます。上位64bitは次の下位64bitとしてLに入れます。
それから添え字iを1からN-1まで増やしつつmulxした結果をaddcarryするのです。
ループを抜けたら最後にもう一度addcarryで0とL（一つ前のmulxのH）を足してその結果を返します。

```cpp
template<size_t N>
uint64_t mulUnitT2(uint64_t *z, const uint64_t *x, uint64_t y)
{
  unsigned long long L, H;
  uint8_t c = 0;
  z[0] = _mulx_u64(x[0], y, &L);
  for (size_t i = 1; i < N; i++) {
    uint64_t t = _mulx_u64(x[i], y, &H);
    c = _addcarry_u64(c, t, L, (unsigned long long *)&z[i]);
    L = H;
  }
  _addcarry_u64(c, 0, L, &H);
  return H;
}
```

clang-12での結果を見てみましょう。mulxはBMI2という命令群に所属するのでコンパイラにその命令を使うよう指示します。
[mulunit-cpp.cpp](https://github.com/herumi/blog/blob/main/src/mulunit-cpp.cpp)

```
clang-12 -O2 -S -masm=intel -mbmi2
```

結果は
```nasm
mulUnit2:
  # rdx = y
  mulx    r9, rax, qword ptr [rsi]      # [r9:rax] = x[0] * y
  mov qword ptr [rdi], rax              # z[0] = rax
  mulx    r8, rcx, qword ptr [rsi + 8]  # [r8:rcx] = x[1] * y
  xor eax, eax                          # rax = 0
  add rcx, r9                           # rcx += r9
  mov qword ptr [rdi + 8], rcx          # z[1] = rcx
  mulx    r9, rcx, qword ptr [rsi + 16] # [r9:rcx] = x[2] * y
  adc rcx, r8                           # rcx += r8 with CF
  mov qword ptr [rdi + 16], rcx         # z[2] = rcx
  mulx    rcx, rdx, qword ptr [rsi + 24]# [rcx:rdx] = x[3] * y
  adc rdx, r9                           # rdx += r9 with CF
  mov qword ptr [rdi + 24], rdx         # z[3] = rdx
  adc rax, rcx                          # rax += rcx with CF
  ret
```

mulxが4回、addが1回、adcが3回。利用レジスタ数も最小限となり退避・復元のためのpush/popも無くなっています。
完璧ですね。この方法だとNが大きくなってもレジスタ数が増えないのでよいです。

# mulxの利用可否の方法

一つ補足すると、このコードをHaswell以前の古いCPUで実行すると落ちるので注意が必要です。そこでcpuid命令を使って利用できるか調べます。

https://github.com/herumi/xbyak/blob/9357732aa2aa3cf97809027596dfa5c61d1515b2/xbyak/xbyak_util.h#L302-L315

Visual Studioなら__cpuidex, gcc系なら__cpuid_countを使ってeax=7としてcpuidを呼び出し、結果のebxの8bit目が1ならBMI2命令（mulxを含む）を利用できます。
実行時に確認したいときは参考にしてください。

https://github.com/herumi/xbyak/blob/master/xbyak/xbyak_util.h#L516-L520


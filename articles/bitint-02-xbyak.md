---
title: "多倍長整数の実装2（Xbyak）"
emoji: "🧮"
type: "tech"
topics: ["int","add", "C++", "xbyak"]
published: false
---
## 初めに

前回は固定長256bit整数加算をC++で実装しました。
今回は[Xbyak](https://github.com/herumi/xbyak)を用いて実装します。

## x64レジスタ

まず、多倍長整数の加算に必要な最低限のx64アセンブリ言語（以下asm）を説明します。

64bitの汎用レジスタと呼ばれる変数がrax, rbx, rcx, rdx, rsi, rdi, rbpとr8からr15までの15個あります。
64bitの下位32bitはeax, ebx, ecx, edx, esi, edi, ebp, r8d, r9d, ..., r15dで操作できます。
C/C++の関数内で利用する一次変数や関数の戻る場所などを記憶するためのスタック領域を指すレジスタがrspです。

関数の引数は1番目から順にWindowsならrcx, rdx, r8, r9, Linuxならrdi, rsi, rdx, rcxです。
詳細は[Windowsでの呼び出し既約](http://herumi.in.coocan.jp/prog/x64.html#WIN64)
関数の戻り値はraxを利用します。

いくつかの命令を紹介します。
下記のaやbはレジスタを表します。

- `mov a, [b]` : bで指すアドレスの値をaに読み込む。
- `mov [b], a` : aをbで指すアドレスに書き込む。
- `add a, b` : Cの`a += b`を表す。
- `sub a, b` : Cの`a -= b`を表す。

## キャリーフラグ

x86/x64ではadd命令の実行結果がレジスタサイズを越えない or 超えたに応じてCF（Carry Flag）という特殊なレジスタが0 or 1になります。
そしてCFを考慮した加算命令adcがあります。

`adc a, b`は`a += b + CF`を意味します。
adcの演算結果に応じてCFも更新されます。

これを使うとa, b, c, dが64bitレジスタで`[a:b]`と`[c:d]`という2個の128bit整数の加算`[a:b] += [c:d]`は

```asm
add b, d : b += d
adc a, c : a += c + CF
```
で表現できます。

## Xbyak

準備ができたところでXbyakを使ってみましょう。
Xbyakはx86/x64のアセンブリ言語を記述できるC++のライブラリです。
ヘッダファイルをコピーしてincludeパスを指定すれば使えます。

N桁のUnit同士の加算コードは次のように記述できます。

```cpp
#include <xbyak/xbyak_util.h>

struct Code : Xbyak::CodeGenerator {
  Code(int N)
  {
    Xbyak::util::StackFrame sf(this, 3);
    const auto& z = sf.p[0];
    const auto& x = sf.p[1];
    const auto& y = sf.p[2];
    for (int i = 0; i < N; i++) {
      mov(rax, ptr[x + 8 * i]);
      if (i == 0) {
        add(rax, ptr[y + 8 * i]);
      } else {
        adc(rax, ptr[y + 8 * i]);
      }
      mov(ptr[z + 8 * i], rax);
    }
    setc(al);
    movzx(eax, al);
  }
};
```
以下説明

- `Xbyak::CodeGenerator`クラスを継承するとその中で、クラスメソッドとしてasmの命令を関数呼び出しとして記述できます。
  - asmで`add rax, rcx`は`add(rax, rcx)`, `mov rax, [rsp]`は`mov(rax, ptr[rsp])`となります。
- `Xbyak::util::StackFrame`はWindowsとLinuxの関数の引数の違いを吸収する簡易クラスです。
  - 1番目にそのクラスのthis, 2番目に今から作る関数の引数の個数を指定します。
  - `sf.p[0]`, `sf.p[1]`が1番目, 2番目の汎用レジスタを表します。
  - ここでは`add(Unit *z, const Unit *x, const Unit *y)`という関数を作りたいので、それぞれを`z`, `x`, `y`という名前にしています。
- 前節で述べたように最初は`add`, 2番目以降は`adc`を使うとCFを伝搬させつつ加算を実装できます。
  - 64bitの場合オフセットは8byteずつ増えるので`x + 8 * i`という形になっています。
  - raxは自由に使ってよいレジスタです。
  - `mov(ptr[z + 8 * i], rax)`で演算結果を`z[i]`に保存しています。
- `setc(al)`は`al = CF`という意味です。
  - `movzx(eax, al)`は8bitのalの値を32bitのeaxにゼロ拡張(Zero eXtended)します。
  - 64bit環境でeaxを更新するとraxの上位32bitはクリアされます。結果的にalの値をraxにゼロ拡張します。
- `StackFrame`クラスはそのライフスコープを抜けるとき後始末をします。ここでは呼出元に戻る`ret()`が挿入されます。

## 生成コード

```cpp
Code code(4);
auto add4 = code.getCode<uint64_t (*)(uint64_t *, const uint64_t *, const uint64_t *)>();
```
とすると4桁（256bit）の加算関数が生成されて、その関数ポインタがadd4となります。
生成されたコードサイズは`code.getSize()`で取得できます。

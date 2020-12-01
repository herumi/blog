# BrainFuck JITインタプリタ

オリジナルXbyakのサンプル[bf.cpp](https://github.com/herumi/xbyak/blob/master/sample/bf.cpp)をAArch64に移植した[bf.cpp](https://github.com/fujitsu/xbyak_aarch64/blob/main/sample/bf.cpp)の解説をします。
JITコードならではのサンプルとして手頃だと思います。

## Brainfuck
Brainfuckは小さな言語です。
一定量のゼロ初期化された配列とその先頭ポインタ(stack)が渡されたときに次の命令を処理します。

- `>` // stack++;
- `<` // stack--;
- `+` // (*stack)++;
- `-` // (*stack)--;
- `.` // putchar(*stack);
- `,` // *stack = getchar();
- `[` // while (*stack) {
- `]` // }

なお、元の仕様としてはstackはバイト列ですがここでは`int64_t`の配列として扱いました。

## 関数のプロローグ
Aarch64の関数の中で別の関数を呼び出すときはリンクレジスタ(x30)とフレームポインタ(x29)をスタックに保存しておきます。
生成されたJITコードから直接putchar()やgetchar()の関数を呼ぶために、それらの関数ポインタを受けてレジスタに保存します。
またstackのポインタも渡しておきます。
結果的にJITインタプリタの外枠は次のようにしました。

```
class Brainfuck() : CodeGenerator() {
  // void (*)(void* putchar, void* getchar, int *stack)
  using namespace Xbyak_aarch64;
  const auto &pPutchar = x19;
  const auto &pGetchar = x20;
  const auto &stack = x21;
  const int saveSize = 48;

  stp(x29, x30, pre_ptr(sp, -saveSize)); // (1)
  stp(pPutchar, pGetchar, ptr(sp, 16));  // (2)
  str(stack, ptr(sp, 16 + 16));          // (3)
  mov(pPutchar, x0);                     // (4)
  mov(pGetchar, x1);                     // (5)
  mov(stack, x2);                        // (6)
```

AArch64ではスタックは常に16の倍数なので48バイト確保します。
- (1) spを48バイト減らし、x29とx30を退避します。
- (2) pPutchar(x19)とpGetchar(x20)を退避します。
- (3) stack(x21)を退避します。
- (4) 生成されたJITコードに渡されたputchar, getchar, stackのアドレスをそれぞれのレジスタに設定します。

(1)はAArch64のアセンブリ言語表記では`stp x29, x30, [sp,-48]!`です。
これは

```
sp -= 48;
sp[0] = x29;
sp[8] = x30;
```
相当の動作をします。
`Xbyak_aarch64`では「!」をがんばって演算オーバーロードせずに`pre_ptr`という分かりやすい名前にしました。

(3)まで実行したときのスタックの状態は次の通りです。

アドレス|値|
-|-|
sp+32|x21|
sp+24|x20|
sp+16|x19|
sp+ 8|x30|
sp+ 0|x29|

## 関数のエピローグ
JITコードから抜けるときは退避したレジスタを復元します。

```
  ldr(stack, ptr(sp, 16 + 16));
  ldp(pPutchar, pGetchar, ptr(sp, 16));
  ldp(x29, x30, post_ptr(sp, saveSize));
  ret();
```

`ldp(x29, x30, post_ptr(sp, saveSize));`はx29, x30をスタックから復元した後`sp += saveSize`します(`post_ptr`)。
通常のAArch64アセンブリ言語では

```
ldp x29, x30, [sp], saveSize
```
と書きます。

## ポインタ移動

```
case '+':
case '-': {
  int count = getContinuousChar(is, c);
  ldr(x0, ptr(stack));
  // QQQ : not support large count
  if (c == '+') {
    add(x0, x0, count);
  } else {
    sub(x0, x0, count);
  }
  str(x0, ptr(stack));
} break;
```

`getContinuousChar()`は入力から連続する`+`か`-`を取得してその回数countを返します。
簡単な最適化です。
なおAArch64の即値は最大12ビットなのでそれを超えるとエラーになります。
大きな値を扱いたい場合は分割して加算する必要があります。

`ldr(x0, ptr(stack));`で現在値の値を取得してcount分だけadd/subして`str(x0, ptr(stack));`で書き戻します。

## 値の更新

```
case '>':
case '<': {
  int count = getContinuousChar(is, c) * 8;
  if (c == '>') {
    add(stack, stack, count);
  } else {
    sub(stack, stack, count);
  }
} break;
```

stackの値をadd/subします。
境界チェックはしていないので大きな値を設定するとバッファオーバーフローします。
ご注意ください。

## 入出力

```
case '.':
  ldr(x0, ptr(stack));
  blr(pPutchar);
  break;
case ',':
  blr(pGetchar);
  str(x0, ptr(stack));
  break;
```
- x0に現在のstackの値を入れて`putchar()`を呼び出します。
- `getchar()`を呼び出してその戻り値x0を現在のstackに保存します。

## 分岐

ここはやや複雑です。
`[`と`]`で`while (*stack) {}`を実現しなければなりません。
whileは次のようになります。

```
.B:
  ldr(x0, ptr(stack)); // [
  cmp(x0, 0);
  beq(.F);
  ...
  jmp(.B);            // ]
.F:
```
これが多重にネストしても動作するようにするために、ラベルのstackを用意します。

```
std::stack<Label> labelF, labelB;
```
`[`が入力されたときのループの開始位置`.B`は決まっています(1)がジャンプ先`.F`はその時点でまだ決まっていません。
対応する`]`が入力されたときに決まります。
そこで`[`では中身が未確定のままlabelFにpushして(2)、`]`が出てきたところで取り出して(3)、そのラベルにアサインします(4)。

```
case '[': {
  Label B = L();          // (1)
  labelB.push(B);
  ldr(x0, ptr(stack));
  cmp(x0, 0);
  Label F;
  beq(F);
  labelF.push(F);         // (2)
} break;
case ']': {
  Label B = labelB.top();
  labelB.pop();
  b(B);
  Label F = labelF.top(); // (3)
  labelF.pop();
  L(F);                   // (4)
} break;
```

xbyakのラベルクラスはこのようなことが簡単に記述できるように設定されています。
動作原理を素直にJITに落とせているのが分かるでしょうか。

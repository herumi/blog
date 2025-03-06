---
title: "C++のグローバル静的変数とattribute/declspecによる関数の呼び出し順序制御"
emoji: "📖"
type: "tech"
topics: ["cpp", "static", "初期化順序", "attribute", "declspec"]
published: false
---
## 初めに
C++のグローバルな静的変数が`main`関数の前に宣言されていると`main`の前に初期化されます。
GCCには`__attribute__((constructor))`という[拡張](https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html)があり、この属性を関数の前に付けるとその関数が`main`の前に呼ばれます。
それでは両方使うとどういう順序でなるのか? **C++的には未定義**ですが実験してみました。

## 単一ファイルにおける順序
```cpp
#include <stdio.h>

static struct X1 {
    X1() { puts("X1 cstr"); }
} x1;

static void __attribute__((constructor)) initMain()
{
    puts("initMain");
}

static struct X2 {
    X2() { puts("X2 cstr"); }
} x2;

int main()
{
    puts("main");
}

static struct X3 {
    X3() { puts("X3 cstr"); }
} x3;
```

静的変数`x1`, `x2`, `x3`をぞれぞれ`main`の前、`initMain`の前、`main`の後ろに置いてみました。

```bash
% g++ main.cpp && ./a.out
initMain
X1 cstr
X2 cstr
X3 cstr
main
```

実行するとまず`__attribute__((constructor))`による関数呼出しが行われてから、（C++の規格で決まってる）変数の宣言順序で変数が初期化され、`main`関数に突入しました。

## 変数の初期化順序を変更する
実はGCCには`__attribute__((init_priority (priority)))`という変数の初期化順序を制御する[拡張](https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Attributes.html)もあります。
priorityには101から65535までの数値を指定でき、値が小さいほど先に初期化されます。先程のx1, x2, x3に1030, 1010, 1020というpriorityを与えてみましょう。
属性は変数の後ろにつけるのに注意してください。

前節のファイルからの差分
```diff
diff --git a/static-cstr/main.cpp b/static-cstr/main.cpp
index 85619fa..7e11da5 100644
--- a/static-cstr/main.cpp
+++ b/static-cstr/main.cpp
@@ -10,7 +10,7 @@ namespace {

 static struct X1 {
        X1() { puts("X1 cstr"); }
-} x1;
+} x1 __attribute__((init_priority(1030)));

static void __attribute__((constructor)) initMain()
 {
@@ -24,7 +24,7 @@ __declspec(allocate(".CRT$XCU")) void(*ptr_initMain)() = initMain;

 static struct X2 {
        X2() { puts("X2 cstr"); }
-} x2;
+} x2 __attribute__((init_priority(1010)));

 }

@@ -38,6 +38,6 @@ namespace {

 static struct X3 {
        X3() { puts("X3 cstr"); }
-} x3;
+} x3 __attribute__((init_priority(1020)));

 }
 ```

```bash
% g++ main.cpp && ./a.out
X2 cstr # 1010
X3 cstr # 1020
X1 cstr # 1030
initMain # 後ろに来た
main
```
ファイル内の宣言順序ではなく、与えたpriorityの小さい順にx2, x3, x1と初期化されました。
それから前節とことなり`initMain`よりも先になったことに注意してください。`__attribute__((constructor))`のデフォルトpriorityは65535のようです。

## 関数の呼び出し順序を変更する

なお、そのpriorityも変更できます。

```cpp
// # define PRIORITY 101
static void __attribute__((constructor(PRIORITY))) initMain()
```

PRIORITY=101とconstructorにx1, x2, x3よりも小さいpriorityを与えてみます。
すると

```bash
% g++ main.cpp -DPRIORITY=101 && ./a.out
initMain # 101 前に来た
X2 cstr # 1010
X3 cstr # 1020
X1 cstr # 1030
main
```
PRIORITY=1025なら

```bash
% g++ main.cpp -DPRIORITY=1025 && ./a.out
X2 cstr # 1010
X3 cstr # 1020
initMain # 1025
X1 cstr # 1030
main
```

使い道はあまりなさそうですがおもしろいですね。

## Windows (Visual Studio) の場合
Visual Studioには残念ながら`__attribute__((constructor))`相当の拡張がありません。
代わりに次の方法が使われることがあります。
`.CRT$XCU`というセクションに動的な初期化変数のコードが置かれます。（[CRTの初期化](https://learn.microsoft.com/ja-jp/cpp/c-runtime-library/crt-initialization?view=msvc-170)）
`__declspec(allocate(".CRT$XCU"))`を用いて、ここに呼び出してほしい関数を追加するのです。

```cpp
static void initMain()
{
    puts("initMain");
}

#ifdef _MSC_VER
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) void(*ptr_initMain)() = initMain;
#endif
```

```bash
cl /O2 /EHsc main.cpp && main
X1 cstr
X2 cstr
X3 cstr
initMain # 最後に追加された
main
```

このようにするとVisual Studioでも`__attribute__((constructor))`相当のことができます。`initMain`は最後に追加されています。
GCCのpriorityのような順序付けはできないのですが、CRTセクションのサブセクションはアルファベット順に並べられるので`XCU`の辞書的順序で一つ前の`XCT`（アルファベットUの前はT）に追加すればGCCのpriorityを設定しないデフォルト挙動に近い形にできます。
かつ、`XCU`への追加は推奨されないようですので、この方がより安全です。

```cpp
#ifdef _MSC_VER
#pragma section(".CRT$XCT", read)
__declspec(allocate(".CRT$XCT")) void(*ptr_initMain)() = initMain;
#endif
```
```bash
cl /O2 /EHsc main.cpp && main
initMain # 前に来た
X1 cstr
X2 cstr
X3 cstr
main
```

ただ、この方法は将来に渡って動くかの保証はないそうです。
将来`XCT`サブセクションが登場してぶつかったときのために

```cpp
#pragma warning(default:5247) // セクション 'section-name' は C++ 動的初期化用に予約されています。
#pragma warning(default:5248)
```
を付けて警告を有効にしておくとよいかもしれません（まあ当分は無いでしょうが）。

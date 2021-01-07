# コンパイラの定義マクロの違い

gcc、clang、FCCで定義されるいくつかのマクロについて調べてみました。

## Download

- gcc ; [GNU-A Downloads](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-a/downloads)
  - x86_64上で使うならAArch64 GNU/Linux target (aarch64-none-linux-gnu)
  - AArch64上で使うならAArch64 ELF bare-metal target (aarch64-none-elf)
- clang ; [LLVM Download](https://releases.llvm.org/download.html)
  - AArch64上で使うならAArch64 Linux

## SVEの扱い
コンパイラのinlineアセンブラでSVEの命令を使うにはコンパイラオプションでSVEを有効にしなければなりません。

A64FXはarmv8.2-aベースなので
```
-march=armv8.2-a+sve
```
と指定するとSVEが使えるようになります。
SVEが使えるときは`__ARM_FEATURE_SVE`が定義されるのでそれで区別できます。
ただしclangはclang-10までは有効になっていてもこのマクロは定義されていません。
clang-11以降を使うのがよさそうです。

どんなマクロが定義されているかは`-dM -E`オプションで分かります。

```
echo |gcc -dM -E -
```

FCCはデフォルトでSVEが有効ですがclang互換オプション`-Nclang`を指定するとデフォルトoffになります。
そして`-march=armv8.2-a+sve`オプションを追加すると`__ARM_FEATURE_SVE`は定義されるのでinline asmの中でSVE命令が使えません。
これはどうすればよいんでしょうね。何かオプションがあるのかな。

○ SVEの利用可否

コンパイラ|gcc-8≧|clang-10≦|clang-11≧|FCC|FCC -Nclang
-|-|-|-|-|-
デフォルト|-|-|-|○|-
`-march=armv8.2-a+sve`|○|△|○|○|×

- ○ : SVEが使えて`__ARM_FEATURE_SVE`が定義される
- △ : SVEが使えて`__ARM_FEATURE_SVE`が定義されない
- × : `__ARM_FEATURE_SVE`が定義されるがinline asmでsveが使えない

## 最適化制御プラグマ

inlineアセンブラを使う場合に`.inst`で命令をバイトコードで記述すると依存関係が不明確になって最適化によって削除されることがあります。
それを防ぐには最適化制御プラグラマを使う方法があります(本当はasmで記述するかちゃんとコンパイラオプションを設定すべきですが)。

関数単位で最適化を無効化するには

- gcc : `__attribute__((optimize("O0")))`
- clang : `__attribute__((optnone))`

を使います。FCCのclang互換モードはclangと同じですがデフォルトモードでの設定方法は知りません。

結局SVEのcntbを使ってSVEのレジスタ長を得るには次のような煩雑な方法になりました。
`__CLANG_FUJITSU`はFCCで`-Nclang`を指定したときだけ定義されるマクロです。
もっとよい方法があればお知らせください。

```
#if defined(__ARM_FEATURE_SVE) && !defined(__CLANG_FUJITSU)
  #define USE_INLINE_SVE
#endif

#ifndef USE_INLINE_SVE
  #warning "use option -march=armv8.2-a+sve"
#endif

int
#ifndef USE_INLINE_SVE
  #ifdef __clang__
    __attribute__((optnone))
  #else
    __attribute__((optimize("O0")))
  #endif
#endif
getLen()
{
  uint64_t x = 0;
#ifdef USE_INLINE_SVE
  asm __volatile__("cntb %[x]" : [ x ] "=r"(x));
#else
  asm __volatile__(".inst 0x0420e3e0":"=r"(x));
#endif
  return (int)x;
}
```

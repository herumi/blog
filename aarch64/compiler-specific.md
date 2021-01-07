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
ただしclangはclang-10まではSVEが有効になっていてもこのマクロは定義されていません。
したがってコンパイラマクロでオプションが有効になっているか判別できません。
clang-11以降を使うのがよさそうです。

どんなマクロが定義されているかは`-dM -E`オプションで分かります。

```
echo |gcc -dM -E -
```

FCCはデフォルトでSVEが有効ですがclang互換オプション`-Nclang`を指定するとデフォルトoffになります。
そして`-march=armv8.2-a+sve`オプションを追加すると`__ARM_FEATURE_SVE`は定義されるのにinlineアセンブラの中でSVE命令を使えません。
これはどうすればよいんでしょうね。何かオプションがあるのかな。

○ SVEの利用可否

コンパイラ|gcc-8≧|clang-10≦|clang-11≧|FCC|FCC -Nclang
-|-|-|-|-|-
デフォルト|-|-|-|○|-
`-march=armv8.2-a+sve`|○|△|○|○|×

- ○ : SVEが使えて`__ARM_FEATURE_SVE`が定義される
- △ : SVEが使えて`__ARM_FEATURE_SVE`が定義されない
- × : `__ARM_FEATURE_SVE`が定義されるがinlineアセンブラでsveが使えない

## 最適化制御プラグマ

inlineアセンブラでSVE命令が使えない場合`.inst`で命令をバイトコードで記述する方法があります。
しかし`.inst`命令を使うとコンパイラに対するレジスタの依存関係の指示がうまくできずに最適化によって削除されることがあります(よい方法があれば教えてください)。
それを防ぐにはあまりよい解決法ではありませんが、最適化制御プラグラマを使う方法があります。

関数単位で最適化を無効化するには

- gcc : `__attribute__((optimize("O0")))`
- clang : `__attribute__((optnone))`

を使います。
FCCのclang互換モードではclangと同じです。デフォルトモードでのattributeの設定方法を私は知りません。

結局SVEのcntbを使ってSVEのレジスタ長を得るには次のような煩雑な方法になりました。
`__CLANG_FUJITSU`はFCCで`-Nclang`を指定したときだけ定義されるマクロです。

でもSVEが使える前提でcntbを使ってそれ以外はエラーにする方が安全そうですね。
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

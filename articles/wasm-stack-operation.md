---
title: "JavaScriptからWasm用スタックポインタを操作する（Clang用）"
emoji: "📖"
type: "tech"
topics: ["Wasm", "Clang", "スタックポインタ"]
published: true
---
## 初めに

以前、[JavaScriptからWasmのスタックポインタを操作する](https://zenn.dev/herumi/articles/wasm-emcc-stack)でC/C++コードをWasmに変換するコンパイラEmscripten（以下emcc）でスタックを操作する方法を紹介しました。
その後、暗号ライブラリ[mcl](https://github.com/herumi/mcl)のWasm版[mcl-wasm](https://github.com/herumi/mcl-wasm)のbuildに使用するコンパイラをemccからClangに切り替えました。
emccのバージョンアップの度に細かい仕様変更で時間をとられることが続いていたためです。

今回は、ClangでWasmコードを生成したときにスタックポインタを操作する方法を紹介します。

## EmscriptenからClangへの移行
移行作業にはいくつか解決する問題があったのでそれらを紹介します。
まず最初はWasmが扱うためのスタックポインタの操作です。

emccでは次の関数が提供されていました。

- `stackSave () -> (i32)` : 現在のスタックポインタの値を返す。
- `stackAlloc (n:i32) -> (i32)` : nバイトのスタックを確保してそのポインタを返す。
- `stackRestore (sp:i32) -> ()` : スタックポインタをspの位置に戻す。

しかし、Clangではそれらの関数がないので作る必要があります。そのためにまずWasmのスタックポインタの管理について説明しましょう。

## Wasmのメモリ管理
Wasmではコードとデータは同じメモリ空間にはなく、関数本体や戻りアドレスはリニアメモリから直接参照できません。
一方、C/C++のアドレスを持つローカル変数、静的データ、ヒープ、線形スタックなどは、Wasmのリニアメモリ上に配置されます。
JavaScriptはリニアメモリを`WebAssembly.Memory`インスタンスとして扱い、実際のバイト列には`memory.buffer`経由で`ArrayBuffer`または`SharedArrayBuffer`としてアクセスします。

```javascript
const memory = new WebAssembly.Memory({ initial: 32 }); // 32 page = 32*64KiB
```
memoryの一部が.dataや.bssセクション, ヒープやスタックなどの領域として利用されます。
スタックの場所を示すスタックポインタSPはスタックを確保するときは減る方向に進み、常に16バイトアライメントされています([The linear stack](https://github.com/WebAssembly/tool-conventions/blob/main/BasicCABI.md#the-linear-stack))。

次の図は、リニアメモリ全体での絶対位置ではなく、現在のスタックフレーム周辺の相対的な配置を示します。

*スタックポインタ周りの構造*

位置|内容|フレーム
-|-|-
...|呼び出しもとのオブジェクト|一つ前のフレーム
...|パディング|現在のフレーム
SP + size|動的サイズのオブジェクトの上端|現在のフレーム
SP|動的サイズのオブジェクト|現在のフレーム
SP - 128|小さな固定サイズのオブジェクト|現在のフレーム(red zone)


SPをどのように扱うかはWasm自体の仕様ではなく、clang/LLVMのリンカの仕様として決まっているようです。そこではmutableなglobal変数`__stack_pointer`として定義されます（[Merging Global Sections](https://github.com/WebAssembly/tool-conventions/blob/main/Linking.md#merging-global-sections)）。

この`__stack_pointer`を操作する方法は3通りあります。順に紹介しましょう。

## Wasmで実装
一つ目はemccがしているようにWasmのアセンブリで実装する方法です。
emccの[stack_ops.S](https://github.com/emscripten-core/emscripten/blob/main/system/lib/compiler-rt/stack_ops.S)とほぼ同じものを用意します（[stack.s](https://github.com/herumi/mcl/blob/dev/src/wasm/stack.s)）。

### stackSave

Wasmの命令は、計算途中の値を暗黙のオペランドスタックで受け渡します。
ここではそれを`loc_st`と呼ぶことにします。たとえば`i32.const 123`は値123を`loc_st`に積みます。C++的に書くと

```cpp
// i32.const 123相当
loc_st.push(123);
```

そして関数が値を返すときは`loc_st`のトップにある値が返されます。

`stackSave () -> (i32)`は現在のSPを返します。

```asm
stackSave:
	.functype	stackSave () -> (i32)
	global.get	__stack_pointer
	end_function
```

コードは`global.get __stack_pointer`で`__stack_pointer`の値を取得して`loc_st`に積みます。そしてreturnするのでSPが返されます。

### stackRestore

次にWasmの関数呼び出しについて簡単に解説しましょう。
関数引数やローカル変数は`local.get` / `local.set`で参照されるlocals領域にあります。
Wasmの関数`func(a, b, c)`は関数が呼び出された時点でlocals領域に引数a, b, cが順番に格納されます。

```cpp
locals[0] = a
locals[1] = b
locals[2] = c
```

したがって、`stackRestore(sp)`の引数`sp`はlocals領域の0番目、つまり`locals[0]`に格納されていて、`local.get 0`でその値を取得し、`loc_st`に積まれます。

```cpp
// local.get 0相当
loc_st.push(locals[0]);
```

`global.set	__stack_pointer`はスタック`loc_st`のトップにある値(`locals[0]=sp`)を`__stack_pointer`に格納して`loc_st`をpopします。つまりloc_stは空になり戻り値はありません。

```asm
stackRestore:
	.functype	stackRestore (i32) -> ()
	local.get	0
	global.set	__stack_pointer
	end_function
```

### stackAlloc

`stackAlloc(n)`はSPから引数nを引いて16バイトアライメントしてSPを更新しつつ値を返します。

```asm
stackAlloc:
	.functype	stackAlloc (i32) -> (i32)
	global.get	__stack_pointer ; loc_st = [SP]
	local.get	0               ; loc_st = [SP, n]
	i32.sub                     ; loc_st = [SP - n]
	i32.const	-16             ; loc_st = [SP - n, -16]
	i32.and                     ; loc_st = [(SP - n) & -16]
	local.tee	0               ; loc_st = [(SP - n) & -16], locals[0] = (SP - n) & -16
	global.set	__stack_pointer ; loc_st = []
	local.get	0               ; loc_st = [(SP - n) & -16]
	end_function
```

まず`global.get	__stack_pointer`で`loc_st`にSPを積み、`local.get 0`で引数nを取得して`loc_st`に積みます。

```cpp
loc_st.push(__stack_pointer);
loc_st.push(locals[0]); // n
```

次に`i32.sub`で`n = loc_st.pop()`, `SP = loc_st.pop()`を取得し`loc_st.push(SP - n)`します。
さらに`i32.const -16`で-16を積み、`i32.and`で16バイトアライメントします(`(SP - n) & -16`)。

`local.tee 0`は`loc_st`のトップの値を`locals[0]`に格納します。`global.set __stack_pointer`でSPを更新し、`loc_st`が空になります。
変更後のSPを返すために`locals[0]`の値をもう一度`loc_st`に積んでreturnします。

注意点はリンク時のオプションです。これらの関数はC/C++コードからは参照されないので、そのままではwasm-ldに削除されてexportされません。明示的にexportを指定する必要があります。

```makefile
LDFLAGS+=--export=stackSave --export=stackAlloc --export=stackRestore
```

## Cで実装
二つ目はCで実装する方法です（[stack_c.c](https://github.com/herumi/mcl/blob/dev/src/wasm/stack_c.c)）。

```c
#include <stdint.h>
#include <stddef.h>

extern uint8_t *__stack_pointer __attribute__((address_space(1)));

uint8_t *stackSave(void) {
	return __stack_pointer;
}

void stackRestore(uint8_t *sp) {
	__stack_pointer = sp;
}

uint8_t *stackAlloc(size_t n) {
	uintptr_t sp = (uintptr_t)__stack_pointer;
	sp = (sp - n) & ~(uintptr_t)15;
	__stack_pointer = (uint8_t *)sp;
	return (uint8_t *)sp;
}
```

`stackSave`, `stackRestore`, `stackAlloc`は普通のCのコードです。

ポイントは`__stack_pointer`の宣言につけた`__attribute__((address_space(1)))`で、これは「この変数は通常のアドレス空間ではなく番号1のアドレス空間に属する」という意味です。
Wasm用Clangでは属性のない通常の変数はリニアメモリ上のDATAシンボルとして、`address_space(1)`の変数はWasmのglobal変数（GLOBALシンボル）として扱われます。

つまり、

- Wasm : global変数という概念を提供
- Clang/LLVM : `address_space(1)`をそのglobal変数にマップ

という関係があります。
`address_space`の番号の意味はC/C++の言語仕様で決まっているのではなく、Clang/LLVMの実装依存なので注意してください。

もう一つの注意点は、このファイルだけは`-flto`をつけずにコンパイルすることです。
mclは全体を`-flto`つきでbuildしているのですが、stack_c.cに`-flto`をつけると

```
defined as WASM_SYMBOL_TYPE_GLOBAL in <internal>
defined as WASM_SYMBOL_TYPE_DATA in obj/stack_c.o
```

というリンクエラーになりました。
wasm-ldは`__stack_pointer`を内部でGLOBALシンボルとして扱うのですが、LTO用のbitcodeを経由すると`__stack_pointer`の参照がDATAシンボル側として解決される経路があり、GLOBALとDATAの不一致でリンクに失敗するようです。

リンク時に`--export=stackSave ...`が必要なのはWasmで実装した場合と同じです。

## JavaScriptで実装
三つ目はJavaScriptで実装する方法です（[glue.js](https://github.com/herumi/mcl/blob/dev/src/wasm/glue.js)）。
前述の通り`__stack_pointer`はmutableなglobal変数なので、それ自体をexportすればJavaScriptから直接操作できて、Wasm側にコードを追加する必要はありません。

```makefile
LDFLAGS+=--export=__stack_pointer
```

global変数はリニアメモリとは別の領域に格納されているのでHEAP経由ではアクセスできません。
JavaScript側からは`WebAssembly.Global`のインスタンスとして見えて、`value`プロパティで読み書きします。

```javascript
const g_sp = exports.__stack_pointer // WebAssembly.Global (mutable i32)
mod.stackSave = function () { return g_sp.value }
mod.stackRestore = function (sp) { g_sp.value = sp }
mod.stackAlloc = function (n) {
  const sp = ((g_sp.value - n) & ~15) >>> 0
  g_sp.value = sp
  return sp
}
```

stackAllocの`& ~15`が16バイトアライメント処理です。
この方法では`LDFLAGS+=--export=stackSave --export=stackAlloc --export=stackRestore`は不要です。
なお、mutableなglobal変数のexportにはWasmのmutable-globals拡張が必要ですが、現在の主要な処理系では問題なく使えるはずです。

実際には次のように確保したスタックを利用します。

```javascript
const sp = mod.stackSave()
try {
  const p = mod.stackAlloc(128)
  // pをバッファに使うWasmの関数の呼び出し
  // 絶対に例外を投げないならtry-finallyは不要
} finally {
  mod.stackRestore(sp)
}
```

## それぞれのメリット・デメリット
3通りの方法を簡単に比較します。

実装|メリット|デメリット
-|-|-
Wasm|emccと同じ実装で確実|Wasm命令・記法の知識が必要
C|Cなので読み書きしやすい|`address_space(1)`や`-flto`などコンパイラ実装依存の注意点が多い
JavaScript|Wasm側のコード追加とexport指定が最小限。JSからWasmの関数呼び出しがない|wasmのグローバル変数へのアクセスあり

WasmやCによる実装はスタック操作のたびにJavaScriptからWasmの関数を呼び出すオーバーヘッドがかかります。
JavaScriptによる実装はそれがない分有利な可能性がありますが、JavaScriptからWasmのグローバル変数のアクセスが処理系でどのぐらい最適化されるかは試してみないとわかりません。いずれにしても関数の呼び出し方を間違えるとさくっと落ちるのは同じです。

### まとめ
ClangでWasmコードを生成したときにJavaScriptからスタックポインタを操作する3通りの方法を紹介しました。
`__stack_pointer`がmutableなglobal変数としてexportできると理解すれば、JavaScriptだけで完結する3番目の方法は手軽ですね。

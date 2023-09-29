---
title: "Wasm用C++コンパイラemccのコンパイラオプション"
emoji: "📖"
type: "tech"
topics: ["Wasm", "cpp", "例外", "スタック"]
published: true
---
## 初めに
Wasm用C++コンパイラツールチェーン[emscripten](https://emscripten.org/)のemccで、自分がはまったオプションをいくつか紹介します。

## モジュール化
まず`-s Wasm=1`と`-s MODULARIZE=1`が必要です。
それから、デフォルトではjsファイルとwasmファイルの両方が作られますが、`-s SINGLE_FILE=1`をつけるとjsファイルだけが生成されます。
wasmファイルはjsの中の`var wasmBinaryFile;`という変数にbase64エンコードされた形で取り込まれています。

```js
// 生成されたjsの中
wasmBinaryFile = 'data:application/octet-stream;base64,AGFzbQEAAAAB5ICAgAAPYAF...`
```
ファイルサイズは若干大きくなりますが、ファイルが分散されていないのでwebpackなどで利用しやすくなります。

なお、wasmファイルが組み込まれていると、それを読み込むコードは不要です。
しかし、生成されたJSの中には、

```js
if (ENVIRONMENT_IS_NODE) {
 var fs = require("fs");
 var nodePath = require("path");
 if (ENVIRONMENT_IS_WORKER) {
  scriptDirectory = nodePath.dirname(scriptDirectory) + "/";
  ...
```
というコードがあります。この`require`がwebpackを使うときにエラーを引き起こします。うまい設定があるのかもしれませんが、私は見つけられなかったので上記コードをコメントアウトすることにしました。

```makefile
perl -i -pe 's@(.* = require\(.*)@//\1@' $@
```
処理後は

```js
if (ENVIRONMENT_IS_NODE) {
// var fs = require("fs");
// var nodePath = require("path");
 if (ENVIRONMENT_IS_WORKER) {
  scriptDirectory = nodePath.dirname(scriptDirectory) + "/";
  ...
```
となります。この方法で、webpackでも使えるようになりました。

## exportする関数
emccはデフォルトでは関数は外部から見えません。JavaScriptから呼び出したい関数は`__attribute__((used))`をつける必要があります。
clangの場合は、`__attribute__((visibility("default")))`を使うので、emccのときに定義されるマクロ`__EMSCRIPTEN__`を使ってマクロを定義するのがよいでしょう。

```cpp
#if defined(__EMSCRIPTEN__)
    #define API __attribute__((used))
#elif defined(__wasm__)
    #define API __attribute__((visibility("default")))
#endif

extern "C" {
API 関数...
}
```

なお、mallocなどの標準関数は以前は外部から見えていたのですが、[3.1.31](https://github.com/emscripten-core/emscripten/blob/main/ChangeLog.md#3131---012623)から見えなくなったようです。
ヘッダを書き換えるわけにはいかないのでemccに-sEXPORTED_FUNCTIONSオプションを使います。このようにemccのオプションはちょいちょい後方互換性を壊すのでアップデートするときは注意が必要です。

```makefile
EMCC_OPT+=-sEXPORTED_FUNCTIONS=_malloc,_free
```

## スタックサイズ
[3.1.27](https://github.com/emscripten-core/emscripten/blob/main/ChangeLog.md#3127---112922)でスタックサイズのデフォルトが5MBから64KBに小さくなっています。
今まで動いていたアプリが新しいemccでビルドすると実行時に落ちてしまうことがあります。`-sSTACK_SIZE=5MB`などと明示的に指定しましょう。

## STRICT_JS
以前は`-s STRICT_JS=1`を有効にしていたのですが、[このあたり](https://github.com/emscripten-core/emscripten/issues/18610)でエラーになりました。これは単に削除すればよいです。

## 例外
クイズです。次のコードをemccでコンパイルして、関数を呼び出すとどうなるでしょうか。

```cpp
#include <stdexcept>

// APIは上記定義
extern "C" {
API int func_nocatch(int x);
API int func_catch(int x);
}

int func_nocatch(int x)
{
    if (x < 0) throw std::runtime_error("bad x");
    return x * 2;
}

int func_catch(int x)
{
    try {
        return func_nocatch(x);
    } catch (...) {
        return -1;
    }
}
```

```js
const ex = require('./index.js')

ex.init().then(() => {
  try {
    console.log('nocatch')
    const f = ex.mod.func_nocatch
    console.log(`nocatch 5 -> ${f(5)}`)
    console.log(`nocatch -5 -> ${f(-5)}`)
  } catch (e) {
    console.log(`err ${e}`)
  }
  try {
    console.log('catch')
    const g = ex.mod.func_catch
    console.log(`catch 5 -> ${g(5)}`)
    console.log(`catch -5 -> ${g(-5)}`)
  } catch (e) {
    console.log(`err ${e}`)
  }
})
```

答えはC++で例外が投げられるとC++内でcatchしてようが、JavaScriptでcatchしてようがabortします。

```shell
Aborted(Assertion failed: Exception thrown, but exception catching is not enabled. Compile with -sNO_DISABLE_EXCEPTION_CATCHING or -sEXCEPTION_CATCHING_ALLOWED=[..] to catch.)
err RuntimeError: Aborted(Assertion failed: Exception thrown, but exception catching is not enabled. Compile with -sNO_DISABLE_EXCEPTION_CATCHING or -sEXCEPTION_CATCHING_ALLOWED=[..] to catch.)
```

実はemccのデフォルトは例外が有効ではありません。それならコンパイルエラーにしてほしいところですが、上記エラーの用に`-sNO_DISABLE_EXCEPTION_CATCHING`オプションをつけるとよいです。

```makefile
EMCC_OPT+=-sNO_DISABLE_EXCEPTION_CATCHING
```

あるいは`-fexceptions`でもよいです。C++に慣れている人はこちらの方が覚えやすいでしょう。

```makefile
EMCC_OPT+=-fexceptions
```

いくつかのコードをコンパイルして比べた限りでは両者は同一のWasmファイルを生成しました。有効にして実行すると、

```shell
node test.js
nocatch
nocatch 5 -> 10
err std::runtime_error: bad x
catch
catch 5 -> 10
catch -5 -> -1
```
と、一つ目のC++で投げられた例外はJavaScript側でcatchされ、二つ目は正しくC++内でcatchされていることが分かります。
サンプルコードは[wasm/exception](https://github.com/herumi/misc/tree/main/wasm/exception)をごらんください。

### まとめ
最近私がはまった仕様変更に関わるオプションを紹介しました（例外は違いますが）。たまにしかemccを更新しないと後方互換性が壊されていろいろ痛い目にあいます。こまめに[ChangeLog](https://github.com/emscripten-core/emscripten/blob/main/ChangeLog.md)を確認するとよさそうです。

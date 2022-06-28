# フラグの表示

## addsの挙動を調べる

[サンプルコード](https://github.com/herumi/misc/tree/main/debug/aarch64)

## `uint32_t func(uint32_t, uint32_t)`という関数を作る。

[func.s](https://github.com/herumi/misc/blob/main/debug/aarch64/func.s)

```asm
.global func
func:
  adds w0, w0, w1
  ret
```

## funcを呼び出す本体を準備する。

[test.cpp](https://github.com/herumi/misc/blob/main/debug/aarch64/test.cpp)

```cpp
#include <stdio.h>
#include <stdint.h>

extern "C" uint32_t func(uint32_t, uint32_t);

int main()
{
  const uint32_t tbl[] = { 0, 1, 0x7fffffff, 0x80000000, 0x80000001, 0xffffffff };
  const size_t n = sizeof(tbl) / sizeof(tbl[0]);
  for (size_t i = 0; i < n; i++) {
    for (size_t j = 0; j < n; j++) {
      uint32_t x = tbl[i];
      uint32_t y = tbl[j];
      uint32_t z = func(x, y);
      printf("%08x+%08x=%08x\n", x, y, z);
    }
  }
}
```

## Aarch64のcpsr(Current Program Status Register)

[CPSR](https://developer.arm.com/documentation/ddi0595/2021-09/AArch32-Registers/CPSR--Current-Program-Status-Register?lang=en)

31|30|29|28
-|-|-|-
N|Z|C|V

対応する状態を表示するgdbのコマンドを用意する。

```
define flags
  printf "QQQ %c %c %c %c\n", ($cpsr >> 31) & 1 ? 'N' : '-', ($cpsr >> 30) & 1 ? 'Z' : '-', ($cpsr >> 29) & 1 ? 'C' : '-', ($cpsr >> 28) & 1 ? 'V' : '-'
end
```
後でgrepしやすいように「QQQ」をつけてる。

funcにbreakpointをおき、funcまで実行してそのときの`w0`と`w1`を表示する。

```
printf "QQQ %08x %08x\n", $w0, $w1
```

siで一つコマンドを進めて`adds w0, w0, w1`の結果を表示する。

```
printf "QQQ %08x\n", $w0
```

フラグも表示する。これを繰り返す[スクリプト](https://github.com/herumi/misc/blob/main/debug/aarch64/script.txt)を用意する。
gdbのバッチモードで実行する。

```
gdb -nx -q -batch -x script.txt ./test.exe | grep QQQ
```

[addsの結果](https://github.com/herumi/misc/blob/main/debug/aarch64/adds.txt)

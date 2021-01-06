# CPU命令の特定

AArch64のCPUがAESやSHA2命令をサポートしているかの判定方法を紹介します。
x86/64ではcpuidを利用していましたがAArch64ではいろいろな特殊レジスタを利用します。
ただし下記の方法はM1 macではその命令に対応していないようで動きませんでした。

## AES/SHA2

[ID_AA64ISAR0_EL1](https://developer.arm.com/documentation/101799/0001/Register-descriptions/ID-AA64ISAR0-EL1--AArch64-Instruction-Set-Attribute-Register-0--EL1)という64bitのレジスタをみます。

たとえば4~7ビットの値が2だったらAES用命令を利用できます。

ビットフィールドを使って次のようにすると分かりやすいです。

```
struct Type_id_aa64isar0_el1 {
  int resv0 : 4;
  int aes : 4;
  int sha1 : 4;
  int sha2 : 4;
  int crc32 : 4;
  int atomic : 4;
  int resv1 : 4;
  int rdm : 4;
  int resv2 : 12;
  int dp : 4;
  int resv3 : 16;
};

inline Type_id_aa64isar0_el1 get_id_aa64isar0_el1() {
  Type_id_aa64isar0_el1 x;
  asm __volatile__("mrs %[x], id_aa64isar0_el1" : [ x ] "=r"(x));
  return x;
}
```

使い方

```
Type_id_aa64isar0_el1 isar0 = get_id_aa64isar0_el1();
if (isar0.aes == 2) {
  // AESが使える
}
```

FX700での結果。

```
aes=2
sha1=1
sha2=1
crc32=1
atomic=2
rdm=1
dp=0
```

## SVE

SVEが利用できるかは[ID_AA64PFR0_EL1](https://developer.arm.com/documentation/100403/0200/register-descriptions/aarch64-system-registers/id-aa64pfr0-el1--aarch64-processor-feature-register-0--el1)を見ます。
が、現時点(2021/1/6)でSVEについては書かれていません。
[Linux Arm64 Documentation](http://blog.foool.net/wp-content/uploads/linuxdocs/arm64.pdf)5.4 4. List of registers with visible featuresには書かれているのでそちらを参照します。

```
struct Type_id_aa64pfr0_el1 {
  int el0 : 4;
  int el1 : 4;
  int el2 : 4;
  int el3 : 4;
  int fp : 4;
  int advsimd : 4;
  int gic : 4;
  int ras : 4;
  int sve : 4;
  int resv0 : 28;
};

inline Type_id_aa64pfr0_el1 get_id_aa64pfr0_el1() {
  Type_id_aa64pfr0_el1 x;
  asm __volatile__("mrs %[x], id_aa64pfr0_el1" : [ x ] "=r"(x));
  return x;
}
```

使い方

```
Type_id_aa64pfr0_el1 pfr0 = get_id_aa64pfr0_el1();
if (pfr0.sve == 1) {
  // SVEが使える
}
```

FX700での結果

```
ctest:module=aa64pfr0_el1
el0=1
el1=1
el2=0
el3=0
fp=1
advsimd=1
gic=0
ras=0
sve=1
```

## SVEのビット長

SVEのビット長はアーキテクチャによって異なります。
今のところA64FXの512bitしかありませんが、[ZCR_EL1](https://developer.arm.com/docs/ddi0595/h/aarch64-system-registers/zcr_el1)で取れるようです。

intrinsic命令は対応していないようなのでbyteコードを直接埋め込んで試してみました。

```
uint64_t get_zcr_el1()
{
  uint64_t x;
  // mrs x0, zcr_el1
  asm __volatile__(".inst 0xd5381200":"=r"(x));
  return x;
}

```

しかしFX700で試したところIllegal instructionで落ちました(qemuでも落ちた)。

Linuxならシステムコールprctlが使えます。

```
#include <stdio.h>
#include <sys/prctl.h>

int main()
{
  int x = prctl(PR_SVE_GET_VL);
  x = prctl(PR_SVE_GET_VL);
  printf("sve len=%08x\n", x);
  return 0;
}
```

qemuでSVEの長さを変更して試すと値が変わることが分かります。

```
% env QEMU_LD_PREFIX=/usr/aarch64-linux-gnu qemu-aarch64 -cpu max,sve512=on ./a.out
sve len=00000040

% env QEMU_LD_PREFIX=/usr/aarch64-linux-gnu qemu-aarch64 -cpu max,sve2048=on ./a.out
sve len=00000100
```

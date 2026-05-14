---
title: "ML-KEMのNTTにおけるMontgomery乗算の最適化手法詳解2"
emoji: "📖"
type: "tech"
topics: ["mlkem", "ntt", "montgomery", "simd", "aarch64"]
published: false
---
## 初めに
[前回](https://zenn.dev/herumi/articles/ml-kem-montgomery1)ではML-KEMのNTTにおけるMontgomery乗算のAVX向け最適化手法を解説しました。
今回はAArch64用SIMDでの最適化手法を解説します。

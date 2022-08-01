---
title: "多倍長整数の実装7（Pythonによるasmコード生成）"
emoji: "🧮"
type: "tech"
topics: ["mulx", "adox", "xbyak", "Python", "DSL"]
published: true
---
## 初めに

今回はN桁xN桁の固定多倍長整数の乗算の実装を高速化するための乗算後加算を実装します。
記事全体の一覧は[多倍長整数の実装1（C/C++）](https://zenn.dev/herumi/articles/bitint-01-cpp)参照。

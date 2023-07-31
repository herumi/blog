---
title: "完全準同型暗号CKKSその2 エンコーディング"
emoji: "🧮"
type: "tech"
topics: ["完全準同型暗号", "CKKS", "エンコーディング"]
published: false
---
## 初めに

## 実数係数多項式環
今まで複素数係数多項式を考えてきましたが、実数係数多項式を考えてみましょう。
複素数$z=a+bi$（$a,b \in ℝ）$に対して、複素共役を$conj(z):=a-bi$と書きます（チルダ$\tilde{z}$は表記しづらいので$conj$を使用）。複素数$z$, $w$の積の複素共役はそれぞれの複素共役です。$conj(zw)=conj(z)conj(w).$ 実数$a$の複素共役は変化しません。$conj(a)=a.$
さて、$\xi^u$について$conj(\xi^u)=\xi^{M-u}$ for $u=1, \dots, M-1$です。1の8乗根の例を見ても、$conj(\xi)=\xi^7$, $conj(\xi^2)=\xi^6$, $conj(\xi^3)=\xi^5$, $conj(\xi^4)=\xi^4$が見て取れます。
$f$が実数係数多項式なら$conj(f(\xi^{2j-1}))=f(conf(\xi^{2j-1}))=f(\xi^{M-2j+1})$なので
$f(\xi^{2j-1})$の前半$j=1, \dots, N/2$だけで$σ$は復元できます。そこで、$ℂ[X]$の前半だけを取り出す写像を

$$
π:  ℂ^N \ni (z_1, \dots, z_N) \mapsto (z_1, \dots, z_{N/2}) \in ℂ^{N/2}
$$

とする。
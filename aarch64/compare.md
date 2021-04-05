# ノーマルじゃない数値との演算

[実験コード](https://github.com/herumi/misc/blob/master/sve/cmp-test.cpp)

## floatの種類

符号s|指数部e|仮数部f
-|-|-
1|8|23

- 0 < e < 255 ; 普通の数(normal)
- e = 0
  - f = 0 ; ゼロ(zero)
  - f != 0 ; 非正規数(subnormal)
- e = 255
  - f = 0 ; 無限大(infinite)
  - f != 0 ; 非数(NaN)
    - (f >> 22) == 1 ; quiet NaN
    - (f >> 22) == 0 ; signaling NaN

## 演算結果

- fexpa ; 17ビットシフトした状態で仮数部はそのまま
- fmin, fmax ; NaNはNaNのまま
- frintm(floor) ; NaNはNaNのまま
- fcvtzs ; NaNはゼロになる

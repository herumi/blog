from dataclasses import dataclass

@dataclass(frozen=True)
class Curve:
  name: str  # curve name
  p: int  # characteristic
  a: int
  b: int # y^2 = x^3 + a x + b
  x: int
  y: int # base point
  r: int = 0  # order

CurveTbl = {cv.name: cv for cv in [
  Curve('secp256k1',
    p = 0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f,
    a = 0,
    b = 7,
    r = 0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141,
    x = 0x79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798,
    y = 0x483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8,
  ),
  Curve('NIST_P256',
    p = 0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff,
    a = -3,
    b = 0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b,
    x = 0x6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296,
    y = 0x4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5,
    r = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551,
  ),
  Curve('NIST_P384',
    p = 0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffeffffffff0000000000000000ffffffff,
    a = -3,
    b = 0xb3312fa7e23ee7e4988e056be3f82d19181d9c6efe8141120314088f5013875ac656398d8a2ed19d2a85c8edd3ec2aef,
    x = 0xaa87ca22be8b05378eb1c71ef320ad746e1d3b628ba79b9859f741e082542a385502f25dbf55296c3a545e3872760ab7,
    y = 0x3617de4a96262c6f5d9e98bf9292dc29f8f41dbd289a147ce9da3113b5f0b8c00a60b1ce1d7e819d7a431d7c90ea0e5f,
    r = 0xffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372ddf581a0db248b0a77aecec196accc52973,
  ),
]}

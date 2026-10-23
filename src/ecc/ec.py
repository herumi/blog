from fp import Fp
from fr import Fr
import curve

class Ec:
    # E : y^2 = x^3 + ax + b mod p. r is the order of E
    @classmethod
    def init(cls, a, b, r):
        if type(a) is int:
            a = Fp(a)
        if type(b) is int:
            b = Fp(b)
        cls.a = a
        cls.b = b
        cls.r = r

    def __init__(self, x=None, y=None, doVerify=True):
        self.isZero = True
        if x is None and y is None:
            return
        if type(x) is int:
            x = Fp(x)
        if type(y) is int:
            y = Fp(y)
        self.x = x
        self.y = y
        self.isZero = False
        if doVerify and not self.isValid():
            raise Exception(f"isValid x={x}, y={y}")
    def __str__(self):
        if self.isZero:
            return "O"
        else:
            return f"({self.x}, {self.y})"
    def __eq__(self, rhs):
        if self.isZero:
            return rhs.isZero
        if rhs.isZero:
            return False
        return self.x == rhs.x and self.y == rhs.y

    def isValid(self):
        if self.isZero:
            return True
        return self.y * self.y == (self.x * self.x + self.a) * self.x + self.b

    def __neg__(self):
        if self.isZero:
            return self
        return Ec(self.x, -self.y, False)

    def __add__(self, rhs):
        if self.isZero:
            return rhs
        if rhs.isZero:
            return self
        x1 = self.x
        y1 = self.y
        x2 = rhs.x
        y2 = rhs.y
        if x1 == x2:
            # P + (-P) = 0
            if y1 == -y2:
                return Ec()
            # dbl
            L = x1 * x1
            L = (L + L + L + self.a) / (y1 + y1)
        else:
            L = (y1 - y2) / (x1 - x2)
        x3 = L * L - (x1 + x2)
        y3 = L * (x1 - x3) - y1
        return Ec(x3, y3, False)

    def __mul__(self, rhs):
        if type(rhs) is Fr:
            rhs = rhs.v
        elif type(rhs) is not int:
            raise Exception("bad type", rhs)
        if rhs == 0:
            return Ec()
        bs = bin(rhs)[2:]
        ret = Ec()
        for b in bs:
            ret += ret
            if b == '1':
                ret += self
        return ret

def init(curveName: str):
    cv = curve.CurveTbl[curveName]
    Fp.init(cv.p)
    Fr.init(cv.r)
    Ec.init(cv.a, cv.b, cv.r)
    P = Ec(cv.x, cv.y)
    return P

def main():
    for curveName in ['secp256k1', 'NIST_P384']:
        P = init(curveName)
        Q = Ec()
        for i in range(200):
            R = P * i
            assert Q == R, f"mul i={i}"
            Q += P
        assert (P * Ec.r).isZero, "order"
        a = 12345678932
        b = 98763445345
        aP = P * a
        bP = P * b
        assert aP * b == bP * a, "DH"
        print(f"{curveName}: ok")

if __name__ == '__main__':
    main()

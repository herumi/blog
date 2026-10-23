import sys
from ec import Ec, init


'''
>openssl s_client -connect dv-g3b-emudhra.we-have-a-problem.com:443 -servername dv-g3b-emudhra.we-have-a-problem.com -showcerts > emudhra.pem
>openssl x509 -in bad.pem -noout -pubkey | openssl ec -pubin -text
read EC key
Public-Key: (384 bit)
pub:
    04:e7:9e:d9:c0:4a:97:73:c4:73:5e:51:11:1d:a8:
    5a:c2:bd:5e:64:ab:37:4b:80:e1:2c:bc:20:38:ec:
    1e:4f:0e:3b:81:2f:c9:85:9e:1e:67:af:b0:4e:2f:
    8b:c9:7f:74:85:ba:65:aa:91:1e:e8:a4:e4:5b:62:
    d5:cc:03:d5:6b:e6:78:b3:c3:8c:b4:dc:93:7c:a2:
    46:0e:23:84:53:c8:92:93:5d:29:d4:4f:91:e1:56:
    06:b6:3c:ce:da:b4:68
ASN1 OID: secp384r1
NIST CURVE: P-384
writing EC key
-----BEGIN PUBLIC KEY-----
MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE557ZwEqXc8RzXlERHahawr1eZKs3S4Dh
LLwgOOweTw47gS/JhZ4eZ6+wTi+LyX90hbplqpEe6KTkW2LVzAPVa+Z4s8OMtNyT
fKJGDiOEU8iSk10p1E+R4VYGtjzO2rRo
-----END PUBLIC KEY-----
'''

pubKeyStr = '''
    04:e7:9e:d9:c0:4a:97:73:c4:73:5e:51:11:1d:a8:
    5a:c2:bd:5e:64:ab:37:4b:80:e1:2c:bc:20:38:ec:
    1e:4f:0e:3b:81:2f:c9:85:9e:1e:67:af:b0:4e:2f:
    8b:c9:7f:74:85:ba:65:aa:91:1e:e8:a4:e4:5b:62:
    d5:cc:03:d5:6b:e6:78:b3:c3:8c:b4:dc:93:7c:a2:
    46:0e:23:84:53:c8:92:93:5d:29:d4:4f:91:e1:56:
    06:b6:3c:ce:da:b4:68
'''

def loadPoint():
 # non-compressed point 04||X||Y
 h = ''.join(pubKeyStr.replace('\n','').replace(' ', '').split(':'))
 assert(h[:2] == '04')
 body = h[2:]
 n = len(body) // 2
 x = int(body[:n], 16)
 y = int(body[n:], 16)
 return Ec(x, y)

def recover(curveName):
  P = init('NIST_P384')
  Q = loadPoint()
  print(f'P={P}')
  print(f'Q={Q}')
  acc = Ec()
  for x in range(256):
    if acc == Q:
      return x, P, Q
    acc += P
  return None, P, Q

def main():
  maxD = 256
  curveName = 'NIST_P384'
  x, P, Q = recover(curveName)
  if x is None:
    print(f'not found in 0..{maxD-1}')
    return
  print(f'{curveName}: found x = {x} (0x{x:0x})')
  assert P * x == Q, 'verify failed'
  print('verified: x*P == Q')

if __name__ == '__main__':
  main()

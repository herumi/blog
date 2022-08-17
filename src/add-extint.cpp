#include <stdint.h>
typedef unsigned _ExtInt(256) uint256_t;
typedef unsigned _ExtInt(256+64) uint320_t;

uint64_t mulPre256(uint256_t *pz, const uint256_t *px, uint64_t y)
{
  uint256_t x = *px;
  uint320_t z = uint320_t(x) * uint320_t(y);
  *pz = uint256_t(z);
  return uint64_t(z >> 256);
}

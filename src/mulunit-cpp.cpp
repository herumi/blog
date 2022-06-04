/*
	clang -O2 unrolls loops, but gcc does not it.
	gcc -O2 -funroll-loops does it, but the generated code is not good.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef _WIN32
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

typedef uint64_t Unit;

template<size_t N, size_t I = 0>
Unit addT(Unit *z, const Unit *x, const Unit *y, uint8_t c = 0)
{
	if constexpr (I < N) {
		c = _addcarry_u64(c, x[I], y[I], (unsigned long long *)&z[I]);
		return addT<N, I + 1>(z, x, y, c);
	} else {
		return c;
	}
}

#if !defined(_MSC_VER) || defined(__INTEL_COMPILER) || defined(__clang__)
typedef __attribute__((mode(TI))) unsigned int uint128_t;
#define MCL_DEFINED_UINT128_T
#endif

inline uint64_t mulUnit1(uint64_t *pH, uint64_t x, uint64_t y)
{
#ifdef MCL_DEFINED_UINT128_T
  uint128_t t = uint128_t(x) * y;
  *pH = uint64_t(t >> 64);
  return uint64_t(t);
#else
  return _umul128(x, y, pH);
#endif
}

template<size_t N>
Unit mulUnitT(Unit *z, const Unit *x, const Unit y)
{
	Unit L[N];
	Unit H[N];
	for (size_t i = 0; i < N; i++) {
		L[i] = mulUnit1(&H[i], x[i], y);
	}
	z[0] = L[0];
	return H[N - 1] + addT<N - 1>(z + 1, L + 1, H);
}

extern "C" Unit mulUnit4(Unit *z, const Unit *x, Unit y)
{
	return mulUnitT<4>(z, x, y);
}

template<size_t N>
uint64_t mulUnitT2(uint64_t *z, const uint64_t *x, uint64_t y)
{
  unsigned long long L, H;
  uint8_t c = 0;
  z[0] = _mulx_u64(x[0], y, &L);
  for (size_t i = 1; i < N; i++) {
    uint64_t t = _mulx_u64(x[i], y, &H);
    c = _addcarry_u64(c, t, L, (unsigned long long *)&z[i]);
    L = H;
  }
  _addcarry_u64(c, 0, L, &H);
  return H;
}

extern "C" Unit mulUnit2_4(Unit *z, const Unit *x, Unit y)
{
	return mulUnitT2<4>(z, x, y);
}


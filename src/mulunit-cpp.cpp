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

typedef __attribute__((mode(TI))) unsigned int uint128_t;

template<size_t N>
Unit mulUnitT(Unit *z, const Unit *x, const Unit y)
{
	Unit L[N];
	Unit H[N];
	for (size_t i = 0; i < N; i++) {
		uint128_t v = uint128_t(x[i]) * y;
		L[i] = uint64_t(v);
		H[i] = uint64_t(v >> 64);
	}
	z[0] = L[0];
	return H[N - 1] + addT<N - 1>(z + 1, L + 1, H);
}

extern "C" Unit mulUnit4(Unit *z, const Unit *x, Unit y)
{
	return mulUnitT<4>(z, x, y);
}


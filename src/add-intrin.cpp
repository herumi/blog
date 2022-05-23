#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef _WIN32
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

typedef uint64_t Unit;

typedef uint64_t Unit;

template<size_t N>
Unit addT(Unit *z, const Unit *x, const Unit *y)
{
	uint8_t c = 0;
	for (size_t i = 0; i < N; i++) {
		_addcarry_u64(c, x[i], y[i], &z[i]);
	}
	return c;
}

extern "C" Unit add4(Unit *z, const Unit *x, const Unit *y)
{
	return addT<4>(z, x, y);
}
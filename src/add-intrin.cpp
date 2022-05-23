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

typedef uint64_t Unit;

#ifdef __GNUC__
#pragma GCC push_options
#pragma GCC optimize ("unroll-loops") // or -funroll-loops
#endif
template<size_t N>
Unit addT(Unit *z, const Unit *x, const Unit *y)
{
	uint8_t c = 0;


	for (size_t i = 0; i < N; i++) {
		c = _addcarry_u64(c, x[i], y[i], (unsigned long long*)&z[i]);
	}
	return c;
}
#ifdef __GNUC__
#pragma GCC pop_options
#endif

extern "C" Unit add4(Unit *z, const Unit *x, const Unit *y)
{
	return addT<4>(z, x, y);
}

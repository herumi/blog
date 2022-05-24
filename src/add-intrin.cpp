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

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize ("unroll-loops") // or -funroll-loops
#endif
template<size_t N>
static inline Unit addT(Unit *z, const Unit *x, const Unit *y)
{
	uint8_t c = 0;


	for (size_t i = 0; i < N; i++) {
		c = _addcarry_u64(c, x[i], y[i], (unsigned long long*)&z[i]);
	}
	return c;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

extern "C" Unit add4(Unit *z, const Unit *x, const Unit *y)
{
	return addT<4>(z, x, y);
}

#if  __cplusplus >= 201703L
template<size_t N, size_t I = 0>
Unit Unroll(uint8_t c, Unit *z, const Unit *x, const Unit *y)
{
	if constexpr (I < N) {
		c = _addcarry_u64(c, x[I], y[I], (unsigned long long *)&z[I]);
		return Unroll<N, I + 1>(c, z, x, y);
	}
	return c;
}

extern "C" Unit add4_2(Unit *z, const Unit *x, const Unit *y)
{
	return Unroll<4>(0, z, x, y);
}
#endif

template<size_t N, size_t I = N>
struct UnrollT {
	static inline Unit call(uint8_t c, Unit *z, const Unit *x, const Unit *y) {
		c = _addcarry_u64(c, x[N - I], y[N - I], (unsigned long long *)&z[N - I]);
		return UnrollT<N, I - 1>::call(c, z, x, y);
	}
};

template<size_t N>
struct UnrollT<N, 0> {
	static inline Unit call(uint8_t c, Unit *, const Unit *, const Unit *) {
		return c;
	}
};

extern "C" Unit add4_3(Unit *z, const Unit *x, const Unit *y)
{
	return UnrollT<4>::call(0, z, x, y);
}

int version()
{
	return __cplusplus;
}

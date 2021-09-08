#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <cybozu/bit_operation.hpp>

uint32_t f2u(float ilog2)
{
	uint32_t u;
	memcpy(&u, &ilog2, sizeof(u));
	return u;
}

float u2f(uint32_t u)
{
	float ilog2;
	memcpy(&ilog2, &u, sizeof(ilog2));
	return ilog2;
}

uint64_t d2u(double x)
{
	uint64_t u;
	memcpy(&u, &x, 8);
	return u;
}

double u2d(uint64_t x)
{
	double d;
	memcpy(&d, &x, 8);
	return d;
}

uint64_t mask(uint64_t x)
{
	return (uint64_t(1) << x) - 1;
}

void putH(double x)
{
	uint64_t u = d2u(x);
	printf("[e:%3d m:%016llx]\n", int(u >> 52) - 1023, u & mask(52));
}

void putH(const char *msg, double x)
{
	printf("%8s=", msg);
	putH(x);
}

double f1(uint64_t x)
{
	double a = 4503599627370497. * x;
	return a;
}

double f2(double x)
{
	double b = 0.99999999999999989 * x;
	return b;
}

double g1(uint64_t x)
{
	uint64_t u = d2u(double(x));
	uint64_t c = (u >> 51) & 1;
	double d = u2d(u + (52ull << 52) + 1 + c);
	return d;
}

double g2(double x)
{
	uint64_t u = d2u(x);
	double d = u2d(u - 1);
	return d;
}

uint64_t bsr_shift(uint64_t x)
{
//putH("x", x);
	double a = f1(x);
	double b = f2(a);
	double s = g1(x);
	double t = g2(s);
	if (a != s || b != t) {
		printf("err x=%llx\n", x);
		putH("a", a);
		putH("b", b);
		putH("g1", s);
		putH("g2", t);
//		exit(1);
	}
	return (uint64_t)(a - b);
}

uint64_t g3(uint64_t x)
{
	double a = g1(x);
	double b = g2(a);
	return uint64_t(a - b);
}

uint32_t ilog2(uint64_t x)
{
	return (d2u(double(x)) >> 52) - 1023;
}

void put(uint64_t x)
{
	printf("bsr_shift(%llx)=%llx, g3=%llx, %d, %d\n", x, bsr_shift(x), g3(x), ilog2(x), cybozu::bsr(x));
}

void check(uint64_t x)
{
	uint64_t y = uint64_t(1) << cybozu::bsr(x);
	uint64_t a = bsr_shift(x);
	uint64_t b = g3(x);
	if (a != y || b != y) {
		printf("err x=%llx y=%llx a=%llx b=%llx\n", x, y, a, b);
//		exit(1);
	}
}

int main()
{
	double a = 4503599627370497.; // (1 << 52) + 1
	double b = 0.99999999999999989; // 0x3fefffffffffffff
	putH("a", a);
	putH("b", b);
#if 0
	{
		uint32_t x = 0xffffffff;
		while (x > 0) {
			check(x);
			x--;
		}
		puts("32bit all ok");
	}
#endif
	for (int i = 0; i < 64; i++) {
		uint64_t x = 1ull << i;
		printf("x=%llx cast=%llx\n", x, (uint64_t)(double)x);
		putH("x", x);
		check(x);
		x--;
		if (x == 0) continue;
		printf("x=%llx cast=%llx\n", x, (uint64_t)(double)x);
		putH("x", x);
		check(x);
	}
	const uint64_t tbl[] = {
		1,
		2,
		3,
		(1ull << 31) + 0x12345,
		(1ull << 32) + 0x12345,
		3ull << 50,
		7ull << 50,
		127ull << 48,
		1ull << 63,
		~(0ull) << 50,
		~(0ull) << 51,
		~(0ull),
	};
	for (size_t i = 0; i < CYBOZU_NUM_OF_ARRAY(tbl); i++) {
		uint64_t x = tbl[i];
		putH("x", x);
		check(x);
	}
	puts("tbl ok");
	return 0;
}

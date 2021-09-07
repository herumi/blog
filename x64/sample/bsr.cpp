#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <cybozu/bit_operation.hpp>

uint32_t bsr_double(uint32_t x)
{
	double q = 4503599627370497. * x;
	return (uint32_t)(q - 0.99999999999999989 * q);
}

uint32_t f2u(float f)
{
	uint32_t u;
	memcpy(&u, &f, sizeof(u));
	return u;
}

float u2f(uint32_t u)
{
	float f;
	memcpy(&f, &u, sizeof(f));
	return f;
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

void putH(double x)
{
	printf("%016llx\n", (long long)d2u(x));
}

uint32_t f(uint32_t x)
{
#if 1
	return (f2u(x) >> 23) - 127;
#else
	return (d2u(x) >> 52) - 1023;
#endif
}

void put(uint32_t x)
{
	printf("bsr_double(%x)=%x, %d, %d\n", x, bsr_double(x), f(x), cybozu::bsr(x));
}

int main()
{
	double a = 4503599627370497.; // (1 << 52) + 1
	double b = 0.99999999999999989; // 0x3fefffffffffffff
	// 2^(-1)*(1+(2^52-1)/2^52)
	putH(a);
	putH(b);
	putH(u2d((uint64_t(1023+52) << 52) + 1));
	put(1);
	put(2);
	put(3);
	for (int i = 2; i < 32; i++) {
		uint32_t x = 1 << i;
		put(x-1);
		put(x);
		put(x+1);
	}
}
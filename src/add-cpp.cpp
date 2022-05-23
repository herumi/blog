#include <stdint.h>
#include <stdlib.h>

typedef uint64_t Unit;

template<size_t N>
Unit addT(Unit *z, const Unit *x, const Unit *y)
{
  Unit c = 0; // Å‰‚ÍŒJ‚èã‚ª‚è‚Í‚È‚¢
  for (size_t i = 0; i < N; i++) {
    Unit xc = x[i] + c;
    c = xc < c;
    Unit yi = y[i];
    xc += yi;
    c += xc < yi;
    z[i] = xc;
  }
  return c;
}

extern "C" Unit add4(Unit *z, const Unit *x, const Unit *y)
{
	return addT<4>(z, x, y);
}
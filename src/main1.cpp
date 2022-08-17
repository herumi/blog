#include <stdio.h>
#include <stdint.h>

extern "C" int add(int x, int y);

int main()
{
  for (int i = 0; i < 10; i++) {
    printf("%d + %d = %d\n", i, i + 3, add(i, i + 3));
  }
}


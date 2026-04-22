#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// credit kinda: my friend I was hanging out with who just was like
// what if you did a random thread sleep
//
// but then I realized we don't have threads
// so instead we just do a random operation
//
// thanks friend
//
// Brief explanation:
// Because division broadly took more time than multiplication, this
// adds a randomly sampled operation to two throwaway variables. 
//
// In the case of a multiplication, it masks with a division.
// In the case of a division, it masks with a multiplication.
//
// Surely this has no issues.
//
// Also I disable compiler optimizations here so we don't optimize out these values
#pragma GCC push_options
#pragma GCC optimize("O0")
void evil_cpu(bool mul_div) { // True: Mul, False: Div
    int x = rand();
    int y = rand();
    int z = 0;

    if(mul_div) {
        z = x * y; 
    } else {
        z = x / y;
    }
}
#pragma GCC pop_options

// This is kinda dumb but I'm keeping it
//
// Basically I saw that generally the zero multiplication
// would take similar time to the r1 * r2 multiplication,
// but would generate more instructions. I assume this is due to
// compiler optimizations, so I GET RID OF THEM! AHA!
#pragma GCC push_options
#pragma GCC optimize("O0")
void mul(long long unsigned int *count, int r1, int r2, int ind) {
  for (int i = 0; i < ind; i++) {
    *count += r1 * r2;
    evil_cpu(false);
  }
}
#pragma GCC pop_options

int main(int argc, const char **argv) {
  if (argc < 2) {
    printf("No input data! Exiting\n");
    return -1;
  }

  unsigned long long r1 = 1804289383ULL;
  unsigned long long r2 = 846930886ULL;

  unsigned long long count = 0;
  if (strcmp(argv[1], "supersecretdata") == 0) {
    mul(&count, r1, r2, 4096);
  } else if (strcmp(argv[1], "notsosecretdata") == 0) {
    for (int i = 0; i < 4096; i++) {
      count += r1 / r2;
            evil_cpu(true);
    }
  } else {
    mul(&count, r1, 0, 4096);
  }

  printf("%llu\n", count);

  return 0;
}

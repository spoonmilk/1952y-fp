#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// NOTE: I'm keeping this here mainly because I thought it would be
// fun, but it came to mind that... well... perhaps we don't have
// random variables? At least, it crashed the cpu... so
//
// credit kinda: my friend I was hanging out with who just was like
// what if you did a random thread sleep
//
// but then I realized we don't have threads
// so instead we just do a random operation
//
// thanks friend
//
// I think the random noise might actually help
// void evil_cpu() {
//   int x = rand();
//   int y = rand();
//   int z = 0;
//   int muldiv = rand() % 2;
//   int time_run = rand() % 100;
// 
//   for (int i = 0; i < time_run; i++) {
//     if (muldiv) {
//       z = x * y;
//     } else {
//       z = x / y;
//     }
//   }
// }

// This might be stupid, but why not just do everything?
//
// I think we could feasibly get this to still work without
// disabling compiler optimizations because the scopes here are a little
// evil and static analysis likely cannot determine which t1, t2, t3 are doing
// which thing, but I disable them regardless.
void step(unsigned long long *t1, unsigned long long *t2, unsigned long long *t3, unsigned long long r1, unsigned long long r2) {
  *t1 = r1 / r2;
  *t2 = r1 * r2;
  *t3 = r1 * 0;
  // evil_cpu(); // shits and giggles
}

int main(int argc, const char **argv) {
  if (argc < 2) {
    printf("No input data! Exiting\n");
    return -1;
  }

  unsigned long long r1 = 1804289383ULL;
  unsigned long long r2 = 846930886ULL;

  unsigned long long count = 0;
  unsigned long long t1, t2, t3 = 0;

  if (strcmp(argv[1], "supersecretdata") == 0) {
    for (int i = 0; i < 4096; i++) {
      step(&t1, &t2, &t3, r1, r2);
      count += t2;
    }
  } else if (strcmp(argv[1], "notsosecretdata") == 0) {
    for (int i = 0; i < 4096; i++) {
      step(&t1, &t2, &t3, r1, r2);
      count += t1;
    }
  } else {
    for (int i = 0; i < 4096; i++) {
      step(&t1, &t2, &t3, r1, r2);
      count += t3;
    }
  }

  printf("%llu\n", count);

  return 0;
}


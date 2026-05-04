// stencil.c
// 1D 3-point stencil: smooths an array repeatedly
//   riscv64-unknown-linux-gnu-gcc -O3 -march=rv64gcv -mabi=lp64d \
//     -fno-math-errno -funsafe-math-optimizations \
//     -fopt-info-vec -static stencil.c -o stencil.rvv

#include <stdio.h>
#include <stdlib.h>

#define N 256    // was 4096 — fits easily in 1KB L1
#define ITERS 50 // was 500

static float a[N] __attribute__((aligned(64)));
static float b[N] __attribute__((aligned(64)));

int main(void) {
    for (int i = 0; i < N; i++) a[i] = (float)(i & 0xFF);

    // repeated 3-point smoothing
    for (int iter = 0; iter < ITERS; iter++) {
        for (int i = 1; i < N - 1; i++) {
            b[i] = 0.25f * a[i-1] + 0.5f * a[i] + 0.25f * a[i+1];
        }
        // swap roles for next iter
        for (int i = 1; i < N - 1; i++) {
            a[i] = b[i];
        }
    }

    // print a checksum so the compiler can't dead-code-eliminate everything
    float sum = 0.0f;
    for (int i = 0; i < N; i++) sum += a[i];
    printf("%f\n", sum);
    return 0;
}
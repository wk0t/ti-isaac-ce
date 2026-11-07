#include "rng.h"

static uint32_t st = 0x9E3779B9u;

void rng_seed(uint32_t s)
{
    st = s;
    if (!st) st = 0xDEADBEEFu;
    rng_next();
}

uint32_t rng_next(void)
{
    st ^= st << 13;
    st ^= st >> 17;
    st ^= st << 5;
    return st;
}

uint8_t rng_range(uint8_t n)
{
    if (!n) return 0;
    return (uint8_t)(rng_next() % n);
}

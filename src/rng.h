#ifndef ISAAC_RNG_H
#define ISAAC_RNG_H

#include <stdint.h>

void     rng_seed(uint32_t s);
uint32_t rng_next(void);
uint8_t  rng_range(uint8_t n);        /* 0..n-1, n>0 */

#endif

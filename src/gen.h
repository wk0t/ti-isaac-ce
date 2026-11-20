#ifndef ISAAC_GEN_H
#define ISAAC_GEN_H

#include "types.h"

/* Generates floor `floor` (1 or 2) into g. */
void gen_floor(Game *g, uint8_t floor);

/* Room index from a grid cell (0 = invalid). */
uint8_t gen_room_at(const Game *g, uint8_t cx, uint8_t cy);

/* Neighbor cell (dir: DIR_*); 0 if empty. */
uint8_t gen_neighbor(const Game *g, uint8_t cx, uint8_t cy, uint8_t dir);

/* Room indexes (grid): returns idx+1; 0 if empty. */
uint8_t gen_degree(const Game *g, uint8_t cx, uint8_t cy);

/* Number of rooms reachable from cell (cx,cy) (BFS). */
uint8_t gen_reachable(const Game *g, uint8_t cx, uint8_t cy);

#endif

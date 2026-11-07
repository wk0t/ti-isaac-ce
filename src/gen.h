#ifndef ISAAC_GEN_H
#define ISAAC_GEN_H

#include "types.h"

/* Genere l'etage `floor` (1 ou 2) dans g. */
void gen_floor(Game *g, uint8_t floor);

/* Index de salle a partir d'une case de la grille (0 = invalide). */
uint8_t gen_room_at(const Game *g, uint8_t cx, uint8_t cy);

/* Case voisine (dx:DIR_*) ; 0 si vide. */
uint8_t gen_neighbor(const Game *g, uint8_t cx, uint8_t cy, uint8_t dir);

/* Indices de salle (grid) : retourne idx+1 ; 0 si vide. */
uint8_t gen_degree(const Game *g, uint8_t cx, uint8_t cy);

/* Nombre de SALLES atteignables depuis la case (cx,cy) (BFS). */
uint8_t gen_reachable(const Game *g, uint8_t cx, uint8_t cy);

#endif

#ifndef ISAAC_SELFTEST_H
#define ISAAC_SELFTEST_H

/* Execute la suite de self-tests embarques. Renvoie 0 si OK, sinon un code.
   Affiche l'etat a l'ecran (graphx doit etre actif). */
uint8_t selftest_run(void);

#endif

#ifndef ISAAC_SELFTEST_H
#define ISAAC_SELFTEST_H

/* Runs the embedded self-test suite. Returns 0 if OK, else a code.
   Displays the state on screen (graphx must be active). */
uint8_t selftest_run(void);

#endif

#ifndef TG_RACE_COMMON_H
#define TG_RACE_COMMON_H

#include <stdint.h>

// Helpers shared by the translated race routines.

// $00:A89E with A = 0: the next random byte (also left in $1EF7). In drive.c.
uint16_t race_random(void);

#endif

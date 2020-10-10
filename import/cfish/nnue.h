#ifndef NNUE_H
#define NNUE_H

#include <stdint.h>

#include "nnueif.h"

bool nnue_init(const char *evalFile);
Value nnue_evaluate(const Position *pos);

#endif

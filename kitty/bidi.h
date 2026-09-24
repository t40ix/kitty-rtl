/*
 * bidi.h
 * Lightweight line-level BiDi reordering for kitty.
 */

#pragma once

#include "line.h"
#include <stdbool.h>

bool line_has_bidi(const Line *line);
bool bidi_reorder_line(const Line *line, CPUCell *visual_cells, ListOfChars *lc);
index_type bidi_log2vis(const Line *line, index_type log_x);

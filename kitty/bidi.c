/*
 * bidi.c
 * Lightweight line-level BiDi reordering for kitty.
 */

#include "bidi.h"
#include <fribidi.h>
#include <stdlib.h>
#include <string.h>

static inline bool is_bidi_codepoint(char_type cp) {
    return (cp >= 0x0590 && cp <= 0x08FF) || // Hebrew, Arabic, Syriac, Thaana
           (cp >= 0xFB1D && cp <= 0xFDFF) || // Hebrew & Arabic Presentation Forms A
           (cp >= 0xFE70 && cp <= 0xFEFF);   // Arabic Presentation Forms B
}

bool line_has_bidi(const Line *line) {
    if (!line || !line->cpu_cells || line->xnum == 0) return false;
    for (index_type i = 0; i < line->xnum; i++) {
        const CPUCell *c = line->cpu_cells + i;
        if (!c->ch_is_idx) {
            if (is_bidi_codepoint(c->ch_or_idx)) return true;
        } else {
            ListOfChars lc;
            text_in_cell(c, line->text_cache, &lc);
            for (unsigned int k = 0; k < lc.count; k++) {
                if (is_bidi_codepoint(lc.chars[k])) return true;
            }
        }
    }
    return false;
}

bool bidi_reorder_line(const Line *line, CPUCell *visual_cells, ListOfChars *lc) {
    index_type n = line->xnum;
    if (n == 0) return false;

    // Use stack buffers for typical terminal widths (up to 1024 columns)
    FriBidiChar stack_uni_chars[1024];
    FriBidiCharType stack_bidi_types[1024];
    FriBidiJoiningType stack_joining_types[1024];
    FriBidiLevel stack_levels[1024];
    FriBidiStrIndex stack_vis_map[1024];

    FriBidiChar *uni_chars = stack_uni_chars;
    FriBidiCharType *bidi_types = stack_bidi_types;
    FriBidiJoiningType *joining_types = stack_joining_types;
    FriBidiLevel *levels = stack_levels;
    FriBidiStrIndex *vis_map = stack_vis_map;

    bool allocated = false;
    if (n > 1024) {
        uni_chars = malloc(sizeof(FriBidiChar) * n);
        bidi_types = malloc(sizeof(FriBidiCharType) * n);
        joining_types = malloc(sizeof(FriBidiJoiningType) * n);
        levels = malloc(sizeof(FriBidiLevel) * n);
        vis_map = malloc(sizeof(FriBidiStrIndex) * n);
        if (!uni_chars || !bidi_types || !joining_types || !levels || !vis_map) {
            free(uni_chars); free(bidi_types); free(joining_types); free(levels); free(vis_map);
            return false;
        }
        allocated = true;
    }

    for (index_type i = 0; i < n; i++) {
        const CPUCell *c = line->cpu_cells + i;
        char_type ch = 0;
        if (c->ch_is_idx) {
            text_in_cell(c, line->text_cache, lc);
            ch = lc->count ? lc->chars[0] : ' ';
        } else {
            ch = c->ch_or_idx;
        }
        if (ch == 0) ch = ' ';
        uni_chars[i] = (FriBidiChar)ch;
        vis_map[i] = i;
    }

    fribidi_get_bidi_types(uni_chars, n, bidi_types);
    fribidi_get_joining_types(uni_chars, n, joining_types);

    FriBidiParType base_dir = FRIBIDI_PAR_ON;
    FriBidiLevel max_level = fribidi_get_par_embedding_levels_ex(bidi_types, NULL, n, &base_dir, levels);
    (void)max_level;

    // Apply Arabic joining and presentation forms
    fribidi_join_arabic(bidi_types, n, levels, joining_types);
    fribidi_shape_arabic(FRIBIDI_FLAG_SHAPE_ARAB_PRES, levels, n, joining_types, uni_chars);

    // Apply mirroring for brackets and paired punctuation
    fribidi_shape_mirroring(levels, n, uni_chars);

    // Reorder the line
    FriBidiLevel reorder_level = fribidi_reorder_line(0, bidi_types, n, 0, base_dir, levels, uni_chars, vis_map);
    (void)reorder_level;

    // Populate visual_cells with reordered CPU cells and shaped characters
    for (index_type vis_x = 0; vis_x < n; vis_x++) {
        index_type log_x = vis_map[vis_x];
        if (log_x < n) {
            visual_cells[vis_x] = line->cpu_cells[log_x];
            FriBidiChar shaped = uni_chars[vis_x];
            if (shaped != ' ' && shaped != (FriBidiChar)line->cpu_cells[log_x].ch_or_idx) {
                visual_cells[vis_x].ch_is_idx = 0;
                visual_cells[vis_x].ch_or_idx = shaped & 0x7fffffff;
            }
        } else {
            memset(&visual_cells[vis_x], 0, sizeof(CPUCell));
            visual_cells[vis_x].ch_or_idx = ' ';
        }
    }

    if (allocated) {
        free(uni_chars);
        free(bidi_types);
        free(joining_types);
        free(levels);
        free(vis_map);
    }
    return true;
}

index_type bidi_log2vis(const Line *line, index_type log_x) {
    if (!line || !line->cpu_cells || !line_has_bidi(line) || log_x >= line->xnum) return log_x;
    index_type n = line->xnum;
    FriBidiChar stack_uni_chars[1024];
    FriBidiCharType stack_bidi_types[1024];
    FriBidiLevel stack_levels[1024];
    FriBidiStrIndex stack_vis_map[1024];

    FriBidiChar *uni_chars = stack_uni_chars;
    FriBidiCharType *bidi_types = stack_bidi_types;
    FriBidiLevel *levels = stack_levels;
    FriBidiStrIndex *vis_map = stack_vis_map;

    bool allocated = false;
    if (n > 1024) {
        uni_chars = malloc(sizeof(FriBidiChar) * n);
        bidi_types = malloc(sizeof(FriBidiCharType) * n);
        levels = malloc(sizeof(FriBidiLevel) * n);
        vis_map = malloc(sizeof(FriBidiStrIndex) * n);
        if (!uni_chars || !bidi_types || !levels || !vis_map) {
            free(uni_chars); free(bidi_types); free(levels); free(vis_map);
            return log_x;
        }
        allocated = true;
    }

    ListOfChars lc;
    for (index_type i = 0; i < n; i++) {
        const CPUCell *c = line->cpu_cells + i;
        char_type ch = 0;
        if (c->ch_is_idx) {
            text_in_cell(c, line->text_cache, &lc);
            ch = lc.count ? lc.chars[0] : ' ';
        } else ch = c->ch_or_idx;
        if (ch == 0) ch = ' ';
        uni_chars[i] = (FriBidiChar)ch;
        vis_map[i] = i;
    }

    fribidi_get_bidi_types(uni_chars, n, bidi_types);
    FriBidiParType base_dir = FRIBIDI_PAR_ON;
    FriBidiLevel max_level = fribidi_get_par_embedding_levels_ex(bidi_types, NULL, n, &base_dir, levels);
    (void)max_level;
    FriBidiLevel reorder_level = fribidi_reorder_line(0, bidi_types, n, 0, base_dir, levels, uni_chars, vis_map);
    (void)reorder_level;

    index_type ans = log_x;
    for (index_type vis_x = 0; vis_x < n; vis_x++) {
        if (vis_map[vis_x] == (FriBidiStrIndex)log_x) {
            ans = vis_x;
            break;
        }
    }

    if (allocated) {
        free(uni_chars); free(bidi_types); free(levels); free(vis_map);
    }
    return ans;
}

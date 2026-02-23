/* viewport.c - Software-scrolling viewport implementation
 *
 * Implements Bubbletea-style software scrolling:
 * - Lines stored in memory
 * - Visible lines calculated from y_offset (visual line index)
 * - Rendering uses absolute cursor positioning
 * - No ANSI scroll regions
 * - Supports wrap mode (long lines wrap) and clip mode (truncate at width)
 */

#include <bloom-boba/ansi_sequences.h>
#include <bloom-boba/components/viewport.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEWPORT_TYPE_ID      (TUI_COMPONENT_TYPE_BASE + 20)
#define INITIAL_LINE_CAPACITY 64
#define SGR_STATE_BUF_SIZE    256

/* UTF-8 helper: Get byte length of UTF-8 character starting at ptr */
static int utf8_char_len(const char *ptr)
{
    unsigned char c = (unsigned char)*ptr;
    if ((c & 0x80) == 0)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 1; /* Invalid, treat as single byte */
}

/* UTF-8 helper: Decode a UTF-8 sequence to a Unicode codepoint */
static uint32_t utf8_decode_codepoint(const char *ptr, int len)
{
    unsigned char c = (unsigned char)*ptr;
    uint32_t cp;
    switch (len) {
    case 1:
        cp = c;
        break;
    case 2:
        cp = c & 0x1F;
        break;
    case 3:
        cp = c & 0x0F;
        break;
    case 4:
        cp = c & 0x07;
        break;
    default:
        return c;
    }
    for (int i = 1; i < len; i++)
        cp = (cp << 6) | ((unsigned char)ptr[i] & 0x3F);
    return cp;
}

/* Return terminal display width of a Unicode codepoint */
static int codepoint_display_width(uint32_t cp)
{
    /* Zero-width characters */
    if (cp == 0x200B || cp == 0x200C || cp == 0x200D) /* ZWSP, ZWNJ, ZWJ */
        return 0;
    if (cp >= 0x0300 && cp <= 0x036F) /* Combining diacritical marks */
        return 0;
    if (cp >= 0xFE00 && cp <= 0xFE0F) /* Variation selectors */
        return 0;

    /* East Asian Wide characters */
    if (cp >= 0x1100 && cp <= 0x115F) /* Hangul Jamo */
        return 2;
    if (cp >= 0x2E80 && cp <= 0x303E) /* CJK radicals, Kangxi, ideographic */
        return 2;
    if (cp >= 0x3040 && cp <= 0x33FF) /* Hiragana, Katakana, CJK compat */
        return 2;
    if (cp >= 0x3400 && cp <= 0x4DBF) /* CJK Unified Ext A */
        return 2;
    if (cp >= 0x4E00 && cp <= 0x9FFF) /* CJK Unified Ideographs */
        return 2;
    if (cp >= 0xAC00 && cp <= 0xD7AF) /* Hangul Syllables */
        return 2;
    if (cp >= 0xF900 && cp <= 0xFAFF) /* CJK Compatibility Ideographs */
        return 2;
    if (cp >= 0xFE30 && cp <= 0xFE6F) /* CJK Compatibility Forms */
        return 2;
    if (cp >= 0xFF01 && cp <= 0xFF60) /* Fullwidth forms */
        return 2;
    if (cp >= 0xFFE0 && cp <= 0xFFE6) /* Fullwidth signs */
        return 2;

    /* Misc symbols and dingbats (includes ⚓ U+2693) */
    if (cp >= 0x2600 && cp <= 0x27BF)
        return 2;

    /* Emoji ranges */
    if (cp >= 0x1F000 && cp <= 0x1FBFF)
        return 2;

    /* CJK Unified Ext B and beyond */
    if (cp >= 0x20000 && cp <= 0x2FA1F)
        return 2;

    return 1;
}

/* Emit up to max_cols display columns from text[*pos..len) into out.
 * Returns number of display columns emitted. */
static int emit_cols(const char *text, size_t len, size_t *pos, int max_cols,
                     DynamicBuffer *out)
{
    int col = 0;
    int in_escape = 0;
    for (; *pos < len; (*pos)++) {
        unsigned char ch = (unsigned char)text[*pos];
        if (in_escape) {
            dynamic_buffer_append(out, &text[*pos], 1);
            if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))
                in_escape = 0;
        } else if (ch == '\033' && *pos + 1 < len && text[*pos + 1] == '[') {
            in_escape = 1;
            dynamic_buffer_append(out, &text[*pos], 1); /* ESC */
        } else if (ch >= 0x20) {
            int clen = utf8_char_len(&text[*pos]);
            /* Clamp to remaining bytes */
            if (*pos + clen > len)
                clen = (int)(len - *pos);
            uint32_t cp = utf8_decode_codepoint(&text[*pos], clen);
            int w = codepoint_display_width(cp);
            if (w > 0 && col + w > max_cols)
                break;
            col += w;
            dynamic_buffer_append(out, &text[*pos], clen);
            *pos += clen - 1; /* -1 because loop increments */
        } else {
            dynamic_buffer_append(out, &text[*pos], 1);
        }
    }
    return col;
}

/* Calculate display width of a string (excluding ANSI sequences) */
static size_t calc_display_width(const char *text, size_t len)
{
    size_t width = 0;
    int in_escape = 0;

    for (size_t i = 0; i < len; i++) {
        if (in_escape) {
            /* End of CSI sequence */
            if ((text[i] >= 'A' && text[i] <= 'Z') ||
                (text[i] >= 'a' && text[i] <= 'z')) {
                in_escape = 0;
            }
        } else if (text[i] == '\033' && i + 1 < len && text[i + 1] == '[') {
            in_escape = 1;
            i++; /* Skip '[' */
        } else if ((unsigned char)text[i] >= 0x20) {
            int clen = utf8_char_len(&text[i]);
            /* Clamp to remaining bytes */
            if (i + clen > len)
                clen = (int)(len - i);
            uint32_t cp = utf8_decode_codepoint(&text[i], clen);
            width += codepoint_display_width(cp);
            i += clen - 1; /* -1 because loop increments */
        }
    }

    return width;
}

/* Calculate how many visual (screen) rows a line occupies */
static int calc_visual_line_count(size_t display_width, int viewport_width,
                                  int wrap_mode)
{
    if (!wrap_mode || viewport_width <= 0 || display_width == 0)
        return 1;
    return (int)((display_width + viewport_width - 1) / viewport_width);
}

/* Recompute visual_lines for all lines and total_visual_lines */
static void recompute_all_visual_lines(TuiViewport *vp)
{
    vp->total_visual_lines = 0;
    for (size_t i = 0; i < vp->line_count; i++) {
        vp->lines[i].visual_lines = calc_visual_line_count(
            vp->lines[i].display_width, vp->width, vp->wrap_mode);
        vp->total_visual_lines += vp->lines[i].visual_lines;
    }
}

/* Clamp y_offset to valid range based on total_visual_lines */
static void clamp_y_offset(TuiViewport *vp)
{
    if (vp->total_visual_lines > (size_t)vp->height) {
        size_t max_offset = vp->total_visual_lines - vp->height;
        if (vp->y_offset > max_offset)
            vp->y_offset = max_offset;
    } else {
        vp->y_offset = 0;
    }
}

/* Add a line to the viewport */
static int add_line(TuiViewport *vp, const char *text, size_t len)
{
    /* Grow array if needed */
    if (vp->line_count >= vp->line_capacity) {
        size_t new_cap = vp->line_capacity * 2;
        TuiViewportLine *new_lines =
            realloc(vp->lines, new_cap * sizeof(TuiViewportLine));
        if (!new_lines)
            return -1;
        vp->lines = new_lines;
        vp->line_capacity = new_cap;
    }

    /* Allocate and copy line text */
    char *line_text = malloc(len + 1);
    if (!line_text)
        return -1;
    memcpy(line_text, text, len);
    line_text[len] = '\0';

    /* Add to array */
    TuiViewportLine *line = &vp->lines[vp->line_count];
    line->text = line_text;
    line->len = len;
    line->display_width = calc_display_width(text, len);
    line->visual_lines =
        calc_visual_line_count(line->display_width, vp->width, vp->wrap_mode);
    vp->total_visual_lines += line->visual_lines;
    vp->line_count++;

    return 0;
}

/* Append text to the last line in-place, creating one if needed */
static int append_to_last_line(TuiViewport *vp, const char *text, size_t len)
{
    if (vp->line_count == 0) {
        if (add_line(vp, "", 0) < 0)
            return -1;
    }
    TuiViewportLine *last = &vp->lines[vp->line_count - 1];
    char *new_text = realloc(last->text, last->len + len + 1);
    if (!new_text)
        return -1;
    memcpy(new_text + last->len, text, len);
    last->len += len;
    new_text[last->len] = '\0';
    last->text = new_text;
    last->display_width = calc_display_width(last->text, last->len);

    /* Update visual line count */
    vp->total_visual_lines -= last->visual_lines;
    last->visual_lines =
        calc_visual_line_count(last->display_width, vp->width, vp->wrap_mode);
    vp->total_visual_lines += last->visual_lines;

    return 0;
}

/* Trim old lines if exceeding max_lines */
static void trim_old_lines(TuiViewport *vp)
{
    if (vp->max_lines <= 0 || vp->line_count <= (size_t)vp->max_lines)
        return;

    size_t to_remove = vp->line_count - vp->max_lines;

    /* Free old lines and subtract their visual lines */
    size_t visual_removed = 0;
    for (size_t i = 0; i < to_remove; i++) {
        visual_removed += vp->lines[i].visual_lines;
        vp->total_visual_lines -= vp->lines[i].visual_lines;
        free(vp->lines[i].text);
    }

    /* Shift remaining lines */
    memmove(vp->lines, vp->lines + to_remove,
            (vp->line_count - to_remove) * sizeof(TuiViewportLine));
    vp->line_count -= to_remove;

    /* Adjust y_offset for removed content, then clamp */
    if (vp->y_offset > visual_removed) {
        vp->y_offset -= visual_removed;
    } else {
        vp->y_offset = 0;
    }
    clamp_y_offset(vp);
}

/* Find which content line contains the given visual line offset.
 * Returns the content line index and sets *sub_line to the visual row
 * offset within that content line (0-indexed). */
static size_t find_content_line_for_visual(const TuiViewport *vp,
                                           size_t visual_offset,
                                           int *sub_line)
{
    size_t accumulated = 0;
    for (size_t i = 0; i < vp->line_count; i++) {
        size_t next = accumulated + vp->lines[i].visual_lines;
        if (visual_offset < next) {
            *sub_line = (int)(visual_offset - accumulated);
            return i;
        }
        accumulated = next;
    }
    /* Past end — return last line */
    *sub_line = 0;
    return vp->line_count;
}

/* Render the sub_line-th visual row of a content line to the output buffer.
 * Handles ANSI state replay for wrapped continuation rows. */
static void render_line_segment(const TuiViewportLine *line, int viewport_width,
                                int sub_line, int wrap_mode,
                                DynamicBuffer *out)
{
    const char *text = line->text;
    size_t len = line->len;

    if (!wrap_mode || sub_line == 0) {
        /* Clip mode or first visual row: emit up to viewport_width display cols */
        size_t pos = 0;
        emit_cols(text, len, &pos, viewport_width, out);
        /* Reset at end to prevent color bleeding */
        dynamic_buffer_append_str(out, SGR_RESET);
    } else {
        /* Wrap mode, sub_line > 0: skip first sub_line * viewport_width display
         * columns, collecting ANSI SGR state, then emit next viewport_width cols */
        int skip_target = sub_line * viewport_width;
        int skipped = 0;
        int in_escape = 0;

        /* SGR state buffer — accumulates active SGR sequences */
        char sgr_buf[SGR_STATE_BUF_SIZE];
        size_t sgr_len = 0;

        /* Track start of current CSI sequence for SGR capture */
        size_t csi_start = 0;
        int in_csi = 0;

        size_t i = 0;
        /* Phase 1: skip display columns, collecting ANSI state */
        for (; i < len && skipped < skip_target; i++) {
            if (in_escape) {
                if ((text[i] >= 'A' && text[i] <= 'Z') ||
                    (text[i] >= 'a' && text[i] <= 'z')) {
                    in_escape = 0;
                    /* Check if this was an SGR sequence (ends with 'm') */
                    if (text[i] == 'm' && in_csi) {
                        size_t seq_len = i - csi_start + 1;
                        /* Check for reset: ESC[0m or ESC[m */
                        int is_reset = 0;
                        if (seq_len == 4 && text[csi_start + 2] == '0')
                            is_reset = 1; /* ESC[0m */
                        if (seq_len == 3)
                            is_reset = 1; /* ESC[m */
                        if (is_reset) {
                            sgr_len = 0;
                        } else if (sgr_len + seq_len < SGR_STATE_BUF_SIZE) {
                            memcpy(sgr_buf + sgr_len, text + csi_start, seq_len);
                            sgr_len += seq_len;
                        }
                    }
                    in_csi = 0;
                }
            } else if (text[i] == '\033' && i + 1 < len && text[i + 1] == '[') {
                in_escape = 1;
                in_csi = 1;
                csi_start = i;
                i++; /* Skip '[' */
            } else if ((unsigned char)text[i] >= 0x20) {
                int clen = utf8_char_len(&text[i]);
                if (i + clen > len)
                    clen = (int)(len - i);
                uint32_t cp = utf8_decode_codepoint(&text[i], clen);
                skipped += codepoint_display_width(cp);
                i += clen - 1; /* -1 because loop increments */
            }
        }

        /* Emit accumulated SGR state before visible content */
        if (sgr_len > 0) {
            dynamic_buffer_append(out, sgr_buf, sgr_len);
        }

        /* Phase 2: emit next viewport_width display columns */
        emit_cols(text, len, &i, viewport_width, out);
        /* Reset at end to prevent color bleeding */
        dynamic_buffer_append_str(out, SGR_RESET);
    }
}

/* Create a new viewport */
TuiViewport *tui_viewport_create(void)
{
    TuiViewport *vp = calloc(1, sizeof(TuiViewport));
    if (!vp)
        return NULL;

    vp->base.type = VIEWPORT_TYPE_ID;

    /* Allocate line array */
    vp->lines = malloc(INITIAL_LINE_CAPACITY * sizeof(TuiViewportLine));
    if (!vp->lines) {
        free(vp);
        return NULL;
    }
    vp->line_capacity = INITIAL_LINE_CAPACITY;
    vp->line_count = 0;

    /* Defaults */
    vp->width = 80;
    vp->height = 24;
    vp->y_offset = 0;
    vp->auto_scroll = 1;
    vp->max_lines = 10000; /* Reasonable default */
    vp->render_row = 1;
    vp->render_col = 1;
    vp->wrap_mode = 1; /* Wrap by default */
    vp->total_visual_lines = 0;

    return vp;
}

/* Free viewport */
void tui_viewport_free(TuiViewport *vp)
{
    if (!vp)
        return;

    /* Free all lines */
    for (size_t i = 0; i < vp->line_count; i++) {
        free(vp->lines[i].text);
    }
    free(vp->lines);
    free(vp);
}

/* Append text to the viewport */
void tui_viewport_append(TuiViewport *vp, const char *text, size_t len)
{
    if (!vp || !text || len == 0)
        return;

    int was_at_bottom = tui_viewport_at_bottom(vp);

    const char *start = text;
    const char *end = text + len;

    while (start < end) {
        /* Find next newline */
        const char *nl = memchr(start, '\n', end - start);

        if (nl) {
            /* Found newline - append segment to last line, then start a new line */
            size_t seg_len = nl - start;

            /* Handle \r\n by stripping \r */
            if (seg_len > 0 && start[seg_len - 1] == '\r') {
                seg_len--;
            }

            /* Append this segment to the current (last) line */
            if (seg_len > 0) {
                append_to_last_line(vp, start, seg_len);
            }

            /* Start a new empty line */
            add_line(vp, "", 0);

            start = nl + 1;
            /* Skip \r after \n (handle \n\r line endings) */
            if (start < end && *start == '\r') {
                start++;
            }
        } else {
            /* No newline - append to last line */
            size_t seg_len = end - start;
            append_to_last_line(vp, start, seg_len);
            break;
        }
    }

    /* Trim old lines if needed */
    trim_old_lines(vp);

    /* Auto-scroll to bottom if enabled and was at bottom */
    if (vp->auto_scroll && was_at_bottom) {
        tui_viewport_scroll_to_bottom(vp);
    }
}

/* Clear all content */
void tui_viewport_clear(TuiViewport *vp)
{
    if (!vp)
        return;

    for (size_t i = 0; i < vp->line_count; i++) {
        free(vp->lines[i].text);
    }
    vp->line_count = 0;
    vp->y_offset = 0;
    vp->total_visual_lines = 0;
}

/* Scroll up by N lines */
void tui_viewport_scroll_up(TuiViewport *vp, int lines)
{
    if (!vp || lines <= 0)
        return;

    if ((size_t)lines > vp->y_offset) {
        vp->y_offset = 0;
    } else {
        vp->y_offset -= lines;
    }
}

/* Scroll down by N lines */
void tui_viewport_scroll_down(TuiViewport *vp, int lines)
{
    if (!vp || lines <= 0)
        return;

    vp->y_offset += lines;

    /* Clamp to valid range */
    size_t max_offset = 0;
    if (vp->total_visual_lines > (size_t)vp->height) {
        max_offset = vp->total_visual_lines - vp->height;
    }
    if (vp->y_offset > max_offset) {
        vp->y_offset = max_offset;
    }
}

/* Page up */
void tui_viewport_page_up(TuiViewport *vp)
{
    if (vp) {
        tui_viewport_scroll_up(vp, vp->height);
    }
}

/* Page down */
void tui_viewport_page_down(TuiViewport *vp)
{
    if (vp) {
        tui_viewport_scroll_down(vp, vp->height);
    }
}

/* Scroll to bottom */
void tui_viewport_scroll_to_bottom(TuiViewport *vp)
{
    if (!vp)
        return;

    if (vp->total_visual_lines > (size_t)vp->height) {
        vp->y_offset = vp->total_visual_lines - vp->height;
    } else {
        vp->y_offset = 0;
    }
}

/* Check if at bottom */
int tui_viewport_at_bottom(const TuiViewport *vp)
{
    if (!vp)
        return 1;

    if (vp->total_visual_lines <= (size_t)vp->height) {
        return 1;
    }

    return vp->y_offset >= vp->total_visual_lines - vp->height;
}

/* Set viewport size */
void tui_viewport_set_size(TuiViewport *vp, int width, int height)
{
    if (!vp)
        return;

    vp->width = width > 0 ? width : 1;
    vp->height = height > 0 ? height : 1;

    /* Recompute visual lines (width may have changed) */
    recompute_all_visual_lines(vp);
    clamp_y_offset(vp);
}

/* Set render position */
void tui_viewport_set_render_position(TuiViewport *vp, int row, int col)
{
    if (!vp)
        return;

    vp->render_row = row > 0 ? row : 1;
    vp->render_col = col > 0 ? col : 1;
}

/* Set max lines */
void tui_viewport_set_max_lines(TuiViewport *vp, int max)
{
    if (!vp)
        return;

    vp->max_lines = max;
    trim_old_lines(vp);
}

/* Set auto scroll */
void tui_viewport_set_auto_scroll(TuiViewport *vp, int enabled)
{
    if (vp) {
        vp->auto_scroll = enabled ? 1 : 0;
    }
}

/* Set wrap mode */
void tui_viewport_set_wrap_mode(TuiViewport *vp, int wrap)
{
    if (!vp)
        return;

    vp->wrap_mode = wrap ? 1 : 0;
    recompute_all_visual_lines(vp);
    clamp_y_offset(vp);
}

/* Get line count */
size_t tui_viewport_line_count(const TuiViewport *vp)
{
    return vp ? vp->line_count : 0;
}

/* Render viewport to output buffer */
void tui_viewport_view(const TuiViewport *vp, DynamicBuffer *out)
{
    if (!vp || !out)
        return;

    char buf[64];

    /* Find the content line corresponding to y_offset */
    int sub_line = 0;
    size_t content_idx =
        find_content_line_for_visual(vp, vp->y_offset, &sub_line);

    for (int row = 0; row < vp->height; row++) {
        int screen_row = vp->render_row + row;
        snprintf(buf, sizeof(buf), CSI "%d;%dH", screen_row, vp->render_col);
        dynamic_buffer_append_str(out, buf);
        dynamic_buffer_append_str(out, CSI "K");

        if (content_idx < vp->line_count) {
            render_line_segment(&vp->lines[content_idx], vp->width, sub_line,
                                vp->wrap_mode, out);
            sub_line++;
            if (sub_line >= vp->lines[content_idx].visual_lines) {
                content_idx++;
                sub_line = 0;
            }
        }
    }
}

/* Component interface wrappers */
static TuiInitResult viewport_init(void *config)
{
    (void)config;
    TuiModel *model = (TuiModel *)tui_viewport_create();
    return tui_init_result_none(model);
}

static TuiUpdateResult viewport_update(TuiModel *model, TuiMsg msg)
{
    (void)model;
    (void)msg;
    /* Viewport doesn't handle messages directly - content is added via append */
    return tui_update_result_none();
}

static void viewport_view(const TuiModel *model, DynamicBuffer *out)
{
    tui_viewport_view((const TuiViewport *)model, out);
}

static void viewport_free(TuiModel *model)
{
    tui_viewport_free((TuiViewport *)model);
}

static const TuiComponent viewport_component_instance = {
    .init = viewport_init,
    .update = viewport_update,
    .view = viewport_view,
    .free = viewport_free,
};

const TuiComponent *tui_viewport_component(void)
{
    return &viewport_component_instance;
}

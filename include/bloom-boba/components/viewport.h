/* viewport.h - Software-scrolling viewport component
 *
 * A Bubbletea-style viewport that manages scrollback without ANSI scroll
 * regions. Content is stored as lines in memory, and rendering uses absolute
 * cursor positioning.
 *
 * This approach gives us full control over rendering and avoids the quirks
 * of terminal scroll region handling.
 */

#ifndef TUI_VIEWPORT_H
#define TUI_VIEWPORT_H

#include <bloom-boba/component.h>
#include <bloom-boba/dynamic_buffer.h>
#include <stddef.h>

/* A single line in the viewport */
typedef struct TuiViewportLine {
  char *text;          /* Line content (owned, null-terminated) */
  size_t len;          /* Byte length */
  size_t display_width; /* Visual width (excludes ANSI sequences) */
  int visual_lines;    /* Number of screen rows this line occupies (>= 1) */
} TuiViewportLine;

/* Viewport model */
typedef struct TuiViewport {
  TuiModel base;

  /* Line storage */
  TuiViewportLine *lines;
  size_t line_count;
  size_t line_capacity;

  /* Viewport state */
  int width;
  int height;
  size_t y_offset;  /* First visible visual line index */
  int auto_scroll;  /* Scroll to bottom on new content */
  int max_lines;    /* Max lines to keep (memory limit, 0 = unlimited) */

  /* Wrapping */
  size_t total_visual_lines; /* Sum of all lines' visual_lines */
  int wrap_mode;             /* 0 = clip (truncate at width), 1 = wrap (default) */

  /* Render position (absolute, 1-indexed) */
  int render_row; /* Starting row */
  int render_col; /* Starting column */
} TuiViewport;

/* Create a new viewport */
TuiViewport *tui_viewport_create(void);

/* Free viewport and all owned memory */
void tui_viewport_free(TuiViewport *vp);

/* Append text to the viewport (handles newlines, partial lines) */
void tui_viewport_append(TuiViewport *vp, const char *text, size_t len);

/* Clear all content */
void tui_viewport_clear(TuiViewport *vp);

/* Scrolling */
void tui_viewport_scroll_up(TuiViewport *vp, int lines);
void tui_viewport_scroll_down(TuiViewport *vp, int lines);
void tui_viewport_page_up(TuiViewport *vp);
void tui_viewport_page_down(TuiViewport *vp);
void tui_viewport_scroll_to_bottom(TuiViewport *vp);
int tui_viewport_at_bottom(const TuiViewport *vp);

/* Configuration */
void tui_viewport_set_size(TuiViewport *vp, int width, int height);
void tui_viewport_set_render_position(TuiViewport *vp, int row, int col);
void tui_viewport_set_max_lines(TuiViewport *vp, int max);
void tui_viewport_set_auto_scroll(TuiViewport *vp, int enabled);
void tui_viewport_set_wrap_mode(TuiViewport *vp, int wrap);

/* Get line count (for testing/debugging) */
size_t tui_viewport_line_count(const TuiViewport *vp);

/* Render viewport to output buffer */
void tui_viewport_view(const TuiViewport *vp, DynamicBuffer *out);

/* Component interface for generic use */
const TuiComponent *tui_viewport_component(void);

#endif /* TUI_VIEWPORT_H */

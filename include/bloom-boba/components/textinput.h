/* textinput.h - Multi-line text input component for bloom-boba
 *
 * A text input widget supporting:
 * - Single and multi-line input
 * - Unicode/UTF-8 text
 * - Cursor navigation (arrows, home, end)
 * - Basic editing (insert, delete, backspace)
 * - Optional prompt string
 * - Auto-growing height for multi-line content
 */

#ifndef BLOOM_BOBA_TEXTINPUT_H
#define BLOOM_BOBA_TEXTINPUT_H

#include "../component.h"
#include "../dynamic_buffer.h"
#include "../msg.h"

/* Completion callback function type
 * Returns NULL-terminated array of completions (caller must free each string
 * and the array)
 */
typedef char **(*TuiCompletionCallback)(const char *buffer, int cursor_pos,
                                        void *userdata);

/* Text input model */
typedef struct TuiTextInput {
  TuiModel base; /* Base model for component interface */

  char *text;          /* UTF-8 text content */
  size_t text_len;     /* Current text length in bytes */
  size_t text_cap;     /* Allocated capacity */

  size_t cursor_byte;  /* Cursor position in bytes */
  size_t cursor_col;   /* Visual column (0-indexed) */
  size_t cursor_row;   /* Visual row (0-indexed) */

  int width;           /* Max width (0 = unlimited) */
  int height;          /* Max visible height (0 = grow to fit) */
  int scroll_offset;   /* Vertical scroll position (first visible line) */
  int offset;          /* Horizontal scroll: left edge (codepoint index) */
  int offset_right;    /* Horizontal scroll: right edge (codepoint index) */

  const char *prompt;  /* Optional prompt string (not owned) */
  int prompt_len;      /* Cached prompt display width */

  int focused;         /* Whether component has focus */
  int multiline;       /* Allow multiple lines (Enter inserts newline) */
  int show_prompt;     /* Whether to display the prompt (default: 1) */
  int show_dividers;       /* Whether to show dividers above/below (default: 0) */
  int terminal_width;      /* Terminal width for divider rendering (0 = 80) */
  int terminal_row;        /* Row for absolute positioning (1-indexed, 0 = not set) */

  /* History management */
  char **history;      /* Array of past input lines */
  int history_size;    /* Max history entries */
  int history_count;   /* Current number of history entries */
  int history_pos;     /* Navigation position (-1 = current input) */
  char *saved_input;   /* Saved current input when navigating history */

  /* Tab completion */
  TuiCompletionCallback completer; /* Completion callback */
  void *completer_data;            /* User data for completion callback */
  char **completions;              /* Current completion list (NULL-terminated) */
  int completion_count;            /* Number of completions */
  int completion_index;            /* Current cycling index */
  int completion_word_start;       /* Byte offset where completed word starts */
  int completion_word_len;         /* Byte length of current completed word */

  /* Kill/yank buffer */
  char *kill_buf;          /* Killed text (malloc'd, NULL initially) */
  size_t kill_buf_len;     /* Length of killed text in bytes */
  int last_was_kill;       /* Whether previous key was a kill command */

  /* Undo stack */
  struct { char *text; size_t text_len; size_t cursor_byte; } *undo_stack;
  int undo_count;
  int undo_cap;
  int ctrl_x_prefix;       /* Waiting for second key after C-x */
} TuiTextInput;

/* Configuration for creating text input */
typedef struct TuiTextInputConfig {
  const char *placeholder; /* Placeholder text (shown when empty) */
  const char *prompt;      /* Prompt string (e.g., "> ") */
  int width;               /* Max width (0 = unlimited) */
  int height;              /* Max height (0 = grow to fit) */
  int multiline;           /* Allow multiple lines */
} TuiTextInputConfig;

/* Create a new text input component
 *
 * Parameters:
 *   config: Optional configuration (NULL for defaults)
 *
 * Returns: New text input model, or NULL on failure
 */
TuiTextInput *tui_textinput_create(const TuiTextInputConfig *config);

/* Free text input component */
void tui_textinput_free(TuiTextInput *input);

/* Update text input with message
 *
 * Parameters:
 *   input: Text input model
 *   msg: Message to process
 *
 * Returns: Update result with optional command
 */
TuiUpdateResult tui_textinput_update(TuiTextInput *input, TuiMsg msg);

/* Render text input to output buffer
 *
 * Parameters:
 *   input: Text input model (const)
 *   out: Output buffer to append to
 */
void tui_textinput_view(const TuiTextInput *input, DynamicBuffer *out);

/* Get current text content */
const char *tui_textinput_text(const TuiTextInput *input);

/* Get text length */
size_t tui_textinput_len(const TuiTextInput *input);

/* Set text content */
void tui_textinput_set_text(TuiTextInput *input, const char *text);

/* Clear text content */
void tui_textinput_clear(TuiTextInput *input);

/* Set focus state */
void tui_textinput_set_focus(TuiTextInput *input, int focused);

/* Check if focused */
int tui_textinput_is_focused(const TuiTextInput *input);

/* Get cursor position (byte offset) */
size_t tui_textinput_cursor(const TuiTextInput *input);

/* Set cursor position (byte offset) */
void tui_textinput_set_cursor(TuiTextInput *input, size_t pos);

/* Get number of lines in content */
int tui_textinput_line_count(const TuiTextInput *input);

/* Set maximum history size */
void tui_textinput_set_history_size(TuiTextInput *input, int size);

/* Add a line to history */
void tui_textinput_history_add(TuiTextInput *input, const char *line);

/* Set completion callback */
void tui_textinput_set_completer(TuiTextInput *input, TuiCompletionCallback cb,
                                 void *data);

/* Set whether to show the prompt */
void tui_textinput_set_show_prompt(TuiTextInput *input, int show);

/* Set the prompt string */
void tui_textinput_set_prompt(TuiTextInput *input, const char *prompt);

/* Set whether to show dividers above/below the input */
void tui_textinput_set_show_dividers(TuiTextInput *input, int show);

/* Set terminal width for divider rendering */
void tui_textinput_set_terminal_width(TuiTextInput *input, int width);

/* Set terminal row for absolute positioning (1-indexed)
 * When set, view() uses absolute cursor positioning instead of relative moves.
 * This is the row for the input line; dividers use adjacent rows.
 */
void tui_textinput_set_terminal_row(TuiTextInput *input, int row);

/* Get component interface for text input */
const TuiComponent *tui_textinput_component(void);

#endif /* BLOOM_BOBA_TEXTINPUT_H */

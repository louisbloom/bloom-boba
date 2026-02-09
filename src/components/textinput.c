/* textinput.c - Multi-line text input component implementation */

#include <bloom-boba/ansi_sequences.h>
#include <bloom-boba/cmd.h>
#include <bloom-boba/components/textinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXTINPUT_INITIAL_CAP 256
#define TEXTINPUT_TYPE_ID     (TUI_COMPONENT_TYPE_BASE + 1)

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

/* UTF-8 helper: Get previous character start position */
static size_t utf8_prev_char(const char *text, size_t pos)
{
    if (pos == 0)
        return 0;
    pos--;
    while (pos > 0 && ((unsigned char)text[pos] & 0xC0) == 0x80) {
        pos--;
    }
    return pos;
}

/* UTF-8 helper: Count display width (codepoints) of a UTF-8 string */
static int utf8_display_width(const char *str)
{
    if (!str)
        return 0;
    int width = 0;
    while (*str) {
        int char_len = utf8_char_len(str);
        width++;
        str += char_len;
    }
    return width;
}

/* Count total codepoints in a UTF-8 string */
static int utf8_codepoint_count(const char *text, size_t len)
{
    int count = 0;
    size_t i = 0;
    while (i < len) {
        i += utf8_char_len(text + i);
        count++;
    }
    return count;
}

/* Return byte offset of the Nth codepoint (0-indexed).
 * If cp_index >= total codepoints, returns text_len. */
static size_t utf8_byte_offset_of_cp(const char *text, size_t text_len,
                                     int cp_index)
{
    size_t offset = 0;
    int cp = 0;
    while (offset < text_len && cp < cp_index) {
        offset += utf8_char_len(text + offset);
        cp++;
    }
    return offset;
}

/* Return codepoint index for a given byte position */
static int utf8_cp_index_of_byte(const char *text, size_t byte_pos)
{
    int cp = 0;
    size_t i = 0;
    while (i < byte_pos) {
        i += utf8_char_len(text + i);
        cp++;
    }
    return cp;
}

/* Adjust horizontal scroll offset so cursor stays visible */
static void handle_overflow(TuiTextInput *input)
{
    int prompt_width =
        (input->show_prompt && input->prompt) ? input->prompt_len : 0;
    int content_width = input->terminal_width - prompt_width;

    if (content_width <= 0 || input->terminal_width == 0) {
        input->offset = 0;
        int total = utf8_codepoint_count(input->text, input->text_len);
        input->offset_right = total;
        return;
    }

    int total = utf8_codepoint_count(input->text, input->text_len);
    int cursor_cp = utf8_cp_index_of_byte(input->text, input->cursor_byte);

    if (total <= content_width) {
        /* Everything fits */
        input->offset = 0;
        input->offset_right = total;
        return;
    }

    if (cursor_cp < input->offset) {
        /* Cursor scrolled left of window */
        input->offset = cursor_cp;
        input->offset_right = input->offset + content_width;
        if (input->offset_right > total)
            input->offset_right = total;
    } else if (cursor_cp >= input->offset_right) {
        /* Cursor scrolled right of window */
        input->offset_right = cursor_cp + 1;
        input->offset = input->offset_right - content_width;
        if (input->offset < 0)
            input->offset = 0;
    } else {
        /* Cursor in view — clamp offset_right */
        input->offset_right = input->offset + content_width;
        if (input->offset_right > total)
            input->offset_right = total;
    }
}

/* Count lines and find cursor position */
static void recalculate_cursor_position(TuiTextInput *input)
{
    input->cursor_row = 0;
    input->cursor_col = 0;

    size_t col = 0;
    for (size_t i = 0; i < input->cursor_byte && i < input->text_len; i++) {
        if (input->text[i] == '\n') {
            input->cursor_row++;
            col = 0;
        } else {
            col++;
        }
    }
    input->cursor_col = col;

    /* Adjust horizontal scroll for single-line overflow */
    if (!input->multiline)
        handle_overflow(input);
}

/* Ensure text buffer has enough capacity */
static int ensure_capacity(TuiTextInput *input, size_t needed)
{
    if (input->text_cap >= needed)
        return 0;

    size_t new_cap = input->text_cap;
    if (new_cap == 0)
        new_cap = TEXTINPUT_INITIAL_CAP;
    while (new_cap < needed)
        new_cap *= 2;

    char *new_text = (char *)realloc(input->text, new_cap);
    if (!new_text)
        return -1;

    input->text = new_text;
    input->text_cap = new_cap;
    return 0;
}

/* Insert text at cursor position */
static int insert_text(TuiTextInput *input, const char *text, size_t len)
{
    if (len == 0)
        return 0;

    if (ensure_capacity(input, input->text_len + len + 1) < 0)
        return -1;

    /* Move text after cursor */
    if (input->cursor_byte < input->text_len) {
        memmove(input->text + input->cursor_byte + len,
                input->text + input->cursor_byte,
                input->text_len - input->cursor_byte);
    }

    /* Insert new text */
    memcpy(input->text + input->cursor_byte, text, len);
    input->text_len += len;
    input->cursor_byte += len;
    input->text[input->text_len] = '\0';

    recalculate_cursor_position(input);
    return 0;
}

/* Insert a single UTF-8 codepoint */
static int insert_codepoint(TuiTextInput *input, uint32_t cp)
{
    char buf[5];
    int len;

    if (cp < 0x80) {
        buf[0] = (char)cp;
        len = 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        len = 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        len = 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        len = 4;
    }
    buf[len] = '\0';

    return insert_text(input, buf, len);
}

/* Delete character before cursor (backspace) */
static void delete_before(TuiTextInput *input)
{
    if (input->cursor_byte == 0)
        return;

    size_t prev = utf8_prev_char(input->text, input->cursor_byte);
    size_t del_len = input->cursor_byte - prev;

    memmove(input->text + prev, input->text + input->cursor_byte,
            input->text_len - input->cursor_byte);
    input->text_len -= del_len;
    input->cursor_byte = prev;
    input->text[input->text_len] = '\0';

    recalculate_cursor_position(input);
}

/* Delete character at cursor (delete key) */
static void delete_at(TuiTextInput *input)
{
    if (input->cursor_byte >= input->text_len)
        return;

    int char_len = utf8_char_len(input->text + input->cursor_byte);
    size_t del_end = input->cursor_byte + char_len;
    if (del_end > input->text_len)
        del_end = input->text_len;

    memmove(input->text + input->cursor_byte, input->text + del_end,
            input->text_len - del_end);
    input->text_len -= (del_end - input->cursor_byte);
    input->text[input->text_len] = '\0';
}

/* Move cursor left by one character */
static void cursor_left(TuiTextInput *input)
{
    if (input->cursor_byte > 0) {
        input->cursor_byte = utf8_prev_char(input->text, input->cursor_byte);
        recalculate_cursor_position(input);
    }
}

/* Move cursor right by one character */
static void cursor_right(TuiTextInput *input)
{
    if (input->cursor_byte < input->text_len) {
        input->cursor_byte += utf8_char_len(input->text + input->cursor_byte);
        if (input->cursor_byte > input->text_len)
            input->cursor_byte = input->text_len;
        recalculate_cursor_position(input);
    }
}

/* Move cursor left by one word (Ctrl+Left) */
static void cursor_word_left(TuiTextInput *input)
{
    if (input->cursor_byte == 0)
        return;

    size_t pos = input->cursor_byte;

    /* Skip whitespace before the word */
    while (pos > 0 &&
           (input->text[pos - 1] == ' ' || input->text[pos - 1] == '\t'))
        pos--;

    /* Skip the word itself */
    while (pos > 0 && input->text[pos - 1] != ' ' && input->text[pos - 1] != '\t')
        pos--;

    input->cursor_byte = pos;
    recalculate_cursor_position(input);
}

/* Move cursor right by one word (Ctrl+Right) */
static void cursor_word_right(TuiTextInput *input)
{
    if (input->cursor_byte >= input->text_len)
        return;

    size_t pos = input->cursor_byte;

    /* Skip the current word */
    while (pos < input->text_len && input->text[pos] != ' ' &&
           input->text[pos] != '\t')
        pos++;

    /* Skip whitespace after the word */
    while (pos < input->text_len &&
           (input->text[pos] == ' ' || input->text[pos] == '\t'))
        pos++;

    input->cursor_byte = pos;
    recalculate_cursor_position(input);
}

/* Move cursor to start of current line */
static void cursor_home(TuiTextInput *input)
{
    while (input->cursor_byte > 0 &&
           input->text[input->cursor_byte - 1] != '\n') {
        input->cursor_byte--;
    }
    recalculate_cursor_position(input);
}

/* Move cursor to end of current line */
static void cursor_end(TuiTextInput *input)
{
    while (input->cursor_byte < input->text_len &&
           input->text[input->cursor_byte] != '\n') {
        input->cursor_byte++;
    }
    recalculate_cursor_position(input);
}

/* Find line start position for given line number */
static size_t find_line_start(const char *text, size_t len, int line)
{
    if (line <= 0)
        return 0;

    int current_line = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            current_line++;
            if (current_line == line)
                return i + 1;
        }
    }
    return len;
}

/* Find line length (without newline) */
static size_t line_length(const char *text, size_t len, size_t start)
{
    size_t end = start;
    while (end < len && text[end] != '\n') {
        end++;
    }
    return end - start;
}

/* Move cursor up one line */
static void cursor_up(TuiTextInput *input)
{
    if (input->cursor_row == 0)
        return;

    /* Find previous line start */
    size_t prev_line_start =
        find_line_start(input->text, input->text_len, (int)input->cursor_row - 1);
    size_t prev_line_len =
        line_length(input->text, input->text_len, prev_line_start);

    /* Move to same column or end of line if shorter */
    size_t col = input->cursor_col;
    if (col > prev_line_len)
        col = prev_line_len;

    input->cursor_byte = prev_line_start + col;
    recalculate_cursor_position(input);
}

/* Move cursor down one line */
static void cursor_down(TuiTextInput *input)
{
    /* Find next line */
    size_t next_line_start = input->cursor_byte;
    while (next_line_start < input->text_len &&
           input->text[next_line_start] != '\n') {
        next_line_start++;
    }
    if (next_line_start >= input->text_len)
        return;        /* No next line */
    next_line_start++; /* Skip newline */

    size_t next_line_len =
        line_length(input->text, input->text_len, next_line_start);

    /* Move to same column or end of line if shorter */
    size_t col = input->cursor_col;
    if (col > next_line_len)
        col = next_line_len;

    input->cursor_byte = next_line_start + col;
    recalculate_cursor_position(input);
}

/* Save text to kill buffer (append=1 appends to existing buffer) */
static void kill_save(TuiTextInput *input, const char *text, size_t len,
                      int append)
{
    if (len == 0)
        return;

    if (append && input->kill_buf && input->kill_buf_len > 0) {
        char *new_buf = (char *)realloc(input->kill_buf, input->kill_buf_len + len);
        if (!new_buf)
            return;
        memcpy(new_buf + input->kill_buf_len, text, len);
        input->kill_buf = new_buf;
        input->kill_buf_len += len;
    } else {
        free(input->kill_buf);
        input->kill_buf = (char *)malloc(len);
        if (!input->kill_buf) {
            input->kill_buf_len = 0;
            return;
        }
        memcpy(input->kill_buf, text, len);
        input->kill_buf_len = len;
    }
}

/* Transpose the two characters before the cursor (Ctrl+T) */
static void transpose_chars(TuiTextInput *input)
{
    if (input->cursor_byte == 0 || input->text_len < 2)
        return;

    /* If at end of text, transpose the two chars before cursor.
     * Otherwise, transpose char before and at cursor, then advance. */
    size_t pos = input->cursor_byte;
    if (pos >= input->text_len)
        pos = input->text_len; /* at end */

    /* Find the two characters to swap */
    size_t c2_start, c2_end, c1_start;

    if (pos >= input->text_len) {
        /* At end: swap last two characters */
        c2_end = input->text_len;
        c2_start = utf8_prev_char(input->text, c2_end);
        c1_start = utf8_prev_char(input->text, c2_start);
    } else {
        /* In middle: swap char before cursor with char at cursor */
        c2_start = pos;
        c2_end = pos + utf8_char_len(input->text + pos);
        if (c2_end > input->text_len)
            c2_end = input->text_len;
        c1_start = utf8_prev_char(input->text, c2_start);
    }

    size_t c1_len = c2_start - c1_start;
    size_t c2_len = c2_end - c2_start;

    if (c1_len == 0 || c2_len == 0)
        return;

    /* Swap using a small temp buffer */
    char tmp[8];
    if (c1_len + c2_len > sizeof(tmp))
        return;

    memcpy(tmp, input->text + c2_start, c2_len);
    memcpy(tmp + c2_len, input->text + c1_start, c1_len);
    memcpy(input->text + c1_start, tmp, c1_len + c2_len);

    input->cursor_byte = c2_end;
    recalculate_cursor_position(input);
}

/* Capture pre-edit state into caller-owned variables */
static inline void undo_snapshot(TuiTextInput *input, size_t *out_len,
                                 size_t *out_cursor, char **out_text)
{
    *out_len = input->text_len;
    *out_cursor = input->cursor_byte;
    *out_text = (char *)malloc(input->text_len + 1);
    if (*out_text)
        memcpy(*out_text, input->text, input->text_len + 1);
}

/* Push snapshot onto undo stack only if text actually changed */
static void undo_commit(TuiTextInput *input, char *snap_text, size_t snap_len,
                        size_t snap_cursor)
{
    if (!snap_text)
        return;

    /* Text unchanged — discard snapshot */
    if (snap_len == input->text_len &&
        memcmp(snap_text, input->text, snap_len) == 0) {
        free(snap_text);
        return;
    }

    /* Grow stack if needed */
    if (input->undo_count >= input->undo_cap) {
        int new_cap = input->undo_cap == 0 ? 32 : input->undo_cap * 2;
        void *new_stack =
            realloc(input->undo_stack, new_cap * sizeof(*input->undo_stack));
        if (!new_stack) {
            free(snap_text);
            return;
        }
        input->undo_stack = new_stack;
        input->undo_cap = new_cap;
    }

    /* Transfer ownership of snap_text to the stack */
    int idx = input->undo_count;
    input->undo_stack[idx].text = snap_text;
    input->undo_stack[idx].text_len = snap_len;
    input->undo_stack[idx].cursor_byte = snap_cursor;
    input->undo_count++;
}

/* Restore most recent undo snapshot */
static void undo_pop(TuiTextInput *input)
{
    if (input->undo_count == 0)
        return;

    int idx = input->undo_count - 1;

    /* Restore state */
    if (ensure_capacity(input, input->undo_stack[idx].text_len + 1) < 0)
        return;
    memcpy(input->text, input->undo_stack[idx].text,
           input->undo_stack[idx].text_len + 1);
    input->text_len = input->undo_stack[idx].text_len;
    input->cursor_byte = input->undo_stack[idx].cursor_byte;
    if (input->cursor_byte > input->text_len)
        input->cursor_byte = input->text_len;
    recalculate_cursor_position(input);

    free(input->undo_stack[idx].text);
    input->undo_count--;
}

/* Free the undo stack */
static void undo_free(TuiTextInput *input)
{
    for (int i = 0; i < input->undo_count; i++) {
        free(input->undo_stack[i].text);
    }
    free(input->undo_stack);
    input->undo_stack = NULL;
    input->undo_count = 0;
    input->undo_cap = 0;
}

/* Free completion list */
static void free_completions(TuiTextInput *input)
{
    if (input->completions) {
        for (int i = 0; i < input->completion_count; i++) {
            free(input->completions[i]);
        }
        free(input->completions);
        input->completions = NULL;
        input->completion_count = 0;
        input->completion_index = 0;
    }
    input->completion_word_start = 0;
    input->completion_word_len = 0;
}

/* Check if a history entry matches the current prefix filter */
static int history_matches_prefix(const char *entry, const char *prefix,
                                  size_t prefix_len)
{
    if (prefix_len == 0)
        return 1;
    return strncmp(entry, prefix, prefix_len) == 0;
}

/* Navigate to previous history entry (Up arrow / Ctrl+P)
 * When the user has typed a prefix before navigating, only visit
 * history entries that start with that prefix. */
static void history_prev(TuiTextInput *input)
{
    if (!input->history || input->history_count == 0)
        return;

    /* Save current input if we're at position -1 */
    if (input->history_pos == -1) {
        free(input->saved_input);
        input->saved_input = strdup(input->text);
    }

    /* Search for next matching entry */
    const char *prefix = input->saved_input ? input->saved_input : "";
    size_t prefix_len = strlen(prefix);

    for (int i = input->history_pos + 1; i < input->history_count; i++) {
        if (history_matches_prefix(input->history[i], prefix, prefix_len)) {
            input->history_pos = i;
            tui_textinput_set_text(input, input->history[i]);
            return;
        }
    }
}

/* Navigate to next history entry (Down arrow / Ctrl+N)
 * Respects the same prefix filter as history_prev. */
static void history_next(TuiTextInput *input)
{
    if (input->history_pos < 0)
        return;

    const char *prefix = input->saved_input ? input->saved_input : "";
    size_t prefix_len = strlen(prefix);

    /* Search for previous matching entry (toward more recent) */
    for (int i = input->history_pos - 1; i >= 0; i--) {
        if (history_matches_prefix(input->history[i], prefix, prefix_len)) {
            input->history_pos = i;
            tui_textinput_set_text(input, input->history[i]);
            return;
        }
    }

    /* No more matches — restore saved input */
    input->history_pos = -1;
    if (input->saved_input) {
        tui_textinput_set_text(input, input->saved_input);
        free(input->saved_input);
        input->saved_input = NULL;
    } else {
        tui_textinput_clear(input);
    }
}

/* Replace bytes in text buffer from [start, start+old_len) with new_word */
static void replace_word(TuiTextInput *input, int start, int old_len,
                         const char *new_word)
{
    int new_len = (int)strlen(new_word);
    int tail_start = start + old_len;
    int tail_len = (int)input->text_len - tail_start;
    size_t new_text_len = (size_t)(start + new_len + tail_len);

    if (ensure_capacity(input, new_text_len + 1) < 0)
        return;

    /* Shift tail to make room (or shrink) */
    memmove(input->text + start + new_len, input->text + tail_start,
            (size_t)tail_len);
    /* Copy new word in */
    memcpy(input->text + start, new_word, (size_t)new_len);

    input->text_len = new_text_len;
    input->text[input->text_len] = '\0';
    input->cursor_byte = (size_t)(start + new_len);
    recalculate_cursor_position(input);
}

/* Handle tab completion */
static void handle_tab_completion(TuiTextInput *input)
{
    if (!input->completer)
        return;

    /* If we already have completions, cycle through them */
    if (input->completions && input->completion_count > 0) {
        input->completion_index =
            (input->completion_index + 1) % input->completion_count;
        const char *word = input->completions[input->completion_index];
        replace_word(input, input->completion_word_start,
                     input->completion_word_len, word);
        input->completion_word_len = (int)strlen(word);
        return;
    }

    /* Find word start by scanning backward from cursor for space/tab */
    int word_start = (int)input->cursor_byte;
    while (word_start > 0 && input->text[word_start - 1] != ' ' &&
           input->text[word_start - 1] != '\t') {
        word_start--;
    }
    int word_len = (int)input->cursor_byte - word_start;

    /* Get new completions */
    char **completions = input->completer(input->text, (int)input->cursor_byte,
                                          input->completer_data);
    if (!completions || !completions[0]) {
        /* No completions */
        if (completions)
            free(completions);
        return;
    }

    /* Count completions */
    int count = 0;
    while (completions[count])
        count++;

    input->completions = completions;
    input->completion_count = count;
    input->completion_index = 0;
    input->completion_word_start = word_start;
    input->completion_word_len = word_len;

    /* Apply first completion */
    const char *word = input->completions[0];
    replace_word(input, word_start, word_len, word);
    input->completion_word_len = (int)strlen(word);
}

/* Create a new text input component */
TuiTextInput *tui_textinput_create(const TuiTextInputConfig *config)
{
    TuiTextInput *input = (TuiTextInput *)malloc(sizeof(TuiTextInput));
    if (!input)
        return NULL;

    memset(input, 0, sizeof(TuiTextInput));
    input->base.type = TEXTINPUT_TYPE_ID;

    /* Allocate initial text buffer */
    input->text_cap = TEXTINPUT_INITIAL_CAP;
    input->text = (char *)malloc(input->text_cap);
    if (!input->text) {
        free(input);
        return NULL;
    }
    input->text[0] = '\0';
    input->text_len = 0;
    input->focused = 1; /* Focused by default */
    input->multiline = 0;
    input->show_prompt = 1;  /* Show prompt by default */
    input->history_pos = -1; /* -1 means we're at current input */

    /* Apply config if provided */
    if (config) {
        input->prompt = config->prompt;
        if (input->prompt) {
            input->prompt_len = utf8_display_width(input->prompt);
        }
        input->width = config->width;
        input->height = config->height;
        input->multiline = config->multiline;
    }

    return input;
}

/* Free text input component */
void tui_textinput_free(TuiTextInput *input)
{
    if (!input)
        return;
    if (input->text)
        free(input->text);
    if (input->saved_input)
        free(input->saved_input);
    if (input->history) {
        for (int i = 0; i < input->history_count; i++) {
            free(input->history[i]);
        }
        free(input->history);
    }
    free_completions(input);
    free(input->kill_buf);
    undo_free(input);
    free(input);
}

/* Update text input with message */
TuiUpdateResult tui_textinput_update(TuiTextInput *input, TuiMsg msg)
{
    if (!input || !input->focused)
        return tui_update_result_none();

    if (msg.type == TUI_MSG_FOCUS) {
        input->focused = 1;
        return tui_update_result_none();
    }

    if (msg.type == TUI_MSG_BLUR) {
        input->focused = 0;
        return tui_update_result_none();
    }

    if (msg.type != TUI_MSG_KEY_PRESS)
        return tui_update_result_none();

    TuiKeyMsg key = msg.data.key;

    /* Handle C-x prefix: if set, check for C-x C-u (undo) */
    if (input->ctrl_x_prefix) {
        input->ctrl_x_prefix = 0;
        if ((key.mods & TUI_MOD_CTRL) && (key.rune == 'u' || key.rune == 'U') &&
            key.key == TUI_KEY_NONE) {
            undo_pop(input);
            return tui_update_result_none();
        }
        /* Not C-u after C-x: fall through to normal handling */
    }

    /* Snapshot pre-edit state; will be pushed to undo stack only if text changes
     */
    size_t snap_len, snap_cursor;
    char *snap_text;
    undo_snapshot(input, &snap_len, &snap_cursor, &snap_text);

    /* Track consecutive kill commands for append behavior */
    int was_kill = input->last_was_kill;
    input->last_was_kill = 0;

    /* Any key other than Tab invalidates active completions */
    if (key.key != TUI_KEY_TAB)
        free_completions(input);

    /* Handle special keys */
    switch (key.key) {
    case TUI_KEY_LEFT:
        if (key.mods & TUI_MOD_CTRL)
            cursor_word_left(input);
        else
            cursor_left(input);
        break;

    case TUI_KEY_RIGHT:
        if (key.mods & TUI_MOD_CTRL)
            cursor_word_right(input);
        else
            cursor_right(input);
        break;

    case TUI_KEY_UP:
        if (input->multiline) {
            cursor_up(input);
        } else {
            /* Single-line: navigate history */
            history_prev(input);
        }
        break;

    case TUI_KEY_DOWN:
        if (input->multiline) {
            cursor_down(input);
        } else {
            /* Single-line: navigate history */
            history_next(input);
        }
        break;

    case TUI_KEY_TAB:
        handle_tab_completion(input);
        break;

    case TUI_KEY_HOME:
        cursor_home(input);
        break;

    case TUI_KEY_END:
        cursor_end(input);
        break;

    case TUI_KEY_BACKSPACE:
        delete_before(input);
        break;

    case TUI_KEY_DELETE:
        delete_at(input);
        break;

    case TUI_KEY_ENTER:
        if (input->multiline && (key.mods & TUI_MOD_SHIFT)) {
            /* Shift+Enter in multiline: insert newline */
            insert_codepoint(input, '\n');
        } else {
            /* Submit the line (both single-line and multiline Enter) */
            char *line = strdup(input->text);
            tui_textinput_clear(input);
            input->history_pos = -1; /* Reset history navigation */
            if (input->saved_input) {
                free(input->saved_input);
                input->saved_input = NULL;
            }
            free(snap_text);
            return tui_update_result(tui_cmd_line_submit(line));
        }
        break;

    case TUI_KEY_NONE:
        /* Regular character */
        if (key.rune >= 0x20 || key.rune == '\t') {
            /* Skip control characters with Ctrl modifier (except Tab) */
            if ((key.mods & TUI_MOD_CTRL) && key.rune != '\t') {
                /* Handle Ctrl+key shortcuts */
                if (key.rune == 'a' || key.rune == 'A') {
                    /* Ctrl+A: Move to start of line */
                    cursor_home(input);
                } else if (key.rune == 'b' || key.rune == 'B') {
                    /* Ctrl+B: Move back one character */
                    cursor_left(input);
                } else if (key.rune == 'd' || key.rune == 'D') {
                    /* Ctrl+D: EOF on empty line, delete char otherwise */
                    if (input->text_len == 0 && !input->multiline) {
                        free(snap_text);
                        return tui_update_result(tui_cmd_quit());
                    } else {
                        delete_at(input);
                    }
                } else if (key.rune == 'e' || key.rune == 'E') {
                    /* Ctrl+E: Move to end of line */
                    cursor_end(input);
                } else if (key.rune == 'f' || key.rune == 'F') {
                    /* Ctrl+F: Move forward one character */
                    cursor_right(input);
                } else if (key.rune == 'h' || key.rune == 'H') {
                    /* Ctrl+H: Delete previous character (backspace) */
                    delete_before(input);
                } else if (key.rune == 'j' || key.rune == 'J') {
                    /* Ctrl+J: Insert newline in multiline, submit in single-line */
                    if (input->multiline) {
                        insert_codepoint(input, '\n');
                    } else {
                        char *line = strdup(input->text);
                        tui_textinput_clear(input);
                        input->history_pos = -1;
                        if (input->saved_input) {
                            free(input->saved_input);
                            input->saved_input = NULL;
                        }
                        free(snap_text);
                        return tui_update_result(tui_cmd_line_submit(line));
                    }
                } else if (key.rune == 'k' || key.rune == 'K') {
                    /* Ctrl+K: Kill to end of line */
                    size_t end = input->cursor_byte;
                    while (end < input->text_len && input->text[end] != '\n') {
                        end++;
                    }
                    if (end > input->cursor_byte) {
                        kill_save(input, input->text + input->cursor_byte,
                                  end - input->cursor_byte, was_kill);
                        memmove(input->text + input->cursor_byte, input->text + end,
                                input->text_len - end);
                        input->text_len -= (end - input->cursor_byte);
                        input->text[input->text_len] = '\0';
                    }
                    input->last_was_kill = 1;
                } else if (key.rune == 'n' || key.rune == 'N') {
                    /* Ctrl+N: Next history entry */
                    if (!input->multiline) {
                        history_next(input);
                    }
                } else if (key.rune == 'p' || key.rune == 'P') {
                    /* Ctrl+P: Previous history entry */
                    if (!input->multiline) {
                        history_prev(input);
                    }
                } else if (key.rune == 't' || key.rune == 'T') {
                    /* Ctrl+T: Transpose characters before cursor */
                    transpose_chars(input);
                } else if (key.rune == 'u' || key.rune == 'U') {
                    /* Ctrl+U: Kill to start of line */
                    size_t start = input->cursor_byte;
                    while (start > 0 && input->text[start - 1] != '\n') {
                        start--;
                    }
                    if (start < input->cursor_byte) {
                        size_t del_len = input->cursor_byte - start;
                        kill_save(input, input->text + start, del_len, 0);
                        memmove(input->text + start, input->text + input->cursor_byte,
                                input->text_len - input->cursor_byte);
                        input->text_len -= del_len;
                        input->cursor_byte = start;
                        input->text[input->text_len] = '\0';
                        recalculate_cursor_position(input);
                    }
                    input->last_was_kill = 1;
                } else if (key.rune == 'w' || key.rune == 'W') {
                    /* Ctrl+W: Kill word backward */
                    if (input->cursor_byte > 0) {
                        size_t pos = input->cursor_byte;
                        /* Skip whitespace backward */
                        while (pos > 0 && (input->text[pos - 1] == ' ' ||
                                           input->text[pos - 1] == '\t'))
                            pos--;
                        /* Skip word backward */
                        while (pos > 0 && input->text[pos - 1] != ' ' &&
                               input->text[pos - 1] != '\t')
                            pos--;
                        if (pos < input->cursor_byte) {
                            size_t del_len = input->cursor_byte - pos;
                            kill_save(input, input->text + pos, del_len, was_kill);
                            memmove(input->text + pos, input->text + input->cursor_byte,
                                    input->text_len - input->cursor_byte);
                            input->text_len -= del_len;
                            input->cursor_byte = pos;
                            input->text[input->text_len] = '\0';
                            recalculate_cursor_position(input);
                        }
                        input->last_was_kill = 1;
                    }
                } else if (key.rune == 'x' || key.rune == 'X') {
                    /* Ctrl+X: prefix for C-x C-u (undo) */
                    input->ctrl_x_prefix = 1;
                } else if (key.rune == 'y' || key.rune == 'Y') {
                    /* Ctrl+Y: Yank (paste from kill buffer) */
                    if (input->kill_buf && input->kill_buf_len > 0) {
                        insert_text(input, input->kill_buf, input->kill_buf_len);
                    }
                } else if (key.rune == '_') {
                    /* Ctrl+_: Undo — discard snapshot so the undo itself isn't undoable
                     */
                    free(snap_text);
                    undo_pop(input);
                    return tui_update_result_none();
                }
            } else {
                insert_codepoint(input, key.rune);
            }
        }
        break;

    default:
        break;
    }

    undo_commit(input, snap_text, snap_len, snap_cursor);
    return tui_update_result_none();
}

/* Render a horizontal divider line in place (no newline) */
static void render_divider_inline(DynamicBuffer *out, int width,
                                  const char *color)
{
    /* Use Unicode box-drawing character ─ (U+2500) */
    const char *line_char = "\xe2\x94\x80"; /* UTF-8 encoding of ─ */
    /* Reset any inherited color state before applying color */
    dynamic_buffer_append_str(out, SGR_RESET);
    if (color && color[0] != '\0') {
        dynamic_buffer_append_str(out, color);
    } else {
        dynamic_buffer_append_str(out, SGR_DIM);
    }
    for (int i = 0; i < width; i++) {
        dynamic_buffer_append_str(out, line_char);
    }
    dynamic_buffer_append_str(out, SGR_RESET);
}

/* Render text input to output buffer
 *
 * When terminal_row is set (> 0), uses absolute cursor positioning:
 * - Positions cursor absolutely using CSI row;col H
 * - Renders dividers on adjacent rows (row-1 and row+1)
 * - No relative cursor movements
 *
 * When terminal_row is 0 (not set), uses legacy relative positioning.
 *
 * Layout with dividers (3 lines):
 * - Row N-1: top divider
 * - Row N:   input line (this is terminal_row)
 * - Row N+1: bottom divider
 */
void tui_textinput_view(const TuiTextInput *input, DynamicBuffer *out)
{
    if (!input || !out)
        return;

    int term_width = input->terminal_width > 0 ? input->terminal_width : 80;
    char pos_buf[32];

    if (!input->multiline) {
        /* Single-line mode */

        if (input->terminal_row > 0) {
            /* Absolute positioning mode */
            int input_row = input->terminal_row;

            if (input->show_dividers) {
                /* Top divider (row - 1) */
                int top_row = input_row - 1;
                if (top_row >= 1) {
                    snprintf(pos_buf, sizeof(pos_buf), CSI "%d;1H", top_row);
                    dynamic_buffer_append_str(out, pos_buf);
                    dynamic_buffer_append_str(out, EL_TO_END);
                    render_divider_inline(out, term_width, input->divider_color);
                }
            }

            /* Input line */
            snprintf(pos_buf, sizeof(pos_buf), CSI "%d;1H", input_row);
            dynamic_buffer_append_str(out, pos_buf);
            dynamic_buffer_append_str(out, EL_TO_END);

            /* Output prompt if set and shown */
            if (input->show_prompt && input->prompt && input->prompt_len > 0) {
                dynamic_buffer_append_str(out, input->prompt);
            }

            /* Output text content (visible slice only for horizontal scroll) */
            if (input->text_len > 0) {
                if (input->terminal_width > 0 && input->offset_right > input->offset) {
                    size_t byte_start = utf8_byte_offset_of_cp(
                        input->text, input->text_len, input->offset);
                    size_t byte_end = utf8_byte_offset_of_cp(input->text, input->text_len,
                                                             input->offset_right);
                    dynamic_buffer_append(out, input->text + byte_start,
                                          byte_end - byte_start);
                } else {
                    dynamic_buffer_append(out, input->text, input->text_len);
                }
            }

            if (input->show_dividers) {
                /* Bottom divider (row + 1) */
                int bottom_row = input_row + 1;
                snprintf(pos_buf, sizeof(pos_buf), CSI "%d;1H", bottom_row);
                dynamic_buffer_append_str(out, pos_buf);
                dynamic_buffer_append_str(out, EL_TO_END);
                render_divider_inline(out, term_width, input->divider_color);
            }

            /* Position cursor on input line */
            if (input->focused) {
                int prompt_width =
                    (input->show_prompt && input->prompt) ? input->prompt_len : 0;
                int cursor_cp = utf8_cp_index_of_byte(input->text, input->cursor_byte);
                int cursor_visual_col =
                    prompt_width + (cursor_cp - input->offset) + 1; /* 1-indexed */

                snprintf(pos_buf, sizeof(pos_buf), CSI "%d;%dH", input_row,
                         cursor_visual_col);
                dynamic_buffer_append_str(out, pos_buf);
            }
        } else {
            /* Legacy relative positioning mode (terminal_row not set) */

            /* Just clear and render on current line */
            dynamic_buffer_append_str(out, "\r");
            dynamic_buffer_append_str(out, EL_TO_END);

            /* Output prompt if set and shown */
            if (input->show_prompt && input->prompt && input->prompt_len > 0) {
                dynamic_buffer_append_str(out, input->prompt);
            }

            /* Output text content (visible slice only for horizontal scroll) */
            if (input->text_len > 0) {
                if (input->terminal_width > 0 && input->offset_right > input->offset) {
                    size_t byte_start = utf8_byte_offset_of_cp(
                        input->text, input->text_len, input->offset);
                    size_t byte_end = utf8_byte_offset_of_cp(input->text, input->text_len,
                                                             input->offset_right);
                    dynamic_buffer_append(out, input->text + byte_start,
                                          byte_end - byte_start);
                } else {
                    dynamic_buffer_append(out, input->text, input->text_len);
                }
            }

            /* Position cursor */
            if (input->focused) {
                int prompt_width =
                    (input->show_prompt && input->prompt) ? input->prompt_len : 0;
                int cursor_cp = utf8_cp_index_of_byte(input->text, input->cursor_byte);
                int cursor_visual_col = prompt_width + (cursor_cp - input->offset);

                dynamic_buffer_append_str(out, "\r");
                if (cursor_visual_col > 0) {
                    snprintf(pos_buf, sizeof(pos_buf), CSI "%dC", cursor_visual_col);
                    dynamic_buffer_append_str(out, pos_buf);
                }
            }
        }
    } else if (input->terminal_row > 0) {
        /* Multi-line mode with absolute positioning */
        int line_count = tui_textinput_line_count(input);
        int content_start_row = input->terminal_row;

        if (input->show_dividers) {
            /* Top divider */
            int top_row = content_start_row - 1;
            if (top_row >= 1) {
                snprintf(pos_buf, sizeof(pos_buf), CSI "%d;1H", top_row);
                dynamic_buffer_append_str(out, pos_buf);
                dynamic_buffer_append_str(out, EL_TO_END);
                render_divider_inline(out, term_width, input->divider_color);
            }
        }

        /* Render each line with absolute positioning */
        int current_line = 0;
        size_t line_start = 0;
        for (size_t i = 0; i <= input->text_len; i++) {
            if (i == input->text_len || input->text[i] == '\n') {
                int row = content_start_row + current_line;
                snprintf(pos_buf, sizeof(pos_buf), CSI "%d;1H", row);
                dynamic_buffer_append_str(out, pos_buf);
                dynamic_buffer_append_str(out, EL_TO_END);

                /* Prompt or indentation */
                if (current_line == 0 && input->show_prompt && input->prompt &&
                    input->prompt_len > 0) {
                    dynamic_buffer_append_str(out, input->prompt);
                } else if (current_line > 0 && input->show_prompt && input->prompt &&
                           input->prompt_len > 0) {
                    for (int j = 0; j < input->prompt_len; j++) {
                        dynamic_buffer_append(out, " ", 1);
                    }
                }

                /* Line content */
                if (i > line_start) {
                    dynamic_buffer_append(out, input->text + line_start,
                                          i - line_start);
                }

                current_line++;
                line_start = i + 1;
            }
        }

        if (input->show_dividers) {
            /* Bottom divider */
            int bottom_row = content_start_row + line_count;
            snprintf(pos_buf, sizeof(pos_buf), CSI "%d;1H", bottom_row);
            dynamic_buffer_append_str(out, pos_buf);
            dynamic_buffer_append_str(out, EL_TO_END);
            render_divider_inline(out, term_width, input->divider_color);
        }

        /* Position cursor */
        if (input->focused) {
            int prompt_width =
                (input->show_prompt && input->prompt) ? input->prompt_len : 0;
            int cursor_col = (int)input->cursor_col + prompt_width + 1; /* 1-indexed */
            int cursor_row = content_start_row + (int)input->cursor_row;
            snprintf(pos_buf, sizeof(pos_buf), CSI "%d;%dH", cursor_row,
                     cursor_col);
            dynamic_buffer_append_str(out, pos_buf);
        }
    } else {
        /* Multi-line mode: relative positioning (legacy) */
        /* Output prompt if set and shown */
        if (input->show_prompt && input->prompt && input->prompt_len > 0) {
            dynamic_buffer_append_str(out, input->prompt);
        }

        /* Output text content */
        if (input->text_len > 0) {
            for (size_t i = 0; i < input->text_len; i++) {
                if (input->text[i] == '\n') {
                    dynamic_buffer_append_str(out, "\r\n");
                    /* Add prompt indentation on continued lines */
                    if (input->show_prompt && input->prompt && input->prompt_len > 0) {
                        for (int j = 0; j < input->prompt_len; j++) {
                            dynamic_buffer_append(out, " ", 1);
                        }
                    }
                } else {
                    dynamic_buffer_append(out, &input->text[i], 1);
                }
            }
        }
    }
}

/* Get current text content */
const char *tui_textinput_text(const TuiTextInput *input)
{
    return input ? input->text : NULL;
}

/* Get text length */
size_t tui_textinput_len(const TuiTextInput *input)
{
    return input ? input->text_len : 0;
}

/* Set text content */
void tui_textinput_set_text(TuiTextInput *input, const char *text)
{
    if (!input)
        return;

    if (!text || *text == '\0') {
        tui_textinput_clear(input);
        return;
    }

    size_t len = strlen(text);
    if (ensure_capacity(input, len + 1) < 0)
        return;

    memcpy(input->text, text, len);
    input->text[len] = '\0';
    input->text_len = len;
    input->cursor_byte = len;
    recalculate_cursor_position(input);
}

/* Clear text content */
void tui_textinput_clear(TuiTextInput *input)
{
    if (!input)
        return;
    input->text_len = 0;
    input->cursor_byte = 0;
    input->cursor_row = 0;
    input->cursor_col = 0;
    input->offset = 0;
    input->offset_right = 0;
    if (input->text)
        input->text[0] = '\0';
    undo_free(input);
}

/* Set focus state */
void tui_textinput_set_focus(TuiTextInput *input, int focused)
{
    if (input)
        input->focused = focused;
}

/* Check if focused */
int tui_textinput_is_focused(const TuiTextInput *input)
{
    return input ? input->focused : 0;
}

/* Get cursor position */
size_t tui_textinput_cursor(const TuiTextInput *input)
{
    return input ? input->cursor_byte : 0;
}

/* Set cursor position */
void tui_textinput_set_cursor(TuiTextInput *input, size_t pos)
{
    if (!input)
        return;
    if (pos > input->text_len)
        pos = input->text_len;
    input->cursor_byte = pos;
    recalculate_cursor_position(input);
}

/* Get number of lines */
int tui_textinput_line_count(const TuiTextInput *input)
{
    if (!input || input->text_len == 0)
        return 1;

    int count = 1;
    for (size_t i = 0; i < input->text_len; i++) {
        if (input->text[i] == '\n')
            count++;
    }
    return count;
}

/* Set maximum history size */
void tui_textinput_set_history_size(TuiTextInput *input, int size)
{
    if (!input || size < 0)
        return;

    /* Free existing history if shrinking */
    if (input->history && size < input->history_count) {
        for (int i = size; i < input->history_count; i++) {
            free(input->history[i]);
        }
        input->history_count = size;
    }

    /* Reallocate or allocate history array */
    if (size == 0) {
        free(input->history);
        input->history = NULL;
    } else {
        char **new_history =
            (char **)realloc(input->history, size * sizeof(char *));
        if (new_history) {
            input->history = new_history;
        }
    }
    input->history_size = size;
    input->history_pos = -1;
}

/* Add a line to history */
void tui_textinput_history_add(TuiTextInput *input, const char *line)
{
    if (!input || !line || !line[0] || input->history_size == 0)
        return;

    /* Allocate history array if needed */
    if (!input->history) {
        input->history = (char **)calloc(input->history_size, sizeof(char *));
        if (!input->history)
            return;
    }

    /* Remove any existing duplicate entry (squash) */
    for (int i = 0; i < input->history_count; i++) {
        if (input->history[i] && strcmp(input->history[i], line) == 0) {
            free(input->history[i]);
            /* Shift entries up to fill the gap */
            for (int j = i; j < input->history_count - 1; j++) {
                input->history[j] = input->history[j + 1];
            }
            input->history[input->history_count - 1] = NULL;
            input->history_count--;
            break;
        }
    }

    /* Make room for new entry at position 0 */
    if (input->history_count >= input->history_size) {
        /* Free oldest entry */
        free(input->history[input->history_size - 1]);
        input->history_count = input->history_size - 1;
    }

    /* Shift existing entries down */
    for (int i = input->history_count; i > 0; i--) {
        input->history[i] = input->history[i - 1];
    }

    /* Add new entry at position 0 */
    input->history[0] = strdup(line);
    if (input->history[0]) {
        input->history_count++;
    }

    /* Reset history position */
    input->history_pos = -1;
}

/* Set completion callback */
void tui_textinput_set_completer(TuiTextInput *input, TuiCompletionCallback cb,
                                 void *data)
{
    if (!input)
        return;
    input->completer = cb;
    input->completer_data = data;
    free_completions(input);
}

/* Set whether to show the prompt */
void tui_textinput_set_show_prompt(TuiTextInput *input, int show)
{
    if (input)
        input->show_prompt = show;
}

/* Set the prompt string */
void tui_textinput_set_prompt(TuiTextInput *input, const char *prompt)
{
    if (!input)
        return;
    input->prompt = prompt;
    input->prompt_len = prompt ? utf8_display_width(prompt) : 0;
}

/* Set whether to show dividers above/below the input */
void tui_textinput_set_show_dividers(TuiTextInput *input, int show)
{
    if (input) {
        input->show_dividers = show;
    }
}

/* Set custom ANSI color for dividers */
void tui_textinput_set_divider_color(TuiTextInput *input, const char *color)
{
    if (!input)
        return;
    if (color && color[0] != '\0') {
        snprintf(input->divider_color, sizeof(input->divider_color), "%s",
                 color);
    } else {
        input->divider_color[0] = '\0';
    }
}

/* Set terminal width for divider rendering */
void tui_textinput_set_terminal_width(TuiTextInput *input, int width)
{
    if (input)
        input->terminal_width = width;
}

/* Set terminal row for absolute positioning */
void tui_textinput_set_terminal_row(TuiTextInput *input, int row)
{
    if (input)
        input->terminal_row = row;
}

/* Get render height in rows */
int tui_textinput_get_height(const TuiTextInput *input)
{
    if (!input)
        return 1;
    int h = input->multiline ? tui_textinput_line_count(input) : 1;
    if (input->show_dividers)
        h += 2; /* Top + bottom dividers */
    return h;
}

/* Component interface wrappers */
static TuiInitResult textinput_init(void *config)
{
    TuiModel *model =
        (TuiModel *)tui_textinput_create((const TuiTextInputConfig *)config);
    return tui_init_result_none(model);
}

static TuiUpdateResult textinput_update(TuiModel *model, TuiMsg msg)
{
    return tui_textinput_update((TuiTextInput *)model, msg);
}

static void textinput_view(const TuiModel *model, DynamicBuffer *out)
{
    tui_textinput_view((const TuiTextInput *)model, out);
}

static void textinput_free(TuiModel *model)
{
    tui_textinput_free((TuiTextInput *)model);
}

/* Static component interface instance */
static const TuiComponent textinput_component = {
    .init = textinput_init,
    .update = textinput_update,
    .view = textinput_view,
    .free = textinput_free,
};

/* Get component interface for text input */
const TuiComponent *tui_textinput_component(void)
{
    return &textinput_component;
}

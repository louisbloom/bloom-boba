/* test_textinput.c - Unit tests for the textinput component */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bloom-boba/components/textinput.h>
#include <bloom-boba/dynamic_buffer.h>
#include <bloom-boba/msg.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        tests_run++;                                                            \
        fn();                                                                   \
        tests_passed++;                                                         \
        printf("  PASS: %s\n", #fn);                                            \
    } while (0)

/* ---------- helpers ---------- */

/* Send a string of ASCII characters to the input */
static void send_string(TuiTextInput *input, const char *s) {
    for (const char *p = s; *p; p++) {
        tui_textinput_update(input, tui_msg_char((uint32_t)*p, 0));
    }
}

static void send_key(TuiTextInput *input, int key) {
    TuiUpdateResult r = tui_textinput_update(input, tui_msg_key(key, 0, 0));
    if (r.cmd)
        tui_cmd_free(r.cmd);
}

/* Free any command returned by update (avoid leaks in tests) */
static void send_char(TuiTextInput *input, char c) {
    TuiUpdateResult r = tui_textinput_update(input, tui_msg_char((uint32_t)c, 0));
    if (r.cmd)
        tui_cmd_free(r.cmd);
}

/* Tab completion test helper */
static char **test_completer(const char *buffer, int cursor_pos,
                             void *userdata) {
    (void)cursor_pos;
    (void)userdata;

    /* Simple completer: prefix "he" -> {"hello", "help", NULL} */
    if (strncmp(buffer, "he", 2) == 0) {
        char **completions = malloc(3 * sizeof(char *));
        completions[0] = strdup("hello");
        completions[1] = strdup("help");
        completions[2] = NULL;
        return completions;
    }
    return NULL;
}

/* ---------- tests ---------- */

static void test_create_and_free(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    assert(input != NULL);
    assert(strcmp(tui_textinput_text(input), "") == 0);
    assert(tui_textinput_len(input) == 0);
    assert(tui_textinput_cursor(input) == 0);
    tui_textinput_free(input);
}

static void test_create_with_config(void) {
    TuiTextInputConfig cfg = {
        .prompt = "> ",
        .width = 40,
        .height = 1,
        .multiline = 0,
    };
    TuiTextInput *input = tui_textinput_create(&cfg);
    assert(input != NULL);
    assert(input->width == 40);
    tui_textinput_free(input);
}

static void test_char_insertion(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_char(input, 'h');
    send_char(input, 'e');
    send_char(input, 'l');
    send_char(input, 'l');
    send_char(input, 'o');

    assert(strcmp(tui_textinput_text(input), "hello") == 0);
    assert(tui_textinput_len(input) == 5);
    assert(tui_textinput_cursor(input) == 5);
    tui_textinput_free(input);
}

static void test_backspace(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hello");
    send_key(input, TUI_KEY_BACKSPACE);

    assert(strcmp(tui_textinput_text(input), "hell") == 0);
    assert(tui_textinput_cursor(input) == 4);
    tui_textinput_free(input);
}

static void test_backspace_at_start(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    /* Backspace on empty input should do nothing */
    send_key(input, TUI_KEY_BACKSPACE);
    assert(strcmp(tui_textinput_text(input), "") == 0);
    assert(tui_textinput_cursor(input) == 0);
    tui_textinput_free(input);
}

static void test_delete_key(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hello");
    /* Move cursor to start */
    send_key(input, TUI_KEY_HOME);
    /* Delete first character */
    send_key(input, TUI_KEY_DELETE);

    assert(strcmp(tui_textinput_text(input), "ello") == 0);
    assert(tui_textinput_cursor(input) == 0);
    tui_textinput_free(input);
}

static void test_cursor_left_right(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "abc");
    assert(tui_textinput_cursor(input) == 3);

    send_key(input, TUI_KEY_LEFT);
    assert(tui_textinput_cursor(input) == 2);

    send_key(input, TUI_KEY_LEFT);
    assert(tui_textinput_cursor(input) == 1);

    send_key(input, TUI_KEY_RIGHT);
    assert(tui_textinput_cursor(input) == 2);

    tui_textinput_free(input);
}

static void test_cursor_left_at_start(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    /* Left on empty input should stay at 0 */
    send_key(input, TUI_KEY_LEFT);
    assert(tui_textinput_cursor(input) == 0);
    tui_textinput_free(input);
}

static void test_cursor_right_at_end(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "ab");
    assert(tui_textinput_cursor(input) == 2);

    /* Right at end should stay put */
    send_key(input, TUI_KEY_RIGHT);
    assert(tui_textinput_cursor(input) == 2);
    tui_textinput_free(input);
}

static void test_home_end(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hello");
    assert(tui_textinput_cursor(input) == 5);

    send_key(input, TUI_KEY_HOME);
    assert(tui_textinput_cursor(input) == 0);

    send_key(input, TUI_KEY_END);
    assert(tui_textinput_cursor(input) == 5);

    tui_textinput_free(input);
}

static void test_insert_in_middle(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hllo");
    /* Move cursor after 'h' */
    send_key(input, TUI_KEY_HOME);
    send_key(input, TUI_KEY_RIGHT);
    /* Insert 'e' */
    send_char(input, 'e');

    assert(strcmp(tui_textinput_text(input), "hello") == 0);
    assert(tui_textinput_cursor(input) == 2);
    tui_textinput_free(input);
}

static void test_set_text(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    tui_textinput_set_text(input, "preset");
    assert(strcmp(tui_textinput_text(input), "preset") == 0);
    assert(tui_textinput_len(input) == 6);
    tui_textinput_free(input);
}

static void test_clear(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hello");
    tui_textinput_clear(input);
    assert(strcmp(tui_textinput_text(input), "") == 0);
    assert(tui_textinput_len(input) == 0);
    tui_textinput_free(input);
}

static void test_focus(void) {
    TuiTextInput *input = tui_textinput_create(NULL);

    /* Default is focused */
    assert(tui_textinput_is_focused(input) == 1);
    tui_textinput_set_focus(input, 0);
    assert(tui_textinput_is_focused(input) == 0);
    tui_textinput_set_focus(input, 1);
    assert(tui_textinput_is_focused(input) == 1);
    tui_textinput_free(input);
}

static void test_unfocused_ignores_input(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    /* Explicitly unfocus (default is focused) */
    tui_textinput_set_focus(input, 0);

    send_char(input, 'a');
    assert(strcmp(tui_textinput_text(input), "") == 0);
    tui_textinput_free(input);
}

static void test_history_navigation(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_history_size(input, 10);

    tui_textinput_history_add(input, "first");
    tui_textinput_history_add(input, "second");

    /* Start with empty input — history uses prefix filter, empty matches all */

    /* Up arrow -> most recent history entry */
    send_key(input, TUI_KEY_UP);
    assert(strcmp(tui_textinput_text(input), "second") == 0);

    /* Up again -> older entry */
    send_key(input, TUI_KEY_UP);
    assert(strcmp(tui_textinput_text(input), "first") == 0);

    /* Down -> back to most recent */
    send_key(input, TUI_KEY_DOWN);
    assert(strcmp(tui_textinput_text(input), "second") == 0);

    /* Down again -> back to saved (empty) input */
    send_key(input, TUI_KEY_DOWN);
    assert(strcmp(tui_textinput_text(input), "") == 0);

    tui_textinput_free(input);
}

static void test_tab_completion(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_completer(input, test_completer, NULL);

    send_string(input, "he");

    /* Press Tab -> first completion */
    send_key(input, TUI_KEY_TAB);
    assert(strcmp(tui_textinput_text(input), "hello") == 0);

    tui_textinput_free(input);
}

static void test_tab_cycling(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_completer(input, test_completer, NULL);

    send_string(input, "he");

    /* First Tab -> "hello" */
    send_key(input, TUI_KEY_TAB);
    assert(strcmp(tui_textinput_text(input), "hello") == 0);

    /* Second Tab -> "help" */
    send_key(input, TUI_KEY_TAB);
    assert(strcmp(tui_textinput_text(input), "help") == 0);

    /* Third Tab -> cycles back to "hello" */
    send_key(input, TUI_KEY_TAB);
    assert(strcmp(tui_textinput_text(input), "hello") == 0);

    tui_textinput_free(input);
}

static void test_tab_no_completions(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_completer(input, test_completer, NULL);

    send_string(input, "xyz");

    /* Tab with no matches should leave text unchanged */
    send_key(input, TUI_KEY_TAB);
    assert(strcmp(tui_textinput_text(input), "xyz") == 0);

    tui_textinput_free(input);
}

static void test_view_output(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hello");

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_textinput_view(input, buf);

    /* View output should contain the text */
    assert(dynamic_buffer_len(buf) > 0);
    assert(strstr(dynamic_buffer_data(buf), "hello") != NULL);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

static void test_set_cursor(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);

    send_string(input, "hello");
    tui_textinput_set_cursor(input, 2);
    assert(tui_textinput_cursor(input) == 2);

    /* Insert at cursor position */
    send_char(input, 'X');
    assert(strcmp(tui_textinput_text(input), "heXllo") == 0);

    tui_textinput_free(input);
}

static void test_line_count(void) {
    TuiTextInputConfig cfg = {.multiline = 1};
    TuiTextInput *input = tui_textinput_create(&cfg);
    tui_textinput_set_focus(input, 1);

    send_string(input, "line1");
    assert(tui_textinput_line_count(input) == 1);

    /* Enter in multiline mode inserts newline */
    send_key(input, TUI_KEY_ENTER);
    send_string(input, "line2");
    assert(tui_textinput_line_count(input) == 2);

    tui_textinput_free(input);
}

static void test_prompt(void) {
    TuiTextInput *input = tui_textinput_create(NULL);
    tui_textinput_set_focus(input, 1);
    tui_textinput_set_prompt(input, "$ ");
    tui_textinput_set_show_prompt(input, 1);

    send_string(input, "cmd");

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_textinput_view(input, buf);

    assert(strstr(dynamic_buffer_data(buf), "$ ") != NULL);
    assert(strstr(dynamic_buffer_data(buf), "cmd") != NULL);

    dynamic_buffer_destroy(buf);
    tui_textinput_free(input);
}

/* ---------- main ---------- */

int main(void) {
    printf("textinput tests:\n");

    RUN_TEST(test_create_and_free);
    RUN_TEST(test_create_with_config);
    RUN_TEST(test_char_insertion);
    RUN_TEST(test_backspace);
    RUN_TEST(test_backspace_at_start);
    RUN_TEST(test_delete_key);
    RUN_TEST(test_cursor_left_right);
    RUN_TEST(test_cursor_left_at_start);
    RUN_TEST(test_cursor_right_at_end);
    RUN_TEST(test_home_end);
    RUN_TEST(test_insert_in_middle);
    RUN_TEST(test_set_text);
    RUN_TEST(test_clear);
    RUN_TEST(test_focus);
    RUN_TEST(test_unfocused_ignores_input);
    RUN_TEST(test_history_navigation);
    RUN_TEST(test_tab_completion);
    RUN_TEST(test_tab_cycling);
    RUN_TEST(test_tab_no_completions);
    RUN_TEST(test_view_output);
    RUN_TEST(test_set_cursor);
    RUN_TEST(test_line_count);
    RUN_TEST(test_prompt);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

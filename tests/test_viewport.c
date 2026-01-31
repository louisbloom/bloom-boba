/* test_viewport.c - Unit tests for the viewport component */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bloom-boba/components/viewport.h>
#include <bloom-boba/dynamic_buffer.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        tests_run++;                                                            \
        fn();                                                                   \
        tests_passed++;                                                         \
        printf("  PASS: %s\n", #fn);                                            \
    } while (0)

/* ---------- tests ---------- */

static void test_create_and_free(void) {
    TuiViewport *vp = tui_viewport_create();
    assert(vp != NULL);
    assert(tui_viewport_line_count(vp) == 0);
    tui_viewport_free(vp);
}

static void test_append_single_line(void) {
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 24);

    /* "hello\n" creates the "hello" line plus a new empty line after it */
    tui_viewport_append(vp, "hello\n", 6);
    assert(tui_viewport_line_count(vp) == 2);

    tui_viewport_free(vp);
}

static void test_append_multiple_lines(void) {
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 24);

    /* Each \n finalizes a line and starts a new empty one */
    const char *text = "line1\nline2\nline3\n";
    tui_viewport_append(vp, text, strlen(text));
    assert(tui_viewport_line_count(vp) == 4);

    tui_viewport_free(vp);
}

static void test_append_no_trailing_newline(void) {
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 24);

    /* Text without trailing newline creates one partial line */
    tui_viewport_append(vp, "partial", 7);
    assert(tui_viewport_line_count(vp) == 1);

    tui_viewport_free(vp);
}

static void test_clear(void) {
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 24);

    tui_viewport_append(vp, "hello\n", 6);
    assert(tui_viewport_line_count(vp) == 2);

    tui_viewport_clear(vp);
    assert(tui_viewport_line_count(vp) == 0);

    tui_viewport_free(vp);
}

static void test_scroll_down_and_up(void) {
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 5);
    tui_viewport_set_auto_scroll(vp, 0);

    /* Add more lines than the viewport height */
    for (int i = 0; i < 20; i++) {
        char line[32];
        int n = snprintf(line, sizeof(line), "line %d\n", i);
        tui_viewport_append(vp, line, (size_t)n);
    }

    /* Should start at top */
    tui_viewport_scroll_down(vp, 5);
    /* After scrolling down 5, we should not be at bottom */
    assert(tui_viewport_at_bottom(vp) == 0);

    /* Scroll to bottom */
    tui_viewport_scroll_to_bottom(vp);
    assert(tui_viewport_at_bottom(vp) == 1);

    /* Scroll up */
    tui_viewport_scroll_up(vp, 3);
    assert(tui_viewport_at_bottom(vp) == 0);

    tui_viewport_free(vp);
}

static void test_auto_scroll(void) {
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 5);
    tui_viewport_set_auto_scroll(vp, 1);

    /* Add many lines with auto-scroll on */
    for (int i = 0; i < 20; i++) {
        char line[32];
        int n = snprintf(line, sizeof(line), "line %d\n", i);
        tui_viewport_append(vp, line, (size_t)n);
    }

    /* With auto-scroll, should be at bottom */
    assert(tui_viewport_at_bottom(vp) == 1);

    tui_viewport_free(vp);
}

static void test_page_up_down(void) {
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 5);
    tui_viewport_set_auto_scroll(vp, 0);

    for (int i = 0; i < 30; i++) {
        char line[32];
        int n = snprintf(line, sizeof(line), "line %d\n", i);
        tui_viewport_append(vp, line, (size_t)n);
    }

    /* Page down then page up should work */
    tui_viewport_page_down(vp);
    assert(tui_viewport_at_bottom(vp) == 0);

    tui_viewport_scroll_to_bottom(vp);
    tui_viewport_page_up(vp);
    assert(tui_viewport_at_bottom(vp) == 0);

    tui_viewport_free(vp);
}

static void test_max_lines(void) {
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 24);
    tui_viewport_set_max_lines(vp, 10);

    /* Add more than max_lines */
    for (int i = 0; i < 20; i++) {
        char line[32];
        int n = snprintf(line, sizeof(line), "line %d\n", i);
        tui_viewport_append(vp, line, (size_t)n);
    }

    /* Should be capped at max_lines */
    assert(tui_viewport_line_count(vp) <= 10);

    tui_viewport_free(vp);
}

static void test_view_output(void) {
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 80, 24);
    tui_viewport_set_render_position(vp, 1, 1);

    tui_viewport_append(vp, "hello world\n", 12);

    DynamicBuffer *buf = dynamic_buffer_create(256);
    tui_viewport_view(vp, buf);

    assert(dynamic_buffer_len(buf) > 0);
    assert(strstr(dynamic_buffer_data(buf), "hello world") != NULL);

    dynamic_buffer_destroy(buf);
    tui_viewport_free(vp);
}

static void test_set_size(void) {
    TuiViewport *vp = tui_viewport_create();

    tui_viewport_set_size(vp, 120, 40);
    assert(vp->width == 120);
    assert(vp->height == 40);

    tui_viewport_free(vp);
}

static void test_wrap_mode(void) {
    TuiViewport *vp = tui_viewport_create();
    tui_viewport_set_size(vp, 10, 5);

    tui_viewport_set_wrap_mode(vp, 1);
    assert(vp->wrap_mode == 1);

    tui_viewport_set_wrap_mode(vp, 0);
    assert(vp->wrap_mode == 0);

    tui_viewport_free(vp);
}

/* ---------- main ---------- */

int main(void) {
    printf("viewport tests:\n");

    RUN_TEST(test_create_and_free);
    RUN_TEST(test_append_single_line);
    RUN_TEST(test_append_multiple_lines);
    RUN_TEST(test_append_no_trailing_newline);
    RUN_TEST(test_clear);
    RUN_TEST(test_scroll_down_and_up);
    RUN_TEST(test_auto_scroll);
    RUN_TEST(test_page_up_down);
    RUN_TEST(test_max_lines);
    RUN_TEST(test_view_output);
    RUN_TEST(test_set_size);
    RUN_TEST(test_wrap_mode);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

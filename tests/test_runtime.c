/* test_runtime.c - Tests for tui_runtime_run(), idempotent start/stop,
 * callback config, and terminal size getters.
 *
 * These test the new runtime event loop infrastructure added to support
 * the Bubbletea-style ownership model where the runtime owns raw mode,
 * signals, and the select() event loop.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bloom-boba/cmd.h>
#include <bloom-boba/component.h>
#include <bloom-boba/dynamic_buffer.h>
#include <bloom-boba/msg.h>
#include <bloom-boba/runtime.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        tests_run++;                                                            \
        fn();                                                                   \
        tests_passed++;                                                         \
        printf("  PASS: %s\n", #fn);                                            \
    } while (0)

/* ========================================================================
 * Minimal test component — quits immediately on first update
 * ======================================================================== */

typedef struct {
    TuiModel base;
    int update_count;
    int window_size_received;
    int last_width;
    int last_height;
} TestModel;

static TuiInitResult test_init(void *config)
{
    (void)config;
    TestModel *m = calloc(1, sizeof(TestModel));
    m->base.type = 999;
    return tui_init_result_none((TuiModel *)m);
}

static TuiUpdateResult test_update(TuiModel *model, TuiMsg msg)
{
    TestModel *m = (TestModel *)model;
    m->update_count++;

    if (msg.type == TUI_MSG_WINDOW_SIZE) {
        m->window_size_received = 1;
        m->last_width = msg.data.size.width;
        m->last_height = msg.data.size.height;
        /* Quit after receiving window size (first message from runtime_run) */
        return tui_update_result(tui_cmd_quit());
    }

    return tui_update_result_none();
}

static void test_view(const TuiModel *model, DynamicBuffer *out)
{
    (void)model;
    dynamic_buffer_append_str(out, "test view");
}

static void test_free(TuiModel *model)
{
    free(model);
}

static TuiComponent test_component = {
    .init = test_init,
    .update = test_update,
    .view = test_view,
    .free = test_free,
};

/* ========================================================================
 * Component that doesn't quit (for non-run tests)
 * ======================================================================== */

static TuiInitResult noop_init(void *config)
{
    (void)config;
    TestModel *m = calloc(1, sizeof(TestModel));
    m->base.type = 998;
    return tui_init_result_none((TuiModel *)m);
}

static TuiUpdateResult noop_update(TuiModel *model, TuiMsg msg)
{
    (void)model;
    (void)msg;
    return tui_update_result_none();
}

static TuiComponent noop_component = {
    .init = noop_init,
    .update = noop_update,
    .view = test_view,
    .free = test_free,
};

/* ========================================================================
 * Tests
 * ======================================================================== */

/* Dummy callbacks for config storage tests */
static int dummy_get_fd(void *data)
{
    (void)data;
    return -1;
}
static void dummy_on_ready(void *data) { (void)data; }
static void dummy_on_tick(void *data) { (void)data; }
static void dummy_on_resize(int w, int h, void *data)
{
    (void)w;
    (void)h;
    (void)data;
}
static void dummy_on_stdin(void *data) { (void)data; }

/* Test that runtime stores new config callback fields */
static void test_config_stores_callbacks(void)
{
    int called = 0;

    TuiRuntimeConfig cfg = {
        .output = stdout,
        .get_external_fd = dummy_get_fd,
        .on_external_ready = dummy_on_ready,
        .on_tick = dummy_on_tick,
        .on_resize = dummy_on_resize,
        .on_stdin_processed = dummy_on_stdin,
        .event_data = &called,
    };

    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    /* Verify callbacks are stored in config */
    assert(rt->config.get_external_fd == dummy_get_fd);
    assert(rt->config.on_external_ready == dummy_on_ready);
    assert(rt->config.on_tick == dummy_on_tick);
    assert(rt->config.on_resize == dummy_on_resize);
    assert(rt->config.on_stdin_processed == dummy_on_stdin);
    assert(rt->config.event_data == &called);

    tui_runtime_free(rt);
}

/* Test that tui_runtime_start is idempotent */
static void test_start_idempotent(void)
{
    /* Use /dev/null to avoid writing ANSI sequences to terminal */
    FILE *devnull = fopen("/dev/null", "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = {
        .use_alternate_screen = 1,
        .enable_mouse = 1,
        .enable_keyboard_enhancement = 1,
        .output = devnull,
    };

    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);
    assert(rt->started == 0);

    tui_runtime_start(rt);
    assert(rt->started == 1);

    /* Second start should be a no-op */
    tui_runtime_start(rt);
    assert(rt->started == 1);

    tui_runtime_stop(rt);
    assert(rt->started == 0);

    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test that tui_runtime_stop is idempotent */
static void test_stop_idempotent(void)
{
    FILE *devnull = fopen("/dev/null", "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = {
        .use_alternate_screen = 1,
        .output = devnull,
    };

    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    /* Stop without start should be no-op (started == 0) */
    tui_runtime_stop(rt);
    assert(rt->started == 0);

    /* Start then double-stop */
    tui_runtime_start(rt);
    assert(rt->started == 1);

    tui_runtime_stop(rt);
    assert(rt->started == 0);

    tui_runtime_stop(rt);
    assert(rt->started == 0);

    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test that start+stop cycle can be repeated */
static void test_start_stop_cycle(void)
{
    FILE *devnull = fopen("/dev/null", "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = {
        .use_alternate_screen = 1,
        .enable_mouse = 1,
        .output = devnull,
    };

    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    for (int i = 0; i < 3; i++) {
        tui_runtime_start(rt);
        assert(rt->started == 1);
        tui_runtime_stop(rt);
        assert(rt->started == 0);
    }

    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test get_width/get_height return stored dimensions */
static void test_get_dimensions(void)
{
    TuiRuntimeConfig cfg = {.output = stdout};
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    /* Initially zero (not yet populated by runtime_update_size) */
    assert(tui_runtime_get_width(rt) == 0);
    assert(tui_runtime_get_height(rt) == 0);

    /* Manually set for testing */
    rt->term_width = 120;
    rt->term_height = 40;
    assert(tui_runtime_get_width(rt) == 120);
    assert(tui_runtime_get_height(rt) == 40);

    tui_runtime_free(rt);
}

/* Test get_width/get_height with NULL runtime */
static void test_get_dimensions_null(void)
{
    assert(tui_runtime_get_width(NULL) == 0);
    assert(tui_runtime_get_height(NULL) == 0);
}

/* Test that config with NULL callbacks doesn't crash */
static void test_null_callbacks_in_config(void)
{
    TuiRuntimeConfig cfg = {
        .output = stdout,
        .get_external_fd = NULL,
        .on_external_ready = NULL,
        .on_tick = NULL,
        .on_resize = NULL,
        .on_stdin_processed = NULL,
        .event_data = NULL,
    };

    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);
    assert(rt->config.get_external_fd == NULL);
    assert(rt->config.event_data == NULL);

    tui_runtime_free(rt);
}

/* Test that runtime created with default config has raw_mode = 1 */
static void test_default_config_raw_mode(void)
{
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, NULL);
    assert(rt != NULL);
    assert(rt->config.raw_mode == 1);

    tui_runtime_free(rt);
}

/* Test that WINDOW_SIZE message is delivered to component */
static void test_window_size_message(void)
{
    FILE *devnull = fopen("/dev/null", "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = {.output = devnull};
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);

    TestModel *m = (TestModel *)tui_runtime_model(rt);
    assert(m != NULL);
    assert(m->window_size_received == 0);

    /* Send a window size message manually */
    TuiMsg ws_msg = tui_msg_window_size(132, 50);
    tui_runtime_send(rt, ws_msg);

    /* noop_update doesn't track this, but verify send doesn't crash */
    tui_runtime_free(rt);
    fclose(devnull);
}

/* Test that tui_runtime_quit sets quit_requested and stops running */
static void test_runtime_quit(void)
{
    TuiRuntimeConfig cfg = {.output = stdout};
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);
    assert(rt->running == 1);
    assert(rt->quit_requested == 0);

    tui_runtime_quit(rt);
    assert(rt->running == 0);
    assert(rt->quit_requested == 1);

    tui_runtime_free(rt);
}

/* Test that started flag is initially 0 */
static void test_started_initially_zero(void)
{
    TuiRuntimeConfig cfg = {.output = stdout};
    TuiRuntime *rt = tui_runtime_create(&noop_component, NULL, &cfg);
    assert(rt != NULL);
    assert(rt->started == 0);

    tui_runtime_free(rt);
}

#ifndef _WIN32
/* Test that tui_runtime_run sends WINDOW_SIZE and component can quit.
 * This tests the full run loop with a component that quits on first update.
 * Requires a TTY (stdin must be a terminal for raw mode). */
static void test_runtime_run_immediate_quit(void)
{
    FILE *devnull = fopen("/dev/null", "w");
    assert(devnull != NULL);

    TuiRuntimeConfig cfg = {
        .use_alternate_screen = 0, /* Don't need alt screen for test */
        .raw_mode = 0,             /* Skip raw mode (may not have a TTY) */
        .enable_mouse = 0,
        .enable_keyboard_enhancement = 0,
        .output = devnull,
    };

    TuiRuntime *rt = tui_runtime_create(&test_component, NULL, &cfg);
    assert(rt != NULL);

    TestModel *m = (TestModel *)tui_runtime_model(rt);
    assert(m->window_size_received == 0);

    /* Run — the test_component quits immediately on WINDOW_SIZE message */
    int result = tui_runtime_run(rt);
    assert(result == 0);

    /* Verify the component received a window size message.
     * Dimensions may be 0 if not running in a real TTY (e.g. make check). */
    assert(m->window_size_received == 1);

    /* Verify runtime properly cleaned up */
    assert(rt->started == 0);

    tui_runtime_free(rt);
    fclose(devnull);
}
#endif

/* ======================================================================== */

int main(void)
{
    printf("runtime tests:\n");

    RUN_TEST(test_config_stores_callbacks);
    RUN_TEST(test_start_idempotent);
    RUN_TEST(test_stop_idempotent);
    RUN_TEST(test_start_stop_cycle);
    RUN_TEST(test_get_dimensions);
    RUN_TEST(test_get_dimensions_null);
    RUN_TEST(test_null_callbacks_in_config);
    RUN_TEST(test_default_config_raw_mode);
    RUN_TEST(test_window_size_message);
    RUN_TEST(test_runtime_quit);
    RUN_TEST(test_started_initially_zero);
#ifndef _WIN32
    RUN_TEST(test_runtime_run_immediate_quit);
#endif

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

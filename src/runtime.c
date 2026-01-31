/* runtime.c - Runtime and event loop implementation */

#include <bloom-boba/runtime.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MSGS_PER_FRAME 64

/* Forward declaration */
static int execute_cmd(TuiRuntime *runtime, TuiCmd *cmd);

/* Create runtime with component */
TuiRuntime *tui_runtime_create(TuiComponent *component, void *component_config,
                               const TuiRuntimeConfig *runtime_config)
{
    if (!component)
        return NULL;

    TuiRuntime *runtime = (TuiRuntime *)malloc(sizeof(TuiRuntime));
    if (!runtime)
        return NULL;

    memset(runtime, 0, sizeof(TuiRuntime));

    /* Store component interface */
    runtime->component = component;

    /* Initialize model (Elm Architecture: init returns (Model, Cmd)) */
    TuiInitResult init_result = component->init(component_config);
    if (!init_result.model) {
        free(runtime);
        return NULL;
    }
    runtime->model = init_result.model;

    /* Create input parser */
    runtime->parser = tui_input_parser_create();
    if (!runtime->parser) {
        component->free(runtime->model);
        free(runtime);
        return NULL;
    }

    /* Create view buffer */
    runtime->view_buf = dynamic_buffer_create(4096);
    if (!runtime->view_buf) {
        tui_input_parser_free(runtime->parser);
        component->free(runtime->model);
        free(runtime);
        return NULL;
    }

    /* Apply configuration */
    if (runtime_config) {
        runtime->config = *runtime_config;
    } else {
        /* Defaults */
        runtime->config.use_alternate_screen = 0;
        runtime->config.hide_cursor = 0;
        runtime->config.raw_mode = 1;
    }

    runtime->running = 1;
    runtime->quit_requested = 0;

    /* Execute initial command if provided (Elm Architecture) */
    if (init_result.cmd) {
        execute_cmd(runtime, init_result.cmd);
    }

    return runtime;
}

/* Free runtime and associated resources */
void tui_runtime_free(TuiRuntime *runtime)
{
    if (!runtime)
        return;

    if (runtime->view_buf)
        dynamic_buffer_destroy(runtime->view_buf);

    if (runtime->parser)
        tui_input_parser_free(runtime->parser);

    if (runtime->model && runtime->component)
        runtime->component->free(runtime->model);

    free(runtime);
}

/* Execute a command */
static int execute_cmd(TuiRuntime *runtime, TuiCmd *cmd)
{
    if (!cmd)
        return 1;

    switch (cmd->type) {
    case TUI_CMD_QUIT:
        runtime->quit_requested = 1;
        runtime->running = 0;
        break;

    case TUI_CMD_BATCH:
        for (int i = 0; i < cmd->payload.batch.count; i++) {
            if (!execute_cmd(runtime, cmd->payload.batch.cmds[i])) {
                tui_cmd_free(cmd);
                return 0;
            }
        }
        break;

    case TUI_CMD_NONE:
        break;

    default:
        /* Custom command - execute callback and send result message */
        if (cmd->type >= TUI_CMD_CUSTOM_BASE && cmd->payload.custom.callback) {
            TuiMsg result_msg =
                cmd->payload.custom.callback(cmd->payload.custom.data);
            if (result_msg.type != TUI_MSG_NONE) {
                tui_runtime_send(runtime, result_msg);
            }
        }
        break;
    }

    tui_cmd_free(cmd);
    return runtime->running;
}

/* Process a single message through the runtime */
int tui_runtime_send(TuiRuntime *runtime, TuiMsg msg)
{
    if (!runtime || !runtime->running || !runtime->component)
        return 0;

    /* Update model */
    TuiUpdateResult result = runtime->component->update(runtime->model, msg);

    /* Execute any returned command */
    if (result.cmd) {
        return execute_cmd(runtime, result.cmd);
    }

    return runtime->running;
}

/* Process raw input bytes */
int tui_runtime_process_input(TuiRuntime *runtime, const unsigned char *input,
                              size_t len)
{
    if (!runtime || !runtime->running || !input || len == 0)
        return runtime ? runtime->running : 0;

    TuiMsg msgs[MAX_MSGS_PER_FRAME];
    int count = tui_input_parser_parse(runtime->parser, input, len, msgs,
                                       MAX_MSGS_PER_FRAME);

    for (int i = 0; i < count; i++) {
        if (!tui_runtime_send(runtime, msgs[i])) {
            return 0;
        }
    }

    return runtime->running;
}

/* Render current state to buffer */
const char *tui_runtime_render(TuiRuntime *runtime)
{
    if (!runtime || !runtime->component || !runtime->model)
        return "";

    dynamic_buffer_clear(runtime->view_buf);
    runtime->component->view(runtime->model, runtime->view_buf);

    return dynamic_buffer_data(runtime->view_buf);
}

/* Get the current model */
TuiModel *tui_runtime_model(TuiRuntime *runtime)
{
    return runtime ? runtime->model : NULL;
}

/* Check if runtime should quit */
int tui_runtime_should_quit(TuiRuntime *runtime)
{
    return runtime ? runtime->quit_requested : 1;
}

/* Request runtime to quit */
void tui_runtime_quit(TuiRuntime *runtime)
{
    if (runtime) {
        runtime->quit_requested = 1;
        runtime->running = 0;
    }
}

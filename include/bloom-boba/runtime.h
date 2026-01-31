/* runtime.h - Runtime and event loop for bloom-boba TUI library
 *
 * The runtime handles:
 * - Terminal setup/teardown (raw mode, alternate screen)
 * - Reading input and parsing to messages
 * - Executing commands
 * - Rendering views
 *
 * For applications that want to integrate with an existing event loop
 * (like bloom-telnet), you can use the lower-level functions directly
 * instead of tui_run().
 */

#ifndef BLOOM_BOBA_RUNTIME_H
#define BLOOM_BOBA_RUNTIME_H

#include "cmd.h"
#include "component.h"
#include "dynamic_buffer.h"
#include "input_parser.h"
#include "msg.h"

/* Runtime configuration */
typedef struct TuiRuntimeConfig {
  int use_alternate_screen; /* Use alternate screen buffer */
  int hide_cursor;          /* Hide cursor during rendering */
  int raw_mode;             /* Enable raw terminal mode */
} TuiRuntimeConfig;

/* Runtime state */
typedef struct TuiRuntime {
  TuiComponent *component;     /* Component interface */
  TuiModel *model;             /* Current model state */
  TuiInputParser *parser;      /* Input parser */
  DynamicBuffer *view_buf;     /* Buffer for view rendering */
  TuiRuntimeConfig config;     /* Runtime configuration */
  int running;                 /* Whether runtime is running */
  int quit_requested;          /* Quit has been requested */
} TuiRuntime;

/* Create runtime with component
 *
 * Parameters:
 *   component: Component interface (init/update/view/free)
 *   config: Optional configuration (NULL for defaults)
 *
 * Returns: New runtime, or NULL on failure
 */
TuiRuntime *tui_runtime_create(TuiComponent *component, void *component_config,
                               const TuiRuntimeConfig *runtime_config);

/* Free runtime and associated resources */
void tui_runtime_free(TuiRuntime *runtime);

/* Process a single message through the runtime
 *
 * Parameters:
 *   runtime: Runtime state
 *   msg: Message to process
 *
 * Returns: 1 if should continue, 0 if should quit
 */
int tui_runtime_send(TuiRuntime *runtime, TuiMsg msg);

/* Process raw input bytes
 *
 * Parameters:
 *   runtime: Runtime state
 *   input: Input bytes
 *   len: Number of bytes
 *
 * Returns: 1 if should continue, 0 if should quit
 */
int tui_runtime_process_input(TuiRuntime *runtime, const unsigned char *input,
                              size_t len);

/* Render current state to buffer
 *
 * Parameters:
 *   runtime: Runtime state
 *
 * Returns: Rendered view as string (owned by runtime, do not free)
 */
const char *tui_runtime_render(TuiRuntime *runtime);

/* Get the current model */
TuiModel *tui_runtime_model(TuiRuntime *runtime);

/* Check if runtime should quit */
int tui_runtime_should_quit(TuiRuntime *runtime);

/* Request runtime to quit */
void tui_runtime_quit(TuiRuntime *runtime);

#endif /* BLOOM_BOBA_RUNTIME_H */

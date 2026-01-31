# bloom-boba

A C library for building terminal user interfaces using the Elm Architecture.

## Why "boba"?

The name pays homage to [Bubbletea](https://github.com/charmbracelet/bubbletea), the
Go library that brought Elm Architecture to terminal applications. Boba are the
tapioca pearls in bubble tea.

## What bloom-boba Provides

Like Bubbletea, bloom-boba has two parts:

**Runtime** (`TuiRuntime`) - The event loop that:

- Receives input and converts it to messages
- Calls your model's `update()` function
- Executes returned commands
- Calls `view()` to render output
- Repeats

**Components** - Reusable UI building blocks:

- `textinput` - Single-line text input with history, completion, Unicode support
- `viewport` - Scrollable content area with software-based scrolling
- `textview` - Simple text display (for basic use cases)

## Philosophy

bloom-boba implements the [Elm Architecture](https://guide.elm-lang.org/architecture/),
a pattern for building interactive programs that emerged from the Elm programming
language. The architecture consists of three parts:

- **Model** — the state of your application
- **View** — a way to turn your state into terminal output
- **Update** — a way to update your state based on messages

Data flows in one direction:

    Input → Message → Update(Model, Msg) → Model' → View → Output
                                ↓
                              Command → (async) → Message

This unidirectional flow makes programs predictable and easy to reason about.

## Adapting for C

Since C lacks garbage collection and sum types, bloom-boba makes pragmatic choices:

- **Mutable models** — Update modifies the model in place rather than returning a copy
- **Tagged unions** — Messages use `enum` + `union` to simulate sum types
- **Explicit memory** — Components provide `create` and `free` functions

## Example

```c
#include <bloom-boba/tui.h>
#include <bloom-boba/components/textinput.h>

int main(void) {
    /* Initialize component */
    TuiTextInput *input = tui_textinput_create(NULL);

    /* Handle a key press */
    TuiMsg msg = tui_msg_key(0, 'H', 0);
    tui_textinput_update(input, msg);

    /* Render */
    DynamicBuffer *out = dynamic_buffer_create(256);
    tui_textinput_view(input, out);
    printf("%s", dynamic_buffer_data(out));

    /* Cleanup */
    tui_textinput_free(input);
    dynamic_buffer_destroy(out);
    return 0;
}
```

## Component Interface

The component interface follows the Elm Architecture pattern:

```c
typedef struct TuiComponent {
  TuiInitResult (*init)(void *config);      /* Create model + initial command */
  TuiUpdateResult (*update)(TuiModel *model, TuiMsg msg);  /* Handle message */
  void (*view)(const TuiModel *model, DynamicBuffer *out); /* Render */
  void (*free)(TuiModel *model);            /* Cleanup */
} TuiComponent;
```

### Init returns (Model, Cmd)

Following Elm's `init : () -> (Model, Cmd Msg)`, the init function returns both
a model and an optional initial command:

```c
typedef struct {
  TuiModel *model;  /* Initialized model */
  TuiCmd *cmd;      /* Initial command (NULL for none) */
} TuiInitResult;
```

This allows components to trigger effects at startup (e.g., start a timer,
fetch initial data).

## Components

### textinput

A powerful single-line text input field with extensive editing features, similar to an HTML `<input type="text">` but with advanced terminal capabilities.

Features:

- **Multi-line support** - Toggle between single-line and multi-line text areas
- **Unicode/UTF-8 support** - Full international character handling with proper cursor positioning
- **Advanced cursor navigation** - Arrow keys, Home/End, Ctrl+Left/Right for word navigation
- **Command history** - Up/down navigation with prefix filtering (only shows entries matching current input)
- **Tab completion** - Configurable completion callback with cycling through suggestions
- **Kill ring/yank buffer** - Emacs-style kill/yank operations (Ctrl+K, Ctrl+U, Ctrl+W, Ctrl+Y)
- **Undo functionality** - Multiple undo levels with Ctrl+\_ (Ctrl+X Ctrl+U)
- **Text manipulation** - Ctrl+T for transpose, Ctrl+H for delete
- **Absolute cursor positioning** - Flicker-free rendering with optional divider lines
- **Prompt support** - Custom prompt strings with proper UTF-8 width calculation
- **Visual dividers** - Optional Unicode box-drawing lines above/below input field

```c
TuiTextInput *input = tui_textinput_create(NULL);
tui_textinput_set_prompt(input, "> ");
tui_textinput_set_history_size(input, 100);
tui_textinput_set_terminal_row(input, 23);  /* Absolute positioning */
tui_textinput_set_show_dividers(input, 1);  /* Show decorative lines */
```

### viewport

A sophisticated scrollable content area that stores lines in memory and renders with absolute cursor positioning. This is the recommended component for displaying scrollable output (like a terminal's main content area) with advanced features.

Features:

- **Software-based scrolling** - No ANSI scroll regions for maximum compatibility
- **Line storage** - Configurable maximum line count with automatic trimming
- **Auto-scroll** - Automatically scrolls to bottom when new content is added (optional)
- **Manual scrolling** - Scroll up/down/page commands with proper boundary checking
- **Wrap/clip modes** - Choose between line wrapping or truncation at viewport width
- **ANSI sequence support** - Proper handling of VT100/ANSI color codes and SGR sequences
- **UTF-8 aware** - Correct display width calculation for international characters
- **Memory efficient** - Automatic cleanup of old lines when exceeding maximum
- **Visual line calculation** - Handles long lines that wrap across multiple screen rows
- **State preservation** - Maintains ANSI SGR state across wrapped line segments

```c
TuiViewport *vp = tui_viewport_create();
tui_viewport_set_size(vp, 80, 20);
tui_viewport_set_render_position(vp, 1, 1);  /* Start at row 1, col 1 */
tui_viewport_set_max_lines(vp, 1000);        /* Limit memory usage */
tui_viewport_set_wrap_mode(vp, 1);           /* Enable line wrapping */
tui_viewport_append(vp, "Hello, world!\n", 14);
```

### textview

A simple text buffer for basic text display. Use `viewport` instead for scrollable content with software scrolling, ANSI sequence support, and advanced features like line wrapping and memory management.

Features:

- **Basic text storage** - Simple buffer for accumulating text content
- **Auto-scroll** - Option to automatically reset scroll position on new content
- **Direct output** - Write directly to terminal for live output scenarios
- **Minimal overhead** - Lightweight alternative for simple display needs

```c
TuiTextView *view = tui_textview_create(10);
tui_textview_append_str(view, "Simple text output\n");
tui_textview_write_direct(view, "Live output", 11);
```

## Component Composition

bloom-boba follows the same composition pattern as Bubbletea:
the runtime manages ONE model, and composition happens inside that model.

### Embedding Child Components

A parent component embeds children as struct fields:

```c
typedef struct {
  TuiModel base;           /* Component base type */
  TuiViewport *viewport;   /* Child: scrollable output */
  TuiTextInput *textinput; /* Child: user input */
} MyAppModel;
```

### Routing Messages

The parent's update function routes messages to children:

```c
TuiUpdateResult my_app_update(MyAppModel *app, TuiMsg msg) {
  /* Handle window resize at parent level */
  if (msg.type == TUI_MSG_WINDOW_SIZE) {
    tui_viewport_set_size(app->viewport,
        msg.data.size.width, msg.data.size.height - 3);
    return tui_update_result_none();
  }

  /* Route key messages to focused child */
  if (msg.type == TUI_MSG_KEY_PRESS) {
    return tui_textinput_update(app->textinput, msg);
  }

  return tui_update_result_none();
}
```

### Composing Views

The parent's view function composes child outputs:

```c
void my_app_view(const MyAppModel *app, DynamicBuffer *out) {
  /* Render viewport (uses absolute positioning) */
  tui_viewport_view(app->viewport, out);

  /* Render input area (uses absolute positioning) */
  tui_textinput_view(app->textinput, out);
}
```

### Batching Commands

When children return commands, use `tui_cmd_batch2` to combine them:

```c
TuiCmd *cmd1 = child1_result.cmd;
TuiCmd *cmd2 = child2_result.cmd;
TuiCmd *combined = tui_cmd_batch2(cmd1, cmd2);  /* Handles NULL gracefully */
return tui_update_result(combined);
```

## Architecture Decisions

### Why Software Scrolling?

bloom-boba's viewport uses software-based scrolling rather than ANSI scroll
regions (DECSTBM). This follows Bubbletea's approach:

- ANSI scroll regions are fragile across terminal emulators
- Cursor positioning at scroll region boundaries causes visual glitches
- Software scrolling gives full control over rendering
- Enables features like search, copy/paste from scrollback

### Why Callbacks for Commands?

In pure Elm, commands are declarative data structures that the runtime interprets.
In C, we use callbacks for pragmatism:

- No garbage collection means declarative command data would need manual cleanup
- Callbacks allow direct integration with C libraries and system calls
- The tradeoff is testability, but C testing typically uses integration tests anyway

### Why No Subscriptions?

Elm has a `subscriptions : Model -> Sub Msg` function for external event sources.
bloom-boba defers this complexity:

- Terminal apps typically need few subscriptions (timers, window resize)
- Window resize is handled by the host app's signal handler
- Timers can be added to the host app's event loop
- If needed, subscriptions could be added later without breaking the API

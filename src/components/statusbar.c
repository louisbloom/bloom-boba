/* statusbar.c - Status bar component implementation */

#include <bloom-boba/ansi_sequences.h>
#include <bloom-boba/components/statusbar.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATUSBAR_TYPE_ID (TUI_COMPONENT_TYPE_BASE + 3)

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

/* UTF-8 helper: Count display width (terminal columns) of a UTF-8 string */
static int utf8_display_width(const char *str)
{
    if (!str)
        return 0;
    int width = 0;
    while (*str) {
        int char_len = utf8_char_len(str);
        uint32_t cp = utf8_decode_codepoint(str, char_len);
        width += codepoint_display_width(cp);
        str += char_len;
    }
    return width;
}

/* Create a new status bar component */
TuiStatusBar *tui_statusbar_create(void)
{
    TuiStatusBar *sb = (TuiStatusBar *)malloc(sizeof(TuiStatusBar));
    if (!sb)
        return NULL;

    memset(sb, 0, sizeof(TuiStatusBar));
    sb->base.type = STATUSBAR_TYPE_ID;
    sb->height = 1; /* Default height */

    return sb;
}

/* Free status bar component */
void tui_statusbar_free(TuiStatusBar *sb)
{
    if (!sb)
        return;
    free(sb->mode_text);
    free(sb->notification);
    free(sb);
}

/* Set mode text */
void tui_statusbar_set_mode(TuiStatusBar *sb, const char *text)
{
    if (!sb)
        return;

    free(sb->mode_text);
    sb->mode_text = NULL;
    sb->mode_len = 0;
    sb->mode_display_width = 0;

    if (text && text[0] != '\0') {
        sb->mode_len = strlen(text);
        sb->mode_text = (char *)malloc(sb->mode_len + 1);
        if (sb->mode_text) {
            memcpy(sb->mode_text, text, sb->mode_len + 1);
            sb->mode_display_width = utf8_display_width(text);
        } else {
            sb->mode_len = 0;
        }
    }
}

/* Get mode text */
const char *tui_statusbar_get_mode(const TuiStatusBar *sb)
{
    if (!sb)
        return NULL;
    return sb->mode_text;
}

/* Set notification text */
void tui_statusbar_set_notification(TuiStatusBar *sb, const char *text)
{
    if (!sb)
        return;

    free(sb->notification);
    sb->notification = NULL;
    sb->notification_len = 0;
    sb->notification_display_width = 0;

    if (text && text[0] != '\0') {
        sb->notification_len = strlen(text);
        sb->notification = (char *)malloc(sb->notification_len + 1);
        if (sb->notification) {
            memcpy(sb->notification, text, sb->notification_len + 1);
            sb->notification_display_width = utf8_display_width(text);
        } else {
            sb->notification_len = 0;
        }
    }
}

/* Get notification text */
const char *tui_statusbar_get_notification(const TuiStatusBar *sb)
{
    if (!sb)
        return NULL;
    return sb->notification;
}

/* Clear notification text */
void tui_statusbar_clear_notification(TuiStatusBar *sb)
{
    if (!sb)
        return;

    free(sb->notification);
    sb->notification = NULL;
    sb->notification_len = 0;
    sb->notification_display_width = 0;
}

/* Set terminal width */
void tui_statusbar_set_terminal_width(TuiStatusBar *sb, int width)
{
    if (sb)
        sb->terminal_width = width;
}

/* Set terminal row for absolute positioning */
void tui_statusbar_set_terminal_row(TuiStatusBar *sb, int row)
{
    if (sb)
        sb->terminal_row = row;
}

/* Get render height in rows */
int tui_statusbar_get_height(const TuiStatusBar *sb)
{
    if (!sb)
        return 1;
    return sb->height;
}

/* Render status bar to output buffer */
void tui_statusbar_view(const TuiStatusBar *sb, DynamicBuffer *out)
{
    if (!sb || !out)
        return;

    int term_width = sb->terminal_width > 0 ? sb->terminal_width : 80;
    char pos_buf[32];

    /* Position cursor if absolute row is set */
    if (sb->terminal_row > 0) {
        snprintf(pos_buf, sizeof(pos_buf), CSI "%d;1H", sb->terminal_row);
        dynamic_buffer_append_str(out, pos_buf);
    }

    /* Clear the line */
    dynamic_buffer_append_str(out, EL_TO_END);

    /* Calculate available space for content */
    int mode_width = sb->mode_display_width;
    int notif_width = sb->notification_display_width;

    /* Render mode on the left */
    if (sb->mode_text && sb->mode_len > 0) {
        dynamic_buffer_append(out, sb->mode_text, sb->mode_len);
    }

    /* If we have both mode and notification, add separator and position notification on right */
    if (sb->mode_text && sb->mode_len > 0 && sb->notification && sb->notification_len > 0) {
        /* Calculate padding needed to right-align notification */
        int padding = term_width - mode_width - notif_width;
        if (padding > 0) {
            /* Output spaces for padding */
            for (int i = 0; i < padding; i++) {
                dynamic_buffer_append(out, " ", 1);
            }
        } else {
            /* Not enough room; add minimal separator */
            dynamic_buffer_append_str(out, "  ");
        }
        dynamic_buffer_append(out, sb->notification, sb->notification_len);
    } else if (sb->notification && sb->notification_len > 0) {
        /* Only notification, no mode - right-align it */
        int padding = term_width - notif_width;
        if (padding > 0) {
            for (int i = 0; i < padding; i++) {
                dynamic_buffer_append(out, " ", 1);
            }
        }
        dynamic_buffer_append(out, sb->notification, sb->notification_len);
    }
}

/* Update status bar with message */
TuiUpdateResult tui_statusbar_update(TuiStatusBar *sb, TuiMsg msg)
{
    (void)sb;
    (void)msg;
    /* Statusbar doesn't handle any messages currently */
    return tui_update_result_none();
}

/* Component interface wrappers */
static TuiInitResult statusbar_init(void *config)
{
    (void)config;
    TuiModel *model = (TuiModel *)tui_statusbar_create();
    return tui_init_result_none(model);
}

static TuiUpdateResult statusbar_update(TuiModel *model, TuiMsg msg)
{
    return tui_statusbar_update((TuiStatusBar *)model, msg);
}

static void statusbar_view(const TuiModel *model, DynamicBuffer *out)
{
    tui_statusbar_view((const TuiStatusBar *)model, out);
}

static void statusbar_free(TuiModel *model)
{
    tui_statusbar_free((TuiStatusBar *)model);
}

/* Static component interface instance */
static const TuiComponent statusbar_component = {
    .init = statusbar_init,
    .update = statusbar_update,
    .view = statusbar_view,
    .free = statusbar_free,
};

/* Get component interface for status bar */
const TuiComponent *tui_statusbar_component(void)
{
    return &statusbar_component;
}

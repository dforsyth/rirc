/* rirc configuration header
 *
 * Colours can be set [0, 255], Any other value (e.g. -1) will set
 * the default terminal foreground/background */

#define BUFFER_LINE_HEADER_FG_NEUTRAL 239

#define BUFFER_LINE_HEADER_FG_PINGED  250
#define BUFFER_LINE_HEADER_BG_PINGED  1

#define BUFFER_LINE_TEXT_FG_NEUTRAL 250
#define BUFFER_LINE_TEXT_FG_GREEN   113

/* Number of buffer lines to keep in history, must be power of 2 */
#define BUFFER_LINES_MAX (1 << 10)

/* Colours used for nicks */
#define NICK_COLOURS {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

/* Colours for channel names in response to activity, in order of precedence */
#define ACTIVITY_COLOURS {            \
	239, /* Default colour */         \
	239, /* Join/Part/Quit colour */  \
	247, /* Chat colour */            \
	3    /* Ping colour */            \
};

#define NAV_CURRENT_CHAN 255

/* Characters */
#define QUOTE_CHAR '>'
#define HORIZONTAL_SEPARATOR "-"
#define VERTICAL_SEPARATOR "~"

/* Prefix string for the input line and colours */
#define INPUT_PREFIX " >>> "
#define INPUT_PREFIX_FG 239
#define INPUT_PREFIX_BG -1

/* Input line text colours */
#define INPUT_FG 250
#define INPUT_BG -1

/* BUFFER_PADDING:
 * How the buffer line headers will be padded, options are 0, 1
 *
 * 0 (Unpadded):
 *   12:34 alice ~ hello
 *   12:34 bob ~ is there anybody in there?
 *   12:34 charlie ~ just nod if you can hear me
 *
 * 1 (Padded):
 *   12:34   alice ~ hello
 *   12:34     bob ~ is there anybody in there?
 *   12:34 charlie ~ just nod if you can hear me
 * */
#define BUFFER_PADDING 1

/* Raise terminal bell when pinged in chat */
#define BELL_ON_PINGED 1

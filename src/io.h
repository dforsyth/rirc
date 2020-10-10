#ifndef IO_H
#define IO_H

/* Handling off all network io, user input and signals
 *
 * The state of a connection at any given time can be
 * described by one of the following:
 *
 *  - dxed: disconnected ~ Socket disconnected, passive
 *  - rxng: reconnecting ~ Socket disconnected, pending reconnect
 *  - cxng: connecting   ~ Socket connection in progress
 *  - cxed: connected    ~ Socket connected
 *  - ping: timing out   ~ Socket connected, network state in question
 *
 *                             +--------+
 *                 +----(B1)-- |  rxng  |
 *                 |           +--------+
 *                 |            |      ^
 *   INIT          |         (A2,C)    |
 *    v            |            |     (E)
 *    |            v            v      |
 *    |    +--------+ --(A1)-> +--------+
 *    +--> |  dxed  |          |  cxng  | <--+
 *         +--------+ <-(B2)-- +--------+    |
 *          ^      ^            |      ^   (F2)
 *          |      |           (D)     |     |
 *          |      |            |    (F1)    |
 *          |      |            v      |     |
 *          |      |           +--------+    |
 *          |      +----(B3)-- |  cxed  |    |
 *          |                  +--------+    |
 *          |                   |      ^     |
 *          |                  (G)     |     |
 *          |                   |     (I)    |
 *          |                   v      |     |
 *          |                  +--------+    |
 *          +-----------(B4)-- |  ping  | ---+
 *                             +--------+
 *                              v      ^
 *                              |      |
 *                              +--(H)-+
 *
 * This module exposes functions for explicitly directing network
 * state as well declaring callback functions for state transitions
 * and network activity handling which must be implemented elsewhere
 *
 * Network state can be explicitly driven, returning non-zero error:
 *   (A) io_cx: establish network connection
 *   (B) io_dx: close network connection
 *
 * Network state implicit transitions result in informational callback types:
 *   (C) on connection attempt:  IO_CB_INFO
 *   (E) on connection failure:  IO_CB_ERROR
 *   (D) on connection success:  IO_CB_CXED
 *   (F) on connection loss:     IO_CB_DXED
 *   (G) on ping timeout start:  IO_CB_PING_1
 *   (H) on ping timeout update: IO_CB_PING_N
 *   (I) on ping normal:         IO_CB_PING_0
 *
 * Successful reads on stdin and connected sockets result in data callbacks:
 *   from stdin:  io_cb_read_inp
 *   from socket: io_cb_read_soc
 *
 * Signals registered to be caught result in non-signal handler context
 * callback with type IO_CB_SIGNAL
 *
 * Failed connection attempts enter a retry cycle with exponential
 * backoff time given by:
 *   t(n) = t(n - 1) * factor
 *   t(0) = base
 *
 * Calling io_start starts the io context and doesn't return until after
 * a call to io_stop
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

struct connection;

enum io_cb_t
{
	IO_CB_INVALID,
	IO_CB_CXED,   /* no args */
	IO_CB_DXED,   /* no args */
	IO_CB_ERR,    /* <const char *fmt>, [args, ...] */
	IO_CB_INFO,   /* <const char *fmt>, [args, ...] */
	IO_CB_PING_0, /* <unsigned ping> */
	IO_CB_PING_1, /* <unsigned ping> */
	IO_CB_PING_N, /* <unsigned ping> */
	IO_CB_SIGNAL, /* <io_sig_t sig> */
	IO_CB_SIZE
};

enum io_log_level
{
	IO_LOG_ERROR,
	IO_LOG_WARN,
	IO_LOG_INFO,
	IO_LOG_DEBUG,
};

enum io_sig_t
{
	IO_SIG_INVALID,
	IO_SIGWINCH,
	IO_SIG_SIZE
};

#define IO_IPV_UNSPEC        (1 << 1)
#define IO_IPV_4             (1 << 2)
#define IO_IPV_6             (1 << 3)
#define IO_TLS_ENABLED       (1 << 4)
#define IO_TLS_DISABLED      (1 << 5)
#define IO_TLS_VRFY_DISABLED (1 << 6)
#define IO_TLS_VRFY_OPTIONAL (1 << 7)
#define IO_TLS_VRFY_REQUIRED (1 << 8)

/* Returns a connection, or NULL if limit is reached */
struct connection* connection(
	const void*, /* callback object */
	const char*, /* host */
	const char*, /* port */
	uint32_t);   /* flags */

void connection_free(struct connection*);

/* Explicit direction of net state */
int io_cx(struct connection*);
int io_dx(struct connection*);

/* Formatted write to connection */
int io_sendf(struct connection*, const char*, ...);

/* Init/start/stop IO context */
void io_init(void);
void io_start(void);
void io_stop(void);

/* Get tty dimensions */
unsigned io_tty_cols(void);
unsigned io_tty_rows(void);

/* IO error string */
const char* io_err(int);

/* IO state callback */
void io_cb(enum io_cb_t, const void*, ...);

/* IO data callback */
void io_cb_read_inp(char*, size_t);
void io_cb_read_soc(char*, size_t, const void*);

/* Log message callback */
void io_cb_log(const void*, enum io_log_level, const char*, ...);

#endif

#ifndef STATE_H
#define STATE_H

#include "draw.h"
#include "buffer.h"
#include "server.h"
#include "mesg.h"
#include "net.h"

/* state.h
 *
 * Interface for retrieving and altering global state of the program */

/* state.c */
/* FIXME: terrible, until i remove references to ccur/rirc */
#define rirc (default_channel())
#define ccur (current_channel())
struct channel* current_channel(void);
struct channel* default_channel(void);

//TODO: rename
unsigned int _term_cols(void);
unsigned int _term_rows(void);

void resize(void);

void init_state(void);
void free_state(void);

//TODO: move to channel.c, function of server's channel list
/* Useful state retrieval abstractions */
struct channel* channel_get(char*, struct server*);
struct channel* channel_get_first(void);
struct channel* channel_get_last(void);
struct channel* channel_get_next(struct channel*);
struct channel* channel_get_prev(struct channel*);

/* State altering interface */
struct channel* new_channel(char*, struct server*, struct channel*, enum buffer_t);
void auto_nick(char**, char*);

/* FIXME: */
void buffer_scrollback_back(struct channel*);
void buffer_scrollback_forw(struct channel*);
void channel_clear(struct channel*);

void channel_close(struct channel*);
void channel_move_prev(void);
void channel_move_next(void);
void channel_set_current(struct channel*);

void free_channel(struct channel*);
void newline(struct channel*, enum buffer_line_t, const char*, const char*);
void newlinef(struct channel*, enum buffer_line_t, const char*, const char*, ...);
void part_channel(struct channel*);
void reset_channel(struct channel*);

/* TODO: refactor, should be static in state */
/* Function prototypes for setting draw bits */
#define X(bit) void draw_##bit(void);
DRAW_BITS
#undef X
void draw_all(void);

void redraw(void);

#endif

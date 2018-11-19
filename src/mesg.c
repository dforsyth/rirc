#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "src/draw.h"
#include "src/io.h"
#include "src/state.h"
#include "src/utils/utils.h"

/* Fail macros used in message sending/receiving handlers */
#define fail(C, M) \
	do { newline((C), 0, "-!!-", (M)); return 0; } while (0)

/* Fail with formatted message */
#define failf(C, M, ...) \
	do { newlinef((C), 0, "-!!-", (M), __VA_ARGS__); return 0; } while (0)

#define IS_ME(X) !strcmp((X), s->nick)

/* Must be kept in sync with mesg.gperf hash table */
#define SEND_HANDLERS \
	X(ctcp) \
	X(join) \
	X(me) \
	X(msg) \
	X(nick) \
	X(part) \
	X(privmsg) \
	X(quit) \
	X(topic) \
	X(version)

/* Send handler prototypes */
#define X(cmd) static int send_##cmd(char*, struct server*, struct channel*);
SEND_HANDLERS
#undef X

/* Must be kept in sync with mesg.gperf hash table */
#define RECV_HANDLERS \
	X(error) \
	X(join) \
	X(kick) \
	X(mode) \
	X(nick) \
	X(notice) \
	X(part) \
	X(ping) \
	X(pong) \
	X(privmsg) \
	X(quit) \
	X(topic)

/* Recv handler prototypes */
#define X(cmd) static int recv_##cmd(struct parsed_mesg*, struct server*);
RECV_HANDLERS
#undef X

struct recv_handler
{
	char *name;
	int (*func)(struct parsed_mesg*, struct server*);
};

struct send_handler
{
	char *name;
	int (*func)(char*, struct server*, struct channel*);
};

#include "src/handlers/recv.gperf.h"
#include "src/handlers/send.gperf.h"

#ifdef JPQ_THRESHOLD
static const unsigned int jpq_threshold = JPQ_THRESHOLD;
#else
static const unsigned int jpq_threshold = 0;
#endif

/* Special cases handlers, CTCP messages are embedded in PRIVMSG */
static int recv_ctcp_req(struct parsed_mesg*, struct server*);
static int recv_ctcp_rpl(struct parsed_mesg*, struct server*);

/* Special case handler for numeric replies */
static int recv_numeric(struct parsed_mesg*, struct server*);

static int recv_mode_chanmodes(struct parsed_mesg*, const struct mode_cfg*, struct channel*);
static int recv_mode_usermodes(struct parsed_mesg*, const struct mode_cfg*, struct server*);

/* Numeric Reply Codes */
enum numeric {
	RPL_WELCOME          =   1,
	RPL_YOURHOST         =   2,
	RPL_CREATED          =   3,
	RPL_MYINFO           =   4,
	RPL_ISUPPORT         =   5,
	RPL_STATSCONN        = 250,
	RPL_LUSERCLIENT      = 251,
	RPL_LUSEROP          = 252,
	RPL_LUSERUNKNOWN     = 253,
	RPL_LUSERCHANNELS    = 254,
	RPL_LUSERME          = 255,
	RPL_LOCALUSERS       = 265,
	RPL_GLOBALUSERS      = 266,
	RPL_CHANNEL_URL      = 328,
	RPL_NOTOPIC          = 331,
	RPL_TOPIC            = 332,
	RPL_TOPICWHOTIME     = 333,
	RPL_NAMEREPLY        = 353,
	RPL_ENDOFNAMES       = 366,
	RPL_MOTD             = 372,
	RPL_MOTDSTART        = 375,
	RPL_ENDOFMOTD        = 376,
	ERR_NOSUCHNICK       = 401,
	ERR_NOSUCHSERVER     = 402,
	ERR_NOSUCHCHANNEL    = 403,
	ERR_CANNOTSENDTOCHAN = 404,
	ERR_ERRONEUSNICKNAME = 432,
	ERR_NICKNAMEINUSE    = 433,
	ERR_INVITEONLYCHAN   = 473,
	ERR_NOCHANMODES      = 477
};

/*
 * Message sending handlers
 */

void
send_mesg(struct server *s, struct channel *chan, char *mesg)
{
	/* Handle the input to a channel, ie:
	 *  - a default message to the channel
	 *  - a default message to the channel beginning with '/'
	 *  - a handled command beginning with '/'
	 *  - an unhandled command beginning with '/'
	 */

	char *p, *cmd_str;

	if (s == NULL) {
		newline(chan, 0, "-!!-", "This is not a server");
	} else if (*mesg == '/' && *(++mesg) != '/') {

		int ret;

		if (!(cmd_str = getarg(&mesg, " "))) {
			newline(chan, 0, "-!!-", "Messages beginning with '/' require a command");
			return;
		}

		/* command -> COMMAND */
		for (p = cmd_str; *p; p++)
			*p = toupper(*p);

		const struct send_handler* handler = send_handler_lookup(cmd_str, strlen(cmd_str));

		if (handler) {
			handler->func(mesg, s, chan);
		} else if ((ret = io_sendf(s->connection, "%s %s", cmd_str, mesg)))
			newlinef(chan, 0, "-!!-", "sendf fail: %s", io_err(ret));

	} else {
		/* Send to current channel */
		int ret;

		if (*mesg == 0)
			fatal("message is empty");

		else if (chan->type != CHANNEL_T_CHANNEL && chan->type != CHANNEL_T_PRIVATE)
			newline(chan, 0, "-!!-", "Error: This is not a channel");

		else if (chan->parted)
			newline(chan, 0, "-!!-", "Error: Parted from channel");

		else if ((ret = io_sendf(s->connection, "PRIVMSG %s :%s", chan->name, mesg)))
			newlinef(chan, 0, "-!!-", "sendf fail: %s", io_err(ret));

		else
			newline(chan, BUFFER_LINE_CHAT, s->nick, mesg);
	}
}

static int
send_ctcp(char *mesg, struct server *s, struct channel *c)
{
	/* /ctcp <target> <message> */

	int ret;
	char *targ, *p;

	if (!(targ = getarg(&mesg, " ")))
		fail(c, "Error: /ctcp <target> <command> [arguments]");

	/* Crude to check that at least some ctcp command exists */
	while (*mesg == ' ')
		mesg++;

	if (*mesg == '\0')
		fail(c, "Error: /ctcp <target> <command> [arguments]");

	/* Ensure the command is uppercase */
	for (p = mesg; *p && *p != ' '; p++)
		*p = toupper(*p);

	if ((ret = io_sendf(s->connection, "PRIVMSG %s :\x01""%s\x01", targ, mesg)))
		failf(c, "sendf fail: %s", io_err(ret));

	return 0;
}

static int
send_me(char *mesg, struct server *s, struct channel *c)
{
	/* /me <message> */

	int ret;

	if (c->type == CHANNEL_T_SERVER)
		fail(c, "Error: This is not a channel");

	if (c->parted)
		fail(c, "Error: Parted from channel");

	if ((ret= io_sendf(s->connection, "PRIVMSG %s :\x01""ACTION %s\x01", c->name, mesg)))
		failf(c, "sendf fail: %s", io_err(ret));

	newlinef(c, 0, "*", "%s %s", s->nick, mesg);

	return 0;
}

static int
send_join(char *mesg, struct server *s, struct channel *c)
{
	/* /join [target[,targets]*] */

	// TODO: pass
	// if no targets, send join/pass for current channel, else
	// send unmodified
	// :set pass

	int ret;
	char *targ;

	if ((targ = getarg(&mesg, " "))) {
		if ((ret = io_sendf(s->connection, "JOIN %s", targ)))
			failf(c, "sendf fail: %s", io_err(ret));
	} else {
		if (c->type == CHANNEL_T_SERVER)
			fail(c, "Error: JOIN requires a target");

		if (c->type == CHANNEL_T_PRIVATE)
			fail(c, "Error: Can't rejoin private buffers");

		if (!c->parted)
			fail(c, "Error: Not parted from channel");

		if ((ret = io_sendf(s->connection, "JOIN %s", c->name)))
			failf(c, "sendf fail: %s", io_err(ret));
	}

	return 0;
}

static int
send_msg(char *mesg, struct server *s, struct channel *c)
{
	/* Alias for /priv */
	send_privmsg(mesg, s, c);

	return 0;
}

static int
send_nick(char *mesg, struct server *s, struct channel *c)
{
	/* /nick [nick] */

	int ret;
	char *nick;

	if ((nick = getarg(&mesg, " "))) {
		if ((ret = io_sendf(s->connection, "NICK %s", nick)))
			failf(c, "sendf fail: %s", io_err(ret));
	} else {
		if ((ret = io_sendf(s->connection, "NICK")))
			failf(c, "sendf fail: %s", io_err(ret));
	}

	return 0;
}

static int
send_part(char *mesg, struct server *s, struct channel *c)
{
	/* /part [[target[,targets]*] part message]*/

	int ret;
	char *targ;

	if ((targ = getarg(&mesg, " "))) {
		if ((ret = io_sendf(s->connection, "PART %s :%s", targ, (*mesg) ? mesg : DEFAULT_QUIT_MESG)))
			failf(c, "sendf fail: %s", io_err(ret));
	} else {
		if (c->type == CHANNEL_T_SERVER)
			fail(c, "Error: PART requires a target");

		if (c->type == CHANNEL_T_PRIVATE)
			fail(c, "Error: Can't part private buffers");

		if (c->parted)
			fail(c, "Error: Already parted from channel");

		if ((ret = io_sendf(s->connection, "PART %s :%s", c->name, DEFAULT_QUIT_MESG)))
			failf(c, "sendf fail: %s", io_err(ret));
	}

	return 0;
}

static int
send_privmsg(char *mesg, struct server *s, struct channel *c)
{
	/* /(priv | msg) <target> <message> */

	char *targ;
	int ret;
	struct channel *cc;

	if (!(targ = getarg(&mesg, " ")))
		fail(c, "Error: Private messages require a target");

	if (*mesg == '\0')
		fail(c, "Error: Private messages was null");

	if ((ret = io_sendf(s->connection, "PRIVMSG %s :%s", targ, mesg)))
		failf(c, "sendf fail: %s", io_err(ret));

	if ((cc = channel_list_get(&s->clist, targ)) == NULL) {
		cc = channel(targ, CHANNEL_T_PRIVATE);
		cc->server = s;
		channel_list_add(&s->clist, cc);
	}

	newline(cc, BUFFER_LINE_CHAT, s->nick, mesg);

	return 0;
}

static int
send_topic(char *mesg, struct server *s, struct channel *c)
{
	/* /topic [topic] */

	int ret;

	/* If no actual message is given, retrieve the current topic */
	while (*mesg == ' ')
		mesg++;

	if (*mesg == '\0') {
		if ((ret = io_sendf(s->connection, "TOPIC %s", c->name)))
			failf(c, "sendf fail: %s", io_err(ret));
	} else {
		if ((ret = io_sendf(s->connection, "TOPIC %s :%s", c->name, mesg)))
			failf(c, "sendf fail: %s", io_err(ret));
	}

	return 0;
}

static int
send_quit(char *mesg, struct server *s, struct channel *c)
{
	/* /quit :[quit message] */

	int ret;

	if (!s)
		fail(c, "Error: Not connected to server");

	s->quitting = 1;

	if ((ret = io_sendf(s->connection, "QUIT :%s", (*mesg ? mesg : DEFAULT_QUIT_MESG))))
		failf(c, "sendf fail: %s", io_err(ret));

	return 0;
}

// TODO: :version
static int
send_version(char *mesg, struct server *s, struct channel *c)
{
	/* /version [target] */

	int ret;
	char *targ;

	if (s == NULL) {
		newline(c, 0, "--", "rirc v"VERSION);
		newline(c, 0, "--", "http://rcr.io/rirc");
		return 0;
	}

	if ((targ = getarg(&mesg, " "))) {
		if ((ret = io_sendf(s->connection, "VERSION %s", targ)))
			failf(c, "sendf fail: %s", io_err(ret));
	} else {
		if ((ret = io_sendf(s->connection, "VERSION")))
			failf(c, "sendf fail: %s", io_err(ret));
	}

	return 0;
}

/*
 * Message receiving handlers
 */

void
recv_mesg(struct server *s, struct parsed_mesg *p)
{
	const struct recv_handler* handler;

	/* TODO: parsed_mesg can cache the length of command/args/etc */

	if (isdigit(*p->command))
		recv_numeric(p, s);
	else if ((handler = recv_handler_lookup(p->command, strlen(p->command))))
		handler->func(p, s);
	else
		newlinef(s->channel, 0, "-!!-", "Message type '%s' unknown", p->command);
}

static int
recv_ctcp_req(struct parsed_mesg *p, struct server *s)
{
	/* CTCP Requests:
	 * PRIVMSG <target> :0x01<command> <arguments>0x01
	 *
	 * All replies must be:
	 * NOTICE <target> :0x01<reply>0x01 */

	char *targ, *cmd, *mesg;
	int ret;

	if (!p->from)
		fail(s->channel, "CTCP: sender's nick is null");

	/* CTCP request from ignored user, do nothing */
	if (user_list_get(&(s->ignore), p->from, 0))
		return 0;

	if (!(targ = getarg(&p->params, " ")))
		fail(s->channel, "CTCP: target is null");

	if (!(mesg = getarg(&p->trailing, "\x01")))
		fail(s->channel, "CTCP: invalid markup");

	/* Markup is valid, get command */
	if (!(cmd = getarg(&mesg, " ")))
		fail(s->channel, "CTCP: command is null");

	/* Handle the CTCP request if supported */

	if (!strcmp(cmd, "ACTION")) {
		/* ACTION <message> */

		struct channel *c;

		if (IS_ME(targ)) {
			/* Sending emote to private channel */

			if ((c = channel_list_get(&s->clist, p->from)) == NULL) {
				c = channel(p->from, CHANNEL_T_PRIVATE);
				c->server = s;
				channel_list_add(&s->clist, c);
			}

			if (c != current_channel()) {
				c->activity = ACTIVITY_PINGED;
				draw_nav();
			}

		} else if ((c = channel_list_get(&s->clist, targ)) == NULL)
			failf(s->channel, "CTCP ACTION: channel '%s' not found", targ);

		newlinef(c, 0, "*", "%s %s", p->from, mesg);

		return 0;
	}

	if (!strcmp(cmd, "CLIENTINFO")) {
		/* CLIENTINFO
		 *
		 * Returns a list of CTCP commands supported by rirc */

		newlinef(s->channel, 0, "--", "CTCP CLIENTINFO request from %s", p->from);

		if ((ret = io_sendf(s->connection, "NOTICE %s :\x01""CLIENTINFO ACTION PING VERSION TIME\x01", p->from)))
			failf(s->channel, "sendf fail: %s", io_err(ret));

		return 0;
	}

	if (!strcmp(cmd, "PING")) {
		/* PING
		 *
		 * Returns a millisecond precision timestamp */

		struct timeval t;

		gettimeofday(&t, NULL);

		long long milliseconds = t.tv_sec * 1000LL + t.tv_usec;

		newlinef(s->channel, 0, "--", "CTCP PING request from %s", p->from);

		if ((ret = io_sendf(s->connection, "NOTICE %s :\x01""PING %lld\x01", p->from, milliseconds)))
			failf(s->channel, "sendf fail: %s", io_err(ret));

		return 0;
	}

	if (!strcmp(cmd, "VERSION")) {
		/* VERSION
		 *
		 * Returns version info about rirc */

		newlinef(s->channel, 0, "--", "CTCP VERSION request from %s", p->from);

		if ((ret = io_sendf(s->connection, "NOTICE %s :\x01""VERSION rirc v"VERSION", http://rcr.io/rirc\x01", p->from)))
			failf(s->channel, "sendf fail: %s", io_err(ret));

		return 0;
	}

	if (!strcmp(cmd, "TIME")) {
		/* TIME
		 *
		 * Returns the localtime in human readable form */

		char time_str[64];
		struct tm *tm;
		time_t t;

		t = time(NULL);
		tm = localtime(&t);

		/* Mon Jan 01 20:30 GMT */
		strftime(time_str, sizeof(time_str), "%a %b %d %H:%M %Z", tm);

		newlinef(s->channel, 0, "--", "CTCP TIME request from %s", p->from);

		if ((ret = io_sendf(s->connection, "NOTICE %s :\x01""TIME %s\x01", p->from, time_str)))
			failf(s->channel, "sendf fail: %s", io_err(ret));

		return 0;
	}

	/* Unsupported CTCP request */
	if ((ret = io_sendf(s->connection, "NOTICE %s :\x01""ERRMSG %s not supported\x01", p->from, cmd)))
		failf(s->channel, "sendf fail: %s", io_err(ret));

	failf(s->channel, "CTCP: Unknown command '%s' from %s", cmd, p->from);
}

static int
recv_ctcp_rpl(struct parsed_mesg *p, struct server *s)
{
	/* CTCP replies:
	 * NOTICE <target> :0x01<command> <arguments>0x01 */

	char *cmd, *mesg;

	if (!p->from)
		fail(s->channel, "CTCP: sender's nick is null");

	/* CTCP reply from ignored user, do nothing */
	if (user_list_get(&(s->ignore), p->from, 0))
		return 0;

	if (!(mesg = getarg(&p->trailing, "\x01")))
		fail(s->channel, "CTCP: invalid markup");

	/* Markup is valid, get command */
	if (!(cmd = getarg(&mesg, " ")))
		fail(s->channel, "CTCP: command is null");

	// FIXME: CTCP PING replies should come back with the same
	// <second> <millisecond> value that was sent out, and is
	// used to calculate the ping here

	newlinef(s->channel, 0, p->from, "CTCP %s reply: %s", cmd, mesg);

	return 0;
}

static int
recv_error(struct parsed_mesg *p, struct server *s)
{
	/* ERROR :<message>
	 *
	 * Sent to clients before terminating their connection
	 */

	newlinef(s->channel, 0, (s->quitting ? "--" : "ERROR"), "%s", p->trailing);

	return 0;
}

static int
recv_join(struct parsed_mesg *p, struct server *s)
{
	/* :nick!user@hostname.domain JOIN [:]<channel> */

	char *chan;
	struct channel *c;

	if (!p->from)
		fail(s->channel, "JOIN: sender's nick is null");

	if (!(chan = getarg(&p->params, " ")) && !(chan = getarg(&p->trailing, " ")))
		fail(s->channel, "JOIN: channel is null");

	if (IS_ME(p->from)) {
		if ((c = channel_list_get(&s->clist, chan)) == NULL) {
			c = channel(chan, CHANNEL_T_CHANNEL);
			c->server = s;
			channel_list_add(&s->clist, c);
			channel_set_current(c);
		} else {
			c->parted = 0;
		}
		newlinef(c, 0, ">", "Joined %s", chan);
		draw_all();
	} else {

		if ((c = channel_list_get(&s->clist, chan)) == NULL)
			failf(s->channel, "JOIN: channel '%s' not found", chan);

		if (user_list_add(&(c->users), p->from, MODE_EMPTY) == USER_ERR_DUPLICATE)
			failf(s->channel, "Error: user '%s' alread on channel '%s'", p->from, c->name);

		if (c->users.count <= jpq_threshold)
			newlinef(c, 0, ">", "%s!%s has joined %s", p->from, p->host, chan);

		draw_status();
	}

	return 0;
}

static int
recv_kick(struct parsed_mesg *p, struct server *s)
{
	/* :nick!user@hostname.domain KICK <channel> <user> :comment */

	char *chan, *user;
	struct channel *c;

	if (!p->from)
		fail(s->channel, "KICK: sender's nick is null");

	if (!(chan = getarg(&p->params, " ")))
		fail(s->channel, "KICK: channel is null");

	if (!(user = getarg(&p->params, " ")))
		fail(s->channel, "KICK: user is null");

	if ((c = channel_list_get(&s->clist, chan)) == NULL)
		failf(s->channel, "KICK: channel '%s' not found", chan);

	/* RFC 2812, section 3.2.8:
	 *
	 * If a "comment" is given, this will be sent instead of the default message,
	 * the nickname of the user issuing the KICK.
	 */
	if (!strcmp(p->from, p->trailing))
		p->trailing = NULL;

	if (IS_ME(user)) {

		channel_part(c);

		if (p->trailing)
			newlinef(c, 0, "--", "You've been kicked by %s (%s)", p->from, p->trailing);
		else
			newlinef(c, 0, "--", "You've been kicked by %s", p->from, user);
	} else {

		if (user_list_del(&(c->users), user) == USER_ERR_NOT_FOUND)
			failf(s->channel, "KICK: nick '%s' not found in '%s'", user, chan);

		if (p->trailing)
			newlinef(c, 0, "--", "%s has kicked %s (%s)", p->from, user, p->trailing);
		else
			newlinef(c, 0, "--", "%s has kicked %s", p->from, user);
	}

	draw_status();

	return 0;
}

static int
recv_mode(struct parsed_mesg *p, struct server *s)
{
	/* MODE <targ> 1*[<modestring> [<mode arguments>]]
	 *
	 * modestring  =  1*(modeset)
	 * modeset     =  plusminus *(modechar)
	 * plusminus   =  %x53 / %x55            ; '+' / '-'
	 * modechar    =  ALPHA
	 *
	 * Any number of mode flags can be set or unset in a MODE message, but
	 * the maximum number of modes with parameters is given by the server's
	 * MODES configuration.
	 *
	 * Mode flags that require a parameter are configured as the server's
	 * CHANMODE subtypes; A,B,C,D
	 *
	 * The following formats are equivalent, if e.g.:
	 *  - 'a' and 'c' require parameters
	 *  - 'b' has no parameter
	 *
	 *   MODE <chan> +ab  <param a> +c <param c>
	 *   MODE <chan> +abc <param a>    <param c>
	 */

	struct channel *c;

	char *targ;

	if (!(targ = getarg(&p->params, " ")))
		fail(s->channel, "MODE: target is null");

	if (IS_ME(targ))
		return recv_mode_usermodes(p, &(s->mode_cfg), s);

	if ((c = channel_list_get(&s->clist, targ)))
		return recv_mode_chanmodes(p, &(s->mode_cfg), c);

	failf(s->channel, "MODE: target '%s' not found", targ);
}

static int
recv_mode_chanmodes(struct parsed_mesg *p, const struct mode_cfg *cfg, struct channel *c)
{
	struct mode *chanmodes = &(c->chanmodes);
	struct user *user;

	char flag, *modestring, *modearg;
	enum mode_err_t mode_err;
	enum mode_set_t mode_set;

#define MODE_GETARG(M, P) \
	(((M) = getarg(&(P)->params, " ")) || ((M) = getarg(&(P)->trailing, " ")))

	if (!MODE_GETARG(modestring, p))
		fail(c, "MODE: modestring is null");

	do {
		mode_set = MODE_SET_INVALID;
		mode_err = MODE_ERR_NONE;

		while ((flag = *modestring++)) {

			if (flag == '+') {
				mode_set = MODE_SET_ON;
				continue;
			}

			if (flag == '-') {
				mode_set = MODE_SET_OFF;
				continue;
			}

			modearg = NULL;

			switch (chanmode_type(cfg, mode_set, flag)) {

				/* Doesn't consume an argument */
				case MODE_FLAG_CHANMODE:

					mode_err = mode_chanmode_set(chanmodes, cfg, flag, mode_set);

					if (mode_err == MODE_ERR_NONE) {
						newlinef(c, 0, "--", "%s%s%s mode: %c%c",
								(p->from ? p->from : ""),
								(p->from ? " set " : ""),
								c->name,
								(mode_set == MODE_SET_ON ? '+' : '-'),
								flag);
					}
					break;

				/* Consumes an argument */
				case MODE_FLAG_CHANMODE_PARAM:

					if (!MODE_GETARG(modearg, p)) {
						newlinef(c, 0, "-!!-", "MODE: flag '%c' expected argument", flag);
						continue;
					}

					mode_err = mode_chanmode_set(chanmodes, cfg, flag, mode_set);

					if (mode_err == MODE_ERR_NONE) {
						newlinef(c, 0, "--", "%s%s%s mode: %c%c %s",
								(p->from ? p->from : ""),
								(p->from ? " set " : ""),
								c->name,
								(mode_set == MODE_SET_ON ? '+' : '-'),
								flag,
								modearg);
					}
					break;

				/* Consumes an argument and sets a usermode */
				case MODE_FLAG_PREFIX:

					if (!MODE_GETARG(modearg, p)) {
						newlinef(c, 0, "-!!-", "MODE: flag '%c' argument is null", flag);
						continue;
					}

					if (!(user = user_list_get(&(c->users), modearg, 0))) {
						newlinef(c, 0, "-!!-", "MODE: flag '%c' user '%s' not found", flag, modearg);
						continue;
					}

					mode_prfxmode_set(&(user->prfxmodes), cfg, flag, mode_set);

					if (mode_err == MODE_ERR_NONE) {
						newlinef(c, 0, "--", "%s%suser %s mode: %c%c",
								(p->from ? p->from : ""),
								(p->from ? " set " : ""),
								modearg,
								(mode_set == MODE_SET_ON ? '+' : '-'),
								flag);
					}
					break;

				case MODE_FLAG_INVALID_SET:
					mode_err = MODE_ERR_INVALID_SET;
					break;

				case MODE_FLAG_INVALID_FLAG:
					mode_err = MODE_ERR_INVALID_FLAG;
					break;

				default:
					newlinef(c, 0, "-!!-", "MODE: unhandled error, flag '%c'");
					continue;
			}

			switch (mode_err) {

				case MODE_ERR_INVALID_FLAG:
					newlinef(c, 0, "-!!-", "MODE: invalid flag '%c'", flag);
					break;

				case MODE_ERR_INVALID_SET:
					newlinef(c, 0, "-!!-", "MODE: missing '+'/'-'");
					break;

				default:
					break;
			}
		}
	} while (MODE_GETARG(modestring, p));

#undef MODE_GETARG

	mode_str(&(c->chanmodes), &(c->chanmodes_str));
	draw_status();

	return 0;
}

static int
recv_mode_usermodes(struct parsed_mesg *p, const struct mode_cfg *cfg, struct server *s)
{
	struct mode *usermodes = &(s->usermodes);

	char flag, *modes;
	enum mode_err_t mode_err;
	enum mode_set_t mode_set;

#define MODE_GETARG(M, P) \
	(((M) = getarg(&(P)->params, " ")) || ((M) = getarg(&(P)->trailing, " ")))

	if (!MODE_GETARG(modes, p))
		fail(s->channel, "MODE: modes are null");

	do {
		mode_set = MODE_SET_INVALID;

		while ((flag = *modes++)) {

			if (flag == '+') {
				mode_set = MODE_SET_ON;
				continue;
			}

			if (flag == '-') {
				mode_set = MODE_SET_OFF;
				continue;
			}

			mode_err = mode_usermode_set(usermodes, cfg, flag, mode_set);

			if (mode_err == MODE_ERR_NONE)
				newlinef(s->channel, 0, "--", "%s%smode: %c%c",
						(p->from ? p->from : ""),
						(p->from ? " set " : ""),
						(mode_set == MODE_SET_ON ? '+' : '-'),
						flag);

			else if (mode_err == MODE_ERR_INVALID_SET)
				newlinef(s->channel, 0, "-!!-", "MODE: missing '+'/'-'");

			else if (mode_err == MODE_ERR_INVALID_FLAG)
				newlinef(s->channel, 0, "-!!-", "MODE: invalid flag '%c'", flag);
		}
	} while (MODE_GETARG(modes, p));

#undef MODE_GETARG

	mode_str(usermodes, &(s->mode_str));
	draw_status();

	return 0;
}

static int
recv_nick(struct parsed_mesg *p, struct server *s)
{
	/* :nick!user@hostname.domain NICK [:]<new nick> */

	char *nick;

	if (!p->from)
		fail(s->channel, "NICK: old nick is null");

	/* Some servers seem to send the new nick in the trailing */
	if (!(nick = getarg(&p->params, " ")) && !(nick = getarg(&p->trailing, " ")))
		fail(s->channel, "NICK: new nick is null");

	if (IS_ME(p->from)) {
		server_nick_set(s, nick);
		newlinef(s->channel, 0, "--", "You are now known as %s", nick);
	}

	struct channel *c = s->channel;
	//TODO: channel_list_foreach
	do {
		enum user_err ret;

		if ((ret = user_list_rpl(&(c->users), p->from, nick)) == USER_ERR_NONE)
			newlinef(c, 0, "--", "%s  >>  %s", p->from, nick);

		else if (ret == USER_ERR_DUPLICATE)
			newlinef(c, 0, "-!!-", "Error: user '%s' alread on channel '%s'", p->from, c->name);

	} while ((c = c->next) != s->channel);

	return 0;
}

static int
recv_notice(struct parsed_mesg *p, struct server *s)
{
	/* :nick.hostname.domain NOTICE <target> :<message> */

	char *targ;
	struct channel *c;

	if (!p->trailing)
		fail(s->channel, "NOTICE: message is null");

	/* CTCP reply */
	if (*p->trailing == 0x01)
		return recv_ctcp_rpl(p, s);

	if (!p->from)
		fail(s->channel, "NOTICE: sender's nick is null");

	/* Notice from ignored user, do nothing */
	if (user_list_get(&(s->ignore), p->from, 0))
		return 0;

	if (!(targ = getarg(&p->params, " ")))
		fail(s->channel, "NOTICE: target is null");

	if ((c = channel_list_get(&s->clist, targ)))
		newline(c, 0, p->from, p->trailing);
	else
		newline(s->channel, 0, p->from, p->trailing);

	return 0;
}

static int
recv_numeric(struct parsed_mesg *p, struct server *s)
{
	/* :server <code> <target> [args] */

	char *targ, *nick, *chan, *time, *type, *num;
	int ret, _code;

	struct channel *c;

	/* Extract numeric code */
	for (_code = 0; isdigit(*p->command); p->command++) {

		_code = _code * 10 + (*p->command - '0');

		if (_code > 999)
			fail(s->channel, "NUMERIC: greater than 999");
	}

	enum numeric code = _code;

	/* Message target is only used to establish s->nick when registering with a server */
	if (!(targ = getarg(&p->params, " "))) {
		io_dx(s->connection);
		fail(s->channel, "NUMERIC: target is null");
		return 1;
	}

	/* Message target should match s->nick or '*' if unregistered, otherwise out of sync */
	if (strcmp(targ, s->nick) && strcmp(targ, "*") && code != RPL_WELCOME) {
		io_dx(s->connection);
		failf(s->channel, "NUMERIC: target mismatched, nick is '%s', received '%s'", s->nick, targ);
	}

	switch (code) {

	/* 001 :<Welcome message> */
	case RPL_WELCOME:

		/* Establishing new connection with a server,
		 * handle any channel auto-join or rejoins */

		c = s->channel;

		/* join any non-parted channels */
		do {
			//TODO: channel_list_foreach
			if (c->type == CHANNEL_T_CHANNEL && !c->parted) {
				if ((ret = io_sendf(s->connection, "JOIN %s", c->name)))
					failf(s->channel, "sendf fail: %s", io_err(ret));
			}
			c = c->next;
		} while (c != s->channel);

		if (p->trailing)
			newline(s->channel, 0, "--", p->trailing);

		newlinef(s->channel, 0, "--", "You are known as %s", s->nick);
		break;


	case RPL_YOURHOST:  /* 002 :<Host info, server version, etc> */
	case RPL_CREATED:   /* 003 :<Server creation date message> */

		/* FIXME: trailing can be null, here and elsewhere, eg `:d 003 nick VG` */
		newline(s->channel, 0, "--", p->trailing);
		break;


	case RPL_MYINFO:    /* 004 <params> :Are supported by this server */

		newlinef(s->channel, 0, "--", "%s ~ supported by this server", p->params);

		server_set_004(s, p->params);
		break;

	case RPL_ISUPPORT:  /* 005 <params> :Are supported by this server */

		newlinef(s->channel, 0, "--", "%s ~ supported by this server", p->params);

		server_set_005(s, p->params);
		break;


	/* 328 <channel> :<url> */
	case RPL_CHANNEL_URL:

		if (!(chan = getarg(&p->params, " ")))
			fail(s->channel, "RPL_CHANNEL_URL: channel is null");

		if ((c = channel_list_get(&s->clist, chan)) == NULL)
			failf(s->channel, "RPL_CHANNEL_URL: channel '%s' not found", chan);

		newlinef(c, 0, "--", "URL for %s is: \"%s\"", chan, p->trailing);
		break;


	/* 332 <channel> :<topic> */
	case RPL_TOPIC:

		if (!(chan = getarg(&p->params, " ")))
			fail(s->channel, "RPL_TOPIC: channel is null");

		if ((c = channel_list_get(&s->clist, chan)) == NULL)
			failf(s->channel, "RPL_TOPIC: channel '%s' not found", chan);

		newlinef(c, 0, "--", "Topic for %s is \"%s\"", chan, p->trailing);
		break;


	/* 333 <channel> <nick> <time> */
	case RPL_TOPICWHOTIME:

		if (!(chan = getarg(&p->params, " ")))
			fail(s->channel, "RPL_TOPICWHOTIME: channel is null");

		if (!(nick = getarg(&p->params, " ")))
			fail(s->channel, "RPL_TOPICWHOTIME: nick is null");

		if (!(time = getarg(&p->params, " ")))
			fail(s->channel, "RPL_TOPICWHOTIME: time is null");

		if ((c = channel_list_get(&s->clist, chan)) == NULL)
			failf(s->channel, "RPL_TOPICWHOTIME: channel '%s' not found", chan);

		time_t raw_time = atoi(time);
		time = ctime(&raw_time);

		newlinef(c, 0, "--", "Topic set by %s, %s", nick, time);
		break;


	// FIXME: this is returned from /names <target>
	// ... /names returns all names on all channels
	// flag channel namereply :1
	//
	// differentiate reply after JOIN or NAMES?
	/* 353 ("="/"*"/"@") <channel> :*([ "@" / "+" ]<nick>) */
	case RPL_NAMEREPLY:

		/* @:secret   *:private   =:public */
		if (!(type = getarg(&p->params, " ")))
			fail(s->channel, "RPL_NAMEREPLY: type is null");

		if (!(chan = getarg(&p->params, " ")))
			fail(s->channel, "RPL_NAMEREPLY: channel is null");

		if ((c = channel_list_get(&s->clist, chan)) == NULL)
			failf(s->channel, "RPL_NAMEREPLY: channel '%s' not found", chan);

		if ((ret = mode_chanmode_prefix(&(c->chanmodes), &(s->mode_cfg), *type)))
			newlinef(c, 0, "-!!-", "RPL_NAMEREPLY: invalid channel flag: '%c'", *type);

		while ((nick = getarg(&p->trailing, " "))) {

			char prefix = 0;

			struct mode m = MODE_EMPTY;

			/* Set user prefix */
			if (!irc_isnickchar(*nick, 1))
				prefix = *nick++;

			if (prefix && mode_prfxmode_prefix(&m, &(s->mode_cfg), prefix) != MODE_ERR_NONE)
				newlinef(c, 0, "-!!-", "Invalid user prefix: '%c'", prefix);

			if (user_list_add(&(c->users), nick, m) == USER_ERR_DUPLICATE)
				newlinef(c, 0, "-!!-", "Duplicate nick: '%s'", nick);
		}

		draw_status();
		break;


	case RPL_STATSCONN:    /* 250 :<Message> */
	case RPL_LUSERCLIENT:  /* 251 :<Message> */

		newline(s->channel, 0, "--", p->trailing);
		break;


	case RPL_LUSEROP:        /* 252 <int> :IRC Operators online */
	case RPL_LUSERUNKNOWN:   /* 253 <int> :Unknown connections */
	case RPL_LUSERCHANNELS:  /* 254 <int> :Channels formed */

		if (!(num = getarg(&p->params, " ")))
			num = "NULL";

		newlinef(s->channel, 0, "--", "%s %s", num, p->trailing);
		break;


	case RPL_LUSERME:      /* 255 :I have <int> clients and <int> servers */
	case RPL_LOCALUSERS:   /* 265 <int> <int> :Local users <int>, max <int> */
	case RPL_GLOBALUSERS:  /* 266 <int> <int> :Global users <int>, max <int> */
	case RPL_MOTD:         /* 372 :- <text> */
	case RPL_MOTDSTART:    /* 375 :- <server> Message of the day - */

		newline(s->channel, 0, "--", p->trailing);
		break;


	/* Not printing these */
	case RPL_NOTOPIC:     /* 331 <chan> :<Message> */
	case RPL_ENDOFNAMES:  /* 366 <chan> :<Message> */
	case RPL_ENDOFMOTD:   /* 376 :End of MOTD command */
		break;


	case ERR_NOSUCHNICK:    /* <nick> :<reason> */
	case ERR_NOSUCHSERVER:  /* <server> :<reason> */
	case ERR_NOSUCHCHANNEL: /* <channel> :<reason> */

		if (!(targ = getarg(&p->params, " "))) {
			if (code == ERR_NOSUCHNICK)
				fail(s->channel, "ERR_NOSUCHNICK: nick is null");
			if (code == ERR_NOSUCHSERVER)
				fail(s->channel, "ERR_NOSUCHSERVER: server is null");
			if (code == ERR_NOSUCHCHANNEL)
				fail(s->channel, "ERR_NOSUCHCHANNEL: channel is null");
		}

		/* Private buffer might not exist */
		if ((c = channel_list_get(&s->clist, targ)) == NULL)
			c = s->channel;

		if (p->trailing)
			newlinef(c, 0, "--", "Cannot send to '%s': %s", targ, p->trailing);
		else
			newlinef(c, 0, "--", "Cannot send to '%s'", targ);
		break;


	case ERR_CANNOTSENDTOCHAN:  /* <channel> :<reason> */

		if (!(chan = getarg(&p->params, " ")))
			fail(s->channel, "ERR_CANNOTSENDTOCHAN: channel is null");

		/* Channel buffer might not exist */
		if ((c = channel_list_get(&s->clist, chan)) == NULL)
			c = s->channel;

		if (p->trailing)
			newlinef(c, 0, "--", "Cannot send to '%s': %s", chan, p->trailing);
		else
			newlinef(c, 0, "--", "Cannot send to '%s'", chan);
		break;


	case ERR_ERRONEUSNICKNAME:  /* 432 <nick> :<reason> */

		if (!(nick = getarg(&p->params, " ")))
			fail(s->channel, "ERR_ERRONEUSNICKNAME: nick is null");

		newlinef(s->channel, 0, "-!!-", "'%s' - %s", nick, p->trailing);
		break;


	case ERR_NICKNAMEINUSE:  /* 433 <nick> :Nickname is already in use */

		if (!(nick = getarg(&p->params, " ")))
			fail(s->channel, "ERR_NICKNAMEINUSE: nick is null");

		newlinef(s->channel, 0, "-!!-", "Nick '%s' in use", nick);

		if (IS_ME(nick)) {

			server_nicks_next(s);

			newlinef(s->channel, 0, "-!!-", "Trying again with '%s'", s->nick);

			if ((ret = io_sendf(s->connection, "NICK %s", s->nick)))
				failf(s->channel, "sendf fail: %s", io_err(ret));

			return 0;
		}
		break;


	case ERR_INVITEONLYCHAN:
	case ERR_NOCHANMODES:

		if (p->trailing)
			newlinef(s->channel, 0, "--", "%s: %s", p->params, p->trailing);
		else
			newlinef(s->channel, 0, "--", "%s", p->params);
		break;


	default:

		newlinef(s->channel, 0, "UNHANDLED", "%d %s :%s", code, p->params, p->trailing);
		break;
	}

	return 0;
}

static int
recv_part(struct parsed_mesg *p, struct server *s)
{
	/* :nick!user@hostname.domain PART <channel> [:message] */

	char *targ;
	struct channel *c;

	if (!p->from)
		fail(s->channel, "PART: sender's nick is null");

	if (!(targ = getarg(&p->params, " ")))
		fail(s->channel, "PART: target is null");

	if (IS_ME(p->from)) {

		/* If receving a PART message from myself channel isn't found, assume it was closed */
		if ((c = channel_list_get(&s->clist, targ)) != NULL) {

			channel_part(c);

			if (p->trailing)
				newlinef(c, 0, "<", "you have left %s (%s)", targ, p->trailing);
			else
				newlinef(c, 0, "<", "you have left %s", targ);
		}

		draw_status();

		return 0;
	}

	if ((c = channel_list_get(&s->clist, targ)) == NULL)
		failf(s->channel, "PART: channel '%s' not found", targ);

	if (user_list_del(&(c->users), p->from) == USER_ERR_NOT_FOUND)
		failf(s->channel, "PART: nick '%s' not found in '%s'", p->from, targ);

	if (c->users.count <= jpq_threshold) {
		if (p->trailing)
			newlinef(c, 0, "<", "%s!%s has left %s (%s)", p->from, p->host, targ, p->trailing);
		else
			newlinef(c, 0, "<", "%s!%s has left %s", p->from, p->host, targ);
	}

	draw_status();

	return 0;
}

static int
recv_ping(struct parsed_mesg *p, struct server *s)
{
	/* PING :<server> */

	int ret;

	if (!p->trailing)
		fail(s->channel, "PING: server is null");

	if ((ret = io_sendf(s->connection, "PONG %s", p->trailing)))
		failf(s->channel, "sendf fail: %s", io_err(ret));

	return 0;
}

static int
recv_pong(struct parsed_mesg *p, struct server *s)
{
	/*  PONG <server> [<server2>] */

	// FIXME:
	UNUSED(p);
	UNUSED(s);

#if 0
	/*  PING sent explicitly by the user */
	if (!s->pinging)
		newlinef(s->channel, 0, "!!", "PONG %s", p->params);

	s->pinging = 0;
#endif

	return 0;
}

static int
recv_privmsg(struct parsed_mesg *p, struct server *s)
{
	/* :nick!user@hostname.domain PRIVMSG <target> :<message> */

	char *targ;
	struct channel *c;

	if (!p->trailing)
		fail(s->channel, "PRIVMSG: message is null");

	/* CTCP request */
	if (*p->trailing == 0x01)
		return recv_ctcp_req(p, s);

	if (!p->from)
		fail(s->channel, "PRIVMSG: sender's nick is null");

	/* Privmsg from ignored user, do nothing */
	if (user_list_get(&(s->ignore), p->from, 0))
		return 0;

	if (!(targ = getarg(&p->params, " ")))
		fail(s->channel, "PRIVMSG: target is null");

	/* Find the target channel */
	if (IS_ME(targ)) {

		if ((c = channel_list_get(&s->clist, p->from)) == NULL) {
			c = channel(p->from, CHANNEL_T_PRIVATE);
			c->server = s;
			channel_list_add(&s->clist, c);
		}

		if (c != current_channel()) {
			c->activity = ACTIVITY_PINGED;
			draw_nav();
		}

	} else if ((c = channel_list_get(&s->clist, targ)) == NULL)
		failf(s->channel, "PRIVMSG: channel '%s' not found", targ);

	if (check_pinged(p->trailing, s->nick)) {

		draw_bell();

		if (c != current_channel()) {
			c->activity = ACTIVITY_PINGED;
			draw_nav();
		}

		newline(c, BUFFER_LINE_PINGED, p->from, p->trailing);
	} else
		newline(c, BUFFER_LINE_CHAT, p->from, p->trailing);

	return 0;
}

static int
recv_quit(struct parsed_mesg *p, struct server *s)
{
	/* :nick!user@hostname.domain QUIT [:message] */

	if (!p->from)
		fail(s->channel, "QUIT: sender's nick is null");

	struct channel *c = s->channel;
	//TODO: channel_list_foreach
	do {
		if (user_list_del(&(c->users), p->from) == USER_ERR_NONE) {
			if (c->users.count <= jpq_threshold) {
				if (p->trailing)
					newlinef(c, 0, "<", "%s!%s has quit (%s)", p->from, p->host, p->trailing);
				else
					newlinef(c, 0, "<", "%s!%s has quit", p->from, p->host);
			}
		}
		c = c->next;
	} while (c != s->channel);

	draw_status();

	return 0;
}

static int
recv_topic(struct parsed_mesg *p, struct server *s)
{
	/* :nick!user@hostname.domain TOPIC <channel> :[topic] */

	char *targ;
	struct channel *c;

	if (!p->from)
		fail(s->channel, "TOPIC: sender's nick is null");

	if (!(targ = getarg(&p->params, " ")))
		fail(s->channel, "TOPIC: target is null");

	if (!p->trailing)
		fail(s->channel, "TOPIC: topic is null");

	if ((c = channel_list_get(&s->clist, targ)) == NULL)
		failf(s->channel, "TOPIC: channel '%s' not found", targ);

	if (*p->trailing) {
		newlinef(c, 0, "--", "%s has changed the topic:", p->from);
		newlinef(c, 0, "--", "\"%s\"", p->trailing);
	} else {
		newlinef(c, 0, "--", "%s has unset the topic", p->from);
	}

	return 0;
}

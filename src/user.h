#ifndef NICKLIST_H
#define NICKLIST_H

#include "mode.h"
#include "tree.h"
#include "utils.h"

enum user_err
{
	USER_ERR_DUPLICATE = -2,
	USER_ERR_NOT_FOUND = -1,
	USER_ERR_NONE
};

struct user
{
	AVL_NODE(user) ul;
	struct mode prfxmodes;
	struct string nick;
	char _[];
};

struct user_list
{
	AVL_HEAD(user);
	unsigned int count;
};

enum user_err user_list_add(struct user_list*, const char*, struct mode);
enum user_err user_list_del(struct user_list*, const char*);
enum user_err user_list_rpl(struct user_list*, const char*, const char*);

struct user* user_list_get(struct user_list*, const char*, size_t);

void user_list_free(struct user_list*);

#endif

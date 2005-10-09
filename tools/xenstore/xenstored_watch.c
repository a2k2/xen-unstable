/* 
    Watch code for Xen Store Daemon.
    Copyright (C) 2005 Rusty Russell IBM Corporation

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdio.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include "talloc.h"
#include "list.h"
#include "xenstored_watch.h"
#include "xs_lib.h"
#include "utils.h"
#include "xenstored_test.h"
#include "xenstored_domain.h"

/* FIXME: time out unacked watches. */
struct watch_event
{
	/* The events on this watch. */
	struct list_head list;

	/* Data to send (node\0token\0). */
	unsigned int len;
	char *data;
};

struct watch
{
	/* Watches on this connection */
	struct list_head list;

	/* Current outstanding events applying to this watch. */
	struct list_head events;

	/* Is this relative to connnection's implicit path? */
	const char *relative_path;

	char *token;
	char *node;
};

/* Look through our watches: if any of them have an event, queue it. */
void queue_next_event(struct connection *conn)
{
	struct watch_event *event;
	struct watch *watch;

	/* We had a reply queued already?  Send it: other end will
	 * discard watch. */
	if (conn->waiting_reply) {
		conn->out = conn->waiting_reply;
		conn->waiting_reply = NULL;
		return;
	}

	list_for_each_entry(watch, &conn->watches, list) {
		event = list_top(&watch->events, struct watch_event, list);
		if (event) {
			list_del(&event->list);
			talloc_free(event);
			send_reply(conn,XS_WATCH_EVENT,event->data,event->len);
			break;
		}
	}
}

static int destroy_watch_event(void *_event)
{
	struct watch_event *event = _event;

	trace_destroy(event, "watch_event");
	return 0;
}

static void add_event(struct connection *conn,
		      struct watch *watch,
		      const char *name)
{
	struct watch_event *event;

	if (!check_event_node(name)) {
		/* Can this conn load node, or see that it doesn't exist? */
		struct node *node;

		node = get_node(conn, name, XS_PERM_READ);
		if (!node && errno != ENOENT)
			return;
	}

	if (watch->relative_path) {
		name += strlen(watch->relative_path);
		if (*name == '/') /* Could be "" */
			name++;
	}

	event = talloc(watch, struct watch_event);
	event->len = strlen(name) + 1 + strlen(watch->token) + 1;
	event->data = talloc_array(event, char, event->len);
	strcpy(event->data, name);
	strcpy(event->data + strlen(name) + 1, watch->token);
	talloc_set_destructor(event, destroy_watch_event);
	list_add_tail(&event->list, &watch->events);
	trace_create(event, "watch_event");
}

/* FIXME: we fail to fire on out of memory.  Should drop connections. */
void fire_watches(struct connection *conn, const char *name, bool recurse)
{
	struct connection *i;
	struct watch *watch;

	/* During transactions, don't fire watches. */
	if (conn && conn->transaction)
		return;

	/* Create an event for each watch. */
	list_for_each_entry(i, &connections, list) {
		list_for_each_entry(watch, &i->watches, list) {
			if (is_child(name, watch->node))
				add_event(i, watch, name);
			else if (recurse && is_child(watch->node, name))
				add_event(i, watch, watch->node);
			else
				continue;
			/* If connection not doing anything, queue this. */
			if (i->state == OK)
				queue_next_event(i);
		}
	}
}

static int destroy_watch(void *_watch)
{
	trace_destroy(_watch, "watch");
	return 0;
}

void do_watch(struct connection *conn, struct buffered_data *in)
{
	struct watch *watch;
	char *vec[2];
	bool relative;

	if (get_strings(in, vec, ARRAY_SIZE(vec)) != ARRAY_SIZE(vec)) {
		send_error(conn, EINVAL);
		return;
	}

	if (strstarts(vec[0], "@")) {
		relative = false;
		/* check if valid event */
	} else {
		relative = !strstarts(vec[0], "/");
		vec[0] = canonicalize(conn, vec[0]);
		if (!is_valid_nodename(vec[0])) {
			send_error(conn, errno);
			return;
		}
	}

	/* Check for duplicates. */
	list_for_each_entry(watch, &conn->watches, list) {
		if (streq(watch->node, vec[0]) &&
                    streq(watch->token, vec[1])) {
			send_error(conn, EEXIST);
			return;
		}
	}

	watch = talloc(conn, struct watch);
	watch->node = talloc_strdup(watch, vec[0]);
	watch->token = talloc_strdup(watch, vec[1]);
	if (relative)
		watch->relative_path = get_implicit_path(conn);
	else
		watch->relative_path = NULL;

	INIT_LIST_HEAD(&watch->events);

	list_add_tail(&watch->list, &conn->watches);
	trace_create(watch, "watch");
	talloc_set_destructor(watch, destroy_watch);
	send_ack(conn, XS_WATCH);

	/* We fire once up front: simplifies clients and restart. */
	add_event(conn, watch, watch->node);
}

void do_unwatch(struct connection *conn, struct buffered_data *in)
{
	struct watch *watch;
	char *node, *vec[2];

	if (get_strings(in, vec, ARRAY_SIZE(vec)) != ARRAY_SIZE(vec)) {
		send_error(conn, EINVAL);
		return;
	}

	node = canonicalize(conn, vec[0]);
	list_for_each_entry(watch, &conn->watches, list) {
		if (streq(watch->node, node) && streq(watch->token, vec[1])) {
			list_del(&watch->list);
			talloc_free(watch);
			send_ack(conn, XS_UNWATCH);
			return;
		}
	}
	send_error(conn, ENOENT);
}

#ifdef TESTING
void dump_watches(struct connection *conn)
{
	struct watch *watch;
	struct watch_event *event;

	list_for_each_entry(watch, &conn->watches, list) {
		printf("    watch on %s token %s\n",
		       watch->node, watch->token);
		list_for_each_entry(event, &watch->events, list)
			printf("        event: %s\n", event->data);
	}
}
#endif

#ifndef __CONN_H__
#define __CONN_H__

#include <stdbool.h>
#include "ae.h"
#include "ez_buffer.h"

#define CONN_CONNECTED	(1 << 0)
#define CONN_VERYFIED	(1 << 1)
#define CONN_CLOSED	(1 << 2)
#define CONN_CHAP_SEND	(1 << 3)

enum conn_state { 
	conn_undef,
	conn_closing,
	conn_chap_send,
	conn_veryfied,
};

typedef struct conn {
	enum conn_state conn_status;
	int sfd;
	int mask;
	aeTimeEvent *timer_id;
	aeEventLoop *el;
	void (*on_error)(struct conn *conn);
	void (*on_close)(struct conn *conn, int sync_write);
	void (*on_message)(struct conn *conn);
	void (*get_message)(struct conn *conn, const char **buf, size_t *len);
	bool (*use_message)(struct conn *conn, size_t len);
	int  (*send_message)(struct conn *conn, const char *buf, size_t len);
	ez_buffer inbuf;
	ez_buffer outbuf;
	char chap[32]; //
} conn;

int handle_read(aeEventLoop *el, int fd, void *privdata, int mask);
void conn_free(conn *conn);
conn *conn_new(aeEventLoop *el, int sfd);
void set_conn_state(struct conn *conn, enum conn_state st);
#endif

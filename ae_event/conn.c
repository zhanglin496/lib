#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>

#include "ae.h"
#include "ez_buffer.h"
#include "conn.h"
#include "debug.h"

void conn_free(conn *conn)
{
	if (!conn)
		return;
	if (conn->mask & AE_READABLE)
		aeDeleteFileEvent(conn->el, conn->sfd, AE_READABLE);
	if (conn->mask & AE_WRITABLE)
		aeDeleteFileEvent(conn->el, conn->sfd, AE_WRITABLE);
	close(conn->sfd);
	ez_buffer_free(&conn->outbuf);
	ez_buffer_free(&conn->inbuf);
	if (conn->timer_id != NULL)
		aeDeleteTimeEvent(conn->el, conn->timer_id);
	free(conn);
}

static void default_on_error(conn *conn)
{
	TRACE
	conn_free(conn);
}

static void default_on_close(conn *conn, int sync_write)
{
	if (sync_write) {
		if (!get_buffer_length(&conn->outbuf)) {
			conn_free(conn);  //already send finished
                  	return;
          	}
		// delete readable event
		if (conn->mask & AE_READABLE)
			aeDeleteFileEvent(conn->el, conn->sfd, AE_READABLE);
		
		if (!(conn->mask & AE_WRITABLE)) {
			//should not happen
	         //     	   aeCreateFileEvent(conn->el, conn->sfd, AE_WRITABLE,
                   //                      handle_write, conn);
                     //      conn->mask |= AE_WRITABLE;
	        }
		//wait next write
	} else
		conn_free(conn);
}

static bool default_use_message(conn *conn, size_t len)
{
	return erase_buffer(&conn->inbuf, len);
}

static void default_get_message(conn *conn, const char **buf, size_t *len)
{
	get_buffer_begin(&conn->inbuf, buf, len);
}

int handle_read(aeEventLoop *el, int fd, void *privdata, int mask)
{
    	char *buf;
    	size_t len;
	struct conn *conn = (struct conn *)privdata;
	TRACE

	reserve_space(&conn->inbuf, 512);
   	get_space_begin(&conn->inbuf, &buf, &len);
    	int ret = read(conn->sfd, buf, len);
   	if (ret < 0) {
        	if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            		conn->on_error(conn);
			return -1;
        	}
        	return 0;
    	} else if (ret == 0) {
        	conn->on_close(conn, 0); //peer closed
		return -1;
    	} else {
        	append_buffer_ex(&conn->inbuf, ret);
        	conn->on_message(conn); //callback
	}
	return ret;
}

static int handle_write(aeEventLoop *el, int fd, void *privdata, int mask)
{
	struct conn *conn = (struct conn *)privdata;
	const char *buf;
        size_t len;

       	get_buffer_begin(&conn->outbuf, &buf, &len);
        int ret = write(conn->sfd, buf, len);
        if (ret < 0) {
            	if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
			conn->on_error(conn);
			return -1;
		}
            	return 0;
        } else {
            	erase_buffer(&conn->outbuf, ret);
            	if (!get_buffer_length(&conn->outbuf)) {
			if (conn->conn_status == conn_closing)
				conn->on_close(conn, 0);
			else {
				aeDeleteFileEvent(conn->el, conn->sfd, AE_WRITABLE);
				conn->mask &= ~AE_WRITABLE;
			}
		}
        }
	return ret;
}

static int send_message(struct conn *conn, const char *buf, size_t len)
{
    	if (get_buffer_length(&conn->outbuf)) {  //have last data
        	append_buffer(&conn->outbuf, buf, len);
        	return 0;
    	}

    	int ret = write(conn->sfd, buf, len);
    	if (ret < 0) {
        	if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
			return -1;  //do not call conn->on_close, return error to caller
		}
        	ret = 0;
    	} else if (len == ret) {
		TRACE
            	return ret;
	}
    	append_buffer(&conn->outbuf, buf + ret, len - ret);
	
	if (!(conn->mask & AE_WRITABLE)) {
		aeCreateFileEvent(conn->el, conn->sfd, AE_WRITABLE,
					handle_write, conn);
		conn->mask |= AE_WRITABLE;
	}
    	return ret;
}

conn *conn_new(aeEventLoop *el, int sfd)
{
	struct conn *conn = malloc(sizeof(*conn));
	if (!conn) {
		close(sfd);
		return NULL;
	}
	conn->conn_status = conn_undef;
	conn->el = el;
	conn->sfd = sfd;
	conn->mask = AE_NONE;
	conn->timer_id = NULL;
	conn->on_close = default_on_close;
	conn->on_error = default_on_error;
	conn->get_message = default_get_message;
	conn->use_message = default_use_message;
	conn->send_message = send_message;

	if (!ez_buffer_init(&conn->inbuf)
		|| !ez_buffer_init(&conn->outbuf))
		goto out;

	return conn;
out:
	conn_free(conn);
	return NULL;
}

void set_conn_state(struct conn *conn, enum conn_state st)
{
	conn->conn_status = st;
}


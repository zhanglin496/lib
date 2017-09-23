#ifndef _APUE_DB_H
#define _APUE_DB_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef	void *	DBHANDLE;

DBHANDLE  db_open(const char *, int, ...);
void      db_close(DBHANDLE);
char     *db_fetch(DBHANDLE, const void *,  size_t);
int       db_store(DBHANDLE, const void *, size_t, const void *, size_t, int);
int       db_delete(DBHANDLE, const void *, size_t);
void      db_rewind(DBHANDLE);
char    *db_nextrec(DBHANDLE, void *);
int 	db_drop(const char *);
int db_fsync(DBHANDLE h);

int lock_reg(int fd, int cmd, int type, off_t offset, int whence, off_t len);
#define read_lock(fd, offset, whence, len) \
                         lock_reg((fd), F_SETLK, F_RDLCK, (offset), (whence), (len))
#define readw_lock(fd, offset, whence, len) \
                         lock_reg((fd), F_SETLKW, F_RDLCK, (offset), (whence), (len))
#define write_lock(fd, offset, whence, len) \
                         lock_reg((fd), F_SETLK, F_WRLCK, (offset), (whence), (len))
#define writew_lock(fd, offset, whence, len) \
                         lock_reg((fd), F_SETLKW, F_WRLCK, (offset), (whence), (len))
#define un_lock(fd, offset, whence, len) \
                         lock_reg((fd), F_SETLK, F_UNLCK, (offset), (whence), (len))

/*
 * Flags for db_store().
 */
#define DB_INSERT	   1	/* insert new record only */
#define DB_REPLACE	   2	/* replace existing record */
#define DB_STORE	   3	/* replace or insert */
#define DB_DELETE	   4

/*
 * Implementation limits.
 */
//#define IDXLEN_MIN	   6	/* key, sep, start, sep, length, \n */
#define IDXLEN_MAX	1024	/* arbitrary */
//#define DATLEN_MIN	   2	/* data byte, newline */
#define DATLEN_MAX	2048	/* arbitrary */

#define IDX_INVALID	(1<<0)

struct idx_record {
	off_t  idx_nextptr;
	size_t  flags;
	size_t  idx_len;
	size_t  offset;
	size_t  datalen;
	//key
};

#ifdef __cplusplus
}
#endif

#endif /* _APUE_DB_H */

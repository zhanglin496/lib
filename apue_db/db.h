#ifndef _APUE_DB_H
#define _APUE_DB_H

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef	void * DBHANDLE;

#define min_t(type, x, y) ({				\
		type __min1 = (x);                      \
		type __min2 = (y);                      \
		__min1 < __min2 ? __min1 : __min2; })

DBHANDLE db_open(const char *, int, ...);
void db_close(DBHANDLE);
void *db_fetch(DBHANDLE, const void *,  size_t, size_t *);
int db_store(DBHANDLE, const void *, size_t, const void *, size_t, int);
int db_delete(DBHANDLE, const void *, size_t);
void db_rewind(DBHANDLE);
void *db_nextrec(DBHANDLE, void *, size_t, size_t *);
int db_drop(const char *);
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
#define KEYLEN_MAX	1024	/* arbitrary */
#define DATLEN_MAX	2048	/* arbitrary */

#define IDX_INVALID	(1U<<0)

struct idx_record {
	off_t idx_nextptr;	/* the next offset of the index file, 0 means end */
	off_t dataoff;	/* the data offset of the data file, 0 means no data */
	size_t keylen;	/* the key used space */
	size_t keyfree;	/* the key free space */
	size_t datalen;	/* the data used space */
	size_t datafree; /* the data free space */
	uint32_t flags; /* internal use */
	/*	save the key 
	*	char key[0];
	*/
};

#ifdef __cplusplus
}
#endif

#endif /* _APUE_DB_H */

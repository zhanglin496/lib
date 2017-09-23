#include <fcntl.h>		/* open & db_open flags */
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/uio.h>	/* struct iovec */
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>

#include "apue_db.h"
#include "../include/jhash.h"

/* 
 * Internal index file constants.
 * These are used to construct records in the
 * index file and data file.
 */

//index file struct 
/*
*	---------------------------------------------------
*	|   	     |	        |	|       |
*	| hash_bucket| FREE_LIST| hash table... | index record ...
*	|   number   |	        |	|       |	
*/

/*
 * The following definitions are for hash chains and free
 * list chain in the index file.
 */
#define PTR_SZ          sizeof(off_t)	/* size of ptr field in hash chain */
#define NHASH_DEF	256	/* default hash table size */
#define FREE_OFF      	(1 * PTR_SZ)	/* free list offset in index file */
#define HASH_OFF 	(2 * PTR_SZ)	/* hash table offset in index file */

typedef unsigned long DBHASH;	/* hash values */
typedef unsigned int COUNT;	/* unsigned counter */

/*
 * Library's private representation of the database.
 */
typedef struct {
	int    idxfd;  /* fd for index file */
  	int    datfd;  /* fd for data file */
  	char  *keybuf; /* malloc'ed buffer for index record */
  	char  *datbuf; /* malloc'ed buffer for data record*/
  	char  *name;   /* name db was opened under */
  	off_t  idxoff; /* offset in index file of index record */
			      /* key is at (idxoff + PTR_SZ + IDXLEN_SZ) */
  	off_t  curroff; /* offset in index file of index record */
	size_t idxflags;
  	size_t keylen; /* length of index record */
			      /* excludes IDXLEN_SZ bytes at front of record */
			      /* includes newline at end of index record */
  	off_t  datoff; /* offset in data file of data record */
  	size_t datlen; /* length of data record */
			      /* includes newline at end */
  	off_t  ptrval; /* contents of chain ptr in index record */
  	off_t  ptroff; /* chain ptr offset pointing to this idx record */
  	off_t  chainoff; /* offset of hash chain for this index record */
  	off_t  hashoff;  /* offset in index file of hash table */
  	DBHASH nhash;    /* current hash table size */
 	COUNT  cnt_delok;    /* delete OK */
  	COUNT  cnt_delerr;   /* delete error */
  	COUNT  cnt_fetchok;  /* fetch OK */
  	COUNT  cnt_fetcherr; /* fetch error */
  	COUNT  cnt_nextrec;  /* nextrec */
  	COUNT  cnt_stor1;    /* store: DB_INSERT, no empty, appended */
  	COUNT  cnt_stor2;    /* store: DB_INSERT, found empty, reused */
  	COUNT  cnt_stor3;    /* store: DB_REPLACE, diff len, appended */
  	COUNT  cnt_stor4;    /* store: DB_REPLACE, same len, overwrote */
  	COUNT  cnt_storerr;  /* store error */
} DB;

/*
 * Internal functions.
 */
static DB     *_db_alloc(int);
static void    _db_dodelete(DB *);
static int     _db_find_and_lock(DB *, const void *, size_t, int);
static int     _db_findfree(DB *, int, int);
static void    _db_free(DB *);
static DBHASH  _db_hash(DB *, const void *, size_t);
static char   *_db_readdat(DB *);
static off_t   _db_readidx(DB *, off_t);
static off_t   _db_readptr(DB *, off_t);
static void    _db_writedat(DB *, const void *, size_t, off_t, int);
static void    _db_writeidx(DB *, const void *, size_t, off_t, int, off_t, size_t);
static void    _db_writeptr(DB *, off_t, off_t);

int lock_reg(int fd, int cmd, int type, off_t offset, int whence, off_t len)
{
	struct flock    lock;
	
	lock.l_type = type;             /* F_RDLCK, F_WRLCK, F_UNLCK */
	lock.l_start = offset;  /* byte offset, relative to l_whence */
	lock.l_whence = whence; /* SEEK_SET, SEEK_CUR, SEEK_END */
	lock.l_len = len;               /* #bytes (0 means to EOF) */
	
	return(fcntl(fd, cmd, &lock));
}

/*
 * Open or create a database.  Same arguments as open(2).
 */
DBHANDLE db_open(const char *pathname, int oflag, ...)
{
	DB *db;
	int	len, mode;
	off_t hash[NHASH_DEF];	/* +1 for free list */
	struct stat statbuff;

	/*
	 * Allocate a DB structure, and the buffers it needs.
	 */
	len = strlen(pathname);
	if ((db = _db_alloc(len)) == NULL)
		return NULL;

	db->hashoff = HASH_OFF;	/* offset in index file of hash table */
	strcpy(db->name, pathname);
	strcat(db->name, ".idx");

	if (oflag & O_CREAT) {
		va_list ap;

		va_start(ap, oflag);
		mode = va_arg(ap, int);
		va_end(ap);

		/*
		 * Open index file and data file.
		 */
		db->idxfd = open(db->name, oflag, mode);
		strcpy(db->name + len, ".dat");
		db->datfd = open(db->name, oflag, mode);
	} else {
		/*
		 * Open index file and data file.
		 */
		db->idxfd = open(db->name, oflag);
		strcpy(db->name + len, ".dat");
		db->datfd = open(db->name, oflag);
	}

	if (db->idxfd < 0 || db->datfd < 0)
		goto fail;

	if (oflag & O_CREAT) {
		/*
		 * If the database was created, we have to initialize
		 * it.  Write lock the entire file so that we can stat
		 * it, check its size, and initialize it, atomically.
		 */
		writew_lock(db->idxfd, 0, SEEK_SET, 0);

		if (fstat(db->idxfd, &statbuff) < 0) {
			un_lock(db->idxfd, 0, SEEK_SET, 0);
			goto fail;
		}
		if (statbuff.st_size == 0) {
			va_list ap;
			off_t hash_bucket;
                  	va_start(ap, oflag);
                	va_arg(ap, int);
			hash_bucket = (off_t)va_arg(ap, int);
                 	va_end(ap);

			if (hash_bucket < 1 || hash_bucket > 2048) {
				un_lock(db->idxfd, 0, SEEK_SET, 0);
				goto fail;
			}
			memset(hash, 0, sizeof(hash));
			/*
			 * We have to build a list of (NHASH_DEF + 1) chain
			 * ptrs with a value of 0.  The +1 is for the free
			 * list pointer that precedes the hash table.
			 */
			int i, j, k;
			i = hash_bucket / NHASH_DEF;
			j = hash_bucket % NHASH_DEF;
			write(db->idxfd, &hash_bucket, PTR_SZ);	
			write(db->idxfd, hash, PTR_SZ); //free list
			for (k = 0; k < i; k++)
				write(db->idxfd, hash, sizeof(hash));
			if (j > 0)
				write(db->idxfd, hash, j * PTR_SZ);
		}	
		un_lock(db->idxfd, 0, SEEK_SET, 0);
	} 

	lseek(db->idxfd, 0, SEEK_SET);
	readw_lock(db->idxfd, 0, SEEK_SET, 1);
	read(db->idxfd, &db->nhash, PTR_SZ);
	un_lock(db->idxfd, 0, SEEK_SET, 1);
	
	db_rewind(db);
	return db;
fail:	
	_db_free(db);
	return NULL;
}

/*
 * Allocate & initialize a DB structure and its buffers.
 */
static DB *_db_alloc(int namelen)
{
	DB		*db;

	/*
	 * Use calloc, to initialize the structure to zero.
	 */
	if ((db = calloc(1, sizeof(DB))) == NULL)
		return NULL;

	db->idxfd = db->datfd = -1;				/* descriptors */

	/*
	 * Allocate room for the name.
	 * +5 for ".idx" or ".dat" plus null at end.
	 */
	if ((db->name = malloc(namelen + 5)) == NULL)
		goto fail;
	/*
	 * Allocate an index buffer and a data buffer.
	 * +2 for newline and null at end.
	 */
	if ((db->keybuf = malloc(IDXLEN_MAX)) == NULL)
		goto fail;
	if ((db->datbuf = malloc(DATLEN_MAX)) == NULL)
		goto fail;
	return db;
fail:
	_db_free(db);
	return NULL;
}

/*
 * Relinquish access to the database.
 */
void db_close(DBHANDLE h)
{
	_db_free((DB *)h);	/* closes fds, free buffers & struct */
}

/*
 * Free up a DB structure, and all the malloc'ed buffers it
 * may point to.  Also close the file descriptors if still open.
 */
static void _db_free(DB *db)
{
	if (!db)
		return;
	if (db->idxfd >= 0)
		close(db->idxfd);
	if (db->datfd >= 0)
		close(db->datfd);
	if (db->keybuf != NULL)
		free(db->keybuf);
	if (db->datbuf != NULL)
		free(db->datbuf);
	if (db->name != NULL)
		free(db->name);
	free(db);
}

/*
 * Fetch a record.  Return a pointer to the data.
 */
char *db_fetch(DBHANDLE h, const void *key, size_t keylen)
{
	DB      *db = h;
	char	*ptr;

	if (_db_find_and_lock(db, key, keylen, 0) < 0) {
		ptr = NULL;				/* error, record not found */
		db->cnt_fetcherr++;
	} else {
		ptr = _db_readdat(db);	/* return pointer to data */
		db->cnt_fetchok++;
	}

	/*
	 * Unlock the hash chain that _db_find_and_lock locked.
	 */
	un_lock(db->idxfd, db->chainoff, SEEK_SET, 1);
	return ptr;
}

/*
 * Find the specified record.  Called by db_delete, db_fetch,
 * and db_store.  Returns with the hash chain locked.
 */
static int _db_find_and_lock(DB *db, const void *key, size_t keylen, int writelock)
{
	off_t	offset, nextoffset;
	
	/*
	 * Calculate the hash value for this key, then calculate the
	 * byte offset of corresponding chain ptr in hash table.
	 * This is where our search starts.  First we calculate the
	 * offset in the hash table for this key.
	 */

	/*
	*	---------+
	*                +
	*                +
	*/
	db->chainoff = (_db_hash(db, key, keylen) * PTR_SZ) + db->hashoff;
	db->ptroff = db->chainoff;

	/*
	 * We lock the hash chain here.  The caller must unlock it
	 * when done.  Note we lock and unlock only the first byte.
	 */
	if (writelock) {
		writew_lock(db->idxfd, db->chainoff, SEEK_SET, 1);
	} else {
		readw_lock(db->idxfd, db->chainoff, SEEK_SET, 1);
	}

//	int key_len;
	/*
	 * Get the offset in the index file of first record
	 * on the hash chain (can be 0).
	 */
	offset = _db_readptr(db, db->ptroff);
	while (offset > 0) {
		nextoffset = _db_readidx(db, offset);
		if (db->keylen == keylen && !memcmp(db->keybuf, key, db->keylen))
			break;
		db->ptroff = offset; /* offset of this (unequal) record */
		offset = nextoffset; /* next one to compare */
	}
	/*
	 * offset == 0 on error (record not found).
	 */
	return(offset <= 0 ? -1 : 0);
}

/*
 * Calculate the hash value for a key.
 */
static DBHASH _db_hash(DB *db, const void *key, size_t keylen)
{
//	DBHASH	hval = 0;
//	const char *c = (const char *)key;
//	int	i;
//
	return jhash(key, keylen, 0) & (db->nhash - 1);
//	for (i = 0; i < keylen; i++)
//		hval += *c++ * i;		/* ascii char times its 1-based index */
//	return(hval % db->nhash);
}

/*
 * Read a chain ptr field from anywhere in the index file:
 * the free list pointer, a hash table chain ptr, or an
 * index record chain ptr.
 */
static off_t _db_readptr(DB *db, off_t offset)
{
	off_t ptr;

	if (lseek(db->idxfd, offset, SEEK_SET) == -1)
		return -1;
	if (read(db->idxfd, &ptr, PTR_SZ) != PTR_SZ) {
		return -1;
	}
	return ptr;
}


/*
 * Read the next index record.  We start at the specified offset
 * in the index file.  We read the index record into db->idxbuf
 * and replace the separators with null bytes.  If all is OK we
 * set db->datoff and db->datlen to the offset and length of the
 * corresponding data record in the data file.
 */
static off_t _db_readidx(DB *db, off_t offset)
{
	ssize_t	i;
	struct idx_record idx;
	/*
	 * Position index file and record the offset.  db_nextrec
	 * calls us with offset==0, meaning read from current offset.
	 * We still need to call lseek to record the current offset.
	 */
	db->idxoff = lseek(db->idxfd, offset,
		  offset == 0 ? SEEK_CUR : SEEK_SET);
	if (db->idxoff == -1)
		return -1;
	if ((i = read(db->idxfd, &idx, sizeof(idx))) != sizeof(idx))
		return -1;
	
	db->idxflags = idx.flags;
	db->keylen = idx.idx_len - sizeof(idx);
	db->datoff = idx.offset;
	db->datlen = idx.datalen;
	if ((i = read(db->idxfd, db->keybuf, db->keylen)) != db->keylen)
		return -1;

	//get current offset
	db->curroff = lseek(db->idxfd, 0, SEEK_CUR);

	return db->ptrval = idx.idx_nextptr;
}

/*
 * Read the current data record into the data buffer.
 * Return a pointer to the null-terminated data buffer.
 */
static char *_db_readdat(DB *db)
{
	if (lseek(db->datfd, db->datoff, SEEK_SET) == -1)
		return NULL;
	if (read(db->datfd, db->datbuf, db->datlen) != db->datlen) {
		return NULL;
	}
	return db->datbuf;		/* return pointer to data record */
}

/*
 * Delete the specified record.
 */
int db_delete(DBHANDLE h, const void *key, size_t keylen)
{
	DB  *db = h;
	int rc = 0;			/* assume record will be found */

	if (_db_find_and_lock(db, key, keylen, 1) == 0) {
		_db_dodelete(db);
		db->cnt_delok++;
	} else {
		rc = -1;			/* not found */
		db->cnt_delerr++;
	}
	un_lock(db->idxfd, db->chainoff, SEEK_SET, 1);
	
	return rc;
}

/*
 * Delete the current record specified by the DB structure.
 * This function is called by db_delete and db_store, after
 * the record has been located by _db_find_and_lock.
 */
static void _db_dodelete(DB *db)
{
	off_t	freeptr, saveptr;

	/*
	 * Set data buffer and key to all blanks.
	 */
//	for (ptr = db->datbuf, i = 0; i < db->datlen - 1; i++)
//		*ptr++ = SPACE;
//	*ptr = 0;	/* null terminate for _db_writedat */
//	ptr = db->keybuf;
//	while (*ptr)
//		*ptr++ = SPACE;

	/*
	 * We have to lock the free list.
	 */
	writew_lock(db->idxfd, FREE_OFF, SEEK_SET, 1);

	/*
	  Write the data record with all blanks.
	 */
//	_db_writedat(db, db->datbuf, db->datlen, db->datoff, SEEK_SET);

	/*
	 * Read the free list pointer.  Its value becomes the
	 * chain ptr field of the deleted index record.  This means
	 * the deleted record becomes the head of the free list.
	 */
	freeptr = _db_readptr(db, FREE_OFF);

	/*
	 * Save the contents of index record chain ptr,
	 * before it's rewritten by _db_writeidx.
	 */
	saveptr = db->ptrval;

	/*
	 * Rewrite the index record.  This also rewrites the length
	 * of the index record, the data offset, and the data length,
	 * none of which has changed, but that's OK.
	 */
	_db_writeidx(db, db->keybuf, db->keylen, db->idxoff, SEEK_SET, freeptr, IDX_INVALID);

	/*
	 * Write the new free list pointer.
	 */
	_db_writeptr(db, FREE_OFF, db->idxoff);

	/*
	 * Rewrite the chain ptr that pointed to this record being
	 * deleted.  Recall that _db_find_and_lock sets db->ptroff to
	 * point to this chain ptr.  We set this chain ptr to the
	 * contents of the deleted record's chain ptr, saveptr.
	 */
	_db_writeptr(db, db->ptroff, saveptr);
	un_lock(db->idxfd, FREE_OFF, SEEK_SET, 1);
}

/*
 * Write a data record.  Called by _db_dodelete (to write
 * the record with blanks) and db_store.
 */
static void _db_writedat(DB *db, const void *data, size_t datlen, off_t offset, int whence)
{
	/*
	 * If we're appending, we have to lock before doing the lseek
	 * and write to make the two an atomic operation.  If we're
	 * overwriting an existing record, we don't have to lock.
	 */
	if (whence == SEEK_END) /* we're appending, lock entire file */
		writew_lock(db->datfd, 0, SEEK_SET, 0);
	if ((db->datoff = lseek(db->datfd, offset, whence)) == -1)
		goto out;

	write(db->datfd, data, datlen);
	db->datlen = datlen;

out:
	if (whence == SEEK_END)
		un_lock(db->datfd, 0, SEEK_SET, 0);
}

/*
 * Write an index record.  _db_writedat is called before
 * this function to set the datoff and datlen fields in the
 * DB structure, which we need to write the index record.
 */
static void _db_writeidx(DB *db, const void *key, size_t keylen,
             off_t offset, int whence, off_t ptrval, size_t flags)
{
	struct iovec	iov[2];
	struct idx_record idx;
	/*
	 * If we're appending, we have to lock before doing the lseek
	 * and write to make the two an atomic operation.  If we're
	 * overwriting an existing record, we don't have to lock.
	 */
	if (whence == SEEK_END)		/* we're appending */
		writew_lock(db->idxfd, ((db->nhash + 2) * PTR_SZ),
			  SEEK_SET, 0);
	idx.flags = flags;
	idx.idx_nextptr = ptrval;
	idx.idx_len = keylen + sizeof(idx);
	idx.offset = db->datoff;
	idx.datalen = db->datlen;

	/*
	 * Position the index file and record the offset.
	 */
	db->idxoff = lseek(db->idxfd, offset, whence);
	if (db->idxoff == -1)
		goto out;

	iov[0].iov_base = &idx;
	iov[0].iov_len  = sizeof(idx);
	iov[1].iov_base = (void *)key;
	iov[1].iov_len  = keylen;
	if (writev(db->idxfd, &iov[0], 2) != sizeof(idx) + keylen)
			;
out:
	if (whence == SEEK_END)
		un_lock(db->idxfd, ((db->nhash + 2) * PTR_SZ),
			  SEEK_SET, 0);
}

/*
 * Write a chain ptr field somewhere in the index file:
 * the free list, the hash table, or in an index record.
 */
static void _db_writeptr(DB *db, off_t offset, off_t ptrval)
{
	if (lseek(db->idxfd, offset, SEEK_SET) == -1)
		return;
	if (write(db->idxfd, &ptrval, sizeof(ptrval)) != sizeof(ptrval))
		return;
}

/*
 * Store a record in the database.  Return 0 if OK, 1 if record
 * exists and DB_INSERT specified, -1 on error.
 */
int db_store(DBHANDLE h, const void *key, size_t keylen, 
		const void *data, size_t datlen, int flag)
{
	DB		*db = h;
	int		rc;
	off_t	ptrval;

	if (flag != DB_INSERT && flag != DB_REPLACE &&
	  flag != DB_STORE) {
		errno = EINVAL;
		return -1;
	}
	if (datlen > DATLEN_MAX) {
		return -1;
	}

	/*
	 * _db_find_and_lock calculates which hash table this new record
	 * goes into (db->chainoff), regardless of whether it already
	 * exists or not. The following calls to _db_writeptr change the
	 * hash table entry for this chain to point to the new record.
	 * The new record is added to the front of the hash chain.
	 */
	if (_db_find_and_lock(db, key, keylen, 1) < 0) { /* record not found */
		if (flag == DB_REPLACE) {
			rc = -1;
			db->cnt_storerr++;
			errno = ENOENT;		/* error, record does not exist */
			goto doreturn;
		}

		/*
		 * _db_find_and_lock locked the hash chain for us; read
		 * the chain ptr to the first index record on hash chain.
		 */
		ptrval = _db_readptr(db, db->chainoff);

		if (_db_findfree(db, keylen, datlen) < 0) {
			/*
			 * Can't find an empty record big enough. Append the
			 * new record to the ends of the index and data files.
			 */
			_db_writedat(db, data, datlen, 0, SEEK_END);
			_db_writeidx(db, key, keylen, 0, SEEK_END, ptrval, 0);

			/*
			 * db->idxoff was set by _db_writeidx.  The new
			 * record goes to the front of the hash chain.
			 */
			_db_writeptr(db, db->chainoff, db->idxoff);
			db->cnt_stor1++;
		} else {
			/*
			 * Reuse an empty record. _db_findfree removed it from
			 * the free list and set both db->datoff and db->idxoff.
			 * Reused record goes to the front of the hash chain.
			 */
			_db_writedat(db, data, datlen, db->datoff, SEEK_SET);
			_db_writeidx(db, key, keylen, db->idxoff, SEEK_SET, ptrval, 0);
			_db_writeptr(db, db->chainoff, db->idxoff);
			db->cnt_stor2++;
		}
	} else {						/* record found */
		if (flag == DB_INSERT) {
			rc = 1;		/* error, record already in db */
			db->cnt_storerr++;
			goto doreturn;
		}

		/*
		 * We are replacing an existing record.  We know the new
		 * key equals the existing key, but we need to check if
		 * the data records are the same size.
		 */
		if (datlen != db->datlen) {
			_db_dodelete(db);	/* delete the existing record */

			/*
			 * Reread the chain ptr in the hash table
			 * (it may change with the deletion).
			 */
			ptrval = _db_readptr(db, db->chainoff);

			/*
			 * Append new index and data records to end of files.
			 */
			_db_writedat(db, data, datlen, 0, SEEK_END);
			_db_writeidx(db, key, keylen, 0, SEEK_END, ptrval, 0);

			/*
			 * New record goes to the front of the hash chain.
			 */
			_db_writeptr(db, db->chainoff, db->idxoff);
			db->cnt_stor3++;
		} else {
			/*
			 * Same size data, just replace data record.
			 */
			_db_writedat(db, data, datlen, db->datoff, SEEK_SET);
			db->cnt_stor4++;
		}
	}
	rc = 0;		/* OK */

doreturn:	/* unlock hash chain locked by _db_find_and_lock */
	un_lock(db->idxfd, db->chainoff, SEEK_SET, 1);
	return rc;
}

/*
 * Try to find a free index record and accompanying data record
 * of the correct sizes.  We're only called by db_store.
 */
static int _db_findfree(DB *db, int keylen, int datlen)
{
	int		rc;
	off_t	offset, nextoffset, saveoffset;

	/*
	 * Lock the free list.
	 */
	writew_lock(db->idxfd, FREE_OFF, SEEK_SET, 1);

	/*
	 * Read the free list pointer.
	 */
	saveoffset = FREE_OFF;
	offset = _db_readptr(db, saveoffset);

	while (offset > 0) {
		nextoffset = _db_readidx(db, offset);
		if (db->keylen == keylen && db->datlen == datlen)
			break;		/* found a match */
		saveoffset = offset;
		offset = nextoffset;
	}

	if (offset == 0) {
		rc = -1;	/* no match found */
	} else if (offset > 0) {
		/*
		 * Found a free record with matching sizes.
		 * The index record was read in by _db_readidx above,
		 * which sets db->ptrval.  Also, saveoffset points to
		 * the chain ptr that pointed to this empty record on
		 * the free list.  We set this chain ptr to db->ptrval,
		 * which removes the empty record from the free list.
		 */
		_db_writeptr(db, saveoffset, db->ptrval);
		rc = 0;

		/*
		 * Notice also that _db_readidx set both db->idxoff
		 * and db->datoff.  This is used by the caller, db_store,
		 * to write the new index record and data record.
		 */
	} else
		rc = -1;

	/*
	 * Unlock the free list.
	 */
	un_lock(db->idxfd, FREE_OFF, SEEK_SET, 1);

	return rc;
}

/*
 * Rewind the index file for db_nextrec.
 * Automatically called by db_open.
 * Must be called before first db_nextrec.
 */
void db_rewind(DBHANDLE h)
{
	DB	*db = h;
	off_t	offset;

	offset = (db->nhash + 2) * PTR_SZ;	/* +1 for free list ptr */

	/*
	 * We're just setting the file offset for this process
	 * to the start of the index records; no need to lock.
	 * +1 below for newline at end of hash table.
	 */
	db->idxoff = lseek(db->idxfd, offset, SEEK_SET);
	db->curroff = db->idxoff;
}

/*
 * Return the next sequential record.
 * We just step our way through the index file, ignoring deleted
 * records.  db_rewind must be called before this function is
 * called the first time.
 */
char *db_nextrec(DBHANDLE h, void *key)
{
	DB 	*db = h;
	char 	*ptr;

	/*
	 * We read lock the free list so that we don't read
	 * a record in the middle of its being deleted.
	 */
	readw_lock(db->idxfd, FREE_OFF, SEEK_SET, 1);

	lseek(db->idxfd, db->curroff, SEEK_SET);

	while (1) {
//	do {
		/*
		 * Read next sequential index record.
		 */
		if (_db_readidx(db, 0) < 0) {
			ptr = NULL;		/* end of index file, EOF */
			goto doreturn;
		}
		if (db->idxflags & IDX_INVALID)
			continue;
		break;
	}
		/*
		 * Check if key is all blank (empty record).
		 */
//		ptr = db->keybuf;
//		while ((c = *ptr++) != 0  &&  c == SPACE)
//			;	/* skip until null byte or nonblank */
//	} while (c == 0);	/* loop until a nonblank key is found */

	if (key != NULL) {
		memcpy(key, db->keybuf, db->keylen);	/* return key */
//		key[db->keylen] = '\0';
	}
	ptr = _db_readdat(db);	/* return pointer to data buffer */
	db->cnt_nextrec++;
doreturn:
	un_lock(db->idxfd, FREE_OFF, SEEK_SET, 1);

	return ptr;
}

int db_fsync(DBHANDLE h)
{
	DB *db = (DB *)h;
	if (db) {
		fsync(db->datfd);
		fsync(db->idxfd);
	}
	return 0;
}

int db_drop(const char *name)
{
	char buf[1024];
	int len = strlen(name);

	if (len > sizeof(buf) - 5)
		return -1;
	//we need not have lock
	strcpy(buf, name);

	strcpy(buf + len, ".idx");
	unlink(buf);

	strcpy(buf + len, ".dat");
	unlink(buf);
	return 0;
}


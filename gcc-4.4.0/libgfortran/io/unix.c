/* Copyright (C) 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009
   Free Software Foundation, Inc.
   Contributed by Andy Vaught
   F2003 I/O support contributed by Jerry DeLisle

This file is part of the GNU Fortran 95 runtime library (libgfortran).

Libgfortran is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

Libgfortran is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

/* Unix stream I/O module */

#include "io.h"
#include <stdlib.h>
#include <limits.h>

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include <string.h>
#include <errno.h>


/* For mingw, we don't identify files by their inode number, but by a
   64-bit identifier created from a BY_HANDLE_FILE_INFORMATION. */
#if defined(__MINGW32__) && !HAVE_WORKING_STAT

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static uint64_t
id_from_handle (HANDLE hFile)
{
  BY_HANDLE_FILE_INFORMATION FileInformation;

  if (hFile == INVALID_HANDLE_VALUE)
      return 0;

  memset (&FileInformation, 0, sizeof(FileInformation));
  if (!GetFileInformationByHandle (hFile, &FileInformation))
    return 0;

  return ((uint64_t) FileInformation.nFileIndexLow)
	 | (((uint64_t) FileInformation.nFileIndexHigh) << 32);
}


static uint64_t
id_from_path (const char *path)
{
  HANDLE hFile;
  uint64_t res;

  if (!path || !*path || access (path, F_OK))
    return (uint64_t) -1;

  hFile = CreateFile (path, 0, 0, NULL, OPEN_EXISTING,
		      FILE_FLAG_BACKUP_SEMANTICS | FILE_ATTRIBUTE_READONLY,
		      NULL);
  res = id_from_handle (hFile);
  CloseHandle (hFile);
  return res;
}


static uint64_t
id_from_fd (const int fd)
{
  return id_from_handle ((HANDLE) _get_osfhandle (fd));
}

#endif

#ifndef SSIZE_MAX
#define SSIZE_MAX SHRT_MAX
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifndef PROT_READ
#define PROT_READ 1
#endif

#ifndef PROT_WRITE
#define PROT_WRITE 2
#endif

/* These flags aren't defined on all targets (mingw32), so provide them
   here.  */
#ifndef S_IRGRP
#define S_IRGRP 0
#endif

#ifndef S_IWGRP
#define S_IWGRP 0
#endif

#ifndef S_IROTH
#define S_IROTH 0
#endif

#ifndef S_IWOTH
#define S_IWOTH 0
#endif


/* Unix stream I/O module */

#define BUFFER_SIZE 8192

typedef struct
{
  stream st;

  int fd;
  gfc_offset buffer_offset;	/* File offset of the start of the buffer */
  gfc_offset physical_offset;	/* Current physical file offset */
  gfc_offset logical_offset;	/* Current logical file offset */
  gfc_offset dirty_offset;	/* Start of modified bytes in buffer */
  gfc_offset file_length;	/* Length of the file, -1 if not seekable. */

  int len;			/* Physical length of the current buffer */
  int active;			/* Length of valid bytes in the buffer */

  int prot;
  int ndirty;			/* Dirty bytes starting at dirty_offset */

  int special_file;		/* =1 if the fd refers to a special file */

  io_mode method;		/* Method of stream I/O being used */

  char *buffer;
  char small_buffer[BUFFER_SIZE];
}
unix_stream;


/* Stream structure for internal files. Fields must be kept in sync
   with unix_stream above, except for the buffer. For internal files
   we point the buffer pointer directly at the destination memory.  */

typedef struct
{
  stream st;

  int fd;
  gfc_offset buffer_offset;	/* File offset of the start of the buffer */
  gfc_offset physical_offset;	/* Current physical file offset */
  gfc_offset logical_offset;	/* Current logical file offset */
  gfc_offset dirty_offset;	/* Start of modified bytes in buffer */
  gfc_offset file_length;	/* Length of the file, -1 if not seekable. */

  int len;			/* Physical length of the current buffer */
  int active;			/* Length of valid bytes in the buffer */

  int prot;
  int ndirty;			/* Dirty bytes starting at dirty_offset */

  int special_file;		/* =1 if the fd refers to a special file */

  io_mode method;		/* Method of stream I/O being used */

  char *buffer;
}
int_stream;

/* This implementation of stream I/O is based on the paper:
 *
 *  "Exploiting the advantages of mapped files for stream I/O",
 *  O. Krieger, M. Stumm and R. Umrau, "Proceedings of the 1992 Winter
 *  USENIX conference", p. 27-42.
 *
 * It differs in a number of ways from the version described in the
 * paper.  First of all, threads are not an issue during I/O and we
 * also don't have to worry about having multiple regions, since
 * fortran's I/O model only allows you to be one place at a time.
 *
 * On the other hand, we have to be able to writing at the end of a
 * stream, read from the start of a stream or read and write blocks of
 * bytes from an arbitrary position.  After opening a file, a pointer
 * to a stream structure is returned, which is used to handle file
 * accesses until the file is closed.
 *
 * salloc_at_r(stream, len, where)-- Given a stream pointer, return a
 * pointer to a block of memory that mirror the file at position
 * 'where' that is 'len' bytes long.  The len integer is updated to
 * reflect how many bytes were actually read.  The only reason for a
 * short read is end of file.  The file pointer is updated.  The
 * pointer is valid until the next call to salloc_*.
 *
 * salloc_at_w(stream, len, where)-- Given the stream pointer, returns
 * a pointer to a block of memory that is updated to reflect the state
 * of the file.  The length of the buffer is always equal to that
 * requested.  The buffer must be completely set by the caller.  When
 * data has been written, the sfree() function must be called to
 * indicate that the caller is done writing data to the buffer.  This
 * may or may not cause a physical write.
 *
 * Short forms of these are salloc_r() and salloc_w() which drop the
 * 'where' parameter and use the current file pointer. */


/*move_pos_offset()--  Move the record pointer right or left
 *relative to current position */

int
move_pos_offset (stream* st, int pos_off)
{
  unix_stream * str = (unix_stream*)st;
  if (pos_off < 0)
    {
      str->logical_offset += pos_off;

      if (str->dirty_offset + str->ndirty > str->logical_offset)
	{
	  if (str->ndirty + pos_off > 0)
	    str->ndirty += pos_off;
	  else
	    {
	      str->dirty_offset +=  pos_off + pos_off;
	      str->ndirty = 0;
	    }
	}

    return pos_off;
  }
  return 0;
}


/* fix_fd()-- Given a file descriptor, make sure it is not one of the
 * standard descriptors, returning a non-standard descriptor.  If the
 * user specifies that system errors should go to standard output,
 * then closes standard output, we don't want the system errors to a
 * file that has been given file descriptor 1 or 0.  We want to send
 * the error to the invalid descriptor. */

static int
fix_fd (int fd)
{
#ifdef HAVE_DUP
  int input, output, error;

  input = output = error = 0;

  /* Unix allocates the lowest descriptors first, so a loop is not
     required, but this order is. */
  if (fd == STDIN_FILENO)
    {
      fd = dup (fd);
      input = 1;
    }
  if (fd == STDOUT_FILENO)
    {
      fd = dup (fd);
      output = 1;
    }
  if (fd == STDERR_FILENO)
    {
      fd = dup (fd);
      error = 1;
    }

  if (input)
    close (STDIN_FILENO);
  if (output)
    close (STDOUT_FILENO);
  if (error)
    close (STDERR_FILENO);
#endif

  return fd;
}

int
is_preconnected (stream * s)
{
  int fd;

  fd = ((unix_stream *) s)->fd;
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
    return 1;
  else
    return 0;
}

/* If the stream corresponds to a preconnected unit, we flush the
   corresponding C stream.  This is bugware for mixed C-Fortran codes
   where the C code doesn't flush I/O before returning.  */
void
flush_if_preconnected (stream * s)
{
  int fd;

  fd = ((unix_stream *) s)->fd;
  if (fd == STDIN_FILENO)
    fflush (stdin);
  else if (fd == STDOUT_FILENO)
    fflush (stdout);
  else if (fd == STDERR_FILENO)
    fflush (stderr);
}


/* Reset a stream after reading/writing. Assumes that the buffers have
   been flushed.  */

inline static void
reset_stream (unix_stream * s, size_t bytes_rw)
{
  s->physical_offset += bytes_rw;
  s->logical_offset = s->physical_offset;
  if (s->file_length != -1 && s->physical_offset > s->file_length)
    s->file_length = s->physical_offset;
}


/* Read bytes into a buffer, allowing for short reads.  If the nbytes
 * argument is less on return than on entry, it is because we've hit
 * the end of file. */

static int
do_read (unix_stream * s, void * buf, size_t * nbytes)
{
  ssize_t trans;
  size_t bytes_left;
  char *buf_st;
  int status;

  status = 0;
  bytes_left = *nbytes;
  buf_st = (char *) buf;

  /* We must read in a loop since some systems don't restart system
     calls in case of a signal.  */
  while (bytes_left > 0)
    {
      /* Requests between SSIZE_MAX and SIZE_MAX are undefined by SUSv3,
	 so we must read in chunks smaller than SSIZE_MAX.  */
      trans = (bytes_left < SSIZE_MAX) ? bytes_left : SSIZE_MAX;
      trans = read (s->fd, buf_st, trans);
      if (trans < 0)
	{
	  if (errno == EINTR)
	    continue;
	  else
	    {
	      status = errno;
	      break;
	    }
	}
      else if (trans == 0) /* We hit EOF.  */
	break;
      buf_st += trans;
      bytes_left -= trans;
    }

  *nbytes -= bytes_left;
  return status;
}


/* Write a buffer to a stream, allowing for short writes.  */

static int
do_write (unix_stream * s, const void * buf, size_t * nbytes)
{
  ssize_t trans;
  size_t bytes_left;
  char *buf_st;
  int status;

  status = 0;
  bytes_left = *nbytes;
  buf_st = (char *) buf;

  /* We must write in a loop since some systems don't restart system
     calls in case of a signal.  */
  while (bytes_left > 0)
    {
      /* Requests between SSIZE_MAX and SIZE_MAX are undefined by SUSv3,
	 so we must write in chunks smaller than SSIZE_MAX.  */
      trans = (bytes_left < SSIZE_MAX) ? bytes_left : SSIZE_MAX;
      trans = write (s->fd, buf_st, trans);
      if (trans < 0)
	{
	  if (errno == EINTR)
	    continue;
	  else
	    {
	      status = errno;
	      break;
	    }
	}
      buf_st += trans;
      bytes_left -= trans;
    }

  *nbytes -= bytes_left;
  return status;
}


/* get_oserror()-- Get the most recent operating system error.  For
 * unix, this is errno. */

const char *
get_oserror (void)
{
  return strerror (errno);
}


/*********************************************************************
    File descriptor stream functions
*********************************************************************/


/* fd_flush()-- Write bytes that need to be written */

static try
fd_flush (unix_stream * s)
{
  size_t writelen;

  if (s->ndirty == 0)
    return SUCCESS;
  
  if (s->file_length != -1 && s->physical_offset != s->dirty_offset &&
      lseek (s->fd, s->dirty_offset, SEEK_SET) < 0)
    return FAILURE;

  writelen = s->ndirty;
  if (do_write (s, s->buffer + (s->dirty_offset - s->buffer_offset),
		&writelen) != 0)
    return FAILURE;

  s->physical_offset = s->dirty_offset + writelen;

  /* don't increment file_length if the file is non-seekable */
  if (s->file_length != -1 && s->physical_offset > s->file_length)
      s->file_length = s->physical_offset; 

  s->ndirty -= writelen;
  if (s->ndirty != 0)
    return FAILURE;

  return SUCCESS;
}


/* fd_alloc()-- Arrange a buffer such that the salloc() request can be
 * satisfied.  This subroutine gets the buffer ready for whatever is
 * to come next. */

static void
fd_alloc (unix_stream * s, gfc_offset where,
	  int *len __attribute__ ((unused)))
{
  char *new_buffer;
  int n, read_len;

  if (*len <= BUFFER_SIZE)
    {
      new_buffer = s->small_buffer;
      read_len = BUFFER_SIZE;
    }
  else
    {
      new_buffer = get_mem (*len);
      read_len = *len;
    }

  /* Salvage bytes currently within the buffer.  This is important for
   * devices that cannot seek. */

  if (s->buffer != NULL && s->buffer_offset <= where &&
      where <= s->buffer_offset + s->active)
    {

      n = s->active - (where - s->buffer_offset);
      memmove (new_buffer, s->buffer + (where - s->buffer_offset), n);

      s->active = n;
    }
  else
    {				/* new buffer starts off empty */
      s->active = 0;
    }

  s->buffer_offset = where;

  /* free the old buffer if necessary */

  if (s->buffer != NULL && s->buffer != s->small_buffer)
    free_mem (s->buffer);

  s->buffer = new_buffer;
  s->len = read_len;
}


/* fd_alloc_r_at()-- Allocate a stream buffer for reading.  Either
 * we've already buffered the data or we need to load it.  Returns
 * NULL on I/O error. */

static char *
fd_alloc_r_at (unix_stream * s, int *len)
{
  gfc_offset m;
  gfc_offset where = s->logical_offset;

  if (s->buffer != NULL && s->buffer_offset <= where &&
      where + *len <= s->buffer_offset + s->active)
    {

      /* Return a position within the current buffer */

      s->logical_offset = where + *len;
      return s->buffer + where - s->buffer_offset;
    }

  fd_alloc (s, where, len);

  m = where + s->active;

  if (s->physical_offset != m && lseek (s->fd, m, SEEK_SET) < 0)
    return NULL;

  /* do_read() hangs on read from terminals for *BSD-systems.  Only
     use read() in that case.  */

  if (s->special_file)
    {
      ssize_t n;

      n = read (s->fd, s->buffer + s->active, s->len - s->active);
      if (n < 0)
	return NULL;

      s->physical_offset = m + n;
      s->active += n;
    }
  else
    {
      size_t n;

      n = s->len - s->active;
      if (do_read (s, s->buffer + s->active, &n) != 0)
	return NULL;

      s->physical_offset = m + n;
      s->active += n;
    }

  if (s->active < *len)
    *len = s->active;		/* Bytes actually available */

  s->logical_offset = where + *len;

  return s->buffer;
}


/* fd_alloc_w_at()-- Allocate a stream buffer for writing.  Either
 * we've already buffered the data or we need to load it. */

static char *
fd_alloc_w_at (unix_stream * s, int *len)
{
  gfc_offset n;
  gfc_offset where = s->logical_offset;

  if (s->buffer == NULL || s->buffer_offset > where ||
      where + *len > s->buffer_offset + s->len)
    {

      if (fd_flush (s) == FAILURE)
	return NULL;
      fd_alloc (s, where, len);
    }

  /* Return a position within the current buffer */
  if (s->ndirty == 0 
      || where > s->dirty_offset + s->ndirty    
      || s->dirty_offset > where + *len)
    {  /* Discontiguous blocks, start with a clean buffer.  */  
	/* Flush the buffer.  */  
      if (s->ndirty != 0)    
	fd_flush (s);  
      s->dirty_offset = where;  
      s->ndirty = *len;
    }
  else
    {  
      gfc_offset start;  /* Merge with the existing data.  */  
      if (where < s->dirty_offset)    
	start = where;  
      else    
	start = s->dirty_offset;  
      if (where + *len > s->dirty_offset + s->ndirty)    
	s->ndirty = where + *len - start;  
      else    
	s->ndirty = s->dirty_offset + s->ndirty - start;  
      s->dirty_offset = start;
    }

  s->logical_offset = where + *len;

  /* Don't increment file_length if the file is non-seekable.  */

  if (s->file_length != -1 && s->logical_offset > s->file_length)
     s->file_length = s->logical_offset;

  n = s->logical_offset - s->buffer_offset;
  if (n > s->active)
    s->active = n;

  return s->buffer + where - s->buffer_offset;
}


static try
fd_sfree (unix_stream * s)
{
  if (s->ndirty != 0 &&
      (s->buffer != s->small_buffer || options.all_unbuffered ||
       s->method == SYNC_UNBUFFERED))
    return fd_flush (s);

  return SUCCESS;
}


static try
fd_seek (unix_stream * s, gfc_offset offset)
{

  if (s->file_length == -1)
    return SUCCESS;

  if (s->physical_offset == offset) /* Are we lucky and avoid syscall?  */
    {
      s->logical_offset = offset;
      return SUCCESS;
    }

  if (lseek (s->fd, offset, SEEK_SET) >= 0)
    {
      s->physical_offset = s->logical_offset = offset;
      s->active = 0;
      return SUCCESS;
    }

  return FAILURE;
}


/* truncate_file()-- Given a unit, truncate the file at the current
 * position.  Sets the physical location to the new end of the file.
 * Returns nonzero on error. */

static try
fd_truncate (unix_stream * s)
{
  /* Non-seekable files, like terminals and fifo's fail the lseek so just
     return success, there is nothing to truncate.  If its not a pipe there
     is a real problem.  */
  if (lseek (s->fd, s->logical_offset, SEEK_SET) == -1)
    {
      if (errno == ESPIPE)
	return SUCCESS;
      else
	return FAILURE;
    }

  /* Using ftruncate on a seekable special file (like /dev/null)
     is undefined, so we treat it as if the ftruncate succeeded.  */
  if (!s->special_file
      && (
#ifdef HAVE_FTRUNCATE
	  ftruncate (s->fd, s->logical_offset) != 0
#elif defined HAVE_CHSIZE
	  chsize (s->fd, s->logical_offset) != 0
#else
	  /* If we have neither, always fail and exit, noisily.  */
	  runtime_error ("required ftruncate or chsize support not present"), 1
#endif
	  ))
    {
      /* The truncation failed and we need to handle this gracefully.
	 The file length remains the same, but the file-descriptor
	 offset needs adjustment per the successful lseek above.
	 (Similarly, the contents of the buffer isn't valid anymore.)
	 A ftruncate call does not affect the physical (file-descriptor)
	 offset, according to the ftruncate manual, so neither should a
	 failed call.  */
      s->physical_offset = s->logical_offset;
      s->active = 0;
      return FAILURE;
    }

  s->physical_offset = s->file_length = s->logical_offset;
  s->active = 0;
  return SUCCESS;
}


/* Similar to memset(), but operating on a stream instead of a string.
   Takes care of not using too much memory.  */

static try
fd_sset (unix_stream * s, int c, size_t n)
{
  size_t bytes_left;
  int trans;
  void *p;

  bytes_left = n;

  while (bytes_left > 0)
    {
      /* memset() in chunks of BUFFER_SIZE.  */
      trans = (bytes_left < BUFFER_SIZE) ? bytes_left : BUFFER_SIZE;

      p = fd_alloc_w_at (s, &trans);
      if (p)
	  memset (p, c, trans);
      else
	return FAILURE;

      bytes_left -= trans;
    }

  return SUCCESS;
}


/* Stream read function. Avoids using a buffer for big reads. The
   interface is like POSIX read(), but the nbytes argument is a
   pointer; on return it contains the number of bytes written. The
   function return value is the status indicator (0 for success).  */

static int
fd_read (unix_stream * s, void * buf, size_t * nbytes)
{
  void *p;
  int tmp, status;

  if (*nbytes < BUFFER_SIZE && s->method == SYNC_BUFFERED)
    {
      tmp = *nbytes;
      p = fd_alloc_r_at (s, &tmp);
      if (p)
	{
	  *nbytes = tmp;
	  memcpy (buf, p, *nbytes);
	  return 0;
	}
      else
	{
	  *nbytes = 0;
	  return errno;
	}
    }

  /* If the request is bigger than BUFFER_SIZE we flush the buffers
     and read directly.  */
  if (fd_flush (s) == FAILURE)
    {
      *nbytes = 0;
      return errno;
    }

  if (is_seekable ((stream *) s) && fd_seek (s, s->logical_offset) == FAILURE)
    {
      *nbytes = 0;
      return errno;
    }

  status = do_read (s, buf, nbytes);
  reset_stream (s, *nbytes);
  return status;
}


/* Stream write function. Avoids using a buffer for big writes. The
   interface is like POSIX write(), but the nbytes argument is a
   pointer; on return it contains the number of bytes written. The
   function return value is the status indicator (0 for success).  */

static int
fd_write (unix_stream * s, const void * buf, size_t * nbytes)
{
  void *p;
  int tmp, status;

  if (*nbytes < BUFFER_SIZE && s->method == SYNC_BUFFERED)
    {
      tmp = *nbytes;
      p = fd_alloc_w_at (s, &tmp);
      if (p)
	{
	  *nbytes = tmp;
	  memcpy (p, buf, *nbytes);
	  return 0;
	}
      else
	{
	  *nbytes = 0;
	  return errno;
	}
    }

  /* If the request is bigger than BUFFER_SIZE we flush the buffers
     and write directly.  */
  if (fd_flush (s) == FAILURE)
    {
      *nbytes = 0;
      return errno;
    }

  if (is_seekable ((stream *) s) && fd_seek (s, s->logical_offset) == FAILURE)
    {
      *nbytes = 0;
      return errno;
    }

  status =  do_write (s, buf, nbytes);
  reset_stream (s, *nbytes);
  return status;
}


static try
fd_close (unix_stream * s)
{
  if (fd_flush (s) == FAILURE)
    return FAILURE;

  if (s->buffer != NULL && s->buffer != s->small_buffer)
    free_mem (s->buffer);

  if (s->fd != STDOUT_FILENO && s->fd != STDERR_FILENO && s->fd != STDIN_FILENO)
    {
      if (close (s->fd) < 0)
	return FAILURE;
    }

  free_mem (s);

  return SUCCESS;
}


static void
fd_open (unix_stream * s)
{
  if (isatty (s->fd))
    s->method = SYNC_UNBUFFERED;
  else
    s->method = SYNC_BUFFERED;

  s->st.alloc_w_at = (void *) fd_alloc_w_at;
  s->st.sfree = (void *) fd_sfree;
  s->st.close = (void *) fd_close;
  s->st.seek = (void *) fd_seek;
  s->st.trunc = (void *) fd_truncate;
  s->st.read = (void *) fd_read;
  s->st.write = (void *) fd_write;
  s->st.set = (void *) fd_sset;

  s->buffer = NULL;
}




/*********************************************************************
  memory stream functions - These are used for internal files

  The idea here is that a single stream structure is created and all
  requests must be satisfied from it.  The location and size of the
  buffer is the character variable supplied to the READ or WRITE
  statement.

*********************************************************************/


static char *
mem_alloc_r_at (int_stream * s, int *len)
{
  gfc_offset n;
  gfc_offset where = s->logical_offset;

  if (where < s->buffer_offset || where > s->buffer_offset + s->active)
    return NULL;

  s->logical_offset = where + *len;

  n = s->buffer_offset + s->active - where;
  if (*len > n)
    *len = n;

  return s->buffer + (where - s->buffer_offset);
}


static char *
mem_alloc_w_at (int_stream * s, int *len)
{
  gfc_offset m;
  gfc_offset where = s->logical_offset;

  assert (*len >= 0);  /* Negative values not allowed. */
  
  m = where + *len;

  if (where < s->buffer_offset)
    return NULL;

  if (m > s->file_length)
    return NULL;

  s->logical_offset = m;

  return s->buffer + (where - s->buffer_offset);
}


/* Stream read function for internal units.  */

static int
mem_read (int_stream * s, void * buf, size_t * nbytes)
{
  void *p;
  int tmp;

  tmp = *nbytes;
  p = mem_alloc_r_at (s, &tmp);
  if (p)
    {
      *nbytes = tmp;
      memcpy (buf, p, *nbytes);
      return 0;
    }
  else
    {
      *nbytes = 0;
      return 0;
    }
}


/* Stream write function for internal units. This is not actually used
   at the moment, as all internal IO is formatted and the formatted IO
   routines use mem_alloc_w_at.  */

static int
mem_write (int_stream * s, const void * buf, size_t * nbytes)
{
  void *p;
  int tmp;

  tmp = *nbytes;
  p = mem_alloc_w_at (s, &tmp);
  if (p)
    {
      *nbytes = tmp;
      memcpy (p, buf, *nbytes);
      return 0;
    }
  else
    {
      *nbytes = 0;
      return 0;
    }
}


static int
mem_seek (int_stream * s, gfc_offset offset)
{
  if (offset > s->file_length)
    {
      errno = ESPIPE;
      return FAILURE;
    }

  s->logical_offset = offset;
  return SUCCESS;
}


static try
mem_set (int_stream * s, int c, size_t n)
{
  void *p;
  int len;

  len = n;
  
  p = mem_alloc_w_at (s, &len);
  if (p)
    {
      memset (p, c, len);
      return SUCCESS;
    }
  else
    return FAILURE;
}


static int
mem_truncate (int_stream * s __attribute__ ((unused)))
{
  return SUCCESS;
}


static try
mem_close (int_stream * s)
{
  if (s != NULL)
    free_mem (s);

  return SUCCESS;
}


static try
mem_sfree (int_stream * s __attribute__ ((unused)))
{
  return SUCCESS;
}



/*********************************************************************
  Public functions -- A reimplementation of this module needs to
  define functional equivalents of the following.
*********************************************************************/

/* empty_internal_buffer()-- Zero the buffer of Internal file */

void
empty_internal_buffer(stream *strm)
{
  int_stream * s = (int_stream *) strm;
  memset(s->buffer, ' ', s->file_length);
}

/* open_internal()-- Returns a stream structure from an internal file */

stream *
open_internal (char *base, int length, gfc_offset offset)
{
  int_stream *s;

  s = get_mem (sizeof (int_stream));
  memset (s, '\0', sizeof (int_stream));

  s->buffer = base;
  s->buffer_offset = offset;

  s->logical_offset = 0;
  s->active = s->file_length = length;

  s->st.alloc_w_at = (void *) mem_alloc_w_at;
  s->st.sfree = (void *) mem_sfree;
  s->st.close = (void *) mem_close;
  s->st.seek = (void *) mem_seek;
  s->st.trunc = (void *) mem_truncate;
  s->st.read = (void *) mem_read;
  s->st.write = (void *) mem_write;
  s->st.set = (void *) mem_set;

  return (stream *) s;
}


/* fd_to_stream()-- Given an open file descriptor, build a stream
 * around it. */

static stream *
fd_to_stream (int fd, int prot)
{
  struct stat statbuf;
  unix_stream *s;

  s = get_mem (sizeof (unix_stream));
  memset (s, '\0', sizeof (unix_stream));

  s->fd = fd;
  s->buffer_offset = 0;
  s->physical_offset = 0;
  s->logical_offset = 0;
  s->prot = prot;

  /* Get the current length of the file. */

  fstat (fd, &statbuf);

  if (lseek (fd, 0, SEEK_CUR) == (off_t) -1)
    s->file_length = -1;
  else
    s->file_length = S_ISREG (statbuf.st_mode) ? statbuf.st_size : -1;

  s->special_file = !S_ISREG (statbuf.st_mode);

  fd_open (s);

  return (stream *) s;
}


/* Given the Fortran unit number, convert it to a C file descriptor.  */

int
unit_to_fd (int unit)
{
  gfc_unit *us;
  int fd;

  us = find_unit (unit);
  if (us == NULL)
    return -1;

  fd = ((unix_stream *) us->s)->fd;
  unlock_unit (us);
  return fd;
}


/* unpack_filename()-- Given a fortran string and a pointer to a
 * buffer that is PATH_MAX characters, convert the fortran string to a
 * C string in the buffer.  Returns nonzero if this is not possible.  */

int
unpack_filename (char *cstring, const char *fstring, int len)
{
  len = fstrlen (fstring, len);
  if (len >= PATH_MAX)
    return 1;

  memmove (cstring, fstring, len);
  cstring[len] = '\0';

  return 0;
}


/* tempfile()-- Generate a temporary filename for a scratch file and
 * open it.  mkstemp() opens the file for reading and writing, but the
 * library mode prevents anything that is not allowed.  The descriptor
 * is returned, which is -1 on error.  The template is pointed to by 
 * opp->file, which is copied into the unit structure
 * and freed later. */

static int
tempfile (st_parameter_open *opp)
{
  const char *tempdir;
  char *template;
  int fd;

  tempdir = getenv ("GFORTRAN_TMPDIR");
  if (tempdir == NULL)
    tempdir = getenv ("TMP");
  if (tempdir == NULL)
    tempdir = getenv ("TEMP");
  if (tempdir == NULL)
    tempdir = DEFAULT_TEMPDIR;

  template = get_mem (strlen (tempdir) + 20);

  sprintf (template, "%s/gfortrantmpXXXXXX", tempdir);

#ifdef HAVE_MKSTEMP

  fd = mkstemp (template);

#else /* HAVE_MKSTEMP */

  if (mktemp (template))
    do
#if defined(HAVE_CRLF) && defined(O_BINARY)
      fd = open (template, O_RDWR | O_CREAT | O_EXCL | O_BINARY,
		 S_IREAD | S_IWRITE);
#else
      fd = open (template, O_RDWR | O_CREAT | O_EXCL, S_IREAD | S_IWRITE);
#endif
    while (!(fd == -1 && errno == EEXIST) && mktemp (template));
  else
    fd = -1;

#endif /* HAVE_MKSTEMP */

  if (fd < 0)
    free_mem (template);
  else
    {
      opp->file = template;
      opp->file_len = strlen (template);	/* Don't include trailing nul */
    }

  return fd;
}


/* regular_file()-- Open a regular file.
 * Change flags->action if it is ACTION_UNSPECIFIED on entry,
 * unless an error occurs.
 * Returns the descriptor, which is less than zero on error. */

static int
regular_file (st_parameter_open *opp, unit_flags *flags)
{
  char path[PATH_MAX + 1];
  int mode;
  int rwflag;
  int crflag;
  int fd;

  if (unpack_filename (path, opp->file, opp->file_len))
    {
      errno = ENOENT;		/* Fake an OS error */
      return -1;
    }

  rwflag = 0;

  switch (flags->action)
    {
    case ACTION_READ:
      rwflag = O_RDONLY;
      break;

    case ACTION_WRITE:
      rwflag = O_WRONLY;
      break;

    case ACTION_READWRITE:
    case ACTION_UNSPECIFIED:
      rwflag = O_RDWR;
      break;

    default:
      internal_error (&opp->common, "regular_file(): Bad action");
    }

  switch (flags->status)
    {
    case STATUS_NEW:
      crflag = O_CREAT | O_EXCL;
      break;

    case STATUS_OLD:		/* open will fail if the file does not exist*/
      crflag = 0;
      break;

    case STATUS_UNKNOWN:
    case STATUS_SCRATCH:
      crflag = O_CREAT;
      break;

    case STATUS_REPLACE:
      crflag = O_CREAT | O_TRUNC;
      break;

    default:
      internal_error (&opp->common, "regular_file(): Bad status");
    }

  /* rwflag |= O_LARGEFILE; */

#if defined(HAVE_CRLF) && defined(O_BINARY)
  crflag |= O_BINARY;
#endif

  mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  fd = open (path, rwflag | crflag, mode);
  if (flags->action != ACTION_UNSPECIFIED)
    return fd;

  if (fd >= 0)
    {
      flags->action = ACTION_READWRITE;
      return fd;
    }
  if (errno != EACCES && errno != EROFS)
     return fd;

  /* retry for read-only access */
  rwflag = O_RDONLY;
  fd = open (path, rwflag | crflag, mode);
  if (fd >=0)
    {
      flags->action = ACTION_READ;
      return fd;		/* success */
    }
  
  if (errno != EACCES)
    return fd;			/* failure */

  /* retry for write-only access */
  rwflag = O_WRONLY;
  fd = open (path, rwflag | crflag, mode);
  if (fd >=0)
    {
      flags->action = ACTION_WRITE;
      return fd;		/* success */
    }
  return fd;			/* failure */
}


/* open_external()-- Open an external file, unix specific version.
 * Change flags->action if it is ACTION_UNSPECIFIED on entry.
 * Returns NULL on operating system error. */

stream *
open_external (st_parameter_open *opp, unit_flags *flags)
{
  int fd, prot;

  if (flags->status == STATUS_SCRATCH)
    {
      fd = tempfile (opp);
      if (flags->action == ACTION_UNSPECIFIED)
	flags->action = ACTION_READWRITE;

#if HAVE_UNLINK_OPEN_FILE
      /* We can unlink scratch files now and it will go away when closed. */
      if (fd >= 0)
	unlink (opp->file);
#endif
    }
  else
    {
      /* regular_file resets flags->action if it is ACTION_UNSPECIFIED and
       * if it succeeds */
      fd = regular_file (opp, flags);
    }

  if (fd < 0)
    return NULL;
  fd = fix_fd (fd);

  switch (flags->action)
    {
    case ACTION_READ:
      prot = PROT_READ;
      break;

    case ACTION_WRITE:
      prot = PROT_WRITE;
      break;

    case ACTION_READWRITE:
      prot = PROT_READ | PROT_WRITE;
      break;

    default:
      internal_error (&opp->common, "open_external(): Bad action");
    }

  return fd_to_stream (fd, prot);
}


/* input_stream()-- Return a stream pointer to the default input stream.
 * Called on initialization. */

stream *
input_stream (void)
{
  return fd_to_stream (STDIN_FILENO, PROT_READ);
}


/* output_stream()-- Return a stream pointer to the default output stream.
 * Called on initialization. */

stream *
output_stream (void)
{
  stream * s;

#if defined(HAVE_CRLF) && defined(HAVE_SETMODE)
  setmode (STDOUT_FILENO, O_BINARY);
#endif

  s = fd_to_stream (STDOUT_FILENO, PROT_WRITE);
  if (options.unbuffered_preconnected)
    ((unix_stream *) s)->method = SYNC_UNBUFFERED;
  return s;
}


/* error_stream()-- Return a stream pointer to the default error stream.
 * Called on initialization. */

stream *
error_stream (void)
{
  stream * s;

#if defined(HAVE_CRLF) && defined(HAVE_SETMODE)
  setmode (STDERR_FILENO, O_BINARY);
#endif

  s = fd_to_stream (STDERR_FILENO, PROT_WRITE);
  if (options.unbuffered_preconnected)
    ((unix_stream *) s)->method = SYNC_UNBUFFERED;
  return s;
}


/* st_vprintf()-- vprintf function for error output.  To avoid buffer
   overruns, we limit the length of the buffer to ST_VPRINTF_SIZE.  2k
   is big enough to completely fill a 80x25 terminal, so it shuld be
   OK.  We use a direct write() because it is simpler and least likely
   to be clobbered by memory corruption.  Writing an error message
   longer than that is an error.  */

#define ST_VPRINTF_SIZE 2048

int
st_vprintf (const char *format, va_list ap)
{
  static char buffer[ST_VPRINTF_SIZE];
  int written;
  int fd;

  fd = options.use_stderr ? STDERR_FILENO : STDOUT_FILENO;
#ifdef HAVE_VSNPRINTF
  written = vsnprintf(buffer, ST_VPRINTF_SIZE, format, ap);
#else
  written = vsprintf(buffer, format, ap);

  if (written >= ST_VPRINTF_SIZE-1)
    {
      /* The error message was longer than our buffer.  Ouch.  Because
	 we may have messed up things badly, report the error and
	 quit.  */
#define ERROR_MESSAGE "Internal error: buffer overrun in st_vprintf()\n"
      write (fd, buffer, ST_VPRINTF_SIZE-1);
      write (fd, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
      sys_exit(2);
#undef ERROR_MESSAGE

    }
#endif

  written = write (fd, buffer, written);
  return written;
}

/* st_printf()-- printf() function for error output.  This just calls
   st_vprintf() to do the actual work.  */

int
st_printf (const char *format, ...)
{
  int written;
  va_list ap;
  va_start (ap, format);
  written = st_vprintf(format, ap);
  va_end (ap);
  return written;
}


/* compare_file_filename()-- Given an open stream and a fortran string
 * that is a filename, figure out if the file is the same as the
 * filename. */

int
compare_file_filename (gfc_unit *u, const char *name, int len)
{
  char path[PATH_MAX + 1];
  struct stat st1;
#ifdef HAVE_WORKING_STAT
  struct stat st2;
#else
# ifdef __MINGW32__
  uint64_t id1, id2;
# endif
#endif

  if (unpack_filename (path, name, len))
    return 0;			/* Can't be the same */

  /* If the filename doesn't exist, then there is no match with the
   * existing file. */

  if (stat (path, &st1) < 0)
    return 0;

#ifdef HAVE_WORKING_STAT
  fstat (((unix_stream *) (u->s))->fd, &st2);
  return (st1.st_dev == st2.st_dev) && (st1.st_ino == st2.st_ino);
#else

# ifdef __MINGW32__
  /* We try to match files by a unique ID.  On some filesystems (network
     fs and FAT), we can't generate this unique ID, and will simply compare
     filenames.  */
  id1 = id_from_path (path);
  id2 = id_from_fd (((unix_stream *) (u->s))->fd);
  if (id1 || id2)
    return (id1 == id2);
# endif

  if (len != u->file_len)
    return 0;
  return (memcmp(path, u->file, len) == 0);
#endif
}


#ifdef HAVE_WORKING_STAT
# define FIND_FILE0_DECL struct stat *st
# define FIND_FILE0_ARGS st
#else
# define FIND_FILE0_DECL uint64_t id, const char *file, gfc_charlen_type file_len
# define FIND_FILE0_ARGS id, file, file_len
#endif

/* find_file0()-- Recursive work function for find_file() */

static gfc_unit *
find_file0 (gfc_unit *u, FIND_FILE0_DECL)
{
  gfc_unit *v;
#if defined(__MINGW32__) && !HAVE_WORKING_STAT
  uint64_t id1;
#endif

  if (u == NULL)
    return NULL;

#ifdef HAVE_WORKING_STAT
  if (u->s != NULL
      && fstat (((unix_stream *) u->s)->fd, &st[1]) >= 0 &&
      st[0].st_dev == st[1].st_dev && st[0].st_ino == st[1].st_ino)
    return u;
#else
# ifdef __MINGW32__ 
  if (u->s && ((id1 = id_from_fd (((unix_stream *) u->s)->fd)) || id1))
    {
      if (id == id1)
	return u;
    }
  else
# endif
    if (compare_string (u->file_len, u->file, file_len, file) == 0)
      return u;
#endif

  v = find_file0 (u->left, FIND_FILE0_ARGS);
  if (v != NULL)
    return v;

  v = find_file0 (u->right, FIND_FILE0_ARGS);
  if (v != NULL)
    return v;

  return NULL;
}


/* find_file()-- Take the current filename and see if there is a unit
 * that has the file already open.  Returns a pointer to the unit if so. */

gfc_unit *
find_file (const char *file, gfc_charlen_type file_len)
{
  char path[PATH_MAX + 1];
  struct stat st[2];
  gfc_unit *u;
  uint64_t id;

  if (unpack_filename (path, file, file_len))
    return NULL;

  if (stat (path, &st[0]) < 0)
    return NULL;

#if defined(__MINGW32__) && !HAVE_WORKING_STAT
  id = id_from_path (path);
#else
  id = 0;
#endif

  __gthread_mutex_lock (&unit_lock);
retry:
  u = find_file0 (unit_root, FIND_FILE0_ARGS);
  if (u != NULL)
    {
      /* Fast path.  */
      if (! __gthread_mutex_trylock (&u->lock))
	{
	  /* assert (u->closed == 0); */
	  __gthread_mutex_unlock (&unit_lock);
	  return u;
	}

      inc_waiting_locked (u);
    }
  __gthread_mutex_unlock (&unit_lock);
  if (u != NULL)
    {
      __gthread_mutex_lock (&u->lock);
      if (u->closed)
	{
	  __gthread_mutex_lock (&unit_lock);
	  __gthread_mutex_unlock (&u->lock);
	  if (predec_waiting_locked (u) == 0)
	    free_mem (u);
	  goto retry;
	}

      dec_waiting_unlocked (u);
    }
  return u;
}

static gfc_unit *
flush_all_units_1 (gfc_unit *u, int min_unit)
{
  while (u != NULL)
    {
      if (u->unit_number > min_unit)
	{
	  gfc_unit *r = flush_all_units_1 (u->left, min_unit);
	  if (r != NULL)
	    return r;
	}
      if (u->unit_number >= min_unit)
	{
	  if (__gthread_mutex_trylock (&u->lock))
	    return u;
	  if (u->s)
	    flush (u->s);
	  __gthread_mutex_unlock (&u->lock);
	}
      u = u->right;
    }
  return NULL;
}

void
flush_all_units (void)
{
  gfc_unit *u;
  int min_unit = 0;

  __gthread_mutex_lock (&unit_lock);
  do
    {
      u = flush_all_units_1 (unit_root, min_unit);
      if (u != NULL)
	inc_waiting_locked (u);
      __gthread_mutex_unlock (&unit_lock);
      if (u == NULL)
	return;

      __gthread_mutex_lock (&u->lock);

      min_unit = u->unit_number + 1;

      if (u->closed == 0)
	{
	  flush (u->s);
	  __gthread_mutex_lock (&unit_lock);
	  __gthread_mutex_unlock (&u->lock);
	  (void) predec_waiting_locked (u);
	}
      else
	{
	  __gthread_mutex_lock (&unit_lock);
	  __gthread_mutex_unlock (&u->lock);
	  if (predec_waiting_locked (u) == 0)
	    free_mem (u);
	}
    }
  while (1);
}


/* stream_at_bof()-- Returns nonzero if the stream is at the beginning
 * of the file. */

int
stream_at_bof (stream * s)
{
  unix_stream *us;

  if (!is_seekable (s))
    return 0;

  us = (unix_stream *) s;

  return us->logical_offset == 0;
}


/* stream_at_eof()-- Returns nonzero if the stream is at the end
 * of the file. */

int
stream_at_eof (stream * s)
{
  unix_stream *us;

  if (!is_seekable (s))
    return 0;

  us = (unix_stream *) s;

  return us->logical_offset == us->dirty_offset;
}


/* delete_file()-- Given a unit structure, delete the file associated
 * with the unit.  Returns nonzero if something went wrong. */

int
delete_file (gfc_unit * u)
{
  char path[PATH_MAX + 1];

  if (unpack_filename (path, u->file, u->file_len))
    {				/* Shouldn't be possible */
      errno = ENOENT;
      return 1;
    }

  return unlink (path);
}


/* file_exists()-- Returns nonzero if the current filename exists on
 * the system */

int
file_exists (const char *file, gfc_charlen_type file_len)
{
  char path[PATH_MAX + 1];
  struct stat statbuf;

  if (unpack_filename (path, file, file_len))
    return 0;

  if (stat (path, &statbuf) < 0)
    return 0;

  return 1;
}



static const char yes[] = "YES", no[] = "NO", unknown[] = "UNKNOWN";

/* inquire_sequential()-- Given a fortran string, determine if the
 * file is suitable for sequential access.  Returns a C-style
 * string. */

const char *
inquire_sequential (const char *string, int len)
{
  char path[PATH_MAX + 1];
  struct stat statbuf;

  if (string == NULL ||
      unpack_filename (path, string, len) || stat (path, &statbuf) < 0)
    return unknown;

  if (S_ISREG (statbuf.st_mode) ||
      S_ISCHR (statbuf.st_mode) || S_ISFIFO (statbuf.st_mode))
    return unknown;

  if (S_ISDIR (statbuf.st_mode) || S_ISBLK (statbuf.st_mode))
    return no;

  return unknown;
}


/* inquire_direct()-- Given a fortran string, determine if the file is
 * suitable for direct access.  Returns a C-style string. */

const char *
inquire_direct (const char *string, int len)
{
  char path[PATH_MAX + 1];
  struct stat statbuf;

  if (string == NULL ||
      unpack_filename (path, string, len) || stat (path, &statbuf) < 0)
    return unknown;

  if (S_ISREG (statbuf.st_mode) || S_ISBLK (statbuf.st_mode))
    return unknown;

  if (S_ISDIR (statbuf.st_mode) ||
      S_ISCHR (statbuf.st_mode) || S_ISFIFO (statbuf.st_mode))
    return no;

  return unknown;
}


/* inquire_formatted()-- Given a fortran string, determine if the file
 * is suitable for formatted form.  Returns a C-style string. */

const char *
inquire_formatted (const char *string, int len)
{
  char path[PATH_MAX + 1];
  struct stat statbuf;

  if (string == NULL ||
      unpack_filename (path, string, len) || stat (path, &statbuf) < 0)
    return unknown;

  if (S_ISREG (statbuf.st_mode) ||
      S_ISBLK (statbuf.st_mode) ||
      S_ISCHR (statbuf.st_mode) || S_ISFIFO (statbuf.st_mode))
    return unknown;

  if (S_ISDIR (statbuf.st_mode))
    return no;

  return unknown;
}


/* inquire_unformatted()-- Given a fortran string, determine if the file
 * is suitable for unformatted form.  Returns a C-style string. */

const char *
inquire_unformatted (const char *string, int len)
{
  return inquire_formatted (string, len);
}


#ifndef HAVE_ACCESS

#ifndef W_OK
#define W_OK 2
#endif

#ifndef R_OK
#define R_OK 4
#endif

/* Fallback implementation of access() on systems that don't have it.
   Only modes R_OK and W_OK are used in this file.  */

static int
fallback_access (const char *path, int mode)
{
  if ((mode & R_OK) && open (path, O_RDONLY) < 0)
    return -1;

  if ((mode & W_OK) && open (path, O_WRONLY) < 0)
    return -1;

  return 0;
}

#undef access
#define access fallback_access
#endif


/* inquire_access()-- Given a fortran string, determine if the file is
 * suitable for access. */

static const char *
inquire_access (const char *string, int len, int mode)
{
  char path[PATH_MAX + 1];

  if (string == NULL || unpack_filename (path, string, len) ||
      access (path, mode) < 0)
    return no;

  return yes;
}


/* inquire_read()-- Given a fortran string, determine if the file is
 * suitable for READ access. */

const char *
inquire_read (const char *string, int len)
{
  return inquire_access (string, len, R_OK);
}


/* inquire_write()-- Given a fortran string, determine if the file is
 * suitable for READ access. */

const char *
inquire_write (const char *string, int len)
{
  return inquire_access (string, len, W_OK);
}


/* inquire_readwrite()-- Given a fortran string, determine if the file is
 * suitable for read and write access. */

const char *
inquire_readwrite (const char *string, int len)
{
  return inquire_access (string, len, R_OK | W_OK);
}


/* file_length()-- Return the file length in bytes, -1 if unknown */

gfc_offset
file_length (stream * s)
{
  return ((unix_stream *) s)->file_length;
}


/* file_position()-- Return the current position of the file */

gfc_offset
file_position (stream *s)
{
  return ((unix_stream *) s)->logical_offset;
}


/* is_seekable()-- Return nonzero if the stream is seekable, zero if
 * it is not */

int
is_seekable (stream *s)
{
  /* By convention, if file_length == -1, the file is not
     seekable.  */
  return ((unix_stream *) s)->file_length!=-1;
}


/* is_special()-- Return nonzero if the stream is not a regular file.  */

int
is_special (stream *s)
{
  return ((unix_stream *) s)->special_file;
}


try
flush (stream *s)
{
  return fd_flush( (unix_stream *) s);
}

int
stream_isatty (stream *s)
{
  return isatty (((unix_stream *) s)->fd);
}

char *
stream_ttyname (stream *s __attribute__ ((unused)))
{
#ifdef HAVE_TTYNAME
  return ttyname (((unix_stream *) s)->fd);
#else
  return NULL;
#endif
}

gfc_offset
stream_offset (stream *s)
{
  return (((unix_stream *) s)->logical_offset);
}


/* How files are stored:  This is an operating-system specific issue,
   and therefore belongs here.  There are three cases to consider.

   Direct Access:
      Records are written as block of bytes corresponding to the record
      length of the file.  This goes for both formatted and unformatted
      records.  Positioning is done explicitly for each data transfer,
      so positioning is not much of an issue.

   Sequential Formatted:
      Records are separated by newline characters.  The newline character
      is prohibited from appearing in a string.  If it does, this will be
      messed up on the next read.  End of file is also the end of a record.

   Sequential Unformatted:
      In this case, we are merely copying bytes to and from main storage,
      yet we need to keep track of varying record lengths.  We adopt
      the solution used by f2c.  Each record contains a pair of length
      markers:

	Length of record n in bytes
	Data of record n
	Length of record n in bytes

	Length of record n+1 in bytes
	Data of record n+1
	Length of record n+1 in bytes

     The length is stored at the end of a record to allow backspacing to the
     previous record.  Between data transfer statements, the file pointer
     is left pointing to the first length of the current record.

     ENDFILE records are never explicitly stored.

*/

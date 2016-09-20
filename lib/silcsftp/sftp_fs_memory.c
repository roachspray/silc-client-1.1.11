/*

  sftp_fs_memory.c

  Author: Pekka Riikonen <priikone@silcnet.org>

  Copyright (C) 2001 - 2007 Pekka Riikonen

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

*/
/* $Id$ */
/* XXX TODO Win32 support */

#include "silc.h"
#include "silcsftp.h"
#include "silcsftp_fs.h"
#include "sftp_util.h"

#define DIR_SEPARATOR "/"

const struct SilcSFTPFilesystemOpsStruct silc_sftp_fs_memory;

typedef struct MemFSEntryStruct {
  struct MemFSEntryStruct **entry;  /* Files and sub-directories */
  SilcUInt32 entry_count;      	    /* Number of files and sub-directories */
  struct MemFSEntryStruct *parent;  /* non-NULL if `directory' is TRUE,
				       includes parent directory. */
  SilcUInt32 created;	            /* Time of creation */
  char *name;                       /* Name of the entry */
  char *data;			    /* Data of the entry */
  unsigned int directory : 1;	    /* Set if this is directory */
  unsigned int perm : 7;	    /* Permissions */
} *MemFSEntry;

/* File handle. */
typedef struct {
  SilcUInt32 handle;		    /* Handle index */
  int fd;			    /* Real file handle */
  MemFSEntry entry;		    /* Filesystem entry */
} *MemFSFileHandle;

/* Memory filesystem */
typedef struct {
  MemFSEntry root;		    /* Root of the filesystem hierarchy */
  SilcSFTPFSMemoryPerm root_perm;
  MemFSFileHandle *handles;	    /* Open file handles */
  SilcUInt32 handles_count;
} *MemFS;

/* Generates absolute path from relative path that may include '.' and '..'
   in the path. */

static char *memfs_expand_path(MemFSEntry root, const char *path)
{
  if (!strstr(path, "./") && !strstr(path, "../") &&
      !strstr(path, "/..") && !strstr(path, "/."))
    return strdup(path);

  /* XXX TODO */
  return NULL;
}

/* Add `entry' to directory `dir'. */

static SilcBool memfs_add_entry(MemFSEntry dir, MemFSEntry entry,
				SilcBool check_perm)
{
  int i;

  /* Must be both write and exec permissions */
  if (check_perm &&
      !((dir->perm & SILC_SFTP_FS_PERM_WRITE) &&
	(dir->perm & SILC_SFTP_FS_PERM_EXEC)))
    return FALSE;

  if (!dir->entry) {
    dir->entry = silc_calloc(3, sizeof(*entry));
    if (!dir->entry)
      return FALSE;
    dir->entry[0] = entry;
    dir->entry_count = 3;
    entry->created = time(0);
    return TRUE;
  }

  for (i = 0; i < dir->entry_count; i++) {
    if (dir->entry[i])
      continue;

    dir->entry[i] = entry;
    entry->created = time(0);
    return TRUE;
  }

  dir->entry = silc_realloc(dir->entry, sizeof(*dir->entry) *
			    (dir->entry_count + 3));
  if (!dir->entry)
    return FALSE;
  for (i = dir->entry_count + 1; i < dir->entry_count + 3; i++)
    dir->entry[i] = NULL;
  dir->entry[dir->entry_count] = entry;
  dir->entry_count += 3;
  entry->created = time(0);

  return TRUE;
}

/* Removes entry `entry' and all entries under it recursively. */

static SilcBool memfs_del_entry(MemFSEntry entry, SilcBool check_perm)
{
  int i;

  /* Directories cannot be removed from remote access */
  if (check_perm)
    return FALSE;

  silc_free(entry->name);
  silc_free(entry->data);

  /* Delete all entries recursively under this entry */
  for (i = 0; i < entry->entry_count; i++) {
    if (entry->entry[i]) {
      if (!memfs_del_entry(entry->entry[i], FALSE))
	return FALSE;
    }
  }
  silc_free(entry->entry);

  /* Remove from parent */
  if (entry->parent) {
    for (i = 0; i < entry->parent->entry_count; i++) {
      if (entry->parent->entry[i] == entry) {
	entry->parent->entry[i] = NULL;
	break;
      }
    }
  }

  silc_free(entry);

  return TRUE;
}

/* Finds first occurence of entry named `name' under the directory `dir'.
   This does not check subdirectories recursively. */

static MemFSEntry memfs_find_entry(MemFSEntry dir, const char *name,
				   SilcUInt32 name_len)
{
  int i;

  for (i = 0; i < dir->entry_count; i++) {
    if (!dir->entry[i])
      continue;

    if (!strncmp(name, dir->entry[i]->name, name_len))
      return dir->entry[i];
  }

  return NULL;
}

/* Finds the entry by the `path' which may include full path or
   relative path. */

static MemFSEntry memfs_find_entry_path(MemFSEntry dir, const char *p)
{
  MemFSEntry entry = NULL;
  int len;
  char *path, *cp;

  cp = path = memfs_expand_path(dir, p);
  if (!cp)
    return NULL;

  if (strlen(cp) == 1 && cp[0] == '/')
    return dir;

  if (cp[0] == '/')
    cp++;
  len = strcspn(cp, DIR_SEPARATOR);
  while (cp && len) {
    entry = memfs_find_entry(dir, cp, len);
    if (!entry) {
      silc_free(cp);
      return NULL;
    }
    cp += len;
    if (!strlen(cp))
      break;
    cp++;
    len = strcspn(cp, DIR_SEPARATOR);
    dir = entry;
  }

  silc_free(path);
  return entry;
}

/* Deletes entry by the name `name' from the directory `dir'. This does
   not check subdirectories recursively. */

static SilcBool memfs_del_entry_name(MemFSEntry dir, const char *name,
				     SilcUInt32 name_len, SilcBool check_perm)
{
  MemFSEntry entry;

  /* Files cannot be removed from remote access */
  if (check_perm)
    return FALSE;

  entry = memfs_find_entry(dir, name, name_len);

  if (entry)
    return memfs_del_entry(entry, check_perm);

  return FALSE;
}

/* Create new handle and add it to the list of open handles. */

static MemFSFileHandle memfs_create_handle(MemFS fs, int fd, MemFSEntry entry)
{
  MemFSFileHandle handle;
  int i;

  handle = silc_calloc(1, sizeof(*handle));
  if (!handle)
    return NULL;
  handle->fd = fd;
  handle->entry = entry;

  if (!fs->handles) {
    fs->handles = silc_calloc(5, sizeof(*fs->handles));
    if (!fs->handles)
      return NULL;
    fs->handles[0] = handle;
    fs->handles_count = 5;

    handle->handle = 0;

    return handle;
  }

  for (i = 0; i < fs->handles_count; i++) {
    if (fs->handles[i])
      continue;

    fs->handles[i] = handle;

    handle->handle = i;

    return handle;
  }

  fs->handles = silc_realloc(fs->handles, sizeof(*fs->handles) *
			     (fs->handles_count + 5));
  if (!fs->handles)
    return NULL;
  for (i = fs->handles_count + 1; i < fs->handles_count + 5; i++)
    fs->handles[i] = NULL;
  fs->handles[fs->handles_count] = handle;
  handle->handle = fs->handles_count;
  fs->handles_count += 5;

  return handle;
}

/* Deletes the handle and remove it from the open handle list. */

static SilcBool memfs_del_handle(MemFS fs, MemFSFileHandle handle)
{
  if (handle->handle > fs->handles_count)
    return FALSE;

  if (!fs->handles[handle->handle])
    return FALSE;

  if (fs->handles[handle->handle] == handle) {
    fs->handles[handle->handle] = NULL;
    if (handle->fd != -1)
      silc_file_close(handle->fd);
    silc_free(handle);
    return TRUE;
  }

  return FALSE;
}

/* Find handle by handle index. */

static MemFSFileHandle memfs_find_handle(MemFS fs, SilcUInt32 handle)
{
  if (handle > fs->handles_count)
    return NULL;

  if (!fs->handles[handle])
    return NULL;

  if (fs->handles[handle]->handle != handle)
    return NULL;

  return fs->handles[handle];
}

/* Allocates memory filesystem context and returns the context.  The
   context can be given as argument to the silc_sftp_server_start function.
   The context must be freed by the caller using the function
   silc_sftp_fs_memory_free. The `perm' is the permissions for the root
   directory of the filesystem (/ dir). */

SilcSFTPFilesystem silc_sftp_fs_memory_alloc(SilcSFTPFSMemoryPerm perm)
{
  SilcSFTPFilesystem filesystem;
  MemFS fs;

  fs = silc_calloc(1, sizeof(*fs));
  if (!fs)
    return NULL;

  fs->root = silc_calloc(1, sizeof(*fs->root));
  if (!fs->root) {
    silc_free(fs);
    return NULL;
  }

  fs->root->perm = perm;
  fs->root_perm = perm;
  fs->root->directory = TRUE;
  fs->root->name = strdup(DIR_SEPARATOR);
  if (!fs->root->name) {
    silc_free(fs->root);
    silc_free(fs);
  }

  filesystem = silc_calloc(1, sizeof(*filesystem));
  if (!filesystem) {
    silc_free(fs->root->name);
    silc_free(fs->root);
    silc_free(fs);
    return NULL;
  }

  filesystem->fs = (struct SilcSFTPFilesystemOpsStruct *)&silc_sftp_fs_memory;
  filesystem->fs_context = (void *)fs;

  return filesystem;
}

/* Frees the memory filesystem context. */

void silc_sftp_fs_memory_free(SilcSFTPFilesystem fs)
{
  MemFS memfs = (MemFS)fs->fs_context;

  silc_free(memfs->root);
  silc_free(memfs);
}

/* Adds a new directory to the memory filesystem. Returns the directory
   context that can be used to add for example files to the directory
   or new subdirectories under the directory. The `dir' is the parent
   directory of the directory to be added. If this directory is to be
   added to the root directory the `dir' is NULL.  The `name' is the name
   of the directory. If error occurs this returns NULL. The caller must
   not free the returned context. The `perm' will indicate the permissions
   for the directory and they work in POSIX style. */

void *silc_sftp_fs_memory_add_dir(SilcSFTPFilesystem fs, void *dir,
				  SilcSFTPFSMemoryPerm perm,
				  const char *name)
{
  MemFS memfs = (MemFS)fs->fs_context;
  MemFSEntry entry;

  entry = silc_calloc(1, sizeof(*entry));
  if (!entry)
    return NULL;

  entry->perm = perm;
  entry->directory = TRUE;
  entry->parent = dir ? dir : memfs->root;
  entry->name = strdup(name);
  if (!entry->name) {
    silc_free(entry);
    return NULL;
  }

  if (!memfs_add_entry(dir ? dir : memfs->root, entry, FALSE)) {
    silc_free(entry->name);
    silc_free(entry);
    return NULL;
  }

  return entry;
}

/* Deletes a directory indicated by the `dir'. All files and subdirectories
   in this directory is also removed.  If the `dir' is NULL then all
   directories and files are removed from the filesystem. Returns TRUE
   if the removing was success. This is the only way to remove directories
   in memory file system. The filesystem does not allow removing directories
   with remote access using the filesystem access function sftp_rmdir. */

SilcBool silc_sftp_fs_memory_del_dir(SilcSFTPFilesystem fs, void *dir)
{
  MemFS memfs = (MemFS)fs->fs_context;
  SilcBool ret;

  if (dir)
    return memfs_del_entry(dir, FALSE);

  /* Remove from root */
  ret = memfs_del_entry(memfs->root, FALSE);

  memfs->root = silc_calloc(1, sizeof(*memfs->root));
  if (!memfs->root)
    return FALSE;

  memfs->root->perm = memfs->root_perm;
  memfs->root->directory = TRUE;
  memfs->root->name = strdup(DIR_SEPARATOR);
  if (!memfs->root->name) {
    silc_free(memfs->root);
    memfs->root = NULL;
    return FALSE;
  }

  return ret;
}

/* Adds a new file to the directory indicated by the `dir'.  If the `dir'
   is NULL the file is added to the root directory. The `filename' is the
   filename in the directory. The `realpath' is the real filepath in the
   physical filesystem. It is used to actually access the file from the
   memory filesystem. The `perm' will indicate the permissions for th e
   file and they work in POSIX style. Returns TRUE if the file was
   added to the directory. */

SilcBool silc_sftp_fs_memory_add_file(SilcSFTPFilesystem fs, void *dir,
				      SilcSFTPFSMemoryPerm perm,
				      const char *filename,
				      const char *realpath)
{
  MemFS memfs = (MemFS)fs->fs_context;
  MemFSEntry entry;

  entry = silc_calloc(1, sizeof(*entry));
  if (!entry)
    return FALSE;

  entry->perm = perm;
  entry->directory = FALSE;
  entry->name = strdup(filename);
  entry->data = strdup(realpath);
  if (!entry->name || !entry->data) {
    silc_free(entry->name);
    silc_free(entry->data);
    silc_free(entry);
    return FALSE;
  }

  return memfs_add_entry(dir ? dir : memfs->root, entry, FALSE);
}

/* Removes a file indicated by the `filename' from the directory
   indicated by the `dir'. Returns TRUE if the removing was success. */

SilcBool silc_sftp_fs_memory_del_file(SilcSFTPFilesystem fs, void *dir,
				      const char *filename)
{
  MemFS memfs = (MemFS)fs->fs_context;

  if (!filename)
    return FALSE;

  return memfs_del_entry_name(dir ? dir : memfs->root, filename,
			    strlen(filename), FALSE);
}

SilcSFTPHandle memfs_get_handle(void *context, SilcSFTP sftp,
				const unsigned char *data,
				SilcUInt32 data_len)
{
  MemFS fs = (MemFS)context;
  SilcUInt32 handle;

  if (data_len < 4)
    return NULL;

  SILC_GET32_MSB(handle, data);
  return (SilcSFTPHandle)memfs_find_handle(fs, handle);
}

unsigned char *memfs_encode_handle(void *context, SilcSFTP sftp,
				   SilcSFTPHandle handle,
				   SilcUInt32 *handle_len)
{
  unsigned char *data;
  MemFSFileHandle h = (MemFSFileHandle)handle;

  data = silc_calloc(4, sizeof(*data));
  if (!data)
    return NULL;

  SILC_PUT32_MSB(h->handle, data);
  *handle_len = 4;
  return data;
}

void memfs_open(void *context, SilcSFTP sftp,
		const char *filename,
		SilcSFTPFileOperation pflags,
		SilcSFTPAttributes attrs,
		SilcSFTPHandleCallback callback,
		void *callback_context)
{
  MemFS fs = (MemFS)context;
  MemFSEntry entry;
  MemFSFileHandle handle;
  int flags = 0, fd;

  /* CREAT and TRUNC not supported */
  if ((pflags & SILC_SFTP_FXF_CREAT) || (pflags & SILC_SFTP_FXF_TRUNC)) {
    (*callback)(sftp, SILC_SFTP_STATUS_OP_UNSUPPORTED, NULL, callback_context);
    return;
  }

  /* Find such file */
  entry = memfs_find_entry_path(fs->root, filename);
  if (!entry) {
    (*callback)(sftp, SILC_SFTP_STATUS_NO_SUCH_FILE, NULL, callback_context);
    return;
  }

  if (entry->directory || !entry->data) {
    (*callback)(sftp, SILC_SFTP_STATUS_FAILURE, NULL, callback_context);
    return;
  }

  /* Check for reading */
  if ((pflags & SILC_SFTP_FXF_READ) &&
      !(entry->perm & SILC_SFTP_FS_PERM_READ)) {
    (*callback)(sftp, SILC_SFTP_STATUS_PERMISSION_DENIED, NULL,
		callback_context);
    return;
  }

  /* Check for writing */
  if (((pflags & SILC_SFTP_FXF_WRITE) || (pflags & SILC_SFTP_FXF_APPEND)) &&
      !(entry->perm & SILC_SFTP_FS_PERM_WRITE)) {
    (*callback)(sftp, SILC_SFTP_STATUS_PERMISSION_DENIED, NULL,
		callback_context);
    return;
  }

  if ((pflags & SILC_SFTP_FXF_READ) && (pflags & SILC_SFTP_FXF_WRITE))
    flags = O_RDWR;
  else if (pflags & SILC_SFTP_FXF_READ)
    flags = O_RDONLY;
  else if (pflags & SILC_SFTP_FXF_WRITE)
    flags = O_WRONLY;
  if (pflags & SILC_SFTP_FXF_APPEND)
    flags |= O_APPEND;

  /* Attempt to open the file for real. */
  fd = silc_file_open_mode(entry->data + 7, flags,
			   (attrs->flags & SILC_SFTP_ATTR_PERMISSIONS ?
			    attrs->permissions : 0600));
  if (fd == -1) {
    (*callback)(sftp, silc_sftp_map_errno(errno), NULL, callback_context);
    return;
  }

  /* File opened, return handle */
  handle = memfs_create_handle(fs, fd, entry);
  if (handle)
    (*callback)(sftp, SILC_SFTP_STATUS_OK, (SilcSFTPHandle)handle,
		callback_context);
  else
    (*callback)(sftp, SILC_SFTP_STATUS_PERMISSION_DENIED, NULL,
		callback_context);
}

void memfs_close(void *context, SilcSFTP sftp,
		 SilcSFTPHandle handle,
		 SilcSFTPStatusCallback callback,
		 void *callback_context)
{
  MemFS fs = (MemFS)context;
  MemFSFileHandle h = (MemFSFileHandle)handle;
  int ret;

  if (h->fd != -1) {
    ret = silc_file_close(h->fd);
    if (ret == -1) {
      (*callback)(sftp, silc_sftp_map_errno(errno), NULL, NULL,
		  callback_context);
      return;
    }
  }

  memfs_del_handle(fs, h);
  (*callback)(sftp, SILC_SFTP_STATUS_OK, NULL, NULL, callback_context);
}

void memfs_read(void *context, SilcSFTP sftp,
		SilcSFTPHandle handle,
		SilcUInt64 offset,
		SilcUInt32 len,
		SilcSFTPDataCallback callback,
		void *callback_context)
{
  MemFSFileHandle h = (MemFSFileHandle)handle;
  unsigned char data[63488];
  int ret;

  if (len > 63488)
    len = 63488;

  ret = lseek(h->fd, (off_t)offset, SEEK_SET);
  if (ret < 0) {
    if (!ret)
      (*callback)(sftp, SILC_SFTP_STATUS_EOF, NULL, 0, callback_context);
    else
      (*callback)(sftp, silc_sftp_map_errno(errno), NULL, 0, callback_context);
    return;
  }

  /* Attempt to read */
  ret = silc_file_read(h->fd, data, len);
  if (ret <= 0) {
    if (!ret)
      (*callback)(sftp, SILC_SFTP_STATUS_EOF, NULL, 0, callback_context);
    else
      (*callback)(sftp, silc_sftp_map_errno(errno), NULL, 0, callback_context);
    return;
  }

  /* Return data */
  (*callback)(sftp, SILC_SFTP_STATUS_OK, (const unsigned char *)data,
	      ret, callback_context);
}

void memfs_write(void *context, SilcSFTP sftp,
		 SilcSFTPHandle handle,
		 SilcUInt64 offset,
		 const unsigned char *data,
		 SilcUInt32 data_len,
		 SilcSFTPStatusCallback callback,
		 void *callback_context)
{
  MemFSFileHandle h = (MemFSFileHandle)handle;
  int ret;

  lseek(h->fd, (off_t)offset, SEEK_SET);

  /* Attempt to write */
  ret = silc_file_write(h->fd, data, data_len);
  if (ret <= 0) {
    (*callback)(sftp, silc_sftp_map_errno(errno), NULL, NULL,
		callback_context);
    return;
  }

  (*callback)(sftp, SILC_SFTP_STATUS_OK, NULL, NULL, callback_context);
}

void memfs_remove(void *context, SilcSFTP sftp,
		  const char *filename,
		  SilcSFTPStatusCallback callback,
		  void *callback_context)
{
  /* Remove is not supported */
  (*callback)(sftp, SILC_SFTP_STATUS_OP_UNSUPPORTED, NULL, NULL,
	      callback_context);
}

void memfs_rename(void *context, SilcSFTP sftp,
		  const char *oldname,
		  const char *newname,
		  SilcSFTPStatusCallback callback,
		  void *callback_context)
{
  /* Rename is not supported */
  (*callback)(sftp, SILC_SFTP_STATUS_OP_UNSUPPORTED, NULL, NULL,
	      callback_context);
}

void memfs_mkdir(void *context, SilcSFTP sftp,
		 const char *path,
		 SilcSFTPAttributes attrs,
		 SilcSFTPStatusCallback callback,
		 void *callback_context)
{
  /* Mkdir is not supported */
  (*callback)(sftp, SILC_SFTP_STATUS_OP_UNSUPPORTED, NULL, NULL,
	      callback_context);
}

void memfs_rmdir(void *context, SilcSFTP sftp,
		 const char *path,
		 SilcSFTPStatusCallback callback,
		 void *callback_context)
{
  /* Rmdir is not supported */
  (*callback)(sftp, SILC_SFTP_STATUS_OP_UNSUPPORTED, NULL, NULL,
	      callback_context);
}

void memfs_opendir(void *context, SilcSFTP sftp,
		   const char *path,
		   SilcSFTPHandleCallback callback,
		   void *callback_context)
{
  MemFS fs = (MemFS)context;
  MemFSEntry entry;
  MemFSFileHandle handle;

  if (!path || !strlen(path))
    path = (const char *)DIR_SEPARATOR;

  /* Find such directory */
  entry = memfs_find_entry_path(fs->root, path);
  if (!entry) {
    (*callback)(sftp, SILC_SFTP_STATUS_NO_SUCH_FILE, NULL, callback_context);
    return;
  }

  if (!entry->directory) {
    (*callback)(sftp, SILC_SFTP_STATUS_FAILURE, NULL, callback_context);
    return;
  }

  /* Must be read permissions to open a directory */
  if (!(entry->perm & SILC_SFTP_FS_PERM_READ)) {
    (*callback)(sftp, SILC_SFTP_STATUS_PERMISSION_DENIED, NULL,
		callback_context);
    return;
  }

  /* Directory opened, return handle */
  handle = memfs_create_handle(fs, 0, entry);
  if (handle)
    (*callback)(sftp, SILC_SFTP_STATUS_OK, (SilcSFTPHandle)handle,
		callback_context);
  else
    (*callback)(sftp, SILC_SFTP_STATUS_PERMISSION_DENIED, NULL,
		callback_context);
}

void memfs_readdir(void *context, SilcSFTP sftp,
		   SilcSFTPHandle handle,
		   SilcSFTPNameCallback callback,
		   void *callback_context)
{
  MemFSFileHandle h = (MemFSFileHandle)handle;
  MemFSEntry entry;
  SilcSFTPName name;
  SilcSFTPAttributes attrs;
  int i;
  char long_name[256];
  SilcUInt64 filesize = 0;
  char *date;
  struct stat stats;

  if (!h->entry->directory) {
    (*callback)(sftp, SILC_SFTP_STATUS_FAILURE, NULL, callback_context);
    return;
  }

  if (h->fd == -1) {
    (*callback)(sftp, SILC_SFTP_STATUS_EOF, NULL, callback_context);
    return;
  }

  name = silc_calloc(1, sizeof(*name));
  if (!name) {
    (*callback)(sftp, SILC_SFTP_STATUS_EOF, NULL, callback_context);
    return;
  }
  for (i = h->fd; i < 100 + h->fd; i++) {
    if (i >= h->entry->entry_count)
      break;

    entry = h->entry->entry[i];
    if (!entry)
      continue;

    filesize = sizeof(*entry);
    memset(long_name, 0, sizeof(long_name));

    date = (char *)silc_time_string(entry->created);
    if (strrchr(date, ':'))
      *strrchr(date, ':') = '\0';

    if (!entry->directory) {
      filesize = silc_file_size(entry->data + 7);
      memset(&stats, 0, sizeof(stats));
      stat(entry->data + 7, &stats);
    }

    /* Long name format is:
       drwx------   1   324210 Apr  8 08:40 mail/
       1234567890 123 12345678 123456789012 */
    silc_snprintf(long_name, sizeof(long_name) - 1,
	     "%c%c%c%c------ %3d %8llu %12s %s%s",
	     (entry->directory ? 'd' : '-'),
	     ((entry->perm & SILC_SFTP_FS_PERM_READ) ? 'r' : '-'),
	     ((entry->perm & SILC_SFTP_FS_PERM_WRITE) ? 'w' : '-'),
	     ((entry->perm & SILC_SFTP_FS_PERM_EXEC) ? 'x' : '-'),
	     (entry->directory ? (int)entry->entry_count : 1),
#ifndef SILC_WIN32
	     (unsigned long long)filesize,
#else
	     (unsigned long)filesize,
#endif
	     date, entry->name,
	     (entry->directory ? "/" :
	      ((entry->perm & SILC_SFTP_FS_PERM_EXEC) ? "*" : "")));

    /* Add attributes */
    attrs = silc_calloc(1, sizeof(*attrs));
    if (!attrs) {
      (*callback)(sftp, SILC_SFTP_STATUS_EOF, NULL, callback_context);
      return;
    }
    attrs->flags = (SILC_SFTP_ATTR_SIZE |
		    SILC_SFTP_ATTR_UIDGID);
    attrs->size = filesize;
    attrs->uid = 0;		    /* We use always 0 UID and GID */
    attrs->gid = 0;
    if (!entry->directory) {
      attrs->flags |= SILC_SFTP_ATTR_ACMODTIME;
      attrs->atime = stats.st_atime;
      attrs->mtime = stats.st_mtime;
    }

    /* Add the name */
    silc_sftp_name_add(name, entry->name, long_name, attrs);
  }

  /* If we didn't read all then udpate the index for next read */
  if (i >= h->entry->entry_count)
    h->fd = -1;
  else
    h->fd = i;

  /* If names was not found then return EOF. */
  if (name->count == 0) {
    (*callback)(sftp, SILC_SFTP_STATUS_EOF, NULL, callback_context);
    silc_sftp_name_free(name);
    return;
  }

  /* Return name(s) */
  (*callback)(sftp, SILC_SFTP_STATUS_OK, (const SilcSFTPName)name,
	      callback_context);

  silc_sftp_name_free(name);
}

void memfs_stat(void *context, SilcSFTP sftp,
		const char *path,
		SilcSFTPAttrCallback callback,
		void *callback_context)
{
  MemFS fs = (MemFS)context;
  MemFSEntry entry;
  SilcSFTPAttributes attrs;
  int ret;
  struct stat stats;

  if (!path || !strlen(path))
    path = (const char *)DIR_SEPARATOR;

  /* Find such directory */
  entry = memfs_find_entry_path(fs->root, path);
  if (!entry) {
    (*callback)(sftp, SILC_SFTP_STATUS_NO_SUCH_FILE, NULL, callback_context);
    return;
  }

  if (entry->directory || !entry->data) {
    (*callback)(sftp, SILC_SFTP_STATUS_FAILURE, NULL, callback_context);
    return;
  }

  /* Get real stat */
  ret = stat(entry->data + 7, &stats);
  if (ret == -1) {
    (*callback)(sftp, silc_sftp_map_errno(errno), NULL, callback_context);
    return;
  }

  attrs = silc_calloc(1, sizeof(*attrs));
  if (!attrs) {
    (*callback)(sftp, SILC_SFTP_STATUS_FAILURE, NULL, callback_context);
    return;
  }
  attrs->flags = (SILC_SFTP_ATTR_SIZE |
		  SILC_SFTP_ATTR_UIDGID |
		  SILC_SFTP_ATTR_ACMODTIME);
  attrs->size = stats.st_size;
  attrs->uid = 0;		    /* We use always 0 UID and GID */
  attrs->gid = 0;
  attrs->atime = stats.st_atime;
  attrs->mtime = stats.st_mtime;

  /* Return attributes */
  (*callback)(sftp, SILC_SFTP_STATUS_OK, (const SilcSFTPAttributes)attrs,
	      callback_context);

  silc_sftp_attr_free(attrs);
}

void memfs_lstat(void *context, SilcSFTP sftp,
		 const char *path,
		 SilcSFTPAttrCallback callback,
		 void *callback_context)
{
  MemFS fs = (MemFS)context;
  MemFSEntry entry;
  SilcSFTPAttributes attrs;
  int ret;
  struct stat stats;

  if (!path || !strlen(path))
    path = (const char *)DIR_SEPARATOR;

  /* Find such directory */
  entry = memfs_find_entry_path(fs->root, path);
  if (!entry) {
    (*callback)(sftp, SILC_SFTP_STATUS_NO_SUCH_FILE, NULL, callback_context);
    return;
  }

  if (entry->directory || !entry->data) {
    (*callback)(sftp, SILC_SFTP_STATUS_FAILURE, NULL, callback_context);
    return;
  }

  /* Get real stat */
#ifdef SILC_WIN32
  ret = stat(entry->data + 7, &stats);
#endif /* SILC_WIN32 */
#ifdef SILC_UNIX
  ret = lstat(entry->data + 7, &stats);
#endif /* SILC_UNIX */
#ifdef SILC_SYMBIAN
  ret = stat(entry->data + 7, &stats);
#endif /* SILC_SYMBIAN */
  if (ret == -1) {
    (*callback)(sftp, silc_sftp_map_errno(errno), NULL, callback_context);
    return;
  }

  attrs = silc_calloc(1, sizeof(*attrs));
  if (!attrs) {
    (*callback)(sftp, SILC_SFTP_STATUS_FAILURE, NULL, callback_context);
    return;
  }
  attrs->flags = (SILC_SFTP_ATTR_SIZE |
		  SILC_SFTP_ATTR_UIDGID |
		  SILC_SFTP_ATTR_ACMODTIME);
  attrs->size = stats.st_size;
  attrs->uid = 0;		    /* We use always 0 UID and GID */
  attrs->gid = 0;
  attrs->atime = stats.st_atime;
  attrs->mtime = stats.st_mtime;

  /* Return attributes */
  (*callback)(sftp, SILC_SFTP_STATUS_OK, (const SilcSFTPAttributes)attrs,
	      callback_context);

  silc_sftp_attr_free(attrs);
}

void memfs_fstat(void *context, SilcSFTP sftp,
		 SilcSFTPHandle handle,
		 SilcSFTPAttrCallback callback,
		 void *callback_context)
{
  MemFSFileHandle h = (MemFSFileHandle)handle;
  SilcSFTPAttributes attrs;
  int ret;
  struct stat stats;

  if (h->entry->directory || !h->entry->data) {
    (*callback)(sftp, SILC_SFTP_STATUS_FAILURE, NULL, callback_context);
    return;
  }

  /* Get real stat */
  ret = fstat(h->fd, &stats);
  if (ret == -1) {
    (*callback)(sftp, silc_sftp_map_errno(errno), NULL, callback_context);
    return;
  }

  attrs = silc_calloc(1, sizeof(*attrs));
  if (!attrs) {
    (*callback)(sftp, SILC_SFTP_STATUS_FAILURE, NULL, callback_context);
    return;
  }
  attrs->flags = (SILC_SFTP_ATTR_SIZE |
		  SILC_SFTP_ATTR_UIDGID |
		  SILC_SFTP_ATTR_ACMODTIME);
  attrs->size = stats.st_size;
  attrs->uid = 0;		    /* We use always 0 UID and GID */
  attrs->gid = 0;
  attrs->atime = stats.st_atime;
  attrs->mtime = stats.st_mtime;

  /* Return attributes */
  (*callback)(sftp, SILC_SFTP_STATUS_OK, (const SilcSFTPAttributes)attrs,
	      callback_context);

  silc_sftp_attr_free(attrs);
}

void memfs_setstat(void *context, SilcSFTP sftp,
		   const char *path,
		   SilcSFTPAttributes attrs,
		   SilcSFTPStatusCallback callback,
		   void *callback_context)
{
  /* Setstat is not supported */
  (*callback)(sftp, SILC_SFTP_STATUS_OP_UNSUPPORTED, NULL, NULL,
	      callback_context);
}

void memfs_fsetstat(void *context, SilcSFTP sftp,
		    SilcSFTPHandle handle,
		    SilcSFTPAttributes attrs,
		    SilcSFTPStatusCallback callback,
		    void *callback_context)
{
  /* Fsetstat is not supported */
  (*callback)(sftp, SILC_SFTP_STATUS_OP_UNSUPPORTED, NULL, NULL,
	      callback_context);
}

void memfs_readlink(void *context, SilcSFTP sftp,
		    const char *path,
		    SilcSFTPNameCallback callback,
		    void *callback_context)
{
  /* Readlink is not supported */
  (*callback)(sftp, SILC_SFTP_STATUS_OP_UNSUPPORTED, NULL,
	      callback_context);
}

void memfs_symlink(void *context, SilcSFTP sftp,
		   const char *linkpath,
		   const char *targetpath,
		   SilcSFTPStatusCallback callback,
		   void *callback_context)
{
  /* Symlink is not supported */
  (*callback)(sftp, SILC_SFTP_STATUS_OP_UNSUPPORTED, NULL, NULL,
	      callback_context);
}

void memfs_realpath(void *context, SilcSFTP sftp,
		    const char *path,
		    SilcSFTPNameCallback callback,
		    void *callback_context)
{
  MemFS fs = (MemFS)context;
  char *realpath;
  SilcSFTPName name;

  if (!path || !strlen(path))
    path = (const char *)DIR_SEPARATOR;

  realpath = memfs_expand_path(fs->root, path);
  if (!realpath)
    goto fail;

  name = silc_calloc(1, sizeof(*name));
  if (!name)
    goto fail;

  name->filename = silc_calloc(1, sizeof(*name->filename));
  if (!name->filename)
    goto fail;
  name->filename[0] = realpath;
  name->long_filename = silc_calloc(1, sizeof(*name->long_filename));
  if (!name->long_filename)
    goto fail;
  name->long_filename[0] = realpath;
  name->attrs = silc_calloc(1, sizeof(*name->attrs));
  if (!name->attrs)
    goto fail;
  name->attrs[0] = silc_calloc(1, sizeof(*name->attrs[0]));
  if (!name->attrs[0])
    goto fail;
  name->count = 1;

  (*callback)(sftp, SILC_SFTP_STATUS_OK, (const SilcSFTPName)name,
	      callback_context);

  silc_sftp_name_free(name);
  return;

 fail:
  (*callback)(sftp, SILC_SFTP_STATUS_FAILURE, NULL, callback_context);
}

void memfs_extended(void *context, SilcSFTP sftp,
		    const char *request,
		    const unsigned char *data,
		    SilcUInt32 data_len,
		    SilcSFTPExtendedCallback callback,
		    void *callback_context)
{
  /* Extended is not supported */
  (*callback)(sftp, SILC_SFTP_STATUS_OP_UNSUPPORTED, NULL, 0,
	      callback_context);
}

const struct SilcSFTPFilesystemOpsStruct silc_sftp_fs_memory = {
  memfs_get_handle,
  memfs_encode_handle,
  memfs_open,
  memfs_close,
  memfs_read,
  memfs_write,
  memfs_remove,
  memfs_rename,
  memfs_mkdir,
  memfs_rmdir,
  memfs_opendir,
  memfs_readdir,
  memfs_stat,
  memfs_lstat,
  memfs_fstat,
  memfs_setstat,
  memfs_fsetstat,
  memfs_readlink,
  memfs_symlink,
  memfs_realpath,
  memfs_extended
};

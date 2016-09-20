/*

  sftp_util.c

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

#include "silc.h"
#include "silcsftp.h"
#include "sftp_util.h"

/* Encodes a SFTP packet of type `packet' of length `len'. The variable
   argument list is encoded as data payload to the buffer. Returns the
   encoded packet or NULL on error. The caller must free the returned
   buffer. If `packet_buf' is non-NULL then the new packet data is put
   to that buffer instead of allocating new one.  If the new data cannot
   fit to `packet_buf' will be reallocated. */

SilcBuffer silc_sftp_packet_encode(SilcSFTPPacket packet,
				   SilcBuffer packet_buf, SilcUInt32 len, ...)
{
  SilcBuffer buffer;
  va_list vp;

  va_start(vp, len);
  buffer = silc_sftp_packet_encode_vp(packet, packet_buf, len, vp);
  va_end(vp);

  return buffer;
}

/* Same as silc_sftp_packet_encode but takes the variable argument list
   pointer as argument. */

SilcBuffer silc_sftp_packet_encode_vp(SilcSFTPPacket packet,
				      SilcBuffer packet_buf, SilcUInt32 len,
				      va_list vp)
{
  SilcBuffer buffer;
  bool dyn;
  int ret;

  if (packet_buf) {
    if (silc_buffer_truelen(packet_buf) < 4 + 1 + len) {
      packet_buf = silc_buffer_realloc(packet_buf, 4 + 1 + len);
      if (!packet_buf)
	return NULL;
    }

    buffer = packet_buf;
    dyn = FALSE;
  } else {
    buffer = silc_buffer_alloc(4 + 1 + len);
    if (!buffer)
      return NULL;
    dyn = TRUE;
  }

  silc_buffer_pull_tail(buffer, 4 + 1 + len);
  silc_buffer_format(buffer,
		     SILC_STR_UI_INT(len),
		     SILC_STR_UI_CHAR(packet),
		     SILC_STR_END);
  silc_buffer_pull(buffer, 5);

  ret = silc_buffer_format_vp(buffer, vp);
  if (ret < 0) {
    if (dyn)
      silc_buffer_free(buffer);
    return NULL;
  }

  silc_buffer_push(buffer, 5);

  return buffer;
}

/* Decodes the SFTP packet data `packet' and return the SFTP packet type.
   The payload of the packet is returned to the `payload' pointer. Returns
   0 if error occurred during decoding and -1 if partial packet was
   received. */

SilcSFTPPacket silc_sftp_packet_decode(SilcBuffer packet,
				       unsigned char **payload,
				       SilcUInt32 *payload_len)
{
  SilcUInt32 len;
  SilcUInt8 type;
  int ret;

  ret = silc_buffer_unformat(packet,
			     SILC_STR_UI_INT(&len),
			     SILC_STR_UI_CHAR(&type),
			     SILC_STR_END);
  if (ret < 0)
    return 0;

  if (type < SILC_SFTP_INIT || type > SILC_SFTP_EXTENDED_REPLY)
    return 0;

  if (len > (silc_buffer_len(packet) - 5))
    return -1;

  silc_buffer_pull(packet, 5);
  ret = silc_buffer_unformat(packet,
			     SILC_STR_UI_XNSTRING(payload, len),
			     SILC_STR_END);
  if (ret < 0)
    return 0;

  silc_buffer_push(packet, 5);

  *payload_len = len;

  return (SilcSFTPPacket)type;
}

/* Encodes the SFTP attributes to a buffer and returns the allocated buffer.
   The caller must free the buffer. */

SilcBuffer silc_sftp_attr_encode(SilcSFTPAttributes attr)
{
  SilcBuffer buffer;
  int i, ret;
  SilcUInt32 len = 4;

  if (attr->flags & SILC_SFTP_ATTR_SIZE)
    len += 8;
  if (attr->flags & SILC_SFTP_ATTR_UIDGID)
    len += 8;
  if (attr->flags & SILC_SFTP_ATTR_PERMISSIONS)
    len += 4;
  if (attr->flags & SILC_SFTP_ATTR_ACMODTIME)
    len += 8;
  if (attr->flags & SILC_SFTP_ATTR_EXTENDED) {
    len += 4;
    for (i = 0; i < attr->extended_count; i++) {
      len += 8;
      len += silc_buffer_len(attr->extended_type[i]);
      len += silc_buffer_len(attr->extended_data[i]);
    }
  }

  buffer = silc_buffer_alloc_size(len);
  if (!buffer)
    return NULL;

  silc_buffer_format(buffer,
		     SILC_STR_UI_INT(attr->flags),
		     SILC_STR_END);
  silc_buffer_pull(buffer, 4);

  if (attr->flags & SILC_SFTP_ATTR_SIZE) {
    silc_buffer_format(buffer,
		       SILC_STR_UI_INT64(attr->size),
		       SILC_STR_END);
    silc_buffer_pull(buffer, 8);
  }

  if (attr->flags & SILC_SFTP_ATTR_UIDGID) {
    silc_buffer_format(buffer,
		       SILC_STR_UI_INT(attr->uid),
		       SILC_STR_UI_INT(attr->gid),
		       SILC_STR_END);
    silc_buffer_pull(buffer, 8);
  }

  if (attr->flags & SILC_SFTP_ATTR_PERMISSIONS) {
    silc_buffer_format(buffer,
		       SILC_STR_UI_INT(attr->permissions),
		       SILC_STR_END);
    silc_buffer_pull(buffer, 4);
  }

  if (attr->flags & SILC_SFTP_ATTR_ACMODTIME) {
    silc_buffer_format(buffer,
		       SILC_STR_UI_INT(attr->atime),
		       SILC_STR_UI_INT(attr->mtime),
		       SILC_STR_END);
    silc_buffer_pull(buffer, 8);
  }

  if (attr->flags & SILC_SFTP_ATTR_EXTENDED) {
    silc_buffer_format(buffer,
		       SILC_STR_UI_INT(attr->extended_count),
		       SILC_STR_END);
    silc_buffer_pull(buffer, 4);

    for (i = 0; i < attr->extended_count; i++) {
      ret =
	silc_buffer_format(
		   buffer,
		   SILC_STR_UI_INT(silc_buffer_len(attr->extended_type[i])),
		   SILC_STR_DATA(silc_buffer_data(attr->extended_type[i]),
				 silc_buffer_len(attr->extended_type[i])),
		   SILC_STR_UI_INT(silc_buffer_len(attr->extended_data[i])),
		   SILC_STR_DATA(silc_buffer_data(attr->extended_data[i]),
				 silc_buffer_len(attr->extended_data[i])),
		   SILC_STR_END);
      silc_buffer_pull(buffer, ret);
    }
  }

  silc_buffer_push(buffer, buffer->data - buffer->head);

  return buffer;
}

/* Decodes SilcSFTPAttributes from the buffer `buffer'. Returns the allocated
   attributes that the caller must free or NULL on error. */

SilcSFTPAttributes silc_sftp_attr_decode(SilcBuffer buffer)
{
  SilcSFTPAttributes attr;

  attr = silc_calloc(1, sizeof(*attr));
  if (!attr)
    return NULL;

  if (silc_buffer_unformat(buffer,
			   SILC_STR_UI_INT(&attr->flags),
			   SILC_STR_END) < 0)
    goto out;

  silc_buffer_pull(buffer, 4);

  if (attr->flags & SILC_SFTP_ATTR_SIZE) {
    if (silc_buffer_unformat(buffer,
			     SILC_STR_UI_INT64(&attr->size),
			     SILC_STR_END) < 0)
      goto out;

    silc_buffer_pull(buffer, 8);
  }

  if (attr->flags & SILC_SFTP_ATTR_UIDGID) {
    if (silc_buffer_unformat(buffer,
			     SILC_STR_UI_INT(&attr->uid),
			     SILC_STR_UI_INT(&attr->gid),
			     SILC_STR_END) < 0)
      goto out;

    silc_buffer_pull(buffer, 8);
  }

  if (attr->flags & SILC_SFTP_ATTR_PERMISSIONS) {
    if (silc_buffer_unformat(buffer,
			     SILC_STR_UI_INT(&attr->permissions),
			     SILC_STR_END) < 0)
      goto out;

    silc_buffer_pull(buffer, 4);
  }

  if (attr->flags & SILC_SFTP_ATTR_ACMODTIME) {
    if (silc_buffer_unformat(buffer,
			     SILC_STR_UI_INT(&attr->atime),
			     SILC_STR_UI_INT(&attr->mtime),
			     SILC_STR_END) < 0)
      goto out;

    silc_buffer_pull(buffer, 8);
  }

  if (attr->flags & SILC_SFTP_ATTR_EXTENDED) {
    int i;

    if (silc_buffer_unformat(buffer,
			     SILC_STR_UI_INT(&attr->extended_count),
			     SILC_STR_END) < 0)
      goto out;

    silc_buffer_pull(buffer, 4);

    attr->extended_type = silc_calloc(attr->extended_count,
				      sizeof(*attr->extended_type));
    attr->extended_data = silc_calloc(attr->extended_count,
				      sizeof(*attr->extended_data));
    if (!attr->extended_type || !attr->extended_data)
      return NULL;

    for (i = 0; i < attr->extended_count; i++) {
      unsigned char *tmp, *tmp2;
      SilcUInt32 tmp_len, tmp2_len;

      if (silc_buffer_unformat(buffer,
			       SILC_STR_UI32_NSTRING(&tmp, &tmp_len),
			       SILC_STR_UI32_NSTRING(&tmp2, &tmp2_len),
			       SILC_STR_END) < 0)
	goto out;

      attr->extended_type[i] = silc_buffer_alloc(tmp_len);
      attr->extended_data[i] = silc_buffer_alloc(tmp2_len);
      if (!attr->extended_type[i] || !attr->extended_data[i])
	return NULL;
      silc_buffer_put(attr->extended_type[i], tmp, tmp_len);
      silc_buffer_put(attr->extended_data[i], tmp2, tmp2_len);

      silc_buffer_pull(buffer, tmp_len + 4 + tmp2_len + 4);
    }
  }

  return attr;

 out:
  silc_sftp_attr_free(attr);
  return NULL;
}

/* Frees the attributes context and its internals. */

void silc_sftp_attr_free(SilcSFTPAttributes attr)
{
  int i;

  for (i = 0; i < attr->extended_count; i++) {
    silc_buffer_free(attr->extended_type[i]);
    silc_buffer_free(attr->extended_data[i]);
  }
  silc_free(attr->extended_type);
  silc_free(attr->extended_data);
  silc_free(attr);
}

/* Adds an entry to the `name' context. */

void silc_sftp_name_add(SilcSFTPName name, const char *short_name,
			const char *long_name, SilcSFTPAttributes attrs)
{
  name->filename = silc_realloc(name->filename, sizeof(*name->filename) *
				(name->count + 1));
  name->long_filename = silc_realloc(name->long_filename,
				     sizeof(*name->long_filename) *
				     (name->count + 1));
  name->attrs = silc_realloc(name->attrs, sizeof(*name->attrs) *
			     (name->count + 1));
  if (!name->filename || !name->long_filename || !name->attrs)
    return;

  name->filename[name->count] = strdup(short_name);
  name->long_filename[name->count] = strdup(long_name);
  name->attrs[name->count] = attrs;
  name->count++;
}

/* Encodes the SilcSFTPName to a buffer and returns the allocated buffer.
   The caller must free the buffer. */

SilcBuffer silc_sftp_name_encode(SilcSFTPName name)
{
  SilcBuffer buffer;
  int i, len = 4;
  SilcBuffer *attr_buf;

  attr_buf = silc_calloc(name->count, sizeof(*attr_buf));
  if (!attr_buf)
    return NULL;

  for (i = 0; i < name->count; i++) {
    len += (8 + strlen(name->filename[i]) + strlen(name->long_filename[i]));
    attr_buf[i] = silc_sftp_attr_encode(name->attrs[i]);
    if (!attr_buf[i])
      return NULL;
    len += silc_buffer_len(attr_buf[i]);
  }

  buffer = silc_buffer_alloc(len);
  if (!buffer)
    return NULL;
  silc_buffer_end(buffer);

  silc_buffer_format(buffer,
		     SILC_STR_UI_INT(name->count),
		     SILC_STR_END);
  silc_buffer_pull(buffer, 4);

  for (i = 0; i < name->count; i++) {
    len =
      silc_buffer_format(buffer,
			 SILC_STR_UI_INT(strlen(name->filename[i])),
			 SILC_STR_UI32_STRING(name->filename[i]),
			 SILC_STR_UI_INT(strlen(name->long_filename[i])),
			 SILC_STR_UI32_STRING(name->long_filename[i]),
			 SILC_STR_DATA(silc_buffer_data(attr_buf[i]),
				       silc_buffer_len(attr_buf[i])),
			 SILC_STR_END);

    silc_buffer_pull(buffer, len);
    silc_free(attr_buf[i]);
  }
  silc_free(attr_buf);

  silc_buffer_push(buffer, buffer->data - buffer->head);

  return buffer;
}

/* Decodes a SilcSFTPName structure from the `buffer' that must include
   `count' many name, longname and attribute values. Returns the allocated
   structure or NULL on error. */

SilcSFTPName silc_sftp_name_decode(SilcUInt32 count, SilcBuffer buffer)
{
  SilcSFTPName name;
  int i;
  int ret;

  name = silc_calloc(1, sizeof(*name));
  if (!name)
    return NULL;
  name->filename = silc_calloc(count, sizeof(*name->filename));
  name->long_filename = silc_calloc(count, sizeof(*name->filename));
  name->attrs = silc_calloc(count, sizeof(*name->attrs));
  if (!name->filename || !name->long_filename || !name->attrs) {
    silc_sftp_name_free(name);
    return NULL;
  }
  name->count = count;

  for (i = 0; i < count; i++) {
    ret =
      silc_buffer_unformat(buffer,
			   SILC_STR_UI32_STRING_ALLOC(&name->filename[i]),
			   SILC_STR_UI32_STRING_ALLOC(&name->long_filename[i]),
			   SILC_STR_END);
    if (ret < 0) {
      silc_sftp_name_free(name);
      return NULL;
    }

    silc_buffer_pull(buffer, ret);

    /* Decode attributes, this will pull the `buffer' to correct place
       for next round automatically. */
    name->attrs[i] = silc_sftp_attr_decode(buffer);
    if (!name->attrs[i]) {
      silc_sftp_name_free(name);
      return NULL;
    }
  }

  return name;
}

/* Frees the name context and its internals. */

void silc_sftp_name_free(SilcSFTPName name)
{
  int i;

  for (i = 0; i < name->count; i++) {
    silc_free(name->filename[i]);
    silc_free(name->long_filename[i]);
    silc_sftp_attr_free(name->attrs[i]);
  }

  silc_free(name->filename);
  silc_free(name->long_filename);
  silc_free(name->attrs);
  silc_free(name);
}

/* Maps errno to SFTP status message. */

SilcSFTPStatus silc_sftp_map_errno(int err)
{
  SilcSFTPStatus ret;

  switch (err) {
  case 0:
    ret = SILC_SFTP_STATUS_OK;
    break;
  case ENOENT:
  case ENOTDIR:
  case EBADF:
    ret = SILC_SFTP_STATUS_NO_SUCH_FILE;
    break;
  case EPERM:
  case EACCES:
  case EFAULT:
    ret = SILC_SFTP_STATUS_PERMISSION_DENIED;
    break;
  case ENAMETOOLONG:
  case EINVAL:
    ret = SILC_SFTP_STATUS_BAD_MESSAGE;
    break;
  default:
    ret = SILC_SFTP_STATUS_FAILURE;
    break;
  }

  return ret;
}

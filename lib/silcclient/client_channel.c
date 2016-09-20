/*

  client_channel.c

  Author: Pekka Riikonen <priikone@silcnet.org>

  Copyright (C) 1997 - 2007 Pekka Riikonen

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
#include "silcclient.h"
#include "client_internal.h"

/************************** Channel Message Send ****************************/

/* Sends channel message to `channel'. */

SilcBool silc_client_send_channel_message(SilcClient client,
					  SilcClientConnection conn,
					  SilcChannelEntry channel,
					  SilcChannelPrivateKey key,
					  SilcMessageFlags flags,
					  SilcHash hash,
					  unsigned char *data,
					  SilcUInt32 data_len)
{
  SilcChannelUser chu;
  SilcBuffer buffer;
  SilcCipher cipher;
  SilcHmac hmac;
  SilcBool ret;
  SilcID sid, rid;

  SILC_LOG_DEBUG(("Sending channel message"));

  if (silc_unlikely(!client || !conn || !channel))
    return FALSE;
  if (silc_unlikely(flags & SILC_MESSAGE_FLAG_SIGNED && !hash))
    return FALSE;
  if (silc_unlikely(conn->internal->disconnected))
    return FALSE;

  chu = silc_client_on_channel(channel, conn->local_entry);
  if (silc_unlikely(!chu)) {
    conn->context_type = SILC_ID_CHANNEL;
    conn->channel_entry = channel;
    client->internal->ops->say(conn->client, conn,
			       SILC_CLIENT_MESSAGE_ERROR,
			       "Cannot talk to channel: not joined");
    conn->context_type = SILC_ID_NONE;
    return FALSE;
  }

  /* Check if it is allowed to send messages to this channel by us. */
  if (silc_unlikely(channel->mode & SILC_CHANNEL_MODE_SILENCE_USERS &&
		    !chu->mode))
    return FALSE;
  if (silc_unlikely(channel->mode & SILC_CHANNEL_MODE_SILENCE_OPERS &&
		    chu->mode & SILC_CHANNEL_UMODE_CHANOP &&
		    !(chu->mode & SILC_CHANNEL_UMODE_CHANFO)))
    return FALSE;
  if (silc_unlikely(chu->mode & SILC_CHANNEL_UMODE_QUIET))
    return FALSE;

  /* Take the key to be used */
  if (channel->internal.private_keys) {
    if (key) {
      /* Use key application specified */
      cipher = key->send_key;
      hmac = key->hmac;
    } else if (channel->mode & SILC_CHANNEL_MODE_PRIVKEY &&
	       channel->internal.curr_key) {
      /* Use current private key */
      cipher = channel->internal.curr_key->send_key;
      hmac = channel->internal.curr_key->hmac;
    } else if (channel->mode & SILC_CHANNEL_MODE_PRIVKEY &&
	       !channel->internal.curr_key &&
	       channel->internal.private_keys) {
      /* Use just some private key since we don't know what to use
	 and private keys are set. */
      silc_dlist_start(channel->internal.private_keys);
      key = silc_dlist_get(channel->internal.private_keys);
      cipher = key->send_key;
      hmac = key->hmac;

      /* Use this key as current private key */
      channel->internal.curr_key = key;
    } else {
      /* Use normal channel key generated by the server */
      cipher = channel->internal.send_key;
      hmac = channel->internal.hmac;
    }
  } else {
    /* Use normal channel key generated by the server */
    cipher = channel->internal.send_key;
    hmac = channel->internal.hmac;
  }

  if (silc_unlikely(!cipher || !hmac)) {
    SILC_LOG_ERROR(("No cipher and HMAC for channel"));
    return FALSE;
  }

  /* Encode the message payload. This also encrypts the message payload. */
  sid.type = SILC_ID_CLIENT;
  sid.u.client_id = chu->client->id;
  rid.type = SILC_ID_CHANNEL;
  rid.u.channel_id = chu->channel->id;
  buffer = silc_message_payload_encode(flags, data, data_len, TRUE, FALSE,
				       cipher, hmac, client->rng, NULL,
				       conn->private_key, hash, &sid, &rid,
				       NULL);
  if (silc_unlikely(!buffer)) {
    SILC_LOG_ERROR(("Error encoding channel message"));
    return FALSE;
  }

  /* Send the channel message */
  ret = silc_packet_send_ext(conn->stream, SILC_PACKET_CHANNEL_MESSAGE, 0,
			     0, NULL, SILC_ID_CHANNEL, &channel->id,
			     silc_buffer_datalen(buffer), NULL, NULL);

  silc_buffer_free(buffer);
  return ret;
}

/************************* Channel Message Receive **************************/

/* Client resolving callback.  Continues with the channel message processing */

static void silc_client_channel_message_resolved(SilcClient client,
						 SilcClientConnection conn,
						 SilcStatus status,
						 SilcDList clients,
						 void *context)
{
  /* If no client found, ignore the channel message, a silent error */
  if (!clients)
    silc_fsm_next(context, silc_client_channel_message_error);

  /* Continue processing the channel message packet */
  SILC_FSM_CALL_CONTINUE(context);
}

/* Process received channel message */

SILC_FSM_STATE(silc_client_channel_message)
{
  SilcClientConnection conn = fsm_context;
  SilcClient client = conn->client;
  SilcPacket packet = state_context;
  SilcBuffer buffer = &packet->buffer;
  SilcMessagePayload payload = NULL;
  SilcChannelEntry channel;
  SilcClientEntry client_entry;
  SilcClientID remote_id;
  SilcChannelID channel_id;
  unsigned char *message;
  SilcUInt32 message_len;
  SilcChannelPrivateKey key = NULL;

  SILC_LOG_DEBUG(("Received channel message"));

  SILC_LOG_HEXDUMP(("Channel message"), silc_buffer_data(buffer),
		   silc_buffer_len(buffer));

  if (silc_unlikely(packet->dst_id_type != SILC_ID_CHANNEL)) {
    /** Invalid packet */
    silc_fsm_next(fsm, silc_client_channel_message_error);
    return SILC_FSM_CONTINUE;
  }

  if (silc_unlikely(!silc_id_str2id(packet->src_id,
				    packet->src_id_len, SILC_ID_CLIENT,
				    &remote_id, sizeof(remote_id)))) {
    /** Invalid source ID */
    silc_fsm_next(fsm, silc_client_channel_message_error);
    return SILC_FSM_CONTINUE;
  }

  /* Get sender client entry */
  client_entry = silc_client_get_client_by_id(client, conn, &remote_id);
  if (!client_entry || !client_entry->internal.valid) {
    /** Resolve client info */
    silc_client_unref_client(client, conn, client_entry);
    SILC_FSM_CALL(silc_client_get_client_by_id_resolve(
					 client, conn, &remote_id, NULL,
					 silc_client_channel_message_resolved,
					 fsm));
    /* NOT REACHED */
  }

  if (silc_unlikely(!silc_id_str2id(packet->dst_id, packet->dst_id_len,
				    SILC_ID_CHANNEL, &channel_id,
				    sizeof(channel_id)))) {
    /** Invalid destination ID */
    silc_fsm_next(fsm, silc_client_channel_message_error);
    return SILC_FSM_CONTINUE;
  }

  /* Find the channel */
  channel = silc_client_get_channel_by_id(client, conn, &channel_id);
  if (silc_unlikely(!channel)) {
    /** Unknown channel */
    silc_fsm_next(fsm, silc_client_channel_message_error);
    return SILC_FSM_CONTINUE;
  }

  /* Check that user is on channel */
  if (silc_unlikely(!silc_client_on_channel(channel, client_entry))) {
    /** User not on channel */
    SILC_LOG_WARNING(("Message from user not on channel, client or "
		      "server bug"));
    silc_fsm_next(fsm, silc_client_channel_message_error);
    return SILC_FSM_CONTINUE;
  }

  /* If there is no channel private key then just decrypt the message
     with the channel key. If private keys are set then just go through
     all private keys and check what decrypts correctly. */
  if (!channel->internal.private_keys) {
    /* Parse the channel message payload. This also decrypts the payload */
    payload = silc_message_payload_parse(silc_buffer_data(buffer),
					 silc_buffer_len(buffer), FALSE,
					 FALSE, channel->internal.receive_key,
					 channel->internal.hmac,
					 packet->src_id, packet->src_id_len,
					 packet->dst_id, packet->dst_id_len,
					 NULL, FALSE, NULL);

    /* If decryption failed and we have just performed channel key rekey
       we will use the old key in decryption. If that fails too then we
       cannot do more and will drop the packet. */
    if (silc_unlikely(!payload)) {
      SilcCipher cipher;
      SilcHmac hmac;

      if (!channel->internal.old_channel_keys ||
	  !silc_dlist_count(channel->internal.old_channel_keys))
	goto out;

      SILC_LOG_DEBUG(("Attempting to decrypt with old channel key(s)"));

      silc_dlist_end(channel->internal.old_channel_keys);
      silc_dlist_end(channel->internal.old_hmacs);
      while ((cipher = silc_dlist_get(channel->internal.old_channel_keys))) {
	hmac = silc_dlist_get(channel->internal.old_hmacs);
	if (!hmac)
	  break;

	payload = silc_message_payload_parse(silc_buffer_data(buffer),
					     silc_buffer_len(buffer),
					     FALSE, FALSE, cipher, hmac,
					     packet->src_id,
					     packet->src_id_len,
					     packet->dst_id,
					     packet->dst_id_len,
					     NULL, FALSE, NULL);
	if (payload)
	  break;
      }
      if (!payload)
	goto out;
    }
  } else {
    /* If the private key mode is not set on the channel then try the actual
       channel key first before trying private keys. */
    if (!(channel->mode & SILC_CHANNEL_MODE_PRIVKEY))
      payload = silc_message_payload_parse(silc_buffer_data(buffer),
					   silc_buffer_len(buffer),
					   FALSE, FALSE,
					   channel->internal.receive_key,
					   channel->internal.hmac,
					   packet->src_id,
					   packet->src_id_len,
					   packet->dst_id,
					   packet->dst_id_len,
					   NULL, FALSE, NULL);

    if (!payload) {
      silc_dlist_start(channel->internal.private_keys);
      while ((key = silc_dlist_get(channel->internal.private_keys))) {
	/* Parse the message payload. This also decrypts the payload */
	payload = silc_message_payload_parse(silc_buffer_data(buffer),
					     silc_buffer_len(buffer),
					     FALSE, FALSE, key->receive_key,
					     key->hmac, packet->src_id,
					     packet->src_id_len,
					     packet->dst_id,
					     packet->dst_id_len,
					     NULL, FALSE, NULL);
	if (payload)
	  break;
      }
      if (key == SILC_LIST_END)
	goto out;
    }
  }

  message = silc_message_get_data(payload, &message_len);

  /* Pass the message to application */
  client->internal->ops->channel_message(
			     client, conn, client_entry, channel, payload,
			     key, silc_message_get_flags(payload),
			     message, message_len);

 out:
  silc_client_unref_client(client, conn, client_entry);
  silc_client_unref_channel(client, conn, channel);
  if (payload)
    silc_message_payload_free(payload);
  return SILC_FSM_FINISH;
}

/* Channel message error. */

SILC_FSM_STATE(silc_client_channel_message_error)
{
  SilcPacket packet = state_context;
  silc_packet_free(packet);
  return SILC_FSM_FINISH;
}

/******************************* Channel Key ********************************/

/* Timeout callback that is called after a short period of time after the
   new channel key has been created.  This removes the first channel key
   in the list. */

SILC_TASK_CALLBACK(silc_client_save_channel_key_rekey)
{
  SilcChannelEntry channel = (SilcChannelEntry)context;
  SilcCipher key;
  SilcHmac hmac;

  if (channel->internal.old_channel_keys) {
    silc_dlist_start(channel->internal.old_channel_keys);
    key = silc_dlist_get(channel->internal.old_channel_keys);
    if (key) {
      silc_dlist_del(channel->internal.old_channel_keys, key);
      silc_cipher_free(key);
    }
  }

  if (channel->internal.old_hmacs) {
    silc_dlist_start(channel->internal.old_hmacs);
    hmac = silc_dlist_get(channel->internal.old_hmacs);
    if (hmac) {
      silc_dlist_del(channel->internal.old_hmacs, hmac);
      silc_hmac_free(hmac);
    }
  }
}

/* Saves channel key from encoded `key_payload'. This is used when we receive
   Channel Key Payload and when we are processing JOIN command reply. */

SilcBool silc_client_save_channel_key(SilcClient client,
				      SilcClientConnection conn,
				      SilcBuffer key_payload,
				      SilcChannelEntry channel)
{
  unsigned char *id_string, *key, *cipher, *hmac, hash[SILC_HASH_MAXLEN];
  SilcUInt32 tmp_len;
  SilcChannelID id;
  SilcChannelKeyPayload payload;

  SILC_LOG_DEBUG(("New channel key"));

  payload = silc_channel_key_payload_parse(silc_buffer_data(key_payload),
					   silc_buffer_len(key_payload));
  if (!payload)
    return FALSE;

  id_string = silc_channel_key_get_id(payload, &tmp_len);
  if (!id_string) {
    silc_channel_key_payload_free(payload);
    return FALSE;
  }

  if (!silc_id_str2id(id_string, tmp_len, SILC_ID_CHANNEL, &id, sizeof(id))) {
    silc_channel_key_payload_free(payload);
    return FALSE;
  }

  /* Find channel. */
  if (!channel) {
    channel = silc_client_get_channel_by_id(client, conn, &id);
    if (!channel) {
      SILC_LOG_DEBUG(("Key for unknown channel"));
      silc_channel_key_payload_free(payload);
      return FALSE;
    }
  } else {
    silc_client_ref_channel(client, conn, channel);
  }

  /* Save the old key for a short period of time so that we can decrypt
     channel message even after the rekey if some client would be sending
     messages with the old key after the rekey. */
  if (!channel->internal.old_channel_keys)
    channel->internal.old_channel_keys = silc_dlist_init();
  if (!channel->internal.old_hmacs)
    channel->internal.old_hmacs = silc_dlist_init();
  if (channel->internal.old_channel_keys && channel->internal.old_hmacs) {
    silc_dlist_add(channel->internal.old_channel_keys,
		   channel->internal.receive_key);
    silc_dlist_add(channel->internal.old_hmacs, channel->internal.hmac);
    silc_schedule_task_add_timeout(client->schedule,
				   silc_client_save_channel_key_rekey,
				   channel, 15, 0);
  }

  /* Get channel cipher */
  cipher = silc_channel_key_get_cipher(payload, NULL);
  if (!silc_cipher_alloc(cipher, &channel->internal.send_key)) {
    conn->context_type = SILC_ID_CHANNEL;
    conn->channel_entry = channel;
    client->internal->ops->say(
			   conn->client, conn,
			   SILC_CLIENT_MESSAGE_ERROR,
			   "Cannot talk to channel: unsupported cipher %s",
			   cipher);
    conn->context_type = SILC_ID_NONE;
    silc_client_unref_channel(client, conn, channel);
    silc_channel_key_payload_free(payload);
    return FALSE;
  }
  if (!silc_cipher_alloc(cipher, &channel->internal.receive_key)) {
    conn->context_type = SILC_ID_CHANNEL;
    conn->channel_entry = channel;
    client->internal->ops->say(
			   conn->client, conn,
			   SILC_CLIENT_MESSAGE_ERROR,
			   "Cannot talk to channel: unsupported cipher %s",
			   cipher);
    conn->context_type = SILC_ID_NONE;
    silc_client_unref_channel(client, conn, channel);
    silc_channel_key_payload_free(payload);
    return FALSE;
  }

  /* Set the cipher key.  Both sending and receiving keys are same */
  key = silc_channel_key_get_key(payload, &tmp_len);
  silc_cipher_set_key(channel->internal.send_key, key, tmp_len * 8, TRUE);
  silc_cipher_set_key(channel->internal.receive_key, key, tmp_len * 8, FALSE);

  /* Get channel HMAC */
  hmac = (channel->internal.hmac ?
	  (char *)silc_hmac_get_name(channel->internal.hmac) :
	  SILC_DEFAULT_HMAC);
  if (!silc_hmac_alloc(hmac, NULL, &channel->internal.hmac)) {
    conn->context_type = SILC_ID_CHANNEL;
    conn->channel_entry = channel;
    client->internal->ops->say(
			   conn->client, conn,
			   SILC_CLIENT_MESSAGE_ERROR,
			   "Cannot talk to channel: unsupported HMAC %s",
			   hmac);
    conn->context_type = SILC_ID_NONE;
    silc_client_unref_channel(client, conn, channel);
    silc_channel_key_payload_free(payload);
    return FALSE;
  }

  channel->cipher = silc_cipher_get_name(channel->internal.send_key);
  channel->hmac = silc_hmac_get_name(channel->internal.hmac);

  /* Set HMAC key */
  silc_hash_make(silc_hmac_get_hash(channel->internal.hmac), key,
		 tmp_len, hash);
  silc_hmac_set_key(channel->internal.hmac, hash,
		    silc_hash_len(silc_hmac_get_hash(channel->internal.hmac)));
  memset(hash, 0, sizeof(hash));
  silc_channel_key_payload_free(payload);

  silc_client_unref_channel(client, conn, channel);

  return TRUE;
}

/* Received channel key packet.  The key will replace old channel key. */

SILC_FSM_STATE(silc_client_channel_key)
{
  SilcClientConnection conn = fsm_context;
  SilcClient client = conn->client;
  SilcPacket packet = state_context;

  SILC_LOG_DEBUG(("Received channel key"));

  /* Save the key */
  silc_client_save_channel_key(client, conn, &packet->buffer, NULL);
  silc_packet_free(packet);

  return SILC_FSM_FINISH;
}

/**************************** Channel Private Key ***************************/

/* Add new channel private key */

SilcBool silc_client_add_channel_private_key(SilcClient client,
					     SilcClientConnection conn,
					     SilcChannelEntry channel,
					     const char *name,
					     char *cipher,
					     char *hmac,
					     unsigned char *key,
					     SilcUInt32 key_len,
					     SilcChannelPrivateKey *ret_key)
{
  SilcChannelPrivateKey entry;
  unsigned char hash[SILC_HASH_MAXLEN];
  SilcSKEKeyMaterial keymat;

  if (!client || !conn || !channel)
    return FALSE;

  if (!cipher)
    cipher = SILC_DEFAULT_CIPHER;
  if (!hmac)
    hmac = SILC_DEFAULT_HMAC;

  if (!silc_cipher_is_supported(cipher))
    return FALSE;
  if (!silc_hmac_is_supported(hmac))
    return FALSE;

  if (!channel->internal.private_keys) {
    channel->internal.private_keys = silc_dlist_init();
    if (!channel->internal.private_keys)
      return FALSE;
  }

  /* Produce the key material */
  keymat = silc_ske_process_key_material_data(key, key_len, 16, 256, 16,
					      conn->internal->sha1hash);
  if (!keymat)
    return FALSE;

  /* Save the key */
  entry = silc_calloc(1, sizeof(*entry));
  if (!entry) {
    silc_ske_free_key_material(keymat);
    return FALSE;
  }
  entry->name = name ? strdup(name) : NULL;

  /* Allocate the cipher and set the key */
  if (!silc_cipher_alloc(cipher, &entry->send_key)) {
    silc_free(entry);
    silc_free(entry->name);
    silc_ske_free_key_material(keymat);
    return FALSE;
  }
  if (!silc_cipher_alloc(cipher, &entry->receive_key)) {
    silc_free(entry);
    silc_free(entry->name);
    silc_cipher_free(entry->send_key);
    silc_ske_free_key_material(keymat);
    return FALSE;
  }
  silc_cipher_set_key(entry->send_key, keymat->send_enc_key,
		      keymat->enc_key_len, TRUE);
  silc_cipher_set_key(entry->receive_key, keymat->send_enc_key,
		      keymat->enc_key_len, FALSE);

  /* Generate HMAC key from the channel key data and set it */
  if (!silc_hmac_alloc(hmac, NULL, &entry->hmac)) {
    silc_free(entry);
    silc_free(entry->name);
    silc_cipher_free(entry->send_key);
    silc_cipher_free(entry->receive_key);
    silc_ske_free_key_material(keymat);
    return FALSE;
  }
  silc_hash_make(silc_hmac_get_hash(entry->hmac), keymat->send_enc_key,
		 keymat->enc_key_len / 8, hash);
  silc_hmac_set_key(entry->hmac, hash,
		    silc_hash_len(silc_hmac_get_hash(entry->hmac)));
  memset(hash, 0, sizeof(hash));

  /* Add to the private keys list */
  silc_dlist_add(channel->internal.private_keys, entry);

  if (!channel->internal.curr_key) {
    channel->internal.curr_key = entry;
    channel->cipher = silc_cipher_get_name(entry->send_key);
    channel->hmac = silc_cipher_get_name(entry->send_key);
  }

  /* Free the key material */
  silc_ske_free_key_material(keymat);

  if (ret_key)
    *ret_key = entry;

  return TRUE;
}

/* Removes all private keys from the `channel'. The old channel key is used
   after calling this to protect the channel messages. Returns FALSE on
   on error, TRUE otherwise. */

SilcBool silc_client_del_channel_private_keys(SilcClient client,
					      SilcClientConnection conn,
					      SilcChannelEntry channel)
{
  SilcChannelPrivateKey entry;

  if (!client || !conn || !channel)
    return FALSE;

  if (!channel->internal.private_keys)
    return FALSE;

  silc_dlist_start(channel->internal.private_keys);
  while ((entry = silc_dlist_get(channel->internal.private_keys))) {
    silc_dlist_del(channel->internal.private_keys, entry);
    silc_free(entry->name);
    silc_cipher_free(entry->send_key);
    silc_cipher_free(entry->receive_key);
    silc_hmac_free(entry->hmac);
    silc_free(entry);
  }

  channel->internal.curr_key = NULL;
  if (channel->internal.send_key)
    channel->cipher = silc_cipher_get_name(channel->internal.send_key);
  else
    channel->cipher = NULL;
  if (channel->internal.hmac)
    channel->hmac = silc_hmac_get_name(channel->internal.hmac);
  else
    channel->hmac = NULL;

  silc_dlist_uninit(channel->internal.private_keys);
  channel->internal.private_keys = NULL;

  return TRUE;
}

/* Removes and frees private key `key' from the channel `channel'. The `key'
   is retrieved by calling the function silc_client_list_channel_private_keys.
   The key is not used after this. If the key was last private key then the
   old channel key is used hereafter to protect the channel messages. This
   returns FALSE on error, TRUE otherwise. */

SilcBool silc_client_del_channel_private_key(SilcClient client,
					     SilcClientConnection conn,
					     SilcChannelEntry channel,
					     SilcChannelPrivateKey key)
{
  SilcChannelPrivateKey entry;

  if (!client || !conn || !channel)
    return FALSE;

  if (!channel->internal.private_keys)
    return FALSE;

  silc_dlist_start(channel->internal.private_keys);
  while ((entry = silc_dlist_get(channel->internal.private_keys))) {
    if (entry != key)
      continue;

    if (channel->internal.curr_key == entry) {
      channel->internal.curr_key = NULL;
      channel->cipher = silc_cipher_get_name(channel->internal.send_key);
      channel->hmac = silc_hmac_get_name(channel->internal.hmac);
    }

    silc_dlist_del(channel->internal.private_keys, entry);
    silc_free(entry->name);
    silc_cipher_free(entry->send_key);
    silc_cipher_free(entry->receive_key);
    silc_hmac_free(entry->hmac);
    silc_free(entry);

    if (silc_dlist_count(channel->internal.private_keys) == 0) {
      silc_dlist_uninit(channel->internal.private_keys);
      channel->internal.private_keys = NULL;
    }

    return TRUE;
  }

  return FALSE;
}

/* Returns array (pointers) of private keys associated to the `channel'.
   The caller must free the array by calling the function
   silc_client_free_channel_private_keys. The pointers in the array may be
   used to delete the specific key by giving the pointer as argument to the
   function silc_client_del_channel_private_key. */

SilcDList silc_client_list_channel_private_keys(SilcClient client,
						SilcClientConnection conn,
						SilcChannelEntry channel)
{
  SilcChannelPrivateKey entry;
  SilcDList list;

  if (!client || !conn || !channel)
    return FALSE;

  if (!channel->internal.private_keys)
    return NULL;

  list = silc_dlist_init();
  if (!list)
    return NULL;

  silc_dlist_start(channel->internal.private_keys);
  while ((entry = silc_dlist_get(channel->internal.private_keys)))
    silc_dlist_add(list, entry);

  return list;
}

/* Sets the `key' to be used as current channel private key on the
   `channel'.  Packet sent after calling this function will be secured
   with `key'. */

void silc_client_current_channel_private_key(SilcClient client,
					     SilcClientConnection conn,
					     SilcChannelEntry channel,
					     SilcChannelPrivateKey key)
{
  if (!channel)
    return;
  channel->internal.curr_key = key;
  channel->cipher = silc_cipher_get_name(key->send_key);
  channel->hmac = silc_hmac_get_name(key->hmac);
}

/***************************** Utility routines *****************************/

/* Returns the SilcChannelUser entry if the `client_entry' is joined on the
   channel indicated by the `channel'. NULL if client is not joined on
   the channel. */

SilcChannelUser silc_client_on_channel(SilcChannelEntry channel,
				       SilcClientEntry client_entry)
{
  SilcChannelUser chu;

  if (silc_hash_table_find(channel->user_list, client_entry, NULL,
			   (void *)&chu))
    return chu;

  return NULL;
}

/* Adds client to channel.  Returns TRUE if user was added or is already
   added to the channel, FALSE on error.  Must be called with both `channel'
   and `client_entry' locked. */

SilcBool silc_client_add_to_channel(SilcClient client,
				    SilcClientConnection conn,
				    SilcChannelEntry channel,
				    SilcClientEntry client_entry,
				    SilcUInt32 cumode)
{
  SilcChannelUser chu;

  if (silc_client_on_channel(channel, client_entry))
    return TRUE;

  SILC_LOG_DEBUG(("Add client %s to channel", client_entry->nickname));

  chu = silc_calloc(1, sizeof(*chu));
  if (!chu)
    return FALSE;

  chu->client = client_entry;
  chu->channel = channel;
  chu->mode = cumode;

  silc_client_ref_client(client, conn, client_entry);
  silc_client_ref_channel(client, conn, channel);

  silc_hash_table_add(channel->user_list, client_entry, chu);
  silc_hash_table_add(client_entry->channels, channel, chu);

  return TRUE;
}

/* Removes client from a channel.  Returns FALSE if user is not on channel.
   This handles entry locking internally. */

SilcBool silc_client_remove_from_channel(SilcClient client,
					 SilcClientConnection conn,
					 SilcChannelEntry channel,
					 SilcClientEntry client_entry)
{
  SilcChannelUser chu;

  chu = silc_client_on_channel(channel, client_entry);
  if (!chu)
    return FALSE;

  SILC_LOG_DEBUG(("Remove client %s from channel", client_entry->nickname));

  silc_rwlock_wrlock(client_entry->internal.lock);
  silc_rwlock_wrlock(channel->internal.lock);

  silc_hash_table_del(chu->client->channels, chu->channel);
  silc_hash_table_del(chu->channel->user_list, chu->client);
  silc_free(chu);

  /* If channel became empty, delete it */
  if (!silc_hash_table_count(channel->user_list))
    silc_client_del_channel(client, conn, channel);

  silc_rwlock_unlock(client_entry->internal.lock);
  silc_rwlock_unlock(channel->internal.lock);

  silc_client_unref_client(client, conn, client_entry);
  silc_client_unref_channel(client, conn, channel);

  return TRUE;
}

/* Removes a client entry from all channels it has joined.  This handles
   entry locking internally. */

void silc_client_remove_from_channels(SilcClient client,
				      SilcClientConnection conn,
				      SilcClientEntry client_entry)
{
  SilcHashTableList htl;
  SilcChannelUser chu;

  if (!silc_hash_table_count(client_entry->channels))
    return;

  SILC_LOG_DEBUG(("Remove client from all joined channels"));

  silc_rwlock_wrlock(client_entry->internal.lock);

  silc_hash_table_list(client_entry->channels, &htl);
  while (silc_hash_table_get(&htl, NULL, (void *)&chu)) {
    silc_rwlock_wrlock(chu->channel->internal.lock);

    silc_hash_table_del(chu->client->channels, chu->channel);
    silc_hash_table_del(chu->channel->user_list, chu->client);

    /* If channel became empty, delete it */
    if (!silc_hash_table_count(chu->channel->user_list))
      silc_client_del_channel(client, conn, chu->channel);

    silc_rwlock_unlock(chu->channel->internal.lock);

    silc_client_unref_client(client, conn, chu->client);
    silc_client_unref_channel(client, conn, chu->channel);
    silc_free(chu);
  }

  silc_rwlock_unlock(client_entry->internal.lock);

  silc_hash_table_list_reset(&htl);
}

/* Empties channel from users.  This handles entry locking internally. */

void silc_client_empty_channel(SilcClient client,
			       SilcClientConnection conn,
			       SilcChannelEntry channel)
{
  SilcHashTableList htl;
  SilcChannelUser chu;

  silc_rwlock_wrlock(channel->internal.lock);

  silc_hash_table_list(channel->user_list, &htl);
  while (silc_hash_table_get(&htl, NULL, (void *)&chu)) {
    silc_hash_table_del(chu->client->channels, chu->channel);
    silc_hash_table_del(chu->channel->user_list, chu->client);
    silc_client_unref_client(client, conn, chu->client);
    silc_client_unref_channel(client, conn, chu->channel);
    silc_free(chu);
  }

  silc_rwlock_unlock(channel->internal.lock);

  silc_hash_table_list_reset(&htl);
}

/* Save public keys to channel public key list.  Removes keys that are
   marked to be removed.  Must be called with `channel' locked. */

SilcBool silc_client_channel_save_public_keys(SilcChannelEntry channel,
					      unsigned char *chpk_list,
					      SilcUInt32 chpk_list_len,
					      SilcBool remove_all)
{
  SilcArgumentDecodedList a, b;
  SilcDList chpks;
  SilcBool found;

  if (remove_all) {
    /* Remove all channel public keys */
    if (!channel->channel_pubkeys)
      return FALSE;

    silc_dlist_start(channel->channel_pubkeys);
    while ((b = silc_dlist_get(channel->channel_pubkeys)))
      silc_dlist_del(channel->channel_pubkeys, b);

    silc_dlist_uninit(channel->channel_pubkeys);
    channel->channel_pubkeys = NULL;

    return TRUE;
  }

  /* Parse channel public key list and add or remove public keys */
  chpks = silc_argument_list_parse_decoded(chpk_list, chpk_list_len,
					   SILC_ARGUMENT_PUBLIC_KEY);
  if (!chpks)
    return FALSE;

  if (!channel->channel_pubkeys) {
    channel->channel_pubkeys = silc_dlist_init();
    if (!channel->channel_pubkeys) {
      silc_argument_list_free(chpks, SILC_ARGUMENT_PUBLIC_KEY);
      return FALSE;
    }
  }

  silc_dlist_start(chpks);
  while ((a = silc_dlist_get(chpks))) {
    found = FALSE;
    silc_dlist_start(channel->channel_pubkeys);
    while ((b = silc_dlist_get(channel->channel_pubkeys))) {
      if (silc_pkcs_public_key_compare(a->argument, b->argument)) {
	found = TRUE;
	break;
      }
    }

    if ((a->arg_type == 0x00 || a->arg_type == 0x03) && !found) {
      silc_dlist_add(channel->channel_pubkeys, a);
      silc_dlist_del(chpks, a);
    } else if (a->arg_type == 0x01 && found) {
      silc_dlist_del(channel->channel_pubkeys, b);
    }
  }

  silc_argument_list_free(chpks, SILC_ARGUMENT_PUBLIC_KEY);

  return TRUE;
}

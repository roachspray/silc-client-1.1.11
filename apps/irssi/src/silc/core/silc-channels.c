/*
  silc-channels.c : irssi

  Copyright (C) 2000 - 2001, 2004, 2006, 2007 Timo Sirainen
                Pekka Riikonen <priikone@silcnet.org>

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

#include "module.h"

#include "net-nonblock.h"
#include "net-sendbuffer.h"
#include "signals.h"
#include "servers.h"
#include "commands.h"
#include "levels.h"
#include "modules.h"
#include "rawlog.h"
#include "misc.h"
#include "settings.h"
#include "special-vars.h"

#include "channels-setup.h"

#include "silc-servers.h"
#include "silc-channels.h"
#include "silc-queries.h"
#include "silc-nicklist.h"
#include "silc-cmdqueue.h"
#include "window-item-def.h"

#include "fe-common/core/printtext.h"
#include "fe-common/silc/module-formats.h"

#include "silc-commands.h"

void sig_mime(SILC_SERVER_REC *server, SILC_CHANNEL_REC *channel,
	      const char *blob, const char *nick, int verified)
{
  unsigned char *message;
  SilcUInt32 message_len;
  SilcMime mime;

  if (!(IS_SILC_SERVER(server)))
    return;

  message = silc_unescape_data(blob, &message_len);

  mime = silc_mime_decode(NULL, message, message_len);
  if (!mime) {
    silc_free(message);
    return;
  }

  printformat_module("fe-common/silc", server,
		     channel == NULL ? NULL : channel->name,
		     MSGLEVEL_CRAP, SILCTXT_MESSAGE_DATA,
		     nick == NULL ? "[<unknown>]" : nick,
		     silc_mime_get_field(mime, "Content-Type"));

  silc_free(message);
  silc_mime_free(mime);
}

SILC_CHANNEL_REC *silc_channel_create(SILC_SERVER_REC *server,
				      const char *name,
				      const char *visible_name,
				      int automatic)
{
  SILC_CHANNEL_REC *rec;

  g_return_val_if_fail(server == NULL || IS_SILC_SERVER(server), NULL);
  g_return_val_if_fail(name != NULL, NULL);

  rec = g_new0(SILC_CHANNEL_REC, 1);
  rec->chat_type = SILC_PROTOCOL;
  channel_init((CHANNEL_REC *)rec, (SERVER_REC *)server, name, name,
	       automatic);
  return rec;
}

static void sig_channel_destroyed(SILC_CHANNEL_REC *channel)
{
  if (!IS_SILC_CHANNEL(channel))
    return;
  if (channel->server && channel->server->disconnected)
    return;

  if (channel->server != NULL && !channel->left && !channel->kicked) {
    /* destroying channel record without actually
       having left the channel yet */
    silc_command_exec(channel->server, "LEAVE", channel->name);
    /* enable queueing because we destroy the channel immedially */
    silc_queue_enable(channel->server->conn);
  }
}

static void silc_channels_join(SILC_SERVER_REC *server,
			       const char *channels, int automatic)
{
  char **list, **tmp;
  char *channel, *key;
  SILC_CHANNEL_REC *chanrec;
  CHANNEL_SETUP_REC *schannel;
  GString *tmpstr;

  list = g_strsplit(channels, ",", -1);
  for (tmp = list; *tmp != NULL; tmp++) {
    chanrec = silc_channel_find(server, *tmp);
    if (chanrec)
      continue;

    channel = *tmp;
    key = strchr(channel, ' ');
    if (key != NULL) {
      *key = '\0';
      key++;
    }
    tmpstr = g_string_new(NULL);

    schannel = channel_setup_find(channel, server->connrec->chatnet);
    if (key && *key != '\0')
      g_string_sprintfa(tmpstr, "%s %s", channel, key);
    else if (schannel && schannel->password && schannel->password[0] != '\0')
      g_string_sprintfa(tmpstr, "%s %s", channel, schannel->password);
    else
      g_string_sprintfa(tmpstr, "%s", channel);


    silc_command_exec(server, "JOIN", tmpstr->str);
    g_string_free(tmpstr, FALSE);
  }

  g_strfreev(list);
}

static void sig_connected(SILC_SERVER_REC *server)
{
  if (IS_SILC_SERVER(server))
    server->channels_join = (void *) silc_channels_join;
}

/* "server quit" signal from the core to indicate that QUIT command
   was called. */

static void sig_server_quit(SILC_SERVER_REC *server, const char *msg)
{
  if (IS_SILC_SERVER(server) && server->conn)
    silc_command_exec(server, "QUIT", msg);
}

static void sig_silc_channel_joined(SILC_CHANNEL_REC *channel)
{
  CHANNEL_SETUP_REC *rec;

  if (!IS_SILC_CHANNEL(channel))
    return;
  if (channel->server && channel->server->disconnected)
    return;
  if (!channel->server)
    return;
  if (channel->session_rejoin)
    return;
  
  rec = channel_setup_find(channel->name, channel->server->connrec->chatnet); 

  if (rec == NULL || rec->autosendcmd == NULL || !*rec->autosendcmd)
    return;

  eval_special_string(rec->autosendcmd, "", (SERVER_REC*)channel->server, (CHANNEL_REC*)channel);
}

/* Find Irssi channel entry by SILC channel entry */

SILC_CHANNEL_REC *silc_channel_find_entry(SILC_SERVER_REC *server,
					  SilcChannelEntry entry)
{
  GSList *tmp;

  g_return_val_if_fail(IS_SILC_SERVER(server), NULL);

  for (tmp = server->channels; tmp != NULL; tmp = tmp->next) {
    SILC_CHANNEL_REC *rec = tmp->data;

    if (rec->entry == entry)
      return rec;
  }

  return NULL;
}

/* PART (LEAVE) command. */

static void command_part(const char *data, SILC_SERVER_REC *server,
			 WI_ITEM_REC *item)
{
  SILC_CHANNEL_REC *chanrec;
  char userhost[256];

  CMD_SILC_SERVER(server);

  if (!IS_SILC_SERVER(server) || !server->connected)
    cmd_return_error(CMDERR_NOT_CONNECTED);

  if (!strcmp(data, "*") || *data == '\0') {
    if (!IS_SILC_CHANNEL(item))
      cmd_return_error(CMDERR_NOT_JOINED);
    data = item->visible_name;
  }

  chanrec = silc_channel_find(server, data);
  if (chanrec == NULL)
    cmd_return_error(CMDERR_CHAN_NOT_FOUND);

  memset(userhost, 0, sizeof(userhost));
  snprintf(userhost, sizeof(userhost) - 1, "%s@%s",
	   server->conn->local_entry->username,
	   server->conn->local_entry->hostname);
  signal_emit("message part", 5, server, chanrec->name,
	      server->nick, userhost, "");

  chanrec->left = TRUE;
  silc_command_exec(server, "LEAVE", chanrec->name);
  /* enable queueing because we destroy the channel immedially */
  silc_queue_enable(server->conn);
  signal_stop();

  channel_destroy(CHANNEL(chanrec));
}


/* ACTION local command. */

static void command_action(const char *data, SILC_SERVER_REC *server,
			   WI_ITEM_REC *item)
{
  GHashTable *optlist;
  char *target, *msg;
  char *message = NULL;
  int target_type;
  void *free_arg;
  SilcBool sign = FALSE;

  CMD_SILC_SERVER(server);
  if (!IS_SILC_SERVER(server) || !server->connected)
    cmd_return_error(CMDERR_NOT_CONNECTED);

  if ((item != NULL) && (!IS_SILC_CHANNEL(item) && !IS_SILC_QUERY(item)))
    cmd_return_error(CMDERR_NOT_JOINED);

  /* Now parse all arguments */
  if (!cmd_get_params(data, &free_arg, 2 | PARAM_FLAG_OPTIONS |
		      PARAM_FLAG_GETREST,
		      "action", &optlist, &target, &msg))
    return;

  if (*target == '\0' || *msg == '\0')
    cmd_param_error(CMDERR_NOT_ENOUGH_PARAMS);

  if (strcmp(target, "*") == 0) {
    /* send to active channel/query */
    if (item == NULL)
      cmd_param_error(CMDERR_NOT_JOINED);

    target_type = IS_SILC_CHANNEL(item) ?
	    SEND_TARGET_CHANNEL : SEND_TARGET_NICK;
    target = (char *)window_item_get_target(item);
  } else if (g_hash_table_lookup(optlist, "channel") != NULL)
    target_type = SEND_TARGET_CHANNEL;
  else {
    target_type = SEND_TARGET_NICK;
  }

  if (!silc_term_utf8()) {
    int len = silc_utf8_encoded_len(msg, strlen(msg),
				    SILC_STRING_LOCALE);
    message = silc_calloc(len + 1, sizeof(*message));
    g_return_if_fail(message != NULL);
    silc_utf8_encode(msg, strlen(msg), SILC_STRING_LOCALE,
		     message, len);
  }

  if (target != NULL) {
    if (target_type == SEND_TARGET_CHANNEL) {
      sign = (g_hash_table_lookup(optlist, "sign") ? TRUE :
	      settings_get_bool("sign_channel_messages") ? TRUE : FALSE);
      if (silc_send_channel(server, target, (message != NULL ? message : msg),
		            SILC_MESSAGE_FLAG_ACTION | SILC_MESSAGE_FLAG_UTF8 |
			    (sign ? SILC_MESSAGE_FLAG_SIGNED : 0))) {
	if (g_hash_table_lookup(optlist, "sign"))
          signal_emit("message silc signed_own_action", 3, server, msg, target);
	else
          signal_emit("message silc own_action", 3, server, msg, target);
      }
    } else {
      sign = (g_hash_table_lookup(optlist, "sign") ? TRUE :
	      settings_get_bool("sign_private_messages") ? TRUE : FALSE);
      if (silc_send_msg(server, target, (message != NULL ? message : msg),
			(message != NULL ? strlen(message) : strlen(msg)),
			SILC_MESSAGE_FLAG_ACTION | SILC_MESSAGE_FLAG_UTF8 |
			(sign ? SILC_MESSAGE_FLAG_SIGNED : 0))) {
	if (g_hash_table_lookup(optlist, "sign"))
	  signal_emit("message silc signed_own_private_action", 3,
			  server, msg, target);
	else
	  signal_emit("message silc own_private_action", 3,
			  server, msg, target);
      }
    }
  }

  cmd_params_free(free_arg);
  silc_free(message);
}

/* ME local command. */

static void command_me(const char *data, SILC_SERVER_REC *server,
		       WI_ITEM_REC *item)
{
  char *tmpcmd;

  CMD_SILC_SERVER(server);
  if (!IS_SILC_SERVER(server) || !server->connected)
    cmd_return_error(CMDERR_NOT_CONNECTED);

  if (!IS_SILC_CHANNEL(item) && !IS_SILC_QUERY(item))
    cmd_return_error(CMDERR_NOT_JOINED);

  if (IS_SILC_CHANNEL(item))
    tmpcmd = g_strdup_printf("-channel %s %s", item->visible_name, data);
  else
    tmpcmd = g_strdup_printf("%s %s", item->visible_name, data);

  command_action(tmpcmd, server, item);
  g_free(tmpcmd);
}

/* NOTICE local command. */

static void command_notice(const char *data, SILC_SERVER_REC *server,
			   WI_ITEM_REC *item)
{
  GHashTable *optlist;
  char *target, *msg;
  char *message = NULL;
  int target_type;
  void *free_arg;
  SilcBool sign;

  CMD_SILC_SERVER(server);
  if (!IS_SILC_SERVER(server) || !server->connected)
    cmd_return_error(CMDERR_NOT_CONNECTED);

  if ((item != NULL) && (!IS_SILC_CHANNEL(item) && !IS_SILC_QUERY(item)))
    cmd_return_error(CMDERR_NOT_JOINED);

  /* Now parse all arguments */
  if (!cmd_get_params(data, &free_arg, 2 | PARAM_FLAG_OPTIONS |
		      PARAM_FLAG_GETREST,
		      "notice", &optlist, &target, &msg))
    return;

  if (*target == '\0' || *msg == '\0')
    cmd_param_error(CMDERR_NOT_ENOUGH_PARAMS);

  if (strcmp(target, "*") == 0) {
    /* send to active channel/query */
    if (item == NULL)
      cmd_param_error(CMDERR_NOT_JOINED);

    target_type = IS_SILC_CHANNEL(item) ?
	    SEND_TARGET_CHANNEL : SEND_TARGET_NICK;
    target = (char *)window_item_get_target(item);
  } else if (g_hash_table_lookup(optlist, "channel") != NULL)
    target_type = SEND_TARGET_CHANNEL;
  else {
    target_type = SEND_TARGET_NICK;
  }

  if (!silc_term_utf8()) {
    int len = silc_utf8_encoded_len(msg, strlen(msg),
				    SILC_STRING_LOCALE);
    message = silc_calloc(len + 1, sizeof(*message));
    g_return_if_fail(message != NULL);
    silc_utf8_encode(msg, strlen(msg), SILC_STRING_LOCALE,
		     message, len);
  }

  if (target != NULL) {
    if (target_type == SEND_TARGET_CHANNEL) {
      sign = (g_hash_table_lookup(optlist, "sign") ? TRUE :
	      settings_get_bool("sign_channel_messages") ? TRUE : FALSE);
      if (silc_send_channel(server, target, (message != NULL ? message : msg),
		            SILC_MESSAGE_FLAG_NOTICE | SILC_MESSAGE_FLAG_UTF8 |
			    (sign ? SILC_MESSAGE_FLAG_SIGNED : 0))) {
	if (g_hash_table_lookup(optlist, "sign"))
          signal_emit("message silc signed_own_notice", 3, server, msg, target);
	else
          signal_emit("message silc own_notice", 3, server, msg, target);
      }
    } else {
      sign = (g_hash_table_lookup(optlist, "sign") ? TRUE :
	      settings_get_bool("sign_private_messages") ? TRUE : FALSE);
      if (silc_send_msg(server, target, (message != NULL ? message : msg),
			(message != NULL ? strlen(message) : strlen(msg)),
			SILC_MESSAGE_FLAG_NOTICE | SILC_MESSAGE_FLAG_UTF8 |
			(sign ? SILC_MESSAGE_FLAG_SIGNED : 0))) {
	if (g_hash_table_lookup(optlist, "sign"))
	  signal_emit("message silc signed_own_private_notice", 3,
			  server, msg, target);
	else
	  signal_emit("message silc own_private_notice", 3,
			  server, msg, target);
      }
    }
  }

  cmd_params_free(free_arg);
  silc_free(message);
}

/* AWAY local command.  Sends UMODE command that sets the SILC_UMODE_GONE
   flag. */

bool silc_set_away(const char *reason, SILC_SERVER_REC *server)
{
  bool set;

  if (!IS_SILC_SERVER(server) || !server->connected)
    return FALSE;

  if (*reason == '\0') {
    /* Remove any possible away message */
    silc_client_set_away_message(silc_client, server->conn, NULL);
    set = FALSE;

    printformat_module("fe-common/silc", server, NULL, MSGLEVEL_CRAP,
		       SILCTXT_UNSET_AWAY);
  } else {
    /* Set the away message */
    silc_client_set_away_message(silc_client, server->conn, (char *)reason);
    set = TRUE;

    printformat_module("fe-common/silc", server, NULL, MSGLEVEL_CRAP,
		       SILCTXT_SET_AWAY, reason);
  }

  server->usermode_away = set;
  g_free_and_null(server->away_reason);
  if (set)
    server->away_reason = g_strdup((char *)reason);

  signal_emit("away mode changed", 1, server);

  return set;
}

static void command_away(const char *data, SILC_SERVER_REC *server,
			 WI_ITEM_REC *item)
{
  CMD_SILC_SERVER(server);

  if (!IS_SILC_SERVER(server) || !server->connected)
    cmd_return_error(CMDERR_NOT_CONNECTED);

  g_free_and_null(server->away_reason);
  if ((data) && (*data != '\0'))
    server->away_reason = g_strdup(data);

  silc_command_exec(server, "UMODE",
		    (server->away_reason != NULL) ? "+g" : "-g");
}

typedef struct {
  SILC_SERVER_REC *server;
  int type;			/* 1 = msg, 2 = channel */
  SilcBool responder;
} *KeyInternal;

/* Key agreement callback that is called after the key agreement protocol
   has been performed. This is called also if error occured during the
   key agreement protocol. The `key' is the allocated key material and
   the caller is responsible of freeing it. The `key' is NULL if error
   has occured. The application can freely use the `key' to whatever
   purpose it needs. See lib/silcske/silcske.h for the definition of
   the SilcSKEKeyMaterial structure. */

static void keyagr_completion(SilcClient client,
			      SilcClientConnection conn,
			      SilcClientEntry client_entry,
			      SilcKeyAgreementStatus status,
			      SilcSKEKeyMaterial key,
			      void *context)
{
  KeyInternal i = (KeyInternal)context;

  switch(status) {
  case SILC_KEY_AGREEMENT_OK:
    printformat_module("fe-common/silc", i->server, NULL, MSGLEVEL_CRAP,
		       SILCTXT_KEY_AGREEMENT_OK, client_entry->nickname);

    if (i->type == 1) {
      /* Set the private key for this client */
      silc_client_del_private_message_key(client, conn, client_entry);
      silc_client_add_private_message_key_ske(client, conn, client_entry,
					      NULL, NULL, key);
      printformat_module("fe-common/silc", i->server, NULL, MSGLEVEL_CRAP,
			 SILCTXT_KEY_AGREEMENT_PRIVMSG,
			 client_entry->nickname);
      silc_ske_free_key_material(key);
    }

    break;

  case SILC_KEY_AGREEMENT_ERROR:
  case SILC_KEY_AGREEMENT_NO_MEMORY:
    printformat_module("fe-common/silc", i->server, NULL, MSGLEVEL_CRAP,
		       SILCTXT_KEY_AGREEMENT_ERROR, client_entry->nickname);
    break;

  case SILC_KEY_AGREEMENT_FAILURE:
    printformat_module("fe-common/silc", i->server, NULL, MSGLEVEL_CRAP,
		       SILCTXT_KEY_AGREEMENT_FAILURE, client_entry->nickname);
    break;

  case SILC_KEY_AGREEMENT_TIMEOUT:
    printformat_module("fe-common/silc", i->server, NULL, MSGLEVEL_CRAP,
		       SILCTXT_KEY_AGREEMENT_TIMEOUT, client_entry->nickname);
    break;

  case SILC_KEY_AGREEMENT_ABORTED:
    printformat_module("fe-common/silc", i->server, NULL, MSGLEVEL_CRAP,
		       SILCTXT_KEY_AGREEMENT_ABORTED, client_entry->nickname);
    break;

  case SILC_KEY_AGREEMENT_ALREADY_STARTED:
    printformat_module("fe-common/silc", i->server, NULL, MSGLEVEL_CRAP,
		       SILCTXT_KEY_AGREEMENT_ALREADY_STARTED,
		       client_entry->nickname);
    break;

  case SILC_KEY_AGREEMENT_SELF_DENIED:
    printformat_module("fe-common/silc", i->server, NULL, MSGLEVEL_CRAP,
		       SILCTXT_KEY_AGREEMENT_SELF_DENIED);
    break;

  default:
    break;
  }

  if (i)
    silc_free(i);
}

/* Local command KEY. This command is used to set and unset private
   keys for channels, set and unset private keys for private messages
   with remote clients and to send key agreement requests and
   negotiate the key agreement protocol with remote client.  The
   key agreement is supported only to negotiate private message keys,
   it currently cannot be used to negotiate private keys for channels,
   as it is not convenient for that purpose. */

typedef struct {
  SILC_SERVER_REC *server;
  char *data;
  char *nick;
  WI_ITEM_REC *item;
} *KeyGetClients;

/* Callback to be called after client information is resolved from the
   server. */

static void silc_client_command_key_get_clients(SilcClient client,
						SilcClientConnection conn,
						SilcStatus status,
						SilcDList clients,
						void *context)
{
  KeyGetClients internal = (KeyGetClients)context;

  if (!clients) {
    printtext(NULL, NULL, MSGLEVEL_CLIENTERROR, "Unknown nick: %s",
	      internal->nick);
    silc_free(internal->data);
    silc_free(internal->nick);
    silc_free(internal);
    return;
  }

  signal_emit("command key", 3, internal->data, internal->server,
	      internal->item);

  silc_free(internal->data);
  silc_free(internal->nick);
  silc_free(internal);
}

static void command_key(const char *data, SILC_SERVER_REC *server,
			WI_ITEM_REC *item)
{
  SilcClientConnection conn;
  SilcClientEntry client_entry = NULL;
  SilcDList clients;
  SILC_CHANNEL_REC *chanrec = NULL;
  SilcChannelEntry channel_entry = NULL;
  char *nickname = NULL, *tmp;
  int command = 0, port = 0, type = 0;
  char *hostname = NULL;
  KeyInternal internal = NULL;
  SilcUInt32 argc = 0;
  unsigned char **argv;
  SilcUInt32 *argv_lens, *argv_types;
  char *bindhost = NULL;
  SilcChannelPrivateKey ch = NULL;
  SilcDList ckeys;
  SilcBool udp = FALSE;
  int i;

  CMD_SILC_SERVER(server);

  if (!server || !IS_SILC_SERVER(server) || !server->connected)
    cmd_return_error(CMDERR_NOT_CONNECTED);

  conn = server->conn;

  /* Now parse all arguments */
  tmp = g_strconcat("KEY", " ", data, NULL);
  silc_parse_command_line(tmp, &argv, &argv_lens, &argv_types, &argc, 7);
  g_free(tmp);

  if (argc < 4)
    cmd_return_error(CMDERR_NOT_ENOUGH_PARAMS);

  /* Get type */
  if (!strcasecmp(argv[1], "msg"))
    type = 1;
  if (!strcasecmp(argv[1], "channel"))
    type = 2;

  if (type == 0)
    cmd_return_error(CMDERR_NOT_ENOUGH_PARAMS);

  if (type == 1) {
    if (argv[2][0] == '*') {
      nickname = strdup("*");
    } else {
      /* Parse the typed nickname. */
      silc_client_nickname_parse(silc_client, conn, argv[2], &nickname);
      if (!nickname)
	nickname = strdup(argv[2]);

      /* Find client entry */
      clients = silc_client_get_clients_local(silc_client, conn, argv[2],
					      FALSE);
      if (!clients) {
	KeyGetClients inter = silc_calloc(1, sizeof(*inter));
	inter->server = server;
	inter->data = strdup(data);
	inter->nick = strdup(nickname);
	inter->item = item;
	silc_client_get_clients(silc_client, conn, nickname, NULL,
				silc_client_command_key_get_clients, inter);
	goto out;
      }

      client_entry = silc_dlist_get(clients);
      silc_client_list_free(silc_client, conn, clients);
    }
  }

  if (type == 2) {
    /* Get channel entry */
    char *name;

    if (argv[2][0] == '*') {
      if (!conn->current_channel)
	cmd_return_error(CMDERR_NOT_JOINED);
      name = conn->current_channel->channel_name;
    } else {
      name = argv[2];
    }

    chanrec = silc_channel_find(server, name);
    if (chanrec == NULL)
      cmd_return_error(CMDERR_CHAN_NOT_FOUND);
    channel_entry = chanrec->entry;
  }

  /* Set command */
  if (!strcasecmp(argv[3], "set")) {
    command = 1;

    if (argc >= 5) {
      char *cipher = NULL, *hmac = NULL;

      if (argc >= 6)
	cipher = argv[5];
      if (argc >= 7)
	hmac = argv[6];

      if (type == 1 && client_entry) {
	/* Set private message key */
	silc_client_del_private_message_key(silc_client, conn, client_entry);
	silc_client_add_private_message_key(silc_client, conn, client_entry,
					    cipher, hmac,
					    argv[4], argv_lens[4]);
      } else if (type == 2) {
	/* Set private channel key */
	if (!(channel_entry) || !(channel_entry->mode & SILC_CHANNEL_MODE_PRIVKEY)) {
	  printformat_module("fe-common/silc", server, NULL, MSGLEVEL_CRAP,
			     SILCTXT_CH_PRIVATE_KEY_NOMODE,
			     channel_entry->channel_name);
	  goto out;
	}

	if (!silc_client_add_channel_private_key(silc_client, conn,
						 channel_entry, NULL,
						 cipher, hmac,
						 argv[4],
						 argv_lens[4], NULL)) {
	  printformat_module("fe-common/silc", server, NULL, MSGLEVEL_CRAP,
			     SILCTXT_CH_PRIVATE_KEY_ERROR,
			     channel_entry->channel_name);
	  goto out;
	}

	printformat_module("fe-common/silc", server, NULL, MSGLEVEL_CRAP,
			   SILCTXT_CH_PRIVATE_KEY_ADD,
			   channel_entry->channel_name);
      }
    }

    goto out;
  }

  /* Unset command */
  if (!strcasecmp(argv[3], "unset")) {
    command = 2;

    if (type == 1 && client_entry) {
      /* Unset private message key */
      silc_client_del_private_message_key(silc_client, conn, client_entry);
    } else if (type == 2) {
      /* Unset channel key(s) */
      int number;

      if (argc == 4)
	silc_client_del_channel_private_keys(silc_client, conn,
					     channel_entry);

      if (argc > 4) {
	number = atoi(argv[4]);
	ckeys = silc_client_list_channel_private_keys(silc_client, conn,
						      channel_entry);
	if (!ckeys)
	  goto out;

	silc_dlist_start(ckeys);
	if (!number || number > silc_dlist_count(ckeys)) {
	  silc_dlist_uninit(ckeys);
	  goto out;
	}

	for (i = 0; i < number; i++)
	  ch = silc_dlist_get(ckeys);
	if (!ch)
	  goto out;

	silc_client_del_channel_private_key(silc_client, conn, channel_entry,
					    ch);
	silc_dlist_uninit(ckeys);
      }

      goto out;
    }
  }

  /* List command */
  if (!strcasecmp(argv[3], "list")) {
    command = 3;

    if (type == 1) {
      SilcPrivateMessageKeys keys;
      SilcUInt32 keys_count;
      int k, i, len;
      char buf[1024];

      keys = silc_client_list_private_message_keys(silc_client, conn,
						   &keys_count);
      if (!keys)
	goto out;

      /* list the private message key(s) */
      if (nickname[0] == '*') {
	printformat_module("fe-common/silc", server, NULL, MSGLEVEL_CRAP,
			   SILCTXT_PRIVATE_KEY_LIST);
	for (k = 0; k < keys_count; k++) {
	  memset(buf, 0, sizeof(buf));
	  strncat(buf, "  ", 2);
	  len = strlen(keys[k].client_entry->nickname);
	  strncat(buf, keys[k].client_entry->nickname, len > 30 ? 30 : len);
	  if (len < 30)
	    for (i = 0; i < 30 - len; i++)
	      strcat(buf, " ");
	  strcat(buf, " ");

	  len = strlen(keys[k].cipher);
	  strncat(buf, keys[k].cipher, len > 14 ? 14 : len);
	  if (len < 14)
	    for (i = 0; i < 14 - len; i++)
	      strcat(buf, " ");
	  strcat(buf, " ");

	  if (keys[k].key)
	    strcat(buf, "<hidden>");
	  else
	    strcat(buf, "*generated*");

	  silc_say(silc_client, conn, SILC_CLIENT_MESSAGE_INFO, "%s", buf);
	}
      } else {
	printformat_module("fe-common/silc", server, NULL, MSGLEVEL_CRAP,
			   SILCTXT_PRIVATE_KEY_LIST_NICK,
			   client_entry->nickname);
	for (k = 0; k < keys_count; k++) {
	  if (keys[k].client_entry != client_entry)
	    continue;

	  memset(buf, 0, sizeof(buf));
	  strncat(buf, "  ", 2);
	  len = strlen(keys[k].client_entry->nickname);
	  strncat(buf, keys[k].client_entry->nickname, len > 30 ? 30 : len);
	  if (len < 30)
	    for (i = 0; i < 30 - len; i++)
	      strcat(buf, " ");
	  strcat(buf, " ");

	  len = strlen(keys[k].cipher);
	  strncat(buf, keys[k].cipher, len > 14 ? 14 : len);
	  if (len < 14)
	    for (i = 0; i < 14 - len; i++)
	      strcat(buf, " ");
	  strcat(buf, " ");

	  if (keys[k].key)
	    strcat(buf, "<hidden>");
	  else
	    strcat(buf, "*generated*");

	  silc_say(silc_client, conn, SILC_CLIENT_MESSAGE_INFO, "%s", buf);
	}
      }

      silc_client_free_private_message_keys(keys, keys_count);

    } else if (type == 2) {
      int len;
      char buf[1024];

      ckeys = silc_client_list_channel_private_keys(silc_client, conn,
						    channel_entry);

      printformat_module("fe-common/silc", server, NULL, MSGLEVEL_CRAP,
			 SILCTXT_CH_PRIVATE_KEY_LIST,
			 channel_entry->channel_name);

      if (!ckeys)
	goto out;

      silc_dlist_start(ckeys);
      while ((ch = silc_dlist_get(ckeys))) {
	memset(buf, 0, sizeof(buf));
	strncat(buf, "  ", 2);

	len = strlen(silc_cipher_get_name(ch->send_key));
	strncat(buf, silc_cipher_get_name(ch->send_key),
		len > 16 ? 16 : len);
	if (len < 16)
	  for (i = 0; i < 16 - len; i++)
	    strcat(buf, " ");
	strcat(buf, " ");

	len = strlen(silc_hmac_get_name(ch->hmac));
	strncat(buf, silc_hmac_get_name(ch->hmac), len > 16 ? 16 : len);
	if (len < 16)
	  for (i = 0; i < 16 - len; i++)
	    strcat(buf, " ");
	strcat(buf, " ");

	strcat(buf, "<hidden>");

	silc_say(silc_client, conn, SILC_CLIENT_MESSAGE_INFO, "%s", buf);
      }

      silc_dlist_uninit(ckeys);
    }

    goto out;
  }

  /* Send command is used to send key agreement */
  if (!strcasecmp(argv[3], "agreement")) {
    command = 4;

    if (argc >= 5)
      hostname = argv[4];
    if (argc >= 6) {
      if (!strcasecmp(argv[5], "UDP"))
	udp = TRUE;
      else
	port = atoi(argv[5]);
    }
    if (argc >= 7)
      udp = TRUE;

    internal = silc_calloc(1, sizeof(*internal));
    internal->type = type;
    internal->server = server;

    if (!hostname) {
      if (settings_get_bool("use_auto_addr")) {
        hostname = (char *)settings_get_str("auto_public_ip");

	/* If the hostname isn't set, treat this case as if auto_public_ip
	   wasn't set. */
        if ((hostname) && (*hostname == '\0')) {
           hostname = NULL;
        } else {
          bindhost = (char *)settings_get_str("auto_bind_ip");

	  /* if the bind_ip isn't set, but the public_ip IS, then assume then
	     public_ip is the same value as the bind_ip. */
          if ((bindhost) && (*bindhost == '\0'))
            bindhost = hostname;
	  port = settings_get_int("auto_bind_port");
        }
      }  /* if use_auto_addr */
    }
  }

  /* Start command is used to start key agreement (after receiving the
     key_agreement client operation). */
  if (!strcasecmp(argv[3], "negotiate")) {
    command = 5;

    if (argc >= 5)
      hostname = argv[4];
    if (argc >= 6) {
      if (!strcasecmp(argv[5], "UDP"))
	udp = TRUE;
      else
	port = atoi(argv[5]);
    }
    if (argc >= 7)
      udp = TRUE;

    internal = silc_calloc(1, sizeof(*internal));
    internal->type = type;
    internal->server = server;
  }

  /* Change current channel private key */
  if (!strcasecmp(argv[3], "change")) {
    command = 6;
    if (type == 2) {
      /* Unset channel key(s) */
      int number;

      ckeys = silc_client_list_channel_private_keys(silc_client, conn,
						    channel_entry);
      if (!ckeys)
	goto out;

      silc_dlist_start(ckeys);
      if (argc == 4) {
	chanrec->cur_key++;
	if (chanrec->cur_key >= silc_dlist_count(ckeys))
	  chanrec->cur_key = 0;
      }

      if (argc > 4) {
	number = atoi(argv[4]);
	if (!number || number > silc_dlist_count(ckeys))
	  chanrec->cur_key = 0;
	else
	  chanrec->cur_key = number - 1;
      }

      for (i = 0; i < chanrec->cur_key; i++)
	ch = silc_dlist_get(ckeys);
      if (!ch)
	goto out;

      /* Set the current channel private key */
      silc_client_current_channel_private_key(silc_client, conn,
					      channel_entry, ch);
      printformat_module("fe-common/silc", server, NULL, MSGLEVEL_CRAP,
			 SILCTXT_CH_PRIVATE_KEY_CHANGE, i + 1,
			 channel_entry->channel_name);

      silc_dlist_uninit(ckeys);
      goto out;
    }
  }

  if (command == 0) {
    silc_say(silc_client, conn, SILC_CLIENT_MESSAGE_INFO,
	     "Usage: /KEY msg|channel <nickname|channel> "
	     "set|unset|agreement|negotiate [<arguments>]");
    goto out;
  }

  if (command == 4 && client_entry) {
    SilcClientConnectionParams params;

    printformat_module("fe-common/silc", server, NULL, MSGLEVEL_CRAP,
		       SILCTXT_KEY_AGREEMENT, argv[2]);
    internal->responder = TRUE;

    memset(&params, 0, sizeof(params));
    params.local_ip = hostname;
    params.bind_ip = bindhost;
    params.local_port = port;
    params.udp = udp;
    params.timeout_secs = settings_get_int("key_exchange_timeout_secs");

    silc_client_send_key_agreement(
			   silc_client, conn, client_entry, &params,
			   irssi_pubkey, irssi_privkey,
			   keyagr_completion, internal);
    if (!hostname)
      silc_free(internal);
    goto out;
  }

  if (command == 5 && client_entry && hostname) {
    SilcClientConnectionParams params;

    printformat_module("fe-common/silc", server, NULL, MSGLEVEL_CRAP,
		       SILCTXT_KEY_AGREEMENT_NEGOTIATE, argv[2]);
    internal->responder = FALSE;

    memset(&params, 0, sizeof(params));
    if (udp) {
      if (settings_get_bool("use_auto_addr")) {
	params.local_ip = (char *)settings_get_str("auto_public_ip");
	if ((params.local_ip) && (*params.local_ip == '\0')) {
	  params.local_ip = silc_net_localip();
	} else {
	  params.bind_ip = (char *)settings_get_str("auto_bind_ip");
	  if ((params.bind_ip) && (*params.bind_ip == '\0'))
	    params.bind_ip = NULL;
	  params.local_port = settings_get_int("auto_bind_port");
	}
      }
      if (!params.local_ip)
	params.local_ip = silc_net_localip();
    }
    params.udp = udp;
    params.timeout_secs = settings_get_int("key_exchange_timeout_secs");

    silc_client_perform_key_agreement(silc_client, conn, client_entry, &params,
				      irssi_pubkey, irssi_privkey,
				      hostname, port, keyagr_completion,
				      internal);
    goto out;
  }

 out:
  silc_free(nickname);
  return;
}

void silc_list_key(const char *pub_filename, int verbose)
{
  SilcPublicKey public_key;
  SilcPublicKeyIdentifier ident;
  SilcSILCPublicKey silc_pubkey;
  char *fingerprint, *babbleprint;
  unsigned char *pk;
  SilcUInt32 pk_len;
  SilcUInt32 key_len = 0;
  int is_server_key = (strstr(pub_filename, "serverkeys") != NULL);

  if (!silc_pkcs_load_public_key((char *)pub_filename, &public_key)) {
    printformat_module("fe-common/silc", NULL, NULL,
		       MSGLEVEL_CRAP, SILCTXT_LISTKEY_LOADPUB,
		       pub_filename);
    return;
  }

  /* Print only SILC public keys */
  if (silc_pkcs_get_type(public_key) != SILC_PKCS_SILC) {
    printformat_module("fe-common/silc", NULL, NULL,
		       MSGLEVEL_CRAP, SILCTXT_LISTKEY_LOADPUB,
		       pub_filename);
    return;
  }

  silc_pubkey = silc_pkcs_get_context(SILC_PKCS_SILC, public_key);
  ident = &silc_pubkey->identifier;

  pk = silc_pkcs_public_key_encode(public_key, &pk_len);
  if (!pk)
    return;
  fingerprint = silc_hash_fingerprint(NULL, pk, pk_len);
  babbleprint = silc_hash_babbleprint(NULL, pk, pk_len);
  key_len = silc_pkcs_public_key_get_len(public_key);

  printformat_module("fe-common/silc", NULL, NULL,
                     MSGLEVEL_CRAP, SILCTXT_LISTKEY_PUB_FILE,
                     pub_filename);

  if (verbose)
    printformat_module("fe-common/silc", NULL, NULL,
                       MSGLEVEL_CRAP, SILCTXT_LISTKEY_PUB_ALG,
		       silc_pkcs_get_name(public_key));
  if (key_len && verbose)
    printformat_module("fe-common/silc", NULL, NULL,
                        MSGLEVEL_CRAP, SILCTXT_LISTKEY_PUB_BITS,
                        (unsigned int)key_len);
  if (ident->version && verbose)
    printformat_module("fe-common/silc", NULL, NULL,
                        MSGLEVEL_CRAP, SILCTXT_LISTKEY_PUB_VER,
                        ident->version);
  if (ident->realname && (!is_server_key || verbose))
    printformat_module("fe-common/silc", NULL, NULL,
                       MSGLEVEL_CRAP, SILCTXT_LISTKEY_PUB_RN,
                       ident->realname);
  if (ident->username && verbose)
    printformat_module("fe-common/silc", NULL, NULL,
                       MSGLEVEL_CRAP, SILCTXT_LISTKEY_PUB_UN,
                       ident->username);
  if (ident->host && (is_server_key || verbose))
    printformat_module("fe-common/silc", NULL, NULL,
                       MSGLEVEL_CRAP, SILCTXT_LISTKEY_PUB_HN,
                       ident->host);
  if (ident->email && verbose)
    printformat_module("fe-common/silc", NULL, NULL,
                       MSGLEVEL_CRAP, SILCTXT_LISTKEY_PUB_EMAIL,
                       ident->email);
  if (ident->org && verbose)
    printformat_module("fe-common/silc", NULL, NULL,
                       MSGLEVEL_CRAP, SILCTXT_LISTKEY_PUB_ORG,
                       ident->org);
  if (ident->country && verbose)
    printformat_module("fe-common/silc", NULL, NULL,
                       MSGLEVEL_CRAP, SILCTXT_LISTKEY_PUB_C,
                       ident->country);

  if (verbose) {
    printformat_module("fe-common/silc", NULL, NULL,
                       MSGLEVEL_CRAP, SILCTXT_LISTKEY_PUB_FINGER,
                       fingerprint);
    printformat_module("fe-common/silc", NULL, NULL,
                       MSGLEVEL_CRAP, SILCTXT_LISTKEY_PUB_BABL,
                       babbleprint);
  }

  silc_free(fingerprint);
  silc_free(babbleprint);
  silc_free(pk);
  silc_pkcs_public_key_free(public_key);
}

void silc_list_keys_in_dir(const char *dirname, const char *where)
{
  DIR *dir;
  struct dirent *entry;

  dir = opendir(dirname);

  if (dir == NULL)
    cmd_return_error(CMDERR_ERRNO);

  printformat_module("fe-common/silc", NULL, NULL,
                     MSGLEVEL_CRAP, SILCTXT_LISTKEY_LIST,
                     where);

  rewinddir(dir);

  while ((entry = readdir(dir)) != NULL) {
    /* try to open everything that isn't a directory */
    struct stat buf;
    char filename[256];

    snprintf(filename, sizeof(filename) - 1, "%s/%s", dirname, entry->d_name);
    if (!stat(filename, &buf) && S_ISREG(buf.st_mode))
      silc_list_key(filename, FALSE);
  }

  closedir(dir);
}

void silc_list_file(const char *filename)
{

  char path[256];
  struct stat buf;

  snprintf(path, sizeof(path) - 1, "%s", filename);
  if (!stat(path, &buf) && S_ISREG(buf.st_mode))
    goto list_key;

  snprintf(path, sizeof(path) - 1, "%s/%s", get_irssi_dir(), filename);
  if (!stat(path, &buf) && S_ISREG(buf.st_mode))
    goto list_key;

  snprintf(path,sizeof(path) - 1, "%s/clientkeys/%s", get_irssi_dir(),
	   filename);
  if (!stat(path, &buf) && S_ISREG(buf.st_mode))
    goto list_key;

  snprintf(path,sizeof(path) - 1, "%s/serverkeys/%s", get_irssi_dir(),
	   filename);
  if (!stat(path, &buf) && S_ISREG(buf.st_mode))
    goto list_key;

  return;

list_key:

  silc_list_key(path, TRUE);
}

/* Lists locally saved client and server public keys. */
static void command_listkeys(const char *data, SILC_SERVER_REC *server,
			     WI_ITEM_REC *item)
{
  GHashTable *optlist;
  char *filename;
  void *free_arg;
  char dirname[256];

  if (!cmd_get_params(data, &free_arg, 1 | PARAM_FLAG_OPTIONS |
		      PARAM_FLAG_GETREST, "listkeys", &optlist,
		      &filename))
    return;

  if (*filename != '\0') {

    silc_list_file(filename);

  } else {
    int clients, servers;

    clients = (g_hash_table_lookup(optlist, "clients") != NULL);
    servers = (g_hash_table_lookup(optlist, "servers") != NULL);

    if (!(clients || servers))
      clients = servers = 1;

    if (servers) {
      snprintf(dirname, sizeof(dirname) - 1, "%s/serverkeys", get_irssi_dir());
      silc_list_keys_in_dir(dirname, "server");
    }

    if (clients) {
      snprintf(dirname, sizeof(dirname) - 1, "%s/clientkeys", get_irssi_dir());
      silc_list_keys_in_dir(dirname, "client");
    }
  }
  cmd_params_free(free_arg);
}

void silc_channels_init(void)
{
  signal_add("channel destroyed", (SIGNAL_FUNC) sig_channel_destroyed);
  signal_add("server connected", (SIGNAL_FUNC) sig_connected);
  signal_add("server quit", (SIGNAL_FUNC) sig_server_quit);
  signal_add("mime", (SIGNAL_FUNC) sig_mime);
  signal_add("channel joined", (SIGNAL_FUNC) sig_silc_channel_joined);

  command_bind_silc("part", MODULE_NAME, (SIGNAL_FUNC) command_part);
  command_bind_silc("me", MODULE_NAME, (SIGNAL_FUNC) command_me);
  command_bind_silc("action", MODULE_NAME, (SIGNAL_FUNC) command_action);
  command_bind_silc("notice", MODULE_NAME, (SIGNAL_FUNC) command_notice);
  command_bind_silc("away", MODULE_NAME, (SIGNAL_FUNC) command_away);
  command_bind_silc("key", MODULE_NAME, (SIGNAL_FUNC) command_key);
  command_bind("listkeys", MODULE_NAME, (SIGNAL_FUNC) command_listkeys);

  command_set_options("listkeys", "clients servers");
  command_set_options("action", "sign channel");
  command_set_options("notice", "sign channel");

  silc_nicklist_init();
}

void silc_channels_deinit(void)
{
  signal_remove("channel destroyed", (SIGNAL_FUNC) sig_channel_destroyed);
  signal_remove("server connected", (SIGNAL_FUNC) sig_connected);
  signal_remove("server quit", (SIGNAL_FUNC) sig_server_quit);
  signal_remove("mime", (SIGNAL_FUNC) sig_mime);
  signal_remove("channel joined", (SIGNAL_FUNC) sig_silc_channel_joined);

  command_unbind("part", (SIGNAL_FUNC) command_part);
  command_unbind("me", (SIGNAL_FUNC) command_me);
  command_unbind("action", (SIGNAL_FUNC) command_action);
  command_unbind("notice", (SIGNAL_FUNC) command_notice);
  command_unbind("away", (SIGNAL_FUNC) command_away);
  command_unbind("key", (SIGNAL_FUNC) command_key);
  command_unbind("listkeys", (SIGNAL_FUNC) command_listkeys);

  silc_nicklist_deinit();
}

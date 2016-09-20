#ifndef __SILC_SERVER_H
#define __SILC_SERVER_H

#include "chat-protocols.h"
#include "servers.h"

/* returns SILC_SERVER_REC if it's SILC server, NULL if it isn't */
#define SILC_SERVER(server) \
	PROTO_CHECK_CAST(SERVER(server), SILC_SERVER_REC, chat_type, "SILC")
#define SILC_SERVER_CONNECT(conn) \
	PROTO_CHECK_CAST(SERVER_CONNECT(conn), SILC_SERVER_CONNECT_REC, \
			 chat_type, "SILC")
#define IS_SILC_SERVER(server) \
	(SILC_SERVER(server) ? TRUE : FALSE)
#define IS_SILC_SERVER_CONNECT(conn) \
	(SILC_SERVER_CONNECT(conn) ? TRUE : FALSE)

/* all strings should be either NULL or dynamically allocated */
/* address and nick are mandatory, rest are optional */
typedef struct {
#include "server-connect-rec.h"
} SILC_SERVER_CONNECT_REC;

typedef struct {
  SilcClientEntry client_entry;
  SilcClientConnection conn;
  SilcUInt32 session_id;
  char *filepath;
  bool send;

  long starttime;		/* Start time of transfer */
  double kps;			/* Kilos per second */
  SilcUInt64 offset;		/* Current offset */
  SilcUInt64 filesize;		/* Total file size */
  SilcUInt32 percent;		/* Percent of current transmission */
} *FtpSession;

#define STRUCT_SERVER_CONNECT_REC SILC_SERVER_CONNECT_REC
typedef struct {
#include "server-rec.h"

  SilcDList ftp_sessions;
  FtpSession current_session;

  gpointer chanqueries;
  SilcClientConnection conn;
  SilcAsyncOperation op;	/* Key exchange operation handle */
  SilcAsyncOperation tcp_op;	/* TCP stream creation operation handle */
  SilcAsyncOperation prompt_op; /* Key verification operation handle */
  SilcUInt32 umode;
} SILC_SERVER_REC;

SERVER_REC *silc_server_init_connect(SERVER_CONNECT_REC *conn);
void silc_server_connect(SERVER_REC *server);

/* Return a string of all channels in server in server->channels_join()
   format */
char *silc_server_get_channels(SILC_SERVER_REC *server);
void silc_command_exec(SILC_SERVER_REC *server,
		       const char *command, const char *args);
void silc_server_init(void);
void silc_server_deinit(void);
void silc_server_free_ftp(SILC_SERVER_REC *server,
			  SilcClientEntry client_entry);
bool silc_term_utf8(void);

int silc_send_msg(SILC_SERVER_REC *server, char *nick, char *msg,
		  int msg_len, SilcMessageFlags flags);
int silc_send_channel(SILC_SERVER_REC *server,
		      char *channel, char *msg,
		      SilcMessageFlags flags);
#endif

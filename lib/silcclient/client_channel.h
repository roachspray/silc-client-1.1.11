/*

  client_channel.h

  Author: Pekka Riikonen <priikone@silcnet.org>

  Copyright (C) 2006 - 2007 Pekka Riikonen

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

*/

#ifndef CLIENT_CHANNEL_H
#define CLIENT_CHANNEL_H

SILC_FSM_STATE(silc_client_channel_message);
SILC_FSM_STATE(silc_client_channel_message_error);
SILC_FSM_STATE(silc_client_channel_key);

SilcBool silc_client_save_channel_key(SilcClient client,
				      SilcClientConnection conn,
				      SilcBuffer key_payload,
				      SilcChannelEntry channel);
SilcChannelUser silc_client_on_channel(SilcChannelEntry channel,
				       SilcClientEntry client_entry);
SilcBool silc_client_add_to_channel(SilcClient client,
				    SilcClientConnection conn,
				    SilcChannelEntry channel,
				    SilcClientEntry client_entry,
				    SilcUInt32 cumode);
SilcBool silc_client_remove_from_channel(SilcClient client,
					 SilcClientConnection conn,
					 SilcChannelEntry channel,
					 SilcClientEntry client_entry);
void silc_client_remove_from_channels(SilcClient client,
				      SilcClientConnection conn,
				      SilcClientEntry client_entry);
void silc_client_empty_channel(SilcClient client,
			       SilcClientConnection conn,
			       SilcChannelEntry channel);
SilcBool silc_client_channel_save_public_keys(SilcChannelEntry channel,
					      unsigned char *chpk_list,
					      SilcUInt32 chpk_list_len,
					      SilcBool remove_all);

#endif /* CLIENT_CHANNEL_H */

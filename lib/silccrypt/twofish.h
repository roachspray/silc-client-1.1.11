/*

  twofish.h

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

#ifndef TWOFISH_H
#define TWOFISH_H

/*
 * SILC Crypto API for Twofish
 */

SILC_CIPHER_API_SET_KEY(twofish_cbc);
SILC_CIPHER_API_SET_IV(twofish_cbc);
SILC_CIPHER_API_CONTEXT_LEN(twofish_cbc);
SILC_CIPHER_API_ENCRYPT(twofish_cbc);
SILC_CIPHER_API_DECRYPT(twofish_cbc);

#endif

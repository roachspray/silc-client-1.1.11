/*

  silcpkcs1_i.h

  Author: Pekka Riikonen <priikone@silcnet.org>

  Copyright (C); 2006 Pekka Riikonen

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

*/

#ifndef SILCPKCS1_I_H
#define SILCPKCS1_I_H

SilcBool silc_pkcs1_generate_key(SilcUInt32 keylen,
				 SilcRng rng,
				 void **ret_public_key,
				 void **ret_private_key);
int silc_pkcs1_import_public_key(unsigned char *key,
				 SilcUInt32 key_len,
				 void **ret_public_key);
unsigned char *silc_pkcs1_export_public_key(void *public_key,
					    SilcUInt32 *ret_len);
SilcUInt32 silc_pkcs1_public_key_bitlen(void *public_key);
void *silc_pkcs1_public_key_copy(void *public_key);
SilcBool silc_pkcs1_public_key_compare(void *key1, void *key2);
void silc_pkcs1_public_key_free(void *public_key);
int silc_pkcs1_import_private_key(unsigned char *key,
				  SilcUInt32 key_len,
				  void **ret_private_key);
unsigned char *silc_pkcs1_export_private_key(void *private_key,
					     SilcUInt32 *ret_len);
SilcUInt32 silc_pkcs1_private_key_bitlen(void *private_key);
void silc_pkcs1_private_key_free(void *private_key);
SilcBool silc_pkcs1_encrypt(void *public_key,
			    unsigned char *src,
			    SilcUInt32 src_len,
			    unsigned char *dst,
			    SilcUInt32 dst_size,
			    SilcUInt32 *ret_dst_len,
			    SilcRng rng);
SilcBool silc_pkcs1_decrypt(void *private_key,
			    unsigned char *src,
			    SilcUInt32 src_len,
			    unsigned char *dst,
			    SilcUInt32 dst_size,
			    SilcUInt32 *ret_dst_len);
SilcBool silc_pkcs1_sign(void *private_key,
			 unsigned char *src,
			 SilcUInt32 src_len,
			 unsigned char *signature,
			 SilcUInt32 signature_size,
			 SilcUInt32 *ret_signature_len,
			 SilcBool compute_hash,
			 SilcHash hash);
SilcBool silc_pkcs1_verify(void *public_key,
			   unsigned char *signature,
			   SilcUInt32 signature_len,
			   unsigned char *data,
			   SilcUInt32 data_len,
			   SilcHash hash);
SilcBool silc_pkcs1_sign_no_oid(void *private_key,
				unsigned char *src,
				SilcUInt32 src_len,
				unsigned char *signature,
				SilcUInt32 signature_size,
				SilcUInt32 *ret_signature_len,
				SilcBool compute_hash,
				SilcHash hash);
SilcBool silc_pkcs1_verify_no_oid(void *public_key,
				  unsigned char *signature,
				  SilcUInt32 signature_len,
				  unsigned char *data,
				  SilcUInt32 data_len,
				  SilcHash hash);

#endif /* SILCPKCS1_I_H */

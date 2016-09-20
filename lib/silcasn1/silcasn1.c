/*

  silcasn1.c

  Author: Pekka Riikonen <priikone@silcnet.org>

  Copyright (C) 2003 - 2007 Pekka Riikonen

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

*/

#include "silc.h"
#include "silcasn1.h"
#include "silcber.h"

/* Allocate ASN.1 context. */

SilcAsn1 silc_asn1_alloc(void)
{
  SilcAsn1 asn1 = silc_calloc(1, sizeof(*asn1));
  if (!asn1)
    return NULL;

  if (!silc_asn1_init(asn1))
    return NULL;

  return asn1;
}

/* Free ASN.1 context */

void silc_asn1_free(SilcAsn1 asn1)
{
  silc_asn1_uninit(asn1);
  silc_free(asn1);
}

/* Init pre-allocated ASN.1 context */

SilcBool silc_asn1_init(SilcAsn1 asn1)
{
  asn1->stack1 = silc_stack_alloc(768);
  if (!asn1->stack1)
    return FALSE;

  asn1->stack2 = silc_stack_alloc(768);
  if (!asn1->stack2) {
    silc_stack_free(asn1->stack1);
    return FALSE;
  }

  asn1->accumul = 0;

  return TRUE;
}

/* Uninit ASN.1 context */

void silc_asn1_uninit(SilcAsn1 asn1)
{
  silc_stack_free(asn1->stack1);
  silc_stack_free(asn1->stack2);
}

#if defined(SILC_DEBUG)
/* Returns string representation of a tag */

const char *silc_asn1_tag_name(SilcAsn1Tag tag)
{
  switch ((long)tag) {
  case SILC_ASN1_END:
    return "END";
  case SILC_ASN1_TAG_OPTS:
    return "";
  case SILC_ASN1_TAG_CHOICE:
    return "choice";
  case SILC_ASN1_TAG_ANY:
    return "any";
  case SILC_ASN1_TAG_ANY_PRIMITIVE:
    return "any primitive";
  case SILC_ASN1_TAG_SEQUENCE_OF:
    return "sequence of";
  case SILC_ASN1_TAG_SEQUENCE:
    return "sequence";
  case SILC_ASN1_TAG_SET:
    return "set";
  case SILC_ASN1_TAG_INTEGER:
    return "integer";
  case SILC_ASN1_TAG_SHORT_INTEGER:
    return "short integer";
  case SILC_ASN1_TAG_OID:
    return "oid";
  case SILC_ASN1_TAG_BOOLEAN:
    return "boolean";
  case SILC_ASN1_TAG_OCTET_STRING:
    return "octet-string";
  case SILC_ASN1_TAG_BIT_STRING:
    return "bit-string";
  case SILC_ASN1_TAG_NULL:
    return "null";
  case SILC_ASN1_TAG_ENUM:
    return "enum";
  case SILC_ASN1_TAG_UTC_TIME:
    return "utc-time";
  case SILC_ASN1_TAG_GENERALIZED_TIME:
    return "generalized-time";
  case SILC_ASN1_TAG_UTF8_STRING:
    return "utf8-string";
  case SILC_ASN1_TAG_NUMERIC_STRING:
    return "numeric-string";
  case SILC_ASN1_TAG_PRINTABLE_STRING:
    return "printable-string";
  case SILC_ASN1_TAG_IA5_STRING:
    return "ia5-string";
  case SILC_ASN1_TAG_VISIBLE_STRING:
    return "visible-string";
  case SILC_ASN1_TAG_UNIVERSAL_STRING:
    return "universal-string";
  case SILC_ASN1_TAG_UNRESTRICTED_STRING:
    return "unrestricted-string";
  case SILC_ASN1_TAG_BMP_STRING:
    return "bmp-string";
  case SILC_ASN1_TAG_ODE:
    return "ode";
  case SILC_ASN1_TAG_ETI:
    return "eti";
  case SILC_ASN1_TAG_REAL:
    return "real";
  case SILC_ASN1_TAG_EMBEDDED:
    return "embedded";
  case SILC_ASN1_TAG_ROI:
    return "roi";
  case SILC_ASN1_TAG_TELETEX_STRING:
    return "teletex-string";
  case SILC_ASN1_TAG_VIDEOTEX_STRING:
    return "videotex-string";
  case SILC_ASN1_TAG_GRAPHIC_STRING:
    return "graphic-string";
  case SILC_ASN1_TAG_GENERAL_STRING:
    return "general-string";
  default:
    break;
  }
  return "unknown";
}
#endif /* SILC_DEBUG */


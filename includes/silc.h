/*

  silc.h

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
/*
  This file includes common definitions for SILC. This file MUST be included
  by all files in SILC (directly or through other global include file).
*/

#ifndef SILCINCLUDES_H
#define SILCINCLUDES_H

#ifdef __cplusplus
extern "C" {
#endif

#define SILC_UNIX

#ifdef WIN32
#ifndef SILC_WIN32
#define SILC_WIN32
#undef SILC_UNIX
#endif
#endif

#if defined(__EPOC32__) || defined(__SYMBIAN32__)
#ifndef SILC_SYMBIAN
#define SILC_SYMBIAN
#undef SILC_UNIX
#undef SILC_WIN32
#endif
#endif

#if defined(__MACH__) && defined(__APPLE__)
#ifndef SILC_MACOSX
#define SILC_MACOSX
#undef SILC_WIN32
#undef SILC_SYMBIAN
#endif
#endif

/* Types */
#define SILC_SIZEOF_LONG_LONG 8
#define SILC_SIZEOF_LONG 8
#define SILC_SIZEOF_INT 4
#define SILC_SIZEOF_SHORT 2
#define SILC_SIZEOF_CHAR 1
#define SILC_SIZEOF_VOID_P 8

/* Compilation time defines, for third-party software */
#define __SILC_HAVE_PTHREAD 1



#if defined(HAVE_SILCDEFS_H)
/* Automatically generated configuration header */
#ifndef SILC_SYMBIAN
#include "silcdefs.h"
#else
#include "../symbian/silcdefs.h"
#endif /* SILC_SYMBIAN */
#include "silcdistdefs.h"
#endif /* HAVE_SILCDEFS_H */

/* Platform specific includes */

#if defined(SILC_WIN32)
#include "silcwin32.h"
#endif

#if defined(SILC_SYMBIAN)
#include "silcsymbian.h"
#endif

#ifndef DLLAPI
#define DLLAPI
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#if !defined(SILC_WIN32)

#include <unistd.h>
#include <sys/time.h>
#include <pwd.h>
#include <sys/times.h>

#ifdef HAVE_GRP_H
#include <grp.h>
#endif

#if defined(HAVE_GETOPT_H) && defined(HAVE_GETOPT)
#include <getopt.h>
#else
#if defined(HAVE_SILCDEFS_H)
#include "getopti.h"
#endif /* HAVE_SILCDEFS_H */
#endif

#ifdef SOCKS5
#include "socks.h"
#endif

#include <sys/socket.h>
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_XTI_H
#include <xti.h>
#else
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#ifndef HAVE_REGEX_H
#if defined(HAVE_SILCDEFS_H)
#include "regexpr.h"
#endif /* HAVE_SILCDEFS_H */
#else
#include <regex.h>
#endif

#ifdef SILC_HAVE_PTHREAD
#include <pthread.h>
#endif

#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#ifdef HAVE_UTIME_H
#include <utime.h>
#endif

#ifdef HAVE_ICONV_H
#include <iconv.h>
#endif

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#endif				/* !SILC_WIN32 */

/* Include generic SILC type definitions */
#include "silctypes.h"
#include "silcmutex.h"
#include "silcatomic.h"
#include "silcversion.h"

/* SILC util library includes */
#include "silcstack.h"
#include "silcmemory.h"
#include "silcsnprintf.h"

/* Math library includes */
#include "silcmp.h"
#include "silcmath.h"

/* More SILC util library includes */
#include "silctime.h"
#include "silccond.h"
#include "silcthread.h"
#include "silcschedule.h"
#include "silclog.h"
#include "silcbuffer.h"
#include "silcbuffmt.h"

/* Crypto library includes */
#include "silccipher.h"
#include "silchash.h"
#include "silchmac.h"
#include "silcrng.h"
#include "silcpkcs.h"
#include "silcpk.h"
#include "silcpkcs1.h"

/* More SILC util library includes */
#include "silchashtable.h"
#include "silclist.h"
#include "silcdlist.h"
#include "silcasync.h"
#include "silcstream.h"
#include "silcnet.h"
#include "silcfileutil.h"
#include "silcstrutil.h"
#include "silcutf8.h"
#include "silcstringprep.h"
#include "silcutil.h"
#include "silcconfig.h"
#include "silcfsm.h"
#include "silcsocketstream.h"
#include "silcfdstream.h"
#include "silcmime.h"

#include "silcvcard.h"

#include "silcasn1.h"
#include "silcber.h"

/* SILC core library includes */
#include "silcargument.h"
#include "silcstatus.h"
#include "silcid.h"
#include "silccommand.h"
#include "silcauth.h"
#include "silcmessage.h"
#include "silcchannel.h"
#include "silcpacket.h"
#include "silcnotify.h"
#include "silcmode.h"
#include "silcattrs.h"
#include "silcpubkey.h"

/* Application utility includes */
#include "silcapputil.h"
#include "silcidcache.h"

#include "silcskr.h"

#if defined(SILC_SIM)
/* SILC Module library includes */
#include "silcsim.h"
#include "silcsimutil.h"
#endif

/* SILC Key Exchange library includes */
#include "silcske.h"
#include "silcske_payload.h"
#include "silcske_groups.h"
#include "silcconnauth.h"

/* SILC SFTP library */
#include "silcsftp.h"
#include "silcsftp_fs.h"


#ifdef __cplusplus
}
#endif

#endif /* SILCINCLUDES_H */

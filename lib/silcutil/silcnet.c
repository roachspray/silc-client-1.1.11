/*

  silcnet.c

  Author: Pekka Riikonen <priikone@silcnet.org>

  Copyright (C) 1997 - 2006 Pekka Riikonen

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

/* Returns bound port from listener */

SilcUInt16 *silc_net_listener_get_port(SilcNetListener listener,
				       SilcUInt32 *port_count)
{
  SilcUInt16 *ports;
  int i;

  ports = silc_calloc(listener->socks_count, sizeof(*ports));
  if (!ports)
    return NULL;

  for (i = 0; i < listener->socks_count; i++)
    ports[i] = silc_net_get_local_port(listener->socks[i]);

  if (port_count)
    *port_count = listener->socks_count;

  return ports;
}

/* Return bound IP from listener */

char **silc_net_listener_get_ip(SilcNetListener listener,
				SilcUInt32 *ip_count)
{
  char **ips = NULL, *ip;
  int i, k;

  ips = silc_calloc(listener->socks_count, sizeof(*ips));
  if (!ips)
    return NULL;

  for (i = 0, k = 0; i < listener->socks_count; i++) {
    if (silc_net_check_local_by_sock(listener->socks[i], NULL, &ip))
      ips[k++] = ip;
  }

  if (ip_count)
    *ip_count = k;

  return ips;
}

/* Return bound hostname from listener */

char **silc_net_listener_get_hostname(SilcNetListener listener,
				      SilcUInt32 *hostname_count)
{
  char **hs = NULL, *h;
  int i, k;

  hs = silc_calloc(listener->socks_count, sizeof(*hs));
  if (!hs)
    return NULL;

  for (i = 0, k = 0; i < listener->socks_count; i++) {
    if (silc_net_check_local_by_sock(listener->socks[i], &h, NULL))
      hs[k++] = h;
  }

  if (hostname_count)
    *hostname_count = k;

  return hs;
}

static const char *silc_net_error[] = {
  "Ok",
  "Unknown IP address",
  "Unknown hostname",
  "Destination unreachable",
  "Connection refused",
  "Connection timeout",
  "System out of memory",
  "Unexpected error",
};

/* Return error as string */

const char *silc_net_get_error_string(SilcNetStatus error)
{
  if (error < SILC_NET_OK || error > SILC_NET_ERROR)
    return "";
  return silc_net_error[error];
}

/* Accepts a connection from a particular socket */

int silc_net_accept_connection(int sock)
{
  return accept(sock, 0, 0);
}

/* Sets a option for a socket. */

int silc_net_set_socket_opt(int sock, int level, int option, int on)
{
  return setsockopt(sock, level, option, (void *)&on, sizeof(on));
}

/* Get socket options */

int silc_net_get_socket_opt(int sock, int level, int option,
			    void *optval, int *opt_len)
{
  return getsockopt(sock, level, option, optval, opt_len);
}

/* Checks whether IP address sent as argument is valid IPv4 address. */

SilcBool silc_net_is_ip4(const char *addr)
{
  int count = 0;

  while (*addr) {
    if (*addr != '.' && !isdigit((int)*addr))
      return FALSE;
    if (*addr == '.')
      count++;
    addr++;
  }

  if (count != 3)
    return FALSE;

  return TRUE;
}

/* Checks whether IP address sent as argument is valid IPv6 address. */

SilcBool silc_net_is_ip6(const char *addr)
{
  /* XXX does this work with all kinds of IPv6 addresses? */
  while (*addr) {
    if (*addr != ':' && !isxdigit((int)*addr))
      return FALSE;
    addr++;
  }

  return TRUE;
}

/* Checks whether IP address sent as argument is valid IP address. */

SilcBool silc_net_is_ip(const char *addr)
{
  if (silc_net_is_ip4(addr))
    return TRUE;
  return silc_net_is_ip6(addr);
}

/* Internal context for async resolving */
typedef struct {
  SilcNetResolveCallback completion;
  void *context;
  SilcBool prefer_ipv6;
  SilcSchedule schedule;
  char *input;
  char *result;
} *SilcNetResolveContext;

SILC_TASK_CALLBACK(silc_net_resolve_completion)
{
  SilcNetResolveContext r = (SilcNetResolveContext)context;

  /* Call the completion callback */
  if (r->completion)
    (*r->completion)(r->result, r->context);

  silc_free(r->input);
  silc_free(r->result);
  silc_free(r);
}

/* Thread function to resolve the address for hostname. */

static void *silc_net_gethostbyname_thread(void *context)
{
  SilcNetResolveContext r = (SilcNetResolveContext)context;
  SilcSchedule schedule = r->schedule;
  char tmp[64];

  if (silc_net_gethostbyname(r->input, r->prefer_ipv6, tmp, sizeof(tmp)))
    r->result = strdup(tmp);

  silc_schedule_task_add(schedule, 0, silc_net_resolve_completion, r, 0, 1,
			 SILC_TASK_TIMEOUT);
  silc_schedule_wakeup(schedule);
  return NULL;
}

/* Thread function to resolve the hostname for address. */

static void *silc_net_gethostbyaddr_thread(void *context)
{
  SilcNetResolveContext r = (SilcNetResolveContext)context;
  SilcSchedule schedule = r->schedule;
  char tmp[256];

  if (silc_net_gethostbyaddr(r->input, tmp, sizeof(tmp)))
    r->result = strdup(tmp);

  silc_schedule_task_add(schedule, 0, silc_net_resolve_completion, r, 0, 1,
			 SILC_TASK_TIMEOUT);
  silc_schedule_wakeup(schedule);
  return NULL;
}

/* Resolves IP address for hostname. */

SilcBool silc_net_gethostbyname(const char *name,
				SilcBool prefer_ipv6, char *address,
				SilcUInt32 address_len)
{
#ifdef HAVE_IPV6
  struct addrinfo hints, *ai, *tmp, *ip4 = NULL, *ip6 = NULL;

  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(name, NULL, &hints, &ai))
    return FALSE;

  for (tmp = ai; tmp; tmp = tmp->ai_next) {
    if (tmp->ai_family == AF_INET6) {
      ip6 = tmp;
      if (ip4)
	break;
      continue;
    }
    if (tmp->ai_family == AF_INET) {
      ip4 = tmp;
      if (ip6)
	break;
      continue;
    }
  }

  tmp = (prefer_ipv6 ? (ip6 ? ip6 : ip4) : (ip4 ? ip4 : ip6));
  if (!tmp) {
    freeaddrinfo(ai);
    return FALSE;
  }

  if (getnameinfo(tmp->ai_addr, tmp->ai_addrlen, address,
		  address_len, NULL, 0, NI_NUMERICHOST)) {
    freeaddrinfo(ai);
    return FALSE;
  }

  freeaddrinfo(ai);
#else
  struct hostent *hp;
  struct in_addr ip;
  char *tmp;

  if (silc_net_is_ip4(name)) {
    memset(address, 0, address_len);
    if (address_len < strlen(name))
      return FALSE;
    strncpy(address, name, strlen(name));
    return TRUE;
  }

  hp = gethostbyname(name);
  if (!hp)
    return FALSE;

  memcpy(&ip.s_addr, hp->h_addr_list[0], 4);
  tmp = inet_ntoa(ip);
  if (!tmp)
    return FALSE;
  if (address_len < strlen(tmp))
    return FALSE;
  memset(address, 0, address_len);
  strncpy(address, tmp, strlen(tmp));
#endif

  return TRUE;
}

/* Resolves IP address for hostname async. */

void silc_net_gethostbyname_async(const char *name,
				  SilcBool prefer_ipv6,
				  SilcSchedule schedule,
				  SilcNetResolveCallback completion,
				  void *context)
{
  SilcNetResolveContext r = silc_calloc(1, sizeof(*r));

  r->completion = completion;
  r->context = context;
  r->prefer_ipv6 = prefer_ipv6;
  r->schedule = schedule;
  r->input = strdup(name);

  silc_thread_create(silc_net_gethostbyname_thread, r, FALSE);
}

/* Resolves hostname by IP address. */

SilcBool silc_net_gethostbyaddr(const char *addr, char *name,
				SilcUInt32 name_len)
{
#ifdef HAVE_IPV6
  struct addrinfo req, *ai;

  memset(&req, 0, sizeof(req));
  req.ai_socktype = SOCK_STREAM;
  req.ai_flags = AI_CANONNAME;

  if (getaddrinfo(addr, NULL, &req, &ai))
    return FALSE;
  if (getnameinfo(ai->ai_addr, ai->ai_addrlen, name, name_len, NULL, 0, 0)) {
    freeaddrinfo(ai);
    return FALSE;
  }
  freeaddrinfo(ai);
#else
  struct hostent *hp;
  unsigned char a[4];

  if (!silc_net_addr2bin(addr, a, sizeof(a)))
    return FALSE;

  hp = gethostbyaddr(a, 4, AF_INET);
  if (!hp)
    return FALSE;
  if (name_len < strlen(hp->h_name))
    return FALSE;
  memset(name, 0, name_len);
  strncpy(name, hp->h_name, strlen(hp->h_name));
#endif

  return TRUE;
}

/* Resolves hostname by IP address async. */

void silc_net_gethostbyaddr_async(const char *addr,
				  SilcSchedule schedule,
				  SilcNetResolveCallback completion,
				  void *context)
{
  SilcNetResolveContext r = silc_calloc(1, sizeof(*r));

  r->completion = completion;
  r->context = context;
  r->schedule = schedule;
  r->input = strdup(addr);

  silc_thread_create(silc_net_gethostbyaddr_thread, r, FALSE);
}

#ifndef SILC_SYMBIAN

/* Performs lookups for remote name and IP address. This peforms reverse
   lookup as well to verify that the IP has FQDN. */

SilcBool silc_net_check_host_by_sock(SilcSocket sock, char **hostname,
				     char **ip)
{
  char host[1024];
  int rval, len;

#ifdef HAVE_IPV6
  struct sockaddr_storage remote;
  char s[NI_MAXHOST];

  if (hostname)
    *hostname = NULL;
  if (ip)
    *ip = NULL;

  SILC_LOG_DEBUG(("Resolving remote hostname and IP address"));

  memset(&remote, 0, sizeof(remote));
  memset(&s, 0, sizeof(s));
  len = sizeof(remote);
  rval = getpeername(sock, (struct sockaddr *)&remote, &len);
  if (rval < 0)
    return FALSE;

  if (getnameinfo((struct sockaddr *)&remote, len, s, sizeof(s), NULL, 0,
		  NI_NUMERICHOST))
    return FALSE;

  if (ip) {
    *ip = silc_memdup(s, strlen(s));
    if (*ip == NULL)
      return FALSE;
  }
#else
  struct sockaddr_in remote;
  char *host_ip;

  if (hostname)
    *hostname = NULL;
  if (ip)
    *ip = NULL;

  SILC_LOG_DEBUG(("Resolving remote hostname and IP address"));

  memset(&remote, 0, sizeof(remote));
  len = sizeof(remote);
  rval = getpeername(sock, (struct sockaddr *)&remote, &len);
  if (rval < 0)
    return FALSE;

  host_ip = inet_ntoa(remote.sin_addr);
  if (!host_ip)
    return FALSE;

  if (ip) {
    *ip = silc_memdup(host_ip, strlen(host_ip));
    if (*ip == NULL)
      return FALSE;
  }
#endif

  /* Do reverse lookup if we want hostname too. */
  if (hostname) {
    /* Get host by address */
    if (!ip || !silc_net_gethostbyaddr(*ip, host, sizeof(host)))
      return FALSE;

    *hostname = silc_memdup(host, strlen(host));
    SILC_LOG_DEBUG(("Resolved hostname `%s'", *hostname));

    /* Reverse */
    if (!silc_net_gethostbyname(*hostname, TRUE, host, sizeof(host)))
      return FALSE;

    if (strcmp(*ip, host))
      return FALSE;
  }

  if (ip)
    SILC_LOG_DEBUG(("Resolved IP address `%s'", *ip));
  return TRUE;
}

/* Performs lookups for local name and IP address. This peforms reverse
   lookup as well to verify that the IP has FQDN. */

SilcBool silc_net_check_local_by_sock(SilcSocket sock, char **hostname,
				      char **ip)
{
  char host[1024];
  int rval, len;

#ifdef HAVE_IPV6
  struct sockaddr_storage local;
  char s[NI_MAXHOST];

  if (hostname)
    *hostname = NULL;
  if (ip)
    *ip = NULL;

  SILC_LOG_DEBUG(("Resolving local hostname and IP address"));

  memset(&local, 0, sizeof(local));
  memset(&s, 0, sizeof(s));
  len = sizeof(local);
  rval = getsockname(sock, (struct sockaddr *)&local, &len);
  if (rval < 0)
    return FALSE;

  if (getnameinfo((struct sockaddr *)&local, len, s, sizeof(s), NULL, 0,
		  NI_NUMERICHOST))
    return FALSE;

  if (ip) {
    *ip = silc_memdup(s, strlen(s));
    if (*ip == NULL)
      return FALSE;
  }
#else
  struct sockaddr_in local;
  char *host_ip;

  if (hostname)
    *hostname = NULL;
  if (ip)
    *ip = NULL;

  SILC_LOG_DEBUG(("Resolving local hostname and IP address"));

  memset(&local, 0, sizeof(local));
  len = sizeof(local);
  rval = getsockname(sock, (struct sockaddr *)&local, &len);
  if (rval < 0)
    return FALSE;

  host_ip = inet_ntoa(local.sin_addr);
  if (!host_ip)
    return FALSE;

  if (ip) {
    *ip = silc_memdup(host_ip, strlen(host_ip));
    if (*ip == NULL)
      return FALSE;
  }
#endif

  /* Do reverse lookup if we want hostname too. */
  if (hostname) {
    /* Get host by address */
    if (!ip || !silc_net_gethostbyaddr(*ip, host, sizeof(host)))
      return FALSE;

    *hostname = silc_memdup(host, strlen(host));
    SILC_LOG_DEBUG(("Resolved hostname `%s'", *hostname));

    /* Reverse */
    if (!silc_net_gethostbyname(*hostname, TRUE, host, sizeof(host)))
      return FALSE;

    if (strcmp(*ip, host))
      return FALSE;
  }

  if (ip)
    SILC_LOG_DEBUG(("Resolved IP address `%s'", *ip));
  return TRUE;
}

/* Return remote port by socket. */

SilcUInt16 silc_net_get_remote_port(SilcSocket sock)
{
#ifdef HAVE_IPV6
  struct sockaddr_storage remote;
  int len;
  char s[NI_MAXSERV];

  memset(&remote, 0, sizeof(remote));
  len = sizeof(remote);
  if (getpeername(sock, (struct sockaddr *)&remote, &len) < 0)
    return 0;

  if (getnameinfo((struct sockaddr *)&remote, len, NULL, 0, s, sizeof(s),
		  NI_NUMERICSERV))
    return 0;

  return atoi(s);
#else
  struct sockaddr_in remote;
  int len;

  memset(&remote, 0, sizeof(remote));
  len = sizeof(remote);
  if (getpeername(sock, (struct sockaddr *)&remote, &len) < 0)
    return 0;

  return ntohs(remote.sin_port);
#endif
}

/* Return local port by socket. */

SilcUInt16 silc_net_get_local_port(SilcSocket sock)
{
#ifdef HAVE_IPV6
  struct sockaddr_storage local;
  int len;
  char s[NI_MAXSERV];

  memset(&local, 0, sizeof(local));
  len = sizeof(local);
  if (getsockname(sock, (struct sockaddr *)&local, &len) < 0)
    return 0;

  if (getnameinfo((struct sockaddr *)&local, len, NULL, 0, s, sizeof(s),
		  NI_NUMERICSERV))
    return 0;

  return atoi(s);
#else
  struct sockaddr_in local;
  int len;

  memset(&local, 0, sizeof(local));
  len = sizeof(local);
  if (getsockname(sock, (struct sockaddr *)&local, &len) < 0)
    return 0;

  return ntohs(local.sin_port);
#endif
}
#endif /* !SILC_SYMBIAN */

/* Return name of localhost. */

char *silc_net_localhost(void)
{
  char hostname[256], ip_addr[64];

  if (gethostname(hostname, sizeof(hostname)))
    return NULL;

  if (!silc_net_gethostbyname(hostname, TRUE, ip_addr, sizeof(ip_addr)))
    return strdup(hostname);

  silc_net_gethostbyaddr(ip_addr, hostname, sizeof(hostname));
  return strdup(hostname);
}

/* Returns local IP address */

char *silc_net_localip(void)
{
  char hostname[256], ip_addr[64];

  if (gethostname(hostname, sizeof(hostname)))
    return NULL;

  if (!silc_net_gethostbyname(hostname, TRUE, ip_addr, sizeof(ip_addr)))
    return NULL;

  return strdup(ip_addr);
}

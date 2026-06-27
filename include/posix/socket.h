#ifndef __SOCKET_H
#define __SOCKET_H

#include "tlibc_types.h"

typedef uint16_t in_port_t;
typedef uint16_t sa_family_t;

struct in_addr {
    uint32_t s_addr;  // network byte order
};

struct sockaddr_in {
    sa_family_t sin_family;   // address family, AF_INET
    in_port_t sin_port;       // port number
    struct in_addr sin_addr;  // IP address
    unsigned char sin_zero[8]; // padding to size of struct sockaddr (16 bytes)
};

//这个enum来自socket_types.h
/* Types of sockets.  */
enum __socket_type
{
  SOCK_STREAM = 1,		/* Sequenced, reliable, connection-based
				   byte streams.  */
#define SOCK_STREAM SOCK_STREAM
  SOCK_DGRAM = 2,		/* Connectionless, unreliable datagrams
				   of fixed maximum length.  */
#define SOCK_DGRAM SOCK_DGRAM
  SOCK_RAW = 3,			/* Raw protocol interface.  */
#define SOCK_RAW SOCK_RAW
  SOCK_RDM = 4,			/* Reliably-delivered messages.  */
#define SOCK_RDM SOCK_RDM
  SOCK_SEQPACKET = 5,		/* Sequenced, reliable, connection-based,
				   datagrams of fixed maximum length.  */
#define SOCK_SEQPACKET SOCK_SEQPACKET
  SOCK_DCCP = 6,		/* Datagram Congestion Control Protocol.  */
#define SOCK_DCCP SOCK_DCCP
  SOCK_PACKET = 10,		/* Linux specific way of getting packets
				   at the dev level.  For writing rarp and
				   other similar things on the user level. */
#define SOCK_PACKET SOCK_PACKET

  /* Flags to be ORed into the type parameter of socket and socketpair and
     used for the flags parameter of paccept.  */

  SOCK_CLOEXEC = 02000000,	/* Atomically set close-on-exec flag for the
				   new descriptor(s).  */
#define SOCK_CLOEXEC SOCK_CLOEXEC
  SOCK_NONBLOCK = 00004000	/* Atomically mark descriptor(s) as
				   non-blocking.  */
#define SOCK_NONBLOCK SOCK_NONBLOCK
};


/* Protocol families.  */
#define PF_INET		2	/* IP protocol family.  */


/* Address families.  */
#define AF_INET		PF_INET

#define SOL_SOCKET    1 //setsockopt的level参数
#define SO_REUSEADDR  2
#define INADDR_ANY   (0x00000000) //bind

/* shutdown() how 参数 */
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2


#endif
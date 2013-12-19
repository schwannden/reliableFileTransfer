#ifndef __COMMON_H
#define __COMMON_H

#define SERV_PORT 9877
#define BACKLOG 10
#define FILENAMELEN 128
#define WINDOW_SIZE 4096
#define PMTU 1400 //it must be that sizeof(msg) > PMTU to garantee maximum performance
#define MSS 1500
#define ERR 1
#define ACK 2
#define SYN 4
#define FIN 8
#define RST 16
#define REQ 32
#define INITTIMEOUT 1
#define MAXTIMEOUT 32

typedef struct {
  size_t msgSize;
  size_t type;
  size_t seq;
  size_t ack;
  char msg[ MAXLINE ];
} packet_t;

packet_t dummyAAK6mn_6djhg;
#define PACKET_OVERHEAD ((char*)dummyAAK6mn_6djhg.msg - (char*)&dummyAAK6mn_6djhg)

void
makePacket( packet_t* ppacket, size_t msgSize, size_t type, size_t seq, size_t ack, const char* msg );

ssize_t
readvTimeout(int fd, const struct iovec *iov, int iovcnt, int timeoutSecond);

ssize_t
recvfromTimeout( int fd, void* vptr, size_t len, int flags, SA* destAddr, socklen_t* paddrLen, int timeoutSecond );

void
timeoutSigalarmHandler( int sig );

//debug utility
void
debug_packet( packet_t* p );
#endif

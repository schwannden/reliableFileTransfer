#include "../nplib/np_header.h"
#include "../nplib/np_lib.h"
#include "common_lib.h"

#define TIMEOUT_SIG 1;
#define TIMEOUT_ACCEPT 2;
#define TIMEOUT_SOCKOPT 3;

#ifndef TIMEOUT_METHOD
#define TIMEOUT_METHOD TIMEOUT_SIG
#endif

void makePacket( packet_t* ppacket, size_t msgSize, size_t type, size_t seq, size_t ack, const char* msg )
{
  ppacket->msgSize = msgSize;
  ppacket->type = type;
  ppacket->seq = seq;
  ppacket->ack = ack;
  if( msg != NULL )
	strncpy( ppacket->msg, msg, msgSize );
}

static void
timeoutSigalarmHandler( int sig )
{
  return;
}

ssize_t readvTimeout(int fd, const struct iovec *iov, int iovcnt, int timeoutSecond)
{
  struct sigaction sa, oldsa;
  int n;

  //set handler for SIGALRM
  sigaction( SIGALRM, NULL, &sa );
  sigemptyset( &sa.sa_mask );
  sa.sa_flags &= ~SA_RESTART;
  sa.sa_handler = timeoutSigalarmHandler;
  if( sigaction( SIGALRM, &sa, &oldsa ) == -1 )
    err_sys( "sigaction" );

  alarm( timeoutSecond );
  if( (n = readv( fd, iov, iovcnt )) < 0 ) {
    if( errno == EINTR ) {
	  errno = ETIMEDOUT;
      fprintf( stderr, "readv timed out\n" );
	}
  }
  alarm(0);
  if( sigaction( SIGALRM, &oldsa, NULL ) == -1 )
    err_sys( "sigaction" );
  
  return n;
}

ssize_t recvfromTimeout( int fd, void* vptr, size_t len, int flags,
                         SA* destAddr, socklen_t* paddrLen, int timeoutSecond )
{
  struct sigaction sa, oldsa;
  int n;

  //set handler for SIGALRM
  sigaction( SIGALRM, NULL, &sa );
  sigemptyset( &sa.sa_mask );
  sa.sa_flags &= ~SA_RESTART;
  sa.sa_handler = timeoutSigalarmHandler;
  if( sigaction( SIGALRM, &sa, &oldsa ) == -1 )
    err_sys( "sigaction" );

  alarm( timeoutSecond );
  if( (n = recvfrom( fd, vptr, len, flags, destAddr, paddrLen )) < 0 ) {
    if( errno == EINTR ) {
	  errno = ETIMEDOUT;
      fprintf( stderr, "recvfrom timed out\n" );
	}
  }
  alarm(0);
  if( sigaction( SIGALRM, &oldsa, NULL ) == -1 )
    err_sys( "sigaction" );
  
  return n;
}

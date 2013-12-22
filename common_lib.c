#include "../nplib/np_header.h"
#include "../nplib/np_lib.h"
#include "../nplib/error_functions.h"
#include "common_lib.h"

void makePacket( packet_t* ppacket, size_t msgSize, size_t type, size_t seq, size_t ack, const char* msg )
{
  ppacket->msgSize = msgSize;
  ppacket->type = type;
  ppacket->seq = seq;
  ppacket->ack = ack;
  if( msg != NULL )
    strncpy( ppacket->msg, msg, msgSize );
}

void sigalarmHandler( int sig )
{
  //timedout = 1;
  return;
}
void debug_packet( packet_t* p )
{
  if( p->type == SYN )
    printf( "packet sent: msgSize = %d, seq = %d\n", p->msgSize, p->seq );
  else if( p->type == ACK )
    printf( "ack received:              ack = %d\n", p->ack );
  else if( p->type == FIN )
    printf( "fin received\n" );
  else
    printf( "unrecognized type: %d\n", p->type );
}

void advanceWindow( int* p )
{
  *p = (((*p)+1)%WINDOW_SIZE);
}

seqnum_t seqtoi( seqnum_t n )
{
  return (n/RMSS)%WINDOW_SIZE;
}

int inorder( seqnum_t x, seqnum_t y, seqnum_t z )
{
  return (x <= y && y <= z)||(z <= x && x <= y)||(y <= z && z <= x);
}

static int timedout;

void makeTimer()
{
  struct sigaction sa;
  sigset_t blockset;
  sigemptyset( &blockset );
  sigemptyset( &sa.sa_mask );
  sa.sa_flags = SA_RESTART;
  sa.sa_handler = sigalarmHandler;
  if( sigaction( SIGALRM, &sa, NULL ) < 0 )
    err_sys( "sigaction error" );
}

void startTimer()
{
  timedout = 0;
  //block SIGALARM
  sigset_t blockset;
  sigemptyset( &blockset );
  sigaddset( &blockset, SIGALRM );
  if( sigprocmask( SIG_BLOCK, &blockset, NULL ) < 0 )
    err_sys( "sigprocmask error" );
  //start SIGALARM timer
  alarm(INITTIMEOUT);
}

void stopTimer()
{
  alarm(0);
}

int checkTimer()
{
  int timedout;
  sigset_t sigset;
  sigemptyset( &sigset );
  if( sigpending(&sigset) < 0 )
    err_sys( "sigpending error" );
  timedout = sigismember( &sigset, SIGALRM );
  //if timed out, unblock mask to flush signal
  if( timedout ) {
    sigaddset( &sigset, SIGALRM );
    if( sigprocmask( SIG_UNBLOCK, &sigset, NULL ) < 0 )
      err_sys( "sigprocmask error" );
  }
  return timedout;
}

/*

void startTimer()
{
  timedout = 0;
  alarm(INITTIMEOUT);
}

void stopTimer()
{
  alarm(0);
  timedout = 0;
}

int checkTimer()
{
  return timedout;
}
*/

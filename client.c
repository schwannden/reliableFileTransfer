#include "../nplib/np_header.h"
#include "../nplib/np_lib.h"
#include "common_lib.h"

void ftp_client( FILE* fpin, int clientfd, const SA* pservaddr, socklen_t servaddrLength );
packet_t packet, recvPacket;

//main function
int main( int argc, char** argv )
{
  int clientfd;
  struct sockaddr_in servaddr;

  if(argc != 2)    err_quit( "usage: client <IPaddress>\n" );

  bzero( &servaddr, sizeof(servaddr) );
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(SERV_PORT);
  Inet_pton( AF_INET, argv[1], &servaddr.sin_addr );

  clientfd = Socket( AF_INET, SOCK_DGRAM, 0 );

  ftp_client( stdin, clientfd, (const SA*)&servaddr, sizeof(servaddr) );

  return 0;
}

void ftp_client( FILE* fpin, int clientfd, const SA* pservaddr, socklen_t servaddrLength )
{
  //data
  int n, outputfd, openFlags, seq, ack, timeoutSecond = INITTIMEOUT, connectionBroke;
  struct sigaction sa, oldsa;
  struct iovec iov[2], riov[2];
  mode_t filePerms;

  openFlags = O_CREAT | O_WRONLY | O_TRUNC;
  filePerms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH; //rw-rw-rw-
  iov[0].iov_base = &packet;
  iov[0].iov_len = PACKET_OVERHEAD;
  iov[1].iov_base = packet.msg;
  iov[1].iov_len = sizeof(packet.msg);
  riov[0].iov_base = &recvPacket;
  riov[0].iov_len = PACKET_OVERHEAD;
  riov[1].iov_base = recvPacket.msg;
  riov[1].iov_len = sizeof(recvPacket.msg);

  //set handler for SIGALRM
  sigaction( SIGALRM, NULL, &sa );
  sigemptyset( &sa.sa_mask );
  sa.sa_flags &= ~SA_RESTART;
  sa.sa_handler = timeoutSigalarmHandler;
  if( sigaction( SIGALRM, &sa, &oldsa ) == -1 )
    err_sys( "sigaction" );
  //there is only one peer, so connect
  Connect( clientfd, pservaddr, servaddrLength );
  //client keep requesting file
  while( Fgets( packet.msg, sizeof(packet.msg), fpin ) != NULL ) {
	//initialize seq, ack, and connection status
    seq = ack = 0;
    connectionBroke = 0;
    //send request, a null terminated filename
    strtok( packet.msg, "\n" );
	n = strlen( packet.msg )+1;
	makePacket( &packet, n, REQ, seq, ack, NULL );
    write( clientfd, &packet, n+PACKET_OVERHEAD );

    //wait for first data packet
    alarm( timeoutSecond );
    while( readv( clientfd, riov, 2 ) < 0 ) {
      //if timed out
      if( errno == EINTR ) {
        //double retransmission interval and retransmit if timeoutSecond < MAXTIMEOUT
        if(timeoutSecond < MAXTIMEOUT) {
          timeoutSecond <<= 1;
          alarm(timeoutSecond);
          write( clientfd, packet.msg, strlen(packet.msg)+1 );
          printf( "request retransmission\n" );
        //otherwise a broken connection is infered, note a broken connection is a fatal error in client
        } else err_sys( "connection timed out" );
      } else
        err_sys( "readvTimeout" );
    }
    alarm(0);

    //if server can't open file
    if( recvPacket.type == ERR ) {
        write( STDOUT_FILENO, recvPacket.msg, recvPacket.msgSize );
    //if file can be tranmited
    } else if(recvPacket.type == SYN){
      //open output file
      strcpy( packet.msg+strlen(packet.msg), "_cp" );
      if( (outputfd = open( packet.msg, openFlags, filePerms)) == -1 )
        err_sys("open file %s", packet.msg);
     
      //start receiving file
      while( recvPacket.type != FIN ) {
        if( recvPacket.type == SYN ){
          //if acked
          if( recvPacket.seq == ack ){
            timeoutSecond = INITTIMEOUT;
            printf( "packet received, seq = %d, msgSize = %d\n", recvPacket.seq, recvPacket.msgSize );
            write( outputfd, recvPacket.msg, recvPacket.msgSize );
            makePacket( &packet, 0, ACK, 0, ack, NULL );
            ack += recvPacket.msgSize;
          }
        }

        write( clientfd, &packet, PACKET_OVERHEAD );
        alarm(timeoutSecond);
        //timed readv
        while( readv( clientfd, riov, 2 ) < 0 ) {
          //if timed out
          if( errno == EINTR ) {
            //double retransmission interval and retransmit if timeoutSecond < MAXTIMEOUT
            if(timeoutSecond < MAXTIMEOUT) {
              timeoutSecond <<= 1;
              alarm(timeoutSecond);
              write( clientfd, &packet, PACKET_OVERHEAD );
              printf( "ack retransmission, packet.ack = %d\n", packet.ack );
            //otherwise a broken connection is infered, note a broken connection is a fatal error in client
            } else err_sys( "connection timed out" );
          } else
            err_sys( "recvfromTimeout" );
        }
        alarm(0);
      }
      printf( "\n*** File transfer conplete ***\n" );
      close(outputfd);
    }
  }
  //restore sigalarm handler
  if( sigaction( SIGALRM, &oldsa, NULL ) == -1 )
    err_sys( "sigaction" );
}


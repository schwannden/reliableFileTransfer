#include "../nplib/np_header.h"
#include "../nplib/np_lib.h"
#include "common_lib.h"

packet_t packet, recvPacket;
static void timeoutSigalarmHandler( int sig );
//debug utility function
void packetSent()
{
  printf( "packet sent: msgSize = %d, seq = %d\n", packet.msgSize, packet.seq );
}
void ackRecv()
{
  printf( "ack received: msgSize = %d, ack = %d\n", recvPacket.msgSize, recvPacket.ack );
}

int main( int argc, char** argv )
{
  //data
  int udp_servfd, bytesRead, len, filefd, fileSize, seq, ack, timeoutSecond = INITTIMEOUT, connectionBroke;
  struct sockaddr_in udp_servaddr, tcp_servaddr, cliaddr;
  char fileName[FILENAMELEN+1], clientIP[INET_ADDRSTRLEN];
  struct sigaction sa, oldsa;
  const int on = 1;
  fd_set rset;

  //UDP socket create and bind
  udp_servfd = Socket( AF_INET, SOCK_DGRAM, 0 );
  bzero( &udp_servaddr, sizeof(udp_servaddr) );
  udp_servaddr.sin_family = AF_INET;
  udp_servaddr.sin_port = htons(SERV_PORT);
  udp_servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  Bind( udp_servfd, (SA*)&udp_servaddr, sizeof(udp_servaddr) );

  //set handler for SIGALRM
  sigaction( SIGALRM, NULL, &sa );
  sigemptyset( &sa.sa_mask );
  sa.sa_flags &= ~SA_RESTART;
  sa.sa_handler = timeoutSigalarmHandler;
  if( sigaction( SIGALRM, &sa, &oldsa ) == -1 )
    err_sys( "sigaction" );

  while( 1 ){
    printf( "waiting for connection...\n" );
    len = sizeof(cliaddr);
    //receiving a null terminated filename
    recvfrom( udp_servfd, fileName, sizeof(fileName), 0, (SA*)&cliaddr, &len  );
    Inet_ntop(AF_INET, &cliaddr.sin_addr, clientIP, sizeof(clientIP));
    printf( "server reads request for '%s' from %s:%d\n", fileName, clientIP, ntohs(cliaddr.sin_port) );
    if( (filefd = open( fileName, O_RDONLY )) < 0 ){
      //file open error
      switch( errno ){
        case EACCES: snprintf( packet.msg, sizeof(packet.msg), "Access denied: %s\n", fileName ); break;
        case ENOENT: snprintf( packet.msg, sizeof(packet.msg), "No such file or directory: %s\n", fileName ); break;
        default: snprintf( packet.msg, sizeof(packet.msg), "Server open failed\n" ); break;
      }
      printf( "open error: %s", packet.msg );
      makePacket( &packet, strlen(packet.msg), ERR, 0, 0, NULL );
      Sendto( udp_servfd, &packet, packet.msgSize + PACKET_OVERHEAD, 0, (const SA*)&cliaddr, len );
    } else {
      //find out file size (just an extra reliable guarantee)
      fileSize = lseek( filefd, -1, SEEK_END )+1;
      printf( "reading %s, size %d bytes\n", fileName, fileSize );
      lseek( filefd, 0, SEEK_SET );
      //initiating first packet
      connectionBroke = 0;
      seq = ack = 0;
      bytesRead = Readn( filefd, packet.msg, min( PMTU, fileSize ) );
      while( (fileSize>0) && (connectionBroke==0) ) {
        makePacket( &packet, bytesRead, SYN, seq, 0, NULL );
        //start timer
        alarm(timeoutSecond);
        Sendto( udp_servfd, &packet, packet.msgSize + PACKET_OVERHEAD, 0, (const SA*)&cliaddr, len );
        packetSent();

        //timed recvfrom
        while( recvfrom( udp_servfd, &recvPacket, sizeof(recvPacket), 0, (SA*)&cliaddr, &len ) < 0 ) {
          //if timed out
          if( errno == EINTR ) {
            //double retransmission interval and retransmit if timeoutSecond < maxTimeout
            if(timeoutSecond < MAXTIMEOUT) {
              timeoutSecond <<= 1;
              alarm(timeoutSecond);
              Sendto( udp_servfd, &packet, packet.msgSize + PACKET_OVERHEAD, 0, (const SA*)&cliaddr, len );
              printf( "packet retransmission, seq = %d\n", packet.seq );
            //otherwise a broken connection is infered
            } else {
              //if connection timed out, send a last RST datagram then waits for other requests
              connectionBroke = 1;
              printf( "connection timed ouit\n" );
              makePacket( &packet, 0, RST, 0, 0, NULL );
            }
          } else
            err_sys( "recvfromTimeout" );
        }
        //trun off timer
        alarm(0);
        if( connectionBroke == 0 ) ackRecv();

        if( recvPacket.type == ACK && connectionBroke == 0 ) {
          //if acked
          if( recvPacket.ack == seq ) {
            //advance window
            seq += bytesRead;
            printf( "%d ACKed\n", recvPacket.ack );
            //read next segment of data
            fileSize -= bytesRead;
            bytesRead = Readn( filefd, packet.msg, min( PMTU, fileSize ) );
            //reset timeout interval for next packet
            timeoutSecond = INITTIMEOUT;
          //if lost ack
          } else if( recvPacket.ack > seq ) {
            //advance window
            seq = recvPacket.ack;
            printf( "%d ACKed\n", packet.ack );
            //read next segment of data
            fileSize -= bytesRead;
            bytesRead = Readn( filefd, packet.msg, min( PMTU, fileSize ) );
            //reset timeout interval for next packet
            timeoutSecond = INITTIMEOUT;
          }
        }
      }

      //sending fin to signal comple file transfer
      if( fileSize == 0 ) {
        makePacket( &packet, 0, FIN, seq, ack, "\0" );
        printf( "***file transfer succeed***\n" );
        Sendto( udp_servfd, &packet, packet.msgSize + PACKET_OVERHEAD, 0, (const SA*)&cliaddr, len );
        close( filefd );
      }
    }
  }
  //restore sigalarm handler
  if( sigaction( SIGALRM, &oldsa, NULL ) == -1 )
    err_sys( "sigaction" );
  return 0;
}

static void timeoutSigalarmHandler( int sig )
{
  return;
}


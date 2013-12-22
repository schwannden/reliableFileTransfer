#include "../nplib/np_header.h"
#include "../nplib/np_lib.h"
#include "common_lib.h"

/*               notation on my usage of variables

int               lw                          uw           ready
 +------+------+------+------+------+------+------+------+------+....
 |acked |acked | sent | sent |acked | sent | data | data |empty |....
 +------+------+------+------+------+------+------+------+------+....
                       ^                    ^             ^
seqnum_t           ackExpected             seq       nextByteToRead */

packet_t packet[WINDOW_SIZE], recvPacket;
int receiveREQ( int servfd, struct sockaddr_in* pcliaddr, socklen_t* plen );
int empty( int lw, int ready );
int remain( int l, int u );
int packetOutstanding( int l, int u );

int main( int argc, char** argv )
{
  //data
  //ackExpected=lower window(the ack expecting to appear, which is the lowest un-acked packet's seq + its size)
  //seq=next-byte-to-send
  //uw=upper window, which is also the next byte to read from file
  int i, n, udp_servfd, bytesRead, len, filefd, fileSize, lw, uw, ready,
      timeoutSecond, connectionBroke, maxfd, fileeof;
  struct sockaddr_in udp_servaddr, tcp_servaddr, cliaddr;
  seqnum_t seq, ackExpected, nextByteToRead;
  int acked[WINDOW_SIZE];
  fd_set rset, wset;
  FD_ZERO( &rset );
  FD_ZERO( &wset );
  //initialize data
  connectionBroke = 1;
  timeoutSecond = INITTIMEOUT;
  makeTimer();
  for( i = 0 ; i < WINDOW_SIZE ; i++ ) acked[i] = 0;
  //RMSS is the real MSS under PMTU and MSS constraints
  //UDP socket create and bind
  udp_servfd = Socket( AF_INET, SOCK_DGRAM, 0 );
  bzero( &udp_servaddr, sizeof(udp_servaddr) );
  udp_servaddr.sin_family = AF_INET;
  udp_servaddr.sin_port = htons(SERV_PORT);
  udp_servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  Bind( udp_servfd, (SA*)&udp_servaddr, sizeof(udp_servaddr) );

  while( 1 ){
    //if state == no-connection, block waiting for request
    if(connectionBroke == 1){ 
      i = Fcntl( udp_servfd, F_GETFL, 0 );
      Fcntl( udp_servfd, F_SETFL, i & ~O_NONBLOCK );
      bzero( &cliaddr, sizeof(cliaddr) );
      len = sizeof(cliaddr);
      if( (filefd = receiveREQ( udp_servfd, &cliaddr, &len )) < 0 ) {
        switch( errno ){
          case EMEDIUMTYPE: printf( "unrecognized packet received\n" ); break;
          case EACCES: snprintf( packet[0].msg, sizeof(packet[0].msg), "Access denied: %s\n", recvPacket.msg); break;
          case ENOENT: snprintf( packet[0].msg, sizeof(packet[0].msg), "No such file or directory: %s\n", recvPacket.msg); break;
          default: snprintf( packet[0].msg, sizeof(packet[0].msg), "Server open failed, errno = %d\n", errno ); break;
        }
        if( errno != EMEDIUMTYPE ){
          printf( "open error: %s\n", recvPacket.msg );
          makePacket( &packet[0], strlen(packet[0].msg), ERR, 0, 0, NULL );
          Sendto( udp_servfd, &packet[0], packet[0].msgSize + PACKET_OVERHEAD, 0, (const SA*)&cliaddr, len );
        }
      } else {
        //find out file size (just an extra reliable guarantee)
        fileSize = lseek( filefd, -1, SEEK_END )+1;
        printf( "reading %s, size %d bytes\n", recvPacket.msg, fileSize );
        lseek( filefd, 0, SEEK_SET );
        //initiating first packet
        connectionBroke = fileeof = lw = uw = ready = 0;
        ackExpected = seq = nextByteToRead = 0;
        startTimer();
      }
    } else{
    //prepare for select
    i = Fcntl( udp_servfd, F_GETFL, 0 );
    Fcntl( udp_servfd, F_SETFL, i | O_NONBLOCK );
    if(fileeof == 0 && remain(ready, lw) )
      FD_SET( filefd, &rset );
    FD_SET( udp_servfd, &rset );
    if( packetOutstanding( uw, ready ) )
      FD_SET( udp_servfd, &wset );
    maxfd = max(udp_servfd, filefd) + 1;
    select( maxfd, &rset, &wset, NULL, NULL );

//event: file ready for read and there is space in buffer
      if( FD_ISSET(filefd, &rset) ) {
        //printf( "event: file ready for read\n" );
        while( fileSize > 0 && remain(ready, lw) ) {
          acked[ready] = 0;
          bytesRead = read( filefd, packet[ready].msg, RMSS );
          //printf( "read %d bytes into slot %d, seq == %d\n", bytesRead, ready, nextByteToRead );
          makePacket( &packet[ready], bytesRead, SYN, nextByteToRead, 0, NULL );
          nextByteToRead += bytesRead;
          fileSize -= bytesRead;
          advanceWindow( &ready );
        }
        if( fileSize == 0 ) {
          fileeof = 1;
          FD_CLR( filefd, &rset );
        }
        FD_SET( udp_servfd, &wset );
      }
//event: sending packet
      if( FD_ISSET(udp_servfd, &wset) ) {
        //printf( "event: udp_servfd ready for write\n" );
        while( packetOutstanding( uw, ready ) ){
          if( sendto( udp_servfd, &packet[uw], packet[uw].msgSize + PACKET_OVERHEAD, 0, (const SA*)&cliaddr, len ) > 0 ){
            //printf( "slot %d, ", uw );
            //debug_packet( &packet[uw] );
            seq += packet[uw].msgSize;
            advanceWindow( &uw );
          } else
            if( errno != EWOULDBLOCK )
              err_sys( "sendto error on udp_servfd" );
        }
        if( checkTimer() ){
          //printf( "timer expired~~~~\n" );
          for( i = lw ; i != uw ; advanceWindow(&i) ){
            if( acked[i] == 0 )
              //printf( "slot %d, seq = %d retransmitted\n", i, packet[i].seq );
              sendto( udp_servfd, &packet[i], packet[i].msgSize + PACKET_OVERHEAD, 0, (const SA*)&cliaddr, len );
          }
          startTimer();
        }
      }
//event: ack received
      if( FD_ISSET(udp_servfd, &rset) ) {
        //printf( "event: udp_servfd ready for read\n" );
        while( recvfrom( udp_servfd, &recvPacket, sizeof(recvPacket), 0, (SA*)&cliaddr, &len ) > 0 ) {
          //debug_packet( &recvPacket );
          if( inorder( ackExpected, recvPacket.ack, seq ) ){
            i = seqtoi( recvPacket.ack - 1 );
            //printf( "slot %d acked, ack = %d\n", i, recvPacket.ack );
            acked[ i ] = 1;
            //advance lower window
            if( i == lw ) {
              //printf( "packet acked, seq = %d\n", packet[i].seq );
              acked[ lw ] = 0;
              lw = (lw+1) % WINDOW_SIZE;
              ackExpected += packet[lw].msgSize;
              while( acked[lw] == 1 ){
                acked[ lw ] = 0;
                lw = (lw+1) % WINDOW_SIZE;
                ackExpected += packet[lw].msgSize;
              }
            }
          } else{
            //printf( "out of order ack received, ack = %d\n", recvPacket.ack );
          }
        }
        if( errno != EWOULDBLOCK ) err_sys( "recvfrom error on udp_servfd" );
        if( fileeof == 1 && lw == uw ) {
          makePacket( &packet[lw], 0, FIN, seq, 0, NULL );
          printf( "***file transfer succeed***\n" );
          Sendto( udp_servfd, &packet[lw], PACKET_OVERHEAD, 0, (const SA*)&cliaddr, len );
          close( filefd );
          connectionBroke = 1;
          checkTimer();
          stopTimer();
        }
      }
    }    
  }
  return 0;
}

int receiveREQ( int servfd, struct sockaddr_in* pcliaddr, socklen_t* plen )
{
  char clientIP[INET_ADDRSTRLEN];
  printf( "waiting for request...\n" );
  //receiving a null terminated filename
  recvfrom( servfd, &recvPacket, sizeof(recvPacket), 0, (SA*)pcliaddr, plen  );
  if( recvPacket.type != REQ ) {
    errno = EMEDIUMTYPE;
    return -1;
  } 

  Inet_ntop(AF_INET, &pcliaddr->sin_addr, clientIP, sizeof(clientIP));
  printf( "server reads request for '%s' from %s:%d\n", recvPacket.msg, clientIP, ntohs(pcliaddr->sin_port) );
  //open file and open error
  return open( recvPacket.msg, O_RDONLY );
}
int empty( int lw, int ready )
{
  return (ready == lw);
}
int remain( int l, int u )
{
  return ((l+1)%WINDOW_SIZE != u);
}
int packetOutstanding( int l, int u )
{
  return (l != u );
}

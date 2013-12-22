#include "../nplib/np_header.h"
#include "../nplib/np_lib.h"
#include "common_lib.h"

/*               notation on my usage of variables

int                      lw
 +------+------+------+------+------+------+------+------+------+....
 |wrote |wrote | wrote|      |      | recv |      | recv |      |....
 +------+------+------+------+------+------+------+------+------+....
                       ^
seqnum_t           seqExpected                                     */

packet_t packet, recvPacket[WINDOW_SIZE];
void ftp_client( FILE* fpin, int clientfd );

//main function
int main( int argc, char** argv )
{
  int clientfd;
  struct sockaddr_in servaddr;

  if(argc != 2)
    err_quit( "usage: client <IPaddress>\n" );

  bzero( &servaddr, sizeof(servaddr) );
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(SERV_PORT);
  Inet_pton( AF_INET, argv[1], &servaddr.sin_addr );
  clientfd = Socket( AF_INET, SOCK_DGRAM, 0 );
  //there is only one peer, so connect
  Connect( clientfd, (const SA*)&servaddr, sizeof(servaddr));

  ftp_client( stdin, clientfd );

  return 0;
}

void ftp_client( FILE* fpin, int clientfd )
{
  //data
  int i, n, filefd, openFlags, second, lw;
  seqnum_t seqExpected;
  struct sigaction sa, oldsa;
  int received[WINDOW_SIZE];
  mode_t filePerms;
  //initializing data
  filePerms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH; //rw-rw-rw-
  openFlags = O_CREAT | O_WRONLY | O_TRUNC;
  //set handler for SIGALRM
  sigaction( SIGALRM, NULL, &sa );
  sigemptyset( &sa.sa_mask );
  sa.sa_flags &= ~SA_RESTART;
  sa.sa_handler = sigalarmHandler;
  if( sigaction( SIGALRM, &sa, &oldsa ) == -1 )
    err_sys( "sigaction" );

  //client keep requesting file
  while( Fgets( packet.msg, sizeof(packet.msg), fpin ) != NULL ) {
    //initialize data for each new request
    second = INITTIMEOUT;
    seqExpected = lw = 0;
    for( i = 0 ; i < WINDOW_SIZE ; i++) received[i] = 0;
    //send request, a null terminated filename
    strtok( packet.msg, "\n" );
    makePacket( &packet, strlen( packet.msg )+1, REQ, 0, 0, NULL );
    write( clientfd, &packet, packet.msgSize+PACKET_OVERHEAD );

    //read first packet from server, and decide whether a connection will be open
    read( clientfd, &recvPacket[lw], RMSS+PACKET_OVERHEAD );
    if( recvPacket[lw].type == ERR ) {
      //if server can't open file, connection end
      write( STDOUT_FILENO, recvPacket[0].msg, recvPacket[0].msgSize );
    } else {
      //otherwise connection begin
      //open output file
      strcpy( packet.msg+strlen(packet.msg), "_cp" );
      if( (filefd = open( packet.msg, openFlags, filePerms)) == -1 )
        err_sys("open file %s", packet.msg);
      //start receiving file
      while( recvPacket[lw].type != FIN ) {
	    //printf( "packet received: seq = %d\n", recvPacket[lw].seq );
        makePacket( &packet, 0, ACK, 0, recvPacket[lw].seq+recvPacket[lw].msgSize, NULL );
        write( clientfd, &packet, PACKET_OVERHEAD );
        received[lw] = 1;
        if( recvPacket[lw].seq == seqExpected ){
          while( received[lw] == 1 ){
            received[lw] = 0;
            seqExpected += recvPacket[lw].msgSize;
            write( filefd, recvPacket[lw].msg, recvPacket[lw].msgSize );
            advanceWindow( &lw );
          }
        } else if( recvPacket[lw].seq > seqExpected ){
          received[lw] = 0;
          i = seqtoi( recvPacket[lw].seq );
          if( received[i] == 0 ){
            memcpy( &recvPacket[i], &recvPacket[lw], recvPacket[lw].msgSize + PACKET_OVERHEAD );
            received[i] = 1;
          }
        }
        read( clientfd, &recvPacket[lw], RMSS+PACKET_OVERHEAD );
      }
      printf( "***file transfer succeed***\n" );
      close( filefd );
    }
  }
  //restore sigalarm handler
  if( sigaction( SIGALRM, &oldsa, NULL ) == -1 )
    err_sys( "sigaction" );
}


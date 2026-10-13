#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>     /* ALTERADO: para formatar HH:MM:SS */
#include <errno.h>
#define SERV_HOST_ADDR "127.0.0.1" /* Don's host machine */
#define SERV_PORT_ID 6090

int main()
{
 int sockid;
 struct sockaddr_in ssock_addr;
 struct timeval tp;
 struct tm tm;
 time_t t;
 char hora[16];
 pid_t pid = getpid();  /* ALTERADO: PID no log, para distinguir clientes simultâneos */

  /* create a socket */
 if ((sockid = socket(AF_INET,SOCK_STREAM, 0)) < 0){
     printf("erro criacao=%d (%s)\n", errno, strerror(errno));
     exit(EXIT_FAILURE);
 }
 printf("[cliente %d] Criando struct addr p/ server\n", pid);
 bzero((char *)&ssock_addr,sizeof(ssock_addr));
 ssock_addr.sin_family = AF_INET;
 ssock_addr.sin_addr.s_addr = inet_addr(SERV_HOST_ADDR);
 ssock_addr.sin_port = htons(SERV_PORT_ID);
 if (connect(sockid, (struct sockaddr *) &ssock_addr, sizeof(ssock_addr)) < 0){
    printf("error connecting to server, error:  %d (%s)\n", errno, strerror(errno));
    close(sockid);
    exit(EXIT_FAILURE);
 }
 /* send time of day */
 gettimeofday(&tp, NULL);   /* ALTERADO: NULL no lugar de struct timezone (obsoleta) */
 /* convert from host byte order to network byte order */
 printf("[cliente %d] client: local time is %ld\n", pid, tp.tv_sec);
 tp.tv_sec = htonl(tp.tv_sec);
 tp.tv_usec = htonl(tp.tv_usec);
 /* send time of day to other side (ALTERADO: verifica retorno) */
 if (write(sockid, &tp, sizeof(tp)) != (ssize_t)sizeof(tp)){
     printf("error writing to socket\n");
     close(sockid);
     exit(EXIT_FAILURE);
 }
 /* get time of day back fro other side and display
    (aqui o cliente fica bloqueado até o servidor liberar o atendimento) */
 if ((read(sockid, &tp, sizeof(tp))) != (ssize_t)sizeof(tp)){   /* ALTERADO: antes só testava < 0 */
     printf("error reading new socket\n");
     close(sockid);
     exit(EXIT_FAILURE);
 }
 /* convert from network byte order to host byte order */
 tp.tv_sec = ntohl(tp.tv_sec);
 tp.tv_usec = ntohl(tp.tv_usec);
 printf("[cliente %d] client: remote time is %ld\n", pid, tp.tv_sec);

 /* ALTERADO: mostra também em formato legível */
 t = tp.tv_sec;
 localtime_r(&t, &tm);
 strftime(hora, sizeof(hora), "%H:%M:%S", &tm);
 printf("[cliente %d] hora recebida do servidor: %s\n", pid, hora);

 close(sockid);
 return 0;
}
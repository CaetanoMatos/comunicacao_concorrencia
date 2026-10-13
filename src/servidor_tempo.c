#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>      /* ALTERADO: para ignorar SIGPIPE */
#include <time.h>        /* ALTERADO: para formatar HH:MM:SS */
#include <pthread.h>     /* ALTERADO: threads (concorrência) */
#include <semaphore.h>   /* ALTERADO: semáforo POSIX */
#define MY_PORT_ID 6090
#define TEMPO_BLOQUEIO 2 /* segundos que o recurso fica bloqueado */

/* SEMÁFORO: variável global, logo compartilhada por todas as threads.
   Será inicializada com valor 1 (semáforo binário) no main(). */
static sem_t semaforo;

/* Dados passados para cada thread de atendimento */
struct atendimento {
    int sock;   /* socket da conexão aceita */
    int num;    /* número sequencial do atendimento (só para o log) */
};

/* Escreve a hora atual (ou de t) como HH:MM:SS em buf */
static void formata_hora(time_t t, char *buf, size_t n)
{
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%H:%M:%S", &tm);
}

/* Função executada por CADA thread: atende um cliente */
static void *atende_cliente(void *arg)
{
    struct atendimento *a = (struct atendimento *)arg;
    int newsockid = a->sock;
    int num = a->num;
    struct timeval tp;
    char hora[16];
    ssize_t n;

    free(a);

    printf("[atend #%d] conexao aceita\n", num);

    /* le tempo remoto (FORA da região crítica: não usa o recurso protegido) */
    n = read(newsockid, &tp, sizeof(tp));
    if (n != (ssize_t)sizeof(tp)) {
        printf("[atend #%d] erro lendo hora do cliente\n", num);
        close(newsockid);
        return NULL;
    }
    /* convert from network byte order to host byte order */
    tp.tv_sec = ntohl(tp.tv_sec);
    tp.tv_usec = ntohl(tp.tv_usec);
    printf("[atend #%d] server: remote time is %ld\n", num, tp.tv_sec);

    /* ---- ENTRADA NA REGIÃO CRÍTICA ---- */
    printf("[atend #%d] aguardando semaforo (sem_wait)...\n", num);
    /* sem_wait: se o semáforo vale 0, a thread BLOQUEIA aqui até alguém
       fazer sem_post. Se vale 1, decrementa para 0 e segue.
       O laço trata interrupção por sinal (EINTR). */
    while (sem_wait(&semaforo) == -1 && errno == EINTR)
        ;

    /* ============== REGIÃO CRÍTICA (início) ============== */
    /* get local time of day and send to client */
    gettimeofday(&tp, NULL);
    formata_hora(tp.tv_sec, hora, sizeof(hora));
    printf("[atend #%d] ENTROU na regiao critica - server: local time is %ld (%s)\n",
           num, tp.tv_sec, hora);
    /* convert from host byte order to network byte order */
    tp.tv_sec = htonl(tp.tv_sec);
    tp.tv_usec = htonl(tp.tv_usec);
    if (write(newsockid, &tp, sizeof(tp)) != (ssize_t)sizeof(tp))
        printf("[atend #%d] erro enviando hora ao cliente\n", num);
    /* (sem return aqui dentro: o sem_post abaixo SEMPRE deve executar) */

    /* BLOQUEIO DE 2 SEGUNDOS: o recurso continua tomado, pois o sem_post
       ainda não foi chamado. Quem está em sem_wait continua esperando. */
    sleep(TEMPO_BLOQUEIO);
    /* ============== REGIÃO CRÍTICA (fim) ============== */

    /* sem_post: devolve o recurso (semáforo volta a 1) e acorda UMA thread
       que esteja bloqueada em sem_wait. */
    sem_post(&semaforo);
    /* ---- SAÍDA DA REGIÃO CRÍTICA ---- */

    formata_hora(time(NULL), hora, sizeof(hora));
    printf("[atend #%d] liberou o recurso (sem_post) as %s\n", num, hora);

    /* encerramento do atendimento deste cliente */
    close(newsockid);
    return NULL;
}

int main(){
  int sockid, newsockid, i;
  int opt = 1;
  struct sockaddr_in ssock_addr;

  /* ALTERADO: line-buffered, para o log aparecer certo mesmo com redirecionamento */
  setvbuf(stdout, NULL, _IOLBF, 0);

  /* ALTERADO: evita que o servidor morra se um cliente fechar antes do write */
  signal(SIGPIPE, SIG_IGN);

  /* CRIAÇÃO E INICIALIZAÇÃO DO SEMÁFORO
     sem_init(&sem, pshared, valor_inicial)
       pshared = 0 -> compartilhado entre THREADS do mesmo processo
       valor = 1   -> só 1 atendimento por vez no recurso (semáforo binário) */
  if (sem_init(&semaforo, 0, 1) < 0){
     printf("erro criando semaforo: %s\n", strerror(errno));
     exit(EXIT_FAILURE);
  }

  if ((sockid = socket(AF_INET,SOCK_STREAM, 0)) < 0){
     printf("erro criação= %d (%s)\n", errno, strerror(errno));
     sem_destroy(&semaforo);
     exit(EXIT_FAILURE);
  }  /* man errno */

  /* ALTERADO: permite reiniciar o servidor sem esperar o TIME_WAIT da porta */
  if (setsockopt(sockid, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
     printf("aviso: setsockopt(SO_REUSEADDR) falhou: %s\n", strerror(errno));

  /* name the socket using wildcards */
  bzero((char *) &ssock_addr,sizeof(ssock_addr));
  ssock_addr.sin_family = AF_INET;
  ssock_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ssock_addr.sin_port = htons(MY_PORT_ID);

  /* bind the socket to port address */
  if (bind(sockid, (struct sockaddr *)&ssock_addr,sizeof(ssock_addr)) < 0){
      printf("error binding socket, error:%d (%s)\n", errno, strerror(errno));
      close(sockid);
      sem_destroy(&semaforo);
      exit(EXIT_FAILURE);
  }

  /* start accepting connections (backlog aumentado de 5 para 10) */
  if (listen(sockid, 10) < 0){
     printf("erro listening: %d (%s)\n", errno, strerror(errno));
     close(sockid);
     sem_destroy(&semaforo);
     exit(EXIT_FAILURE);
  }

  printf("server: aguardando conexoes na porta %d (semaforo inicial = 1)\n", MY_PORT_ID);

  for (i=1; i<=50000 ;i++) {
      pthread_t tid;
      struct atendimento *a;
      int rc;

      /* accept a connection (ALTERADO: NULL no lugar de (int *)0) */
      newsockid = accept(sockid, NULL, NULL);
      if (newsockid < 0){
         if (errno == EINTR) {   /* interrompido por sinal: tenta de novo */
             i--;
             continue;
         }
         printf("error accepting socket, error: %d (%s)\n", errno, strerror(errno));
         close(sockid);
         sem_destroy(&semaforo);
         exit(EXIT_FAILURE);
      }

      /* CRIAÇÃO DA CONCORRÊNCIA: uma thread por cliente.
         A thread principal volta imediatamente ao accept(), então várias
         threads podem estar disputando o semáforo ao mesmo tempo. */
      a = malloc(sizeof(*a));
      if (a == NULL) {
          printf("erro de memoria\n");
          close(newsockid);
          continue;
      }
      a->sock = newsockid;
      a->num = i;

      rc = pthread_create(&tid, NULL, atende_cliente, a);
      if (rc != 0) {
          printf("erro criando thread: %s\n", strerror(rc));
          free(a);
          close(newsockid);
          continue;
      }
      /* detached: a thread libera seus recursos sozinha ao terminar
         (não precisa de pthread_join) */
      pthread_detach(tid);
  }

  /* ENCERRAMENTO / LIMPEZA
     Obs.: em uso normal este ponto nem é alcançado (50000 conexões). Num
     servidor "de verdade" esperaríamos as threads terminarem antes de destruir
     o semáforo. */
  close(sockid);
  sem_destroy(&semaforo);
  return 0;
}
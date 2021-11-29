/*
 ******************************************************************************
 *                                 proxy.c                                    *
 *                           simple HTTP web proxy                            *
 *                        CS230: System Programming                           *
 *                          20200434 Lee, Giyeon                              *
 *  ************************************************************************  *
 */
#include <stdio.h>
#include "csapp.h"

/////////////////////////////////// Macros ///////////////////////////////////
/*************
 * Constants *
 *************/
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
// Recommended max cache and object sizes

/////////////////////////////// Custom Structs ///////////////////////////////

typedef struct {
  char host[100];
  char port[16];
  char path[MAXLINE];
} request_line;

typedef struct {
  char name[100];
  char value[100];
} request_header;

typedef struct {
  char *name;
  char *object;
} cache_line;

typedef struct {
  int used_cnt;
  cache_line *objects;
} cache;

////////////////////////////// Global Variables //////////////////////////////

cache global_cache;
sig_atomic_t readcnt;
sem_t w;

/////////////////////////// Function Declarations ///////////////////////////

int main(int, char **);
void *thread(void *);
void doit(int);
void parse_uri(char *, request_line *);
void read_requesthdrs(rio_t *, request_header *, int *);
request_header build_header(char *line);
int reader(int, char *);
void writer(char *, char *);

/////////////////////////// Function Definitions ///////////////////////////

/* $begin main */
int main(int argc, char **argv) {
  int listenfd, *connfd;
  pthread_t tid;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  Sem_init(&w, 0, 1);
  readcnt = 0;
  global_cache.objects = (cache_line *)Malloc(sizeof(cache_line) * 100);
  global_cache.used_cnt = 0;
  for (int i = 0; i < 100; ++i) {
    global_cache.objects[i].name = Malloc(sizeof(char) * MAXLINE);
    global_cache.objects[i].object = Malloc(sizeof(char) * MAX_OBJECT_SIZE);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Malloc(sizeof(int));
    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0); // line:netp:tiny:accept
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfd);
  }
}
/* $end main */

/*
 * thread - support for concurrency
 */
void *thread(void *vargp) {
  int connfd = *(unsigned int *)vargp;
  Pthread_detach(pthread_self());
  doit(connfd);
  Close(connfd);
  return NULL;
}

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) {
  char buf[MAXLINE], uri[MAXLINE], object_buf[MAX_OBJECT_SIZE], method[MAXLINE],
      version[MAXLINE];
  char *ptr = buf;
  int total_size, connfd, cnt;
  size_t n;
  rio_t rio, serverrio;
  request_line reqln;
  request_header hd[100];

  Rio_readinitb(&rio, fd);
  if (!Rio_readlineb(&rio, buf, MAX_OBJECT_SIZE)) // line:netp:doit:readrequest
    return;
  sscanf(buf, "%s %s %s", method, uri, version); // line:netp:doit:parserequest
  parse_uri(uri, &reqln);
  cnt = 0;
  read_requesthdrs(&rio, hd, &cnt); // line:netp:doit:readrequesthdrs

  strcpy(uri, reqln.host);
  strcpy(uri + strlen(uri), reqln.path);
  if (reader(fd, uri))
    return;

  total_size = 0;

  connfd = Open_clientfd(reqln.host, reqln.port);
  Rio_readinitb(&serverrio, connfd);
  sprintf(ptr, "GET %s HTTP/1.0\r\n", reqln.path);
  ptr = buf + strlen(buf);
  for (int i = 0; i < cnt; ++i) {
    sprintf(ptr, "%s : %s", hd[i].name, hd[i].value);
    ptr = buf + strlen(buf);
  }
  sprintf(ptr, "\r\n");
  Rio_writen(connfd, buf, MAXLINE);

  Rio_readinitb(&rio, connfd);
  while ((n = Rio_readlineb(&rio, buf, MAXLINE))) {
    Rio_writen(fd, buf, n);
    strcpy(object_buf + total_size, buf);
    total_size += n;
  }
  if (total_size < MAX_OBJECT_SIZE)
    writer(uri, object_buf);
  Close(connfd);
}
/* $end doit */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp, request_header *hd, int *nump) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    hd[(*nump)++] = build_header(buf);
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
void parse_uri(char *uri, request_line *reqln) {
  if (strstr(uri, "http://") != uri)
    exit(1);

  uri += strlen("http://");
  char *ptr = strstr(uri, ":");
  *ptr = '\0';
  strcpy(reqln->host, uri);
  uri = ptr + 1;
  ptr = strstr(uri, "/");
  *ptr = '\0';
  strcpy(reqln->port, uri);
  *ptr = '/';
  strcpy(reqln->path, ptr);
}
/* $end parse_uri */

/*
 * build_header - parse header
 */
/* $begin build_header */
request_header build_header(char *line) {
  request_header header;
  char *ptr = strstr(line, ":");
  if (ptr == NULL)
    exit(1);

  *ptr = '\0';
  strcpy(header.name, line);
  strcpy(header.value, ptr + 2);
  return header;
}
/* $end build_header */

/*
 * reader - threads that only reads obj
 */
/* $begin reader */
int reader(int fd, char *uri) {
  int check = 0;
  readcnt++;
  if (readcnt == 1)
    P(&w);
  // Critical section
  // Reading happens
  for (int i = 0; i < 100; ++i) {
    if (!strcmp(global_cache.objects[i].name, uri)) {
      Rio_writen(fd, global_cache.objects[i].object, MAX_OBJECT_SIZE);
      check = 1;
      break;
    }
  }

  readcnt--;
  if (readcnt == 0)
    V(&w);
  return check;
}
/* $end reader */

/*
 * writer - threads that modifies obj
 */
/* $begin writer */
void writer(char *uri, char *buf) {
  P(&w);
  // Critical section
  // Writing happens
  strcpy(global_cache.objects[global_cache.used_cnt].name, uri);
  strcpy(global_cache.objects[global_cache.used_cnt].object, buf);
  ++global_cache.used_cnt;
  V(&w);
}
/* $end writer */

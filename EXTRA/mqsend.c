#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) { \
  if (!(x)) {\
    perror(#x);\
    exit(EXIT_FAILURE);\
  }\
}

#define CHECK_GTE0(x) CHECK((x) >= 0)
#define CHECK_EQ0(x) CHECK((x) == 0)


int main(int argc, char **argv) {
  CHECK(argc == 3);
  const char* qname = argv[1];
  unsigned int priority = atoi(argv[2]);
  
  mqd_t q;
  CHECK_GTE0(q = mq_open(qname, O_WRONLY));
  struct mq_attr attr;
  CHECK_EQ0(mq_getattr(q, &attr));
  
  char *buf = malloc(attr.mq_msgsize);
  size_t n;
  CHECK_GTE0(n = fread(buf, 1, attr.mq_msgsize, stdin));
  CHECK_GTE0(mq_send(q, buf, n, priority));
}



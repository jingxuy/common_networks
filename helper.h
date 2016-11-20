#ifndef HELPER_H_
#define HELPER_H_

#include <stdint.h>
#include <unistd.h>
#include <map>

/* Persistent state for the robust I/O (Rio) package */
#define RIO_BUFSIZE 8192
typedef struct {
    int rio_fd;                /* Descriptor for this internal buf */
    int rio_cnt;               /* Unread bytes in internal buf */
    char *rio_bufptr;          /* Next unread byte in internal buf */
    char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
} rio_t;

using namespace std;

void int_to_byte(char* s, uint32_t i);

uint32_t byte_to_int(char* s);

bool is_post(char* buffer);

bool is_get(char* buffer);

string is_get_slug(char* buffer);

int get_content_length(char* buffer);

ssize_t rio_readn(int fd, char* usrbuf, size_t n);

ssize_t rio_written(int fd, char* usrbuf, size_t n);

void rio_readinitb(rio_t *rp, int fd); 
ssize_t rio_readnb(rio_t *rp, char *usrbuf, size_t n);
ssize_t rio_readlineb(rio_t *rp, char *usrbuf, size_t maxlen);

#endif  // HELPER_H_

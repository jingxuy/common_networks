#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <string>
#include <regex>
#include <arpa/inet.h>

#include "helper.h"

using namespace std;

// Requires |s| >= 4
void int_to_byte(char* s, uint32_t i) {
    uint32_t* i_ptr = (uint32_t*)s;
    *i_ptr = htonl(i);
}

// Requires |s| >= 4
uint32_t byte_to_int(char* s) {
    uint32_t* i_ptr = (uint32_t*)s;
    return ntohl(*i_ptr);
}

bool is_post(char* buffer) {
    cmatch m;
    regex r("POST /urls .*\r\n");
    regex_match(buffer, m, r);
    return m.size() != 0;
}

bool is_get(char* buffer) {
    cmatch m;
    regex r("GET /urls .*\r\n");
    regex_match(buffer, m, r);
    return m.size() != 0;
}

string is_get_slug(char* buffer) {
    cmatch m;
    regex r("GET /urls/([A-Za-z0-9\\.~_-]*) .*\r\n");
    regex_match(buffer, m, r);
    if (m.size() == 0) {
        return "";
    } else {
        return m[1];
    }
}

int get_content_length(char* buffer) {
    cmatch m;
    regex r("Content-Length: ([0-9]+)\r\n");
    regex_match(buffer, m, r);
    if (m.size() == 0) {
        return -1;
    } else {
        return stoi(m[1]);
    }
}

// Robustly read n bytes (unbuffered)
// courtesy of course 15-213 at cmu
ssize_t rio_readn(int fd, char* usrbuf, size_t n) 
{
    size_t nleft = n;
    ssize_t nread;
    cout << "here gain gin" << endl;
    while (nleft > 0) {
        cout << "number left: " << nleft << endl;
        if ((nread = read(fd, usrbuf, nleft)) < 0) {
            if (errno == EINTR) // Interrupted by sig handler return
                nread = 0;      // and call read() again
            else
                return -1;      // errno set by read() 
        } 
        else if (nread == 0)
            break;              // EOF
        nleft -= nread;
        usrbuf += nread;
    }
    return (n - nleft);         // return >= 0
}

// Robustly write n bytes (unbuffered)
// courtesy of course 15-213 at cmu
ssize_t rio_written(int fd, char* usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nwritten;

    while (nleft > 0) {
        if ((nwritten = write(fd, usrbuf, nleft)) <= 0) {
            if (errno == EINTR)  // Interrupted by sig handler return
                nwritten = 0;    // and call write() again
            else
                return -1;       // errno set by write()
        }
        nleft -= nwritten;
        usrbuf += nwritten;
    }
    return n;
}

/* 
 * rio_read - This is a wrapper for the Unix read() function that
 *    transfers min(n, rio_cnt) bytes from an internal buffer to a user
 *    buffer, where n is the number of bytes requested by the user and
 *    rio_cnt is the number of unread bytes in the internal buffer. On
 *    entry, rio_read() refills the internal buffer via a call to
 *    read() if the internal buffer is empty.
 */
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;

    while (rp->rio_cnt <= 0) {  /* Refill if buf is empty */
        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, 
                           sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0) {
            if (errno != EINTR) /* Interrupted by sig handler return */
                return -1;
        }
        else if (rp->rio_cnt == 0)  /* EOF */
            return 0;
        else 
            rp->rio_bufptr = rp->rio_buf; /* Reset buffer ptr */
    }

    /* Copy min(n, rp->rio_cnt) bytes from internal buf to user buf */
    cnt = n;          
    if (rp->rio_cnt < n)   
        cnt = rp->rio_cnt;
    memcpy(usrbuf, rp->rio_bufptr, cnt);
    rp->rio_bufptr += cnt;
    rp->rio_cnt -= cnt;
    return cnt;
}

/*
 * rio_readinitb - Associate a descriptor with a read buffer and reset buffer
 */
void rio_readinitb(rio_t *rp, int fd) 
{
    rp->rio_fd = fd;  
    rp->rio_cnt = 0;  
    rp->rio_bufptr = rp->rio_buf;
}
/* $end rio_readinitb */

/*
 * rio_readnb - Robustly read n bytes (buffered)
 */
ssize_t rio_readnb(rio_t *rp, char *usrbuf, size_t n) 
{
    size_t nleft = n;
    ssize_t nread;
    
    while (nleft > 0) {
        if ((nread = rio_read(rp, usrbuf, nleft)) < 0) 
            return -1;          /* errno set by read() */ 
        else if (nread == 0)
            break;              /* EOF */
        nleft -= nread;
        usrbuf += nread;
    }
    return (n - nleft);         /* return >= 0 */
}

/* 
 * rio_readlineb - robustly read a text line (buffered)
 */
ssize_t rio_readlineb(rio_t *rp, char *usrbuf, size_t maxlen) 
{
    int n, rc;
    char c;

    for (n = 1; n < maxlen; n++) { 
        if ((rc = rio_read(rp, &c, 1)) == 1) {
            *usrbuf++ = c;
            if (c == '\n') {
                n++;
                break;
            }
        } else if (rc == 0) {
            if (n == 1)
                return 0; /* EOF, no data read */
            else
                break;    /* EOF, some data was read */
        } else
            return -1;  /* Error */
    }
    *usrbuf = 0;
    return n-1;
}

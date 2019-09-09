#ifndef UTIL
#define UTIL
#include <cstdlib>
ssize_t readn(int fd,void *buf,size_t n);
ssize_t writen(int fd,void *buf,size_t n);
void handle_for_sigpipe();
int setSocketNonBlocking(int fd);

#endif

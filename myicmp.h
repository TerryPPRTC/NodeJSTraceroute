#ifndef __MYICMP_H__
#define __MYICMP_H__

double get_time (void);

void close_polls (void);
extern void add_poll (int fd, int events);
extern void del_poll (int fd);
extern void do_poll(double timeout, void (*callback) (int fd, int revents, void* data), void* data);

#endif /* __MYICMP_H__ */

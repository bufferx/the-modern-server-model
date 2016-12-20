/* Build: gcc -std=c99 -std=gnu99 echo_server.c -o echo-server */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_FDS 16
#define MAX_EVENTS 48
#define exit_if(r, ...) if(r) {printf(__VA_ARGS__); printf("%s:%d error no: %d error msg %s\n", __FILE__, __LINE__, errno, strerror(errno)); exit(EXIT_FAILURE);}
#define RUN_MODE_LT 10
#define RUN_MODE_ET 20
#define true 1
#define false 0
#define BUFFER_SIZE 4096

uint8_t run_mode = RUN_MODE_LT;

uint8_t stop = false;

char buffer[BUFFER_SIZE];
int loop_counter = 0;
uint8_t read_flag = false; // Indicates whether the data has been read from FD

void
set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    exit_if(flags < 0, "fcntl GETFL failed");

    int rs = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    exit_if(rs < 0, "fcntl SETFL failed");
}

int
bind_socket(short port) {
    /* Set up listening socket, 'listen_sock' (socket(),
        bind(), listen()) */

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    exit_if(listen_sock < 0, "create socket failed");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    int rs = bind(listen_sock,(struct sockaddr *)&addr, sizeof(struct sockaddr));
    exit_if(rs, "Bind to 0.0.0.0:%d failed %d %s", port, errno, strerror(errno));

    rs = listen(listen_sock, 256);
    exit_if(rs, "Listen failed %d %s", errno, strerror(errno));

    printf("Server FD %d listening at %d\n", listen_sock, port);
    set_nonblocking(listen_sock);
    return listen_sock;
}

void
register_listensock(int epollfd, int listen_sock) {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listen_sock;
    int err = epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev);
    exit_if(err == -1, "epoll_ctl: listen_sock");
}

void
show_peername(int sock) {
    socklen_t len;
    struct sockaddr_storage addr;
    char ipstr[INET6_ADDRSTRLEN];
    int port;

    len = sizeof addr;
    getpeername(sock, (struct sockaddr*)&addr, &len);

    // deal with both IPv4 and IPv6:
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
    } else { // AF_INET6
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
        port = ntohs(s->sin6_port);
        inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
    }

    printf("Peer IP address: %s\n", ipstr);
    printf("Peer port      : %d\n", port);
}

void
update_event_4_ADD(int epollfd, int fd) {
    struct epoll_event ev;

    switch (run_mode) {
        case RUN_MODE_LT:
            ev.events = EPOLLIN;
            break;
        case RUN_MODE_ET:
            ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
            break;
        default:
            exit_if(1 == 1, "Invalid RUN MODE: ");
            break;
    }

    ev.data.fd = fd;
    int err = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
    exit_if(err == -1, "epoll_ctl: conn_sock")
}

void
update_event_4_MOD(int epollfd, int fd, int events) {
    struct epoll_event ev;

    switch (run_mode) {
        case RUN_MODE_LT:
            /* ev.events = EPOLLIN; */
            /* ev.events = events; */
            printf("LT: modify [fd:%d] events to %d\n", fd, events);
            ev.events = events;
            break;
        case RUN_MODE_ET:
            // Nothing to do
            /* ev.events = EPOLLIN | EPOLLOUT | EPOLLET; */
            printf("ET: Nothing to do\n");
            return;
        default:
            exit_if(1 == 1, "Invalid RUN MODE: ");
            break;
    }

    ev.data.fd = fd;
    int err = epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
    exit_if(err == -1, "epoll_ctl: conn_sock")
}

void
handle_accept(int epollfd, int listen_sock) {
    printf("accept with fd[%d:%d:%d]\n", epollfd, loop_counter, listen_sock);
    struct sockaddr_in local;
    socklen_t addrlen = sizeof(local);
    int conn_sock = accept(listen_sock,
                    (struct sockaddr *) &local, &addrlen);
    exit_if(conn_sock == -1, "accept failed");

    show_peername(conn_sock);
    set_nonblocking(conn_sock);

    update_event_4_ADD(epollfd, conn_sock);
}

void
handle_read(int epollfd, int fd) {
    printf("Handle read_event with fd[%d:%d:%d]\n", epollfd, loop_counter, fd);

    if (run_mode == RUN_MODE_ET && read_flag == true) {
        printf("Has not write\n");
        return;
    }

    int read_size = 0;
    while ((read_size = read(fd, buffer, sizeof buffer)) > 0) {
        printf("Read size: %d\n", read_size);
    }
    printf("Read to buffer: %s", buffer);

    if (read_size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        printf("Read error [fd:%d] %d, %s\n", fd, errno, strerror(errno));
        read_flag = true;
        update_event_4_MOD(epollfd, fd, EPOLLIN | EPOLLOUT);
        return;
    }

    if (read_size < 0) {
        printf("Read error %d: %d, %s\n", fd, errno, strerror(errno));
    }

    // read_size == 0, read end: EOF
}

void
handle_write(int epollfd, int fd) {
    printf("Handle write_event with fd[%d:%d:%d]\n", epollfd, loop_counter, fd);

    if (run_mode == RUN_MODE_ET && read_flag == false) {
        printf("Has not read\n");
        return;
    }
    read_flag = false;
    update_event_4_MOD(epollfd, fd, EPOLLIN);

    int write_size = 0;
    int left = BUFFER_SIZE;
    while ((write_size = write(fd, buffer, left)) > 0) {
        left -= write_size;
    }

    if (write_size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        printf("Write error [fd:%d] %d, %s\n", fd, errno, strerror(errno));
        return;
    }

    if (write_size < 0) {
        printf("Write error %d: %d, %s\n", fd, errno, strerror(errno));
    }

    // write_size == 0
}

void
event_dispatch(int epollfd, int listen_sock, int timeout_ms, struct epoll_event *events) {
    int nfds;

    nfds = epoll_wait(epollfd, events, MAX_EVENTS, timeout_ms);
    /* exit_if(nfds == -1, "epoll_wait: "); */

    if (nfds == 0) {
        if (timeout_ms != -1) {
            printf("Timer...\n");
        } else {
            printf("epoll_wait return 0\n");
        }
        return;
    }

    for (int i = 0; i < nfds; ++i) {
        int fd = events[i].data.fd;
        int what = events[i].events;

        if (what & EPOLLERR) {
            printf("Sth errors\n");
        }

        if (what & (EPOLLIN | EPOLLHUP)) {
            if (fd == listen_sock) {
                handle_accept(epollfd, listen_sock);
            } else {
                handle_read(epollfd, fd);
            }
        }

        if (what & EPOLLOUT) {
            handle_write(epollfd, fd);
        }
    }
}

void
release_resource() {
    // Nothing to do
}

void
event_loop(int epollfd, int listen_sock, int timeout_ms) {
    struct epoll_event events[MAX_EVENTS];

    for (;;) {
        if (stop == true) {
            printf("Gently Quit.\n");
            release_resource();
            break;
        }

        loop_counter ++;
        event_dispatch(epollfd, listen_sock, timeout_ms, events);
    }
}

void
handle_kill_signal(const int signal) {
    printf("Catch signal: %d\n", signal);
    exit(0);
}

int
main(int argc, const char* args[]) {
    short port = 13523;

    if (argc > 1) {
        port = atoi(args[1]);
    }

    if (argc > 2) {
        run_mode = atoi(args[2]);
    }

    signal(SIGINT, handle_kill_signal);

    printf("SERVER RUN MODE: %d\n", run_mode);

    int listen_sock = bind_socket(port);

    int epollfd = epoll_create(MAX_FDS);
    exit_if(epollfd < 0, "epoll_create failed");

    register_listensock(epollfd, listen_sock);

    event_loop(epollfd, listen_sock, 5000);

    return 0;
}

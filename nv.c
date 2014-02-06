#include <sys/types.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <nv.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int res = 0;

static void
resize(int signal __unused)
{
    res = 1;
}

int
main(int argc, char *argv[])
{
    int fd[2];
    socketpair(PF_UNIX, SOCK_STREAM, 0, fd);

    int kq = kqueue();
    struct kevent ke;
    memset(&ke, 0, sizeof(ke));
    EV_SET(&ke, fd[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
    kevent(kq, &ke, 1, NULL, 0, NULL);
    memset(&ke, 0, sizeof(ke));
    EV_SET(&ke, fd[1], EVFILT_READ, EV_ADD, 0, 0, NULL);
    kevent(kq, &ke, 1, NULL, 0, NULL);

    signal(SIGWINCH, resize);

    nvlist_t *nvl = NULL;

    for (;;) {
        if (res) {
            struct winsize size;
            ioctl(1, TIOCGWINSZ, &size);

            nvl = nvlist_create(0);
            nvlist_add_string(nvl, "type", "resize");
            nvlist_add_number(nvl, "rows", size.ws_row);
            nvlist_add_number(nvl, "cols", size.ws_col);
            if (nvlist_send(fd[0], nvl) < 0)
                err(1, "nvlist_send()");
            nvlist_destroy(nvl);

            res = 0;
        }

        memset(&ke, 0, sizeof(ke));
        int i = kevent(kq, NULL, 0, &ke, 1, NULL);
        if (i == -1) {
            if (errno == EINTR)
                continue;
            err(1, "kevent()");
        } else if (i == 0)
            continue;

        if (ke.ident == fd[0]) {
            nvl = nvlist_recv(fd[0]);
            if (nvl == NULL)
                err(1, "nvlist_recv()");
            const char *type = nvlist_get_string(nvl, "type");
            if (strcmp(type, "ack") == 0) {
                int ok = nvlist_get_bool(nvl, "ok");
                printf("received ack, ok?: %s\n", ok ? "true" : "false");
            }
            nvlist_destroy(nvl);
        } else if (ke.ident == fd[1]) {
            nvl = nvlist_recv(fd[1]);
            if (nvl == NULL)
                err(1, "nvlist_recv()");
            const char *type = nvlist_get_string(nvl, "type");
            if (strcmp(type, "resize") == 0) {
                int rows = nvlist_get_number(nvl, "rows");
                int cows = nvlist_get_number(nvl, "cols");
                printf("got resize signal. new size: %d %d\n", rows, cows);
            }
            nvlist_destroy(nvl);
            nvl = nvlist_create(0);
            nvlist_add_string(nvl, "type", "ack");
            nvlist_add_bool(nvl, "ok", 1);
            if (nvlist_send(fd[1], nvl) < 0)
                err(1, "nvlist_send()");
            nvlist_destroy(nvl);
        }
    }
}


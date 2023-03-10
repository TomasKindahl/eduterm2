#define _XOPEN_SOURCE 600
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

/* Launching /bin/sh may launch a GNU Bash and that can have nasty side
 * effects. On my system, it clobbers ~/.bash_history because it doesn't
 * respect $HISTSIZE from my ~/.bashrc. That's very annoying. So, launch
 * /bin/dash which does nothing of the sort. */
#define SHELL "/bin/dash"

/*****************************************/
/*************** CLASS PTY ***************/
/*****************************************/

typedef struct _PTY_S {
    int master, slave;
} PTY;

bool pty_pair(PTY *pty) {
    char *slave_name;

    /* Opens the PTY master device. This is the file descriptor that
     * we're reading from and writing to in our terminal emulator.
     *
     * We're going for BSD-style management of the controlling terminal:
     * Don't try to change anything now (O_NOCTTY), we'll issue an
     * ioctl() later on. */
    pty->master = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty->master == -1) {
        perror("posix_openpt");
        return false;
    }

    /* grantpt() and unlockpt() are housekeeping functions that have to
     * be called before we can open the slave FD. */
    if (grantpt(pty->master) == -1) { /* man grantpt */
        perror("grantpt");
        return false;
    }

    if (unlockpt(pty->master) == -1) { /* man unlockpt */
        perror("grantpt");
        return false;
    }

    /* Up until now, we only have the master FD. We also need a file
     * descriptor for our child process. We get it by asking for the
     * actual path in /dev/pts which we then open using a regular
     * open(). So, unlike pipe(), you don't get two corresponding file
     * descriptors in one go. */

    slave_name = ptsname(pty->master);
    if (slave_name == NULL) {
        perror("ptsname");
        return false;
    }

    pty->slave = open(slave_name, O_RDWR | O_NOCTTY);
    if (pty->slave == -1) {
        perror("open(slave_name)");
        return false;
    }

    return true;
}

bool pty_spawn(PTY *pty) {
    pid_t p;
    char *env[] = { "TERM=dumb", NULL };

    p = fork();
    if (p == 0) {
        close(pty->master);

        /* Create a new session and make our terminal this process'
         * controlling terminal. The shell that we'll spawn in a second
         * will inherit the status of session leader. */
        setsid();
        if (ioctl(pty->slave, TIOCSCTTY, NULL) == -1) {
            perror("ioctl(TIOCSCTTY)");
            return false;
        }

        dup2(pty->slave, 0);
        dup2(pty->slave, 1);
        dup2(pty->slave, 2);
        close(pty->slave);

        execle(SHELL, "-" SHELL, (char *)NULL, env);
        return false;
    }
    else if (p > 0) {
        close(pty->slave);
        return true;
    }

    perror("fork");
    return false;
}

/********************************************/
/*************** CLASS X11win ***************/
/********************************************/

typedef struct _X11_S {
    int fd;
    Display *dpy;
    int screen;
    Window root;

    Window termwin;
    GC termgc;
    unsigned long col_fg, col_bg, col_txbg;
    int w, h;

    XFontStruct *xfont;
    int font_width, font_height;

    char *buf;
    int buf_w, buf_h;
    int buf_x, buf_y;
} X11win;

bool term_set_size(PTY *pty, X11win *x11) {
    struct winsize ws = { /* struct winsize defined in termios.h */
        .ws_col = x11->buf_w,
        .ws_row = x11->buf_h,
    };

    /* This is the very same ioctl (man ioctl) that normal programs use
     * to query the window size. Normal programs are actually able to do
     * this, too, but it makes little sense: Setting the size has no
     * effect on the PTY driver in the kernel (it just keeps a record of
     * it) or the terminal emulator. IIUC, all that's happening is that
     * subsequent ioctls will report the new size -- until another ioctl
     * sets a new size.
     *
     * I didn't see any response to ioctls of normal programs in any of
     * the popular terminals (XTerm, VTE, st). They are not informed by
     * the kernel when a normal program issues an ioctl like that.
     *
     * On the other hand, if we were to issue this ioctl during runtime
     * and the size actually changed, child programs would get a
     * SIGWINCH. (man signal) */
    if (ioctl(pty->master, TIOCSWINSZ, &ws) == -1) { /* man ioctl_tty */
        perror("ioctl(TIOCSWINSZ)");
        return false;
    }

    return true;
}

bool x11_setup(X11win *x11) {
    Colormap cmap;
    XColor color;
    XSetWindowAttributes wa = { /* man XCreateWindow */
        .background_pixmap = ParentRelative,
        .event_mask = KeyPressMask | KeyReleaseMask | ExposureMask |
                      ResizeRedirectMask,
    };
    
    printf("x11_setup\n");

    x11->dpy = XOpenDisplay(NULL);
    if (x11->dpy == NULL) {
        fprintf(stderr, "Cannot open display\n");
        return false;
    }

    x11->screen = DefaultScreen(x11->dpy);
    x11->root = RootWindow(x11->dpy, x11->screen);
    x11->fd = ConnectionNumber(x11->dpy);

    printf("x11 { screen = %d, root = %ld, connection = %d}\n",
    	   x11->screen, x11->root, x11->fd);
    
    x11->xfont = XLoadQueryFont(x11->dpy, "12x24"); 
    if (x11->xfont == NULL) {
        fprintf(stderr, "Could not load font\n");
        return false;
    }

    x11->font_width = XTextWidth(x11->xfont, "m", 1);
    x11->font_height = x11->xfont->ascent + x11->xfont->descent;

    cmap = DefaultColormap(x11->dpy, x11->screen);

    if (!XAllocNamedColor(x11->dpy, cmap, "#004400", &color, &color)) {
        fprintf(stderr, "Could not load bg color\n");
        return false;
    }
    x11->col_bg = color.pixel;

    if (!XAllocNamedColor(x11->dpy, cmap, "#FFFFCC", &color, &color)) {
        fprintf(stderr, "Could not load fg color\n");
        return false;
    }
    x11->col_fg = color.pixel;

    if (!XAllocNamedColor(x11->dpy, cmap, "#FF0000", &color, &color)) {
        fprintf(stderr, "Could not load txbg color\n");
        return false;
    }
    x11->col_txbg = color.pixel;

    /* The terminal will have a fixed size of 80x25 cells. This is an
     * arbitrary number. No resizing has been implemented and child
     * processes can't even ask us for the current size (for now).
     *
     * buf_x, buf_y will be the current cursor position. */
    x11->buf_w = 80;
    x11->buf_h = 25;
    x11->buf_x = 0;
    x11->buf_y = 0;
    x11->buf = calloc(x11->buf_w * x11->buf_h, 1);
    if (x11->buf == NULL) {
        perror("calloc");
        return false;
    }

    x11->w = x11->buf_w * x11->font_width;
    x11->h = x11->buf_h * x11->font_height;

    x11->termwin = XCreateWindow(x11->dpy, x11->root,
                                 0, 0,
                                 x11->w, x11->h,
                                 0,
                                 DefaultDepth(x11->dpy, x11->screen),
                                 CopyFromParent,
                                 DefaultVisual(x11->dpy, x11->screen),
                                 CWBackPixmap | CWEventMask,
                                 &wa);
    XStoreName(x11->dpy, x11->termwin, "eduterm/2 (TomKi)");
    XMapWindow(x11->dpy, x11->termwin);
    x11->termgc = XCreateGC(x11->dpy, x11->termwin, 0, NULL);

    XSetFont(x11->dpy, x11->termgc, x11->xfont->fid);

    XSync(x11->dpy, False);

    return true;
}

void x11_key(XKeyEvent *ev, PTY *pty) {
    char buf[32];
    int i, num;
    KeySym ksym;

    num = XLookupString(ev, buf, sizeof buf, &ksym, 0);
    for (i = 0; i < num; i++) {
        write(pty->master, &buf[i], 1);
    }
}

void x11_redraw(X11win *x11) {
    int x, y;
    char buf[1];

    /* set window area to background color */
    XSetForeground(x11->dpy, x11->termgc, x11->col_bg);
    XFillRectangle(x11->dpy, x11->termwin, x11->termgc, 0, 0, x11->w, x11->h);

    XSetBackground(x11->dpy, x11->termgc, x11->col_txbg);
    XSetForeground(x11->dpy, x11->termgc, x11->col_fg);
    for (y = 0; y < x11->buf_h; y++) {
        for (x = 0; x < x11->buf_w; x++) {
            buf[0] = x11->buf[y * x11->buf_w + x];
            if (!iscntrl(buf[0])) {
                XDrawString(x11->dpy, x11->termwin, x11->termgc,
                            x * x11->font_width,
                            y * x11->font_height + x11->xfont->ascent,
                            buf, 1);
            }
        }
    }

    XSetForeground(x11->dpy, x11->termgc, x11->col_fg);
    XFillRectangle(x11->dpy, x11->termwin, x11->termgc,
                   x11->buf_x * x11->font_width,
                   x11->buf_y * x11->font_height,
                   x11->font_width, x11->font_height);

    XSync(x11->dpy, False);
}

void x11_resize(X11win *x11, int w, int h) {
	printf("NYI: x11_resize to w=%d, h=%d\n", w, h);
}

int run(PTY *pty, X11win *x11) {
    int i, maxfd;
    fd_set readable;
    XEvent ev;
    char buf[1];
    bool just_wrapped = false;

    maxfd = pty->master > x11->fd ? pty->master : x11->fd;

    for (;;) {
        FD_ZERO(&readable);
        FD_SET(pty->master, &readable);
        FD_SET(x11->fd, &readable);

        if (select(maxfd + 1, &readable, NULL, NULL, NULL) == -1) {
            perror("select");
            return 1;
        }

        if (FD_ISSET(pty->master, &readable)) {
            if (read(pty->master, buf, 1) <= 0) {
                /* This is not necessarily an error but also happens
                 * when the child exits normally. */
                fprintf(stderr, "Nothing to read from child: ");
                perror(NULL);
                return 1;
            }

            if (buf[0] == '\r') {
                /* "Carriage returns" are probably the most simple
                 * "terminal command": They just make the cursor jump
                 * back to the very first column. */
                x11->buf_x = 0;
            }
            else {
                if (buf[0] != '\n') {
                    /* If this is a regular byte, store it and advance
                     * the cursor one cell "to the right". This might
                     * actually wrap to the next line, see below. */
                    x11->buf[x11->buf_y * x11->buf_w + x11->buf_x] = buf[0];
                    x11->buf_x++;

                    if (x11->buf_x >= x11->buf_w) {
                        x11->buf_x = 0;
                        x11->buf_y++;
                        just_wrapped = true;
                    }
                    else {
                        just_wrapped = false;
                    }
                }
                else if (!just_wrapped) {
                    /* We read a newline and we did *not* implicitly
                     * wrap to the next line with the last byte we read.
                     * This means we must *now* advance to the next
                     * line.
                     *
                     * This is the same behaviour that most other
                     * terminals have: If you print a full line and then
                     * a newline, they "ignore" that newline. (Just
                     * think about it: A full line of text could always
                     * wrap to the next line implicitly, so that
                     * additional newline could cause the cursor to jump
                     * to the next line *again*.) */
                    x11->buf_y++;
                    just_wrapped = false;
                }

                /* We now check if "the next line" is actually outside
                 * of the buffer. If it is, we shift the entire content
                 * one line up and then stay in the very last line.
                 *
                 * After the memmove(), the last line still has the old
                 * content. We must clear it. */
                if (x11->buf_y >= x11->buf_h) {
                    memmove(x11->buf, &x11->buf[x11->buf_w],
                            x11->buf_w * (x11->buf_h - 1));
                    x11->buf_y = x11->buf_h - 1;

                    for (i = 0; i < x11->buf_w; i++) {
                        x11->buf[x11->buf_y * x11->buf_w + i] = 0;
                    }
                }
            }
            x11_redraw(x11);
        }

        if (FD_ISSET(x11->fd, &readable)) {
            while (XPending(x11->dpy)) {
                XNextEvent(x11->dpy, &ev);
                switch (ev.type) {
                  case Expose:
                    x11_redraw(x11);
                    break;
                  case ResizeRequest:
                  	 {
                  	     XResizeRequestEvent *rev = (XResizeRequestEvent *)&ev;
                  	     x11_resize(x11, rev->width, rev->height);
                  	 }
                  	 break;
                  case KeyPress:
                    x11_key(&ev.xkey, pty);
                    break;
                }
            }
        }
    }

    return 0;
}

int main() {
    PTY pty;
    X11win x11;

    if (!x11_setup(&x11)) {
        return 1;
    }

    if (!pty_pair(&pty)) {
        return 1;
    }

    if (!term_set_size(&pty, &x11)) {
        return 1;
    }

    if (!pty_spawn(&pty)) {
        return 1;
    }

    return run(&pty, &x11);
}

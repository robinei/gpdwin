/*
 * inputd: the gamepad's Guide (Xbox) button brings up Pegasus, and gamepad use counts as activity.
 *
 * Guide starts Pegasus if it isn't running, or switches to its workspace. While a game started
 * from Pegasus runs (a child process of pegasus-fe), the button is left to the game.
 *
 * sway only counts keyboard, pointer and touch as activity, so playing with the pad alone would
 * let swayidle dim the screen and suspend. Any pad event (at most every POKE_EVERY seconds)
 * sends sway a zero pointer move (`seat seat0 cursor move 0 0`), which resets its idle timers.
 *
 * Sleeps until there is something to do: the untouched pad sends no events, and inotify on
 * /dev/input reports when the pad (re)appears (it disconnects while the screen is off and across
 * suspend).
 *
 * Built and installed by `scripts/sync install` (manifest "build" entry); started by sway
 * (dotfiles/sway/autostart).
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define PAD_NAME "Microsoft X-Box 360 pad"
#define POKE_EVERY 30 /* seconds; the first idle step (dim) comes after 2 min */

extern char **environ;

static int open_pad(void)
{
    DIR *d = opendir("/dev/input");
    struct dirent *e;
    int found = -1;
    while (d && found < 0 && (e = readdir(d))) {
        char path[300], name[256] = "";
        if (strncmp(e->d_name, "event", 5) != 0)
            continue;
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) >= 0 && strcmp(name, PAD_NAME) == 0) {
            found = fd;
        } else {
            close(fd);
        }
    }
    if (d)
        closedir(d);
    return found;
}

/* pid of pegasus-fe (0 if not running), and whether it has a child process (a game). */
static pid_t pegasus(int *game_running)
{
    DIR *d = opendir("/proc");
    struct dirent *e;
    pid_t pid = 0;
    int ppids[4096], n = 0;
    *game_running = 0;
    while (d && (e = readdir(d)) && n < 4096) {
        char path[64], stat[512], *comm_end;
        int p = atoi(e->d_name), ppid;
        if (p <= 0)
            continue;
        snprintf(path, sizeof path, "/proc/%d/stat", p);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        size_t len = fread(stat, 1, sizeof stat - 1, f);
        fclose(f);
        stat[len] = 0;
        /* "pid (comm) state ppid ...": comm may contain spaces, so find the last ')' */
        comm_end = strrchr(stat, ')');
        if (!comm_end || sscanf(comm_end + 2, "%*c %d", &ppid) != 1)
            continue;
        if (strncmp(strchr(stat, '(') + 1, "pegasus-fe)", 11) == 0)
            pid = p;
        ppids[n++] = ppid;
    }
    if (d)
        closedir(d);
    for (int i = 0; pid && i < n; i++)
        if (ppids[i] == pid)
            *game_running = 1;
    return pid;
}

static void swaymsg(const char *command)
{
    char *argv[] = { "swaymsg", (char *)command, NULL };
    pid_t pid;
    if (posix_spawnp(&pid, "swaymsg", NULL, NULL, argv, environ) != 0)
        perror("inputd: swaymsg");
}

/* Tell sway the user is active (it doesn't count gamepads), at most every POKE_EVERY seconds. */
static void activity(void)
{
    static time_t last;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (last && now.tv_sec - last < POKE_EVERY)
        return;
    last = now.tv_sec;
    swaymsg("seat seat0 cursor move 0 0");
}

static void on_guide(void)
{
    int game;
    if (!pegasus(&game))
        swaymsg("workspace number 1; exec ~/.config/pegasus-frontend/run");
    else if (!game)
        swaymsg("workspace number 1");
}

int main(void)
{
    signal(SIGCHLD, SIG_IGN); /* reap swaymsg automatically */

    int watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    /* IN_ATTRIB: udev sets the node's permissions just after the kernel creates it */
    inotify_add_watch(watch, "/dev/input", IN_CREATE | IN_ATTRIB);

    struct pollfd fds[2] = {
        { .fd = watch, .events = POLLIN },
        { .fd = open_pad(), .events = POLLIN },
    };

    for (;;) {
        if (poll(fds, 2, -1) < 0)
            continue;

        if (fds[0].revents) {
            char buf[4096];
            while (read(watch, buf, sizeof buf) > 0)
                ;
            if (fds[1].fd < 0)
                fds[1].fd = open_pad();
        }

        if (fds[1].fd >= 0 && fds[1].revents) {
            struct input_event ev;
            ssize_t n;
            while ((n = read(fds[1].fd, &ev, sizeof ev)) == sizeof ev) {
                if (ev.type == EV_KEY && ev.code == BTN_MODE && ev.value == 1)
                    on_guide();
                if (ev.type == EV_KEY || ev.type == EV_ABS)
                    activity();
            }
            if (n < 0 && errno != EAGAIN) { /* ENODEV once the pad is unplugged */
                close(fds[1].fd); /* pad went away; inotify brings it back */
                fds[1].fd = -1;
            }
        }
    }
}

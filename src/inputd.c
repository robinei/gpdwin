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
 * Lid switch: closing the lid powers the output off and disables all input (sway's devices;
 * the pad is grabbed), opening it undoes that (the CPU/GPU are also throttled and the status bar paused, dotfiles/sway/lid.sh; brightness is left alone; level 0 doesn't turn this
 * panel off). logind ignores the lid, see docs/power.md.
 *
 * DSI underrun watch (docs/hardware.md "Display"): some games leave the panel showing a shifted /
 * colour-shifted picture that only an output power cycle fixes. Then the DSI controller's
 * DPI_FIFO_UNDERRUN status bit is set. Once a second, while the lid is open, the bit is read
 * (read-only mmap of the GPU registers, readable by wheel via system/etc/tmpfiles.d/display-underrun.conf)
 * and the same power cycle as screen-reset.sh (Mod4+F10) is run. Logged to ~/.cache/gpd/dsi-resets.log.
 *
 * Sleeps until there is something to do: the untouched pad sends no events, and inotify on
 * /dev/input reports when the pad (re)appears (it disconnects while the screen is off and across
 * suspend). The only timer is the 1 s underrun check (not while the lid is closed).
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
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PAD_NAME "Microsoft X-Box 360 pad"
#define LID_NAME "Lid Switch"
#define POKE_EVERY 30 /* seconds; the first idle step (dim) comes after 2 min */

extern char **environ;

/* Cherry Trail display registers (VLV_DISPLAY_BASE 0x180000): DSI port C and pipe B. */
#define GPU_BAR "/sys/bus/pci/devices/0000:00:02.0/resource0"
#define DSI_PAGE 0x18b000    /* MIPI_DEVICE_READY(C) +0x800, MIPI_INTR_STAT(C) +0x804 */
#define PIPE_PAGE 0x1f1000   /* PIPEBCONF +0x008 */
#define DPI_FIFO_UNDERRUN (1u << 20)
#define RESET_COOLDOWN 30    /* seconds between automatic resets */
#define RESET_MAX 3          /* at most this many per RESET_WINDOW seconds (the bit should clear) */
#define RESET_WINDOW 300

static int open_named(const char *want)
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
        if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) >= 0 && strcmp(name, want) == 0) {
            found = fd;
        } else {
            close(fd);
        }
    }
    if (d)
        closedir(d);
    return found;
}

static int open_pad(void)
{
    return open_named(PAD_NAME);
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

static void swaymsg_sh(const char *command)
{
    char *argv[] = { "sh", "-c", (char *)command, NULL };
    pid_t pid;
    if (posix_spawnp(&pid, "sh", NULL, NULL, argv, environ) != 0)
        perror("inputd: sh");
}


static volatile uint32_t *dsi_regs, *pipe_regs;

static void dsi_map(void)
{
    int fd = open(GPU_BAR, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        perror("inputd: " GPU_BAR " (underrun watch off)");
        return;
    }
    void *a = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, DSI_PAGE);
    void *b = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, PIPE_PAGE);
    close(fd);
    if (a == MAP_FAILED || b == MAP_FAILED) {
        perror("inputd: mmap GPU registers (underrun watch off)");
        return;
    }
    dsi_regs = a;
    pipe_regs = b;
}

/* DPI FIFO underrun flagged while the DSI controller and pipe B are running (not while the output
 * is off, when the registers read as 0xffffffff or 0). Returns the status register, or 0. */
static uint32_t dsi_underrun(void)
{
    if (!dsi_regs)
        return 0;
    uint32_t ready = dsi_regs[0x800 / 4], stat = dsi_regs[0x804 / 4], pipe = pipe_regs[0x008 / 4];
    if (ready == 0xffffffffu || stat == 0xffffffffu || !(ready & 1) || !(pipe & 0x80000000u))
        return 0;
    return stat & DPI_FIFO_UNDERRUN ? stat : 0;
}

/* Called every loop; checks at most once a second. */
static void dsi_watch(void)
{
    static time_t last_check, last_reset, window_start;
    static int seen, resets;
    struct timespec t;
    uint32_t stat;
    clock_gettime(CLOCK_MONOTONIC, &t);
    if (t.tv_sec == last_check)
        return;
    last_check = t.tv_sec;
    if (!(stat = dsi_underrun())) {
        seen = 0;
        return;
    }
    if (++seen < 2 || (last_reset && t.tv_sec - last_reset < RESET_COOLDOWN))
        return;
    if (t.tv_sec - window_start > RESET_WINDOW) {
        window_start = t.tv_sec;
        resets = 0;
    }
    if (++resets > RESET_MAX)
        return;
    last_reset = t.tv_sec;
    seen = 0;
    const char *home = getenv("HOME");
    char path[300];
    snprintf(path, sizeof path, "%s/.cache", home ? home : "/tmp");
    mkdir(path, 0755);
    strncat(path, "/gpd", sizeof path - strlen(path) - 1);
    mkdir(path, 0755);
    strncat(path, "/dsi-resets.log", sizeof path - strlen(path) - 1);
    FILE *f = fopen(path, "a");
    if (f) {
        time_t now = time(NULL);
        char ts[32];
        strftime(ts, sizeof ts, "%F %T", localtime(&now));
        fprintf(f, "%s DSI underrun (MIPI_INTR_STAT=%08x): output power cycle\n", ts, stat);
        fclose(f);
    }
    swaymsg_sh("swaymsg output DSI-1 power off; sleep 2; swaymsg output DSI-1 power on");
}

static int lid_closed;
static int pad_fd = -1;

/* While the lid is closed the pad is grabbed, so nobody else (games, sway) sees its events. */
static void grab_pad(void)
{
    if (pad_fd >= 0)
        ioctl(pad_fd, EVIOCGRAB, lid_closed);
}

/* Lid closed: switch the screen off and all input off (sway's devices, and the pad by grabbing
 * it); opened: back on, and counts as activity (resets the idle timers). */
static void on_lid(int closed)
{
    lid_closed = closed;
    swaymsg(closed ? "output * power off; input * events disabled"
                   : "input * events enabled; output * power on; seat seat0 cursor move 0 0");
    grab_pad();
    swaymsg_sh(closed ? "~/.config/sway/lid.sh close" : "~/.config/sway/lid.sh open");
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

    struct pollfd fds[3] = {
        { .fd = watch, .events = POLLIN },
        { .fd = (pad_fd = open_pad()), .events = POLLIN },
        { .fd = open_named(LID_NAME), .events = POLLIN },
    };

    unsigned long sw[1] = { 0 };
    if (fds[2].fd >= 0 && ioctl(fds[2].fd, EVIOCGSW(sizeof sw), sw) >= 0 && (sw[0] >> SW_LID & 1))
        on_lid(1); /* started with the lid already closed */

    dsi_map();

    for (;;) {
        int ready = poll(fds, 3, lid_closed ? -1 : 1000);
        if (!lid_closed)
            dsi_watch();
        if (ready <= 0)
            continue;

        if (fds[0].revents) {
            char buf[4096];
            while (read(watch, buf, sizeof buf) > 0)
                ;
            if (fds[1].fd < 0) {
                fds[1].fd = pad_fd = open_pad();
                grab_pad();
            }
        }

        if (fds[1].fd >= 0 && fds[1].revents) {
            struct input_event ev;
            ssize_t n;
            while ((n = read(fds[1].fd, &ev, sizeof ev)) == sizeof ev) {
                if (ev.type == EV_KEY && ev.code == BTN_MODE && ev.value == 1)
                    on_guide();
                if (!lid_closed && (ev.type == EV_KEY || ev.type == EV_ABS))
                    activity();
            }
            if (n < 0 && errno != EAGAIN) { /* ENODEV once the pad is unplugged */
                close(fds[1].fd); /* pad went away; inotify brings it back */
                fds[1].fd = pad_fd = -1;
            }
        }

        if (fds[2].fd >= 0 && fds[2].revents) {
            struct input_event ev;
            while (read(fds[2].fd, &ev, sizeof ev) == sizeof ev)
                if (ev.type == EV_SW && ev.code == SW_LID)
                    on_lid(ev.value);
        }
    }
}

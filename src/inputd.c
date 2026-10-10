/*
 * inputd: the gamepad's Guide (Xbox) button brings up the frontend (Pegasus or shelf), and gamepad
 * use counts as activity.
 *
 * Guide starts Pegasus if it isn't running, or switches to its workspace. While a game started
 * from Pegasus runs (a child process of pegasus-fe), the button is left to the game.
 *
 * sway only counts keyboard, pointer and touch as activity, so playing with the pad alone would
 * let swayidle dim the screen and suspend. Any pad event (at most every POKE_EVERY seconds)
 * sends sway a zero pointer move (`seat seat0 cursor move 0 0`), which resets its idle timers.
 *
 * Lid switch: closing the lid disables all input (sway's devices; the pad is grabbed) and runs
 * dotfiles/sway/lid.sh close (screen off and games frozen via screen.sh, CPU/GPU throttled);
 * opening it undoes that. Brightness is left alone (level 0 doesn't turn this panel off). logind
 * ignores the lid, see docs/power.md.
 *
 * Power button (logind ignores it, HandlePowerKey=ignore), acted on at release, by the screen
 * state at the press (sway counts the press as activity, so the screen is back on by the release):
 *   screen dark (sway reports the output powered off: idle or lid): only wakes the screen (zero
 *                pointer move, which resets swayidle, and screen.sh on). Never sleeps, so a press
 *                meant to "turn it on" can't put an awake device with its screen off to sleep.
 *   screen on:   suspend-then-hibernate.
 *   within RESUME_GUARD s of a resume (the sleep hook resume-time writes the uptime to
 *   RESUME_FILE): ignored, so the press that woke the device doesn't put it back to sleep.
 * Two releases within 2 s count once (the button may show up as several input devices).
 * So "dark screen: hold ~1 s" always turns it on: hibernated/off (the hardware needs the hold),
 * asleep, or screen off.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define PAD_NAME "Microsoft X-Box 360 pad"
#define LID_NAME "Lid Switch"
#define POKE_EVERY 30 /* seconds; the first idle step (dim) comes after 2 min */
#define RESUME_GUARD 3.0 /* seconds after a resume in which the power button is ignored */
#define RESUME_FILE "/run/resume-time"
#define MAX_POWER 8    /* input devices with KEY_POWER (here: 2x ACPI Power Button, gpio-keys, Intel HID, keyboard) */

extern char **environ;

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

/* Open the input devices that have KEY_POWER; returns how many. */
static int open_power(int *fds, int max)
{
    DIR *d = opendir("/dev/input");
    struct dirent *e;
    int n = 0;
    while (d && n < max && (e = readdir(d))) {
        char path[300], name[256] = "";
        unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = { 0 };
        if (strncmp(e->d_name, "event", 5) != 0)
            continue;
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
        if (strcmp(name, PAD_NAME) != 0 && ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 &&
            (keys[KEY_POWER / (8 * sizeof(long))] >> (KEY_POWER % (8 * sizeof(long))) & 1)) {
            fds[n++] = fd;
        } else {
            close(fd);
        }
    }
    if (d)
        closedir(d);
    return n;
}

static double read_double(const char *path)
{
    double v = -1;
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%lf", &v) != 1)
            v = -1;
        fclose(f);
    }
    return v;
}

static int just_resumed(void)
{
    double resumed = read_double(RESUME_FILE);
    return resumed >= 0 && read_double("/proc/uptime") - resumed < RESUME_GUARD;
}

/* Ask sway (IPC GET_OUTPUTS) whether an output is powered off. Unknown counts as on. */
static int screen_dark(void)
{
    const char *sock = getenv("SWAYSOCK");
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    int fd, dark = 0;
    if (!sock || strlen(sock) >= sizeof addr.sun_path)
        return 0;
    strcpy(addr.sun_path, sock);
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return 0;
    char hdr[14] = "i3-ipc";
    uint32_t len = 0, type = 3; /* GET_OUTPUTS */
    memcpy(hdr + 6, &len, 4);
    memcpy(hdr + 10, &type, 4);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0 && write(fd, hdr, 14) == 14) {
        char reply[16384];
        size_t got = 0;
        ssize_t n;
        while (got < sizeof reply - 1 && (n = read(fd, reply + got, sizeof reply - 1 - got)) > 0) {
            got += n;
            if (got >= 14) {
                memcpy(&len, reply + 6, 4);
                if (got >= 14 + (size_t)len)
                    break;
            }
        }
        reply[got] = 0;
        dark = got > 14 && (strstr(reply + 14, "\"power\": false") || strstr(reply + 14, "\"power\":false"));
    }
    close(fd);
    return dark;
}

static int open_pad(void)
{
    return open_named(PAD_NAME);
}

/* pid of the frontend, pegasus-fe or shelf (0 if not running), and whether it has a child process
 * (a game). */
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
        const char *comm = strchr(stat, '(') + 1;
        if (strncmp(comm, "pegasus-fe)", 11) == 0 || strncmp(comm, "shelf)", 6) == 0)
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

static void suspend(void)
{
    char *argv[] = { "sudo", "-n", "systemctl", "suspend-then-hibernate", NULL };
    pid_t pid;
    if (posix_spawnp(&pid, "sudo", NULL, NULL, argv, environ) != 0)
        perror("inputd: sudo");
}

static void swaymsg_sh(const char *command)
{
    char *argv[] = { "sh", "-c", (char *)command, NULL };
    pid_t pid;
    if (posix_spawnp(&pid, "sh", NULL, NULL, argv, environ) != 0)
        perror("inputd: sh");
}

static int lid_closed;
static int pad_fd = -1;

/* While the lid is closed the pad is grabbed, so nobody else (games, sway) sees its events. */
static void grab_pad(void)
{
    if (pad_fd >= 0)
        ioctl(pad_fd, EVIOCGRAB, lid_closed);
}

/* Lid closed: all input off (sway's devices, and the pad by grabbing it); lid.sh does the rest
 * (screen, freezing, throttling). Opened: back on, and counts as activity (resets the idle timers). */
static void on_lid(int closed)
{
    lid_closed = closed;
    swaymsg(closed ? "input * events disabled" : "input * events enabled; seat seat0 cursor move 0 0");
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

    struct pollfd fds[3 + MAX_POWER] = {
        { .fd = watch, .events = POLLIN },
        { .fd = (pad_fd = open_pad()), .events = POLLIN },
        { .fd = open_named(LID_NAME), .events = POLLIN },
    };
    time_t last_power = 0;
    int press_dark = -1; /* screen state when the power button went down; -1: not down */
    int power[MAX_POWER], npower = open_power(power, MAX_POWER), nfds = 3 + npower;
    for (int i = 0; i < npower; i++)
        fds[3 + i] = (struct pollfd){ .fd = power[i], .events = POLLIN };

    unsigned long sw[1] = { 0 };
    if (fds[2].fd >= 0 && ioctl(fds[2].fd, EVIOCGSW(sizeof sw), sw) >= 0 && (sw[0] >> SW_LID & 1))
        on_lid(1); /* started with the lid already closed */

    for (;;) {
        if (poll(fds, nfds, -1) < 0)
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

        for (int i = 3; i < nfds; i++) {
            struct input_event ev;
            ssize_t n;
            if (!fds[i].revents)
                continue;
            while ((n = read(fds[i].fd, &ev, sizeof ev)) == sizeof ev) {
                if (ev.type != EV_KEY || ev.code != KEY_POWER)
                    continue;
                /* Screen state at the press: sway counts the press as activity and swayidle's
                 * resume turns the screen on before the release. */
                if (ev.value == 1 && press_dark < 0)
                    press_dark = screen_dark();
                if (ev.value != 0)
                    continue;
                int dark = press_dark >= 0 ? press_dark : screen_dark();
                press_dark = -1;
                if (lid_closed || just_resumed())
                    continue;
                /* several devices may report the same press: act once */
                struct timespec t;
                clock_gettime(CLOCK_MONOTONIC, &t);
                if (last_power && t.tv_sec - last_power < 2)
                    continue;
                last_power = t.tv_sec;
                if (dark) { /* screen.sh on directly too: swayidle resumes only after its own timeout */
                    swaymsg("seat seat0 cursor move 0 0");
                    swaymsg_sh("~/.config/sway/screen.sh on");
                }
                else
                    suspend();
            }
            if (n < 0 && errno != EAGAIN) { /* device gone (USB keyboard re-enumerated) */
                close(fds[i].fd);
                fds[i].fd = -1; /* poll ignores it */
            }
        }
    }
}

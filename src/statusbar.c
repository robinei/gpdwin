/*
 * statusbar: the swaybar status line (i3bar JSON on stdout).
 *
 *   cpu 2.4G gpu 400M | ram 0.6/3.7G | <ssid> 100% | vol 50% | bat 99%+ | Wed 07 Oct  15:29
 *
 * CPU/GPU show the current clock and are dimmed while turbo is capped (~/.config/sway/turbo.sh).
 * CPU is the fastest core (/proc/cpuinfo, sampled by the kernel on its timer tick). GPU is "idle"
 * when it spent most of the last second in RC6 (its frequency register keeps the last value).
 *
 * Everything happens in one poll() loop, so the program sleeps until something can have changed.
 * Each value is read only as often as it is worth (measured cost per read on this device):
 *   every 1 s          CPU and GPU clock (~0.15 ms), and the clock text
 *   every 5 s          RAM (0.05 ms)
 *   every 30 s         WiFi signal (2.3 ms: asks the WiFi firmware); turbo state as a fallback
 *   every 60 s         battery (9 ms: I2C to the fuel gauge and the charger)
 *   kernel uevents     charger plugged in/out, battery events -> battery at once
 *   rtnetlink          WiFi link up/down -> link state and network name at once
 *   SIGUSR1            volume or turbo changed (sent by osd.sh and turbo.sh) -> at once
 *   SIGSTOP/SIGCONT    sent by the idle scripts while the screen is off: nothing runs then;
 *                      SIGCONT re-reads everything
 * A line is written only when the text changed, so swaybar redraws only on real changes.
 *
 * Built and installed by `scripts/sync install` (manifest "build" entry).
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <glob.h>
#include <stdlib.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#define BATTERY  "/sys/class/power_supply/max170xx_battery/"
#define CHARGER  "/sys/class/power_supply/bq24190-charger/"
#define NO_TURBO "/sys/devices/system/cpu/intel_pstate/no_turbo"
#define WIFI     "wlan0"

/* Colours from the sway theme (Catppuccin Mocha) */
#define TEXT   "#cdd6f4"
#define DIM    "#7f849c"
#define YELLOW "#f9e2af"
#define RED    "#f38ba8"

/* seconds between reads of the slower values */
#define RAM_EVERY     5
#define SIGNAL_EVERY  30
#define TURBO_EVERY   30
#define BATTERY_EVERY 60
#define VOLUME_RETRY  10 /* while PipeWire isn't up yet after login */

static char gpu[64]; /* "/sys/class/drm/cardN/", found at start */
static int gpu_rp0;  /* the GPU's highest (turbo) clock, MHz */

static struct {
    double cpu_ghz;
    int cpu_turbo;
    int gpu_mhz;
    int gpu_turbo;
    double ram_used, ram_total; /* GiB */
    int wifi_up;
    char ssid[64];
    int signal;                 /* percent, -1 = unknown */
    int volume;                 /* percent, -1 = unknown */
    int muted;
    int battery;                /* percent, -1 = unknown */
    int charging;
} st = { .signal = -1, .volume = -1, .battery = -1 };

/* ---- small file helpers ---- */

static int read_int(const char *path, int fallback)
{
    FILE *f = fopen(path, "r");
    int v;
    if (!f)
        return fallback;
    if (fscanf(f, "%d", &v) != 1)
        v = fallback;
    fclose(f);
    return v;
}

static void read_line(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    buf[0] = 0;
    if (f) {
        if (fgets(buf, size, f))
            buf[strcspn(buf, "\n")] = 0;
        fclose(f);
    }
}

static int gpu_int(const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "%s%s", gpu, name);
    return read_int(path, 0);
}

/* ---- readers ---- */

/* Fastest core: the "cpu MHz" lines of /proc/cpuinfo (one read, then a plain search). */
static void read_cpu(void)
{
    static char buf[32768];
    int fd = open("/proc/cpuinfo", O_RDONLY | O_CLOEXEC);
    size_t len = 0;
    ssize_t n;
    double max = 0;
    if (fd < 0)
        return;
    while (len < sizeof buf - 1 && (n = read(fd, buf + len, sizeof buf - 1 - len)) > 0)
        len += n;
    close(fd);
    buf[len] = 0;
    for (char *p = buf; (p = strstr(p, "cpu MHz")); p++) {
        double mhz = strtod(strchr(p, ':') + 1, NULL);
        if (mhz > max)
            max = mhz;
    }
    st.cpu_ghz = max / 1000;
}

static void read_gpu(void)
{
    static long last_rc6_ms;
    static struct timespec last;
    struct timespec now;
    long rc6_ms = gpu_int("power/rc6_residency_ms");
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_ms = (now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000;
    int asleep = rc6_ms - last_rc6_ms >= elapsed_ms * 9 / 10; /* in RC6 for 90% of the time */
    last_rc6_ms = rc6_ms;
    last = now;
    st.gpu_mhz = asleep ? 0 : gpu_int("gt_act_freq_mhz");
}

static void read_turbo(void)
{
    st.cpu_turbo = read_int(NO_TURBO, 0) == 0;
    st.gpu_turbo = gpu_int("gt_max_freq_mhz") >= gpu_rp0;
}

static void read_ram(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[128];
    long kb, total = 0, avail = 0;
    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "MemTotal: %ld", &kb) == 1)
                total = kb;
            else if (sscanf(line, "MemAvailable: %ld", &kb) == 1)
                avail = kb;
        }
        fclose(f);
    }
    st.ram_used = (total - avail) / 1048576.0;
    st.ram_total = total / 1048576.0;
}

static void read_battery(void)
{
    char status[32];
    st.battery = read_int(BATTERY "capacity", -1);
    read_line(CHARGER "status", status, sizeof status);
    st.charging = strcmp(status, "Charging") == 0;
}

/* Signal level in dBm from /proc/net/wireless, mapped -90..-30 dBm -> 0..100% */
static void read_signal(void)
{
    FILE *f = fopen("/proc/net/wireless", "r");
    char line[256];
    float dbm;
    st.signal = -1;
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, " " WIFI ": %*s %*f %f", &dbm) == 1) {
            int pct = (int)(dbm + 90) * 100 / 60;
            st.signal = pct < 0 ? 0 : pct > 100 ? 100 : pct;
        }
    }
    fclose(f);
}

/* Network name from iwd. Runs iwctl, so only at start and when the link changes. */
static void read_ssid(void)
{
    FILE *p = popen("iwctl station " WIFI " show 2>/dev/null", "r");
    char line[256];
    st.ssid[0] = 0;
    if (!p)
        return;
    while (fgets(line, sizeof line, p)) {
        char *s = strstr(line, "Connected network");
        if (!s)
            continue;
        s += strlen("Connected network");
        while (*s == ' ')
            s++;
        /* the value ends at the first escape sequence or line end; trim trailing spaces */
        s[strcspn(s, "\x1b\n")] = 0;
        for (char *e = s + strlen(s); e > s && e[-1] == ' ';)
            *--e = 0;
        snprintf(st.ssid, sizeof st.ssid, "%s", s);
    }
    pclose(p);
}

static void read_wifi_link(void)
{
    char state[32];
    read_line("/sys/class/net/" WIFI "/operstate", state, sizeof state);
    int up = strcmp(state, "up") == 0;
    if (up && (!st.wifi_up || !st.ssid[0]))
        read_ssid();
    if (!up)
        st.ssid[0] = 0;
    st.wifi_up = up;
}

/* Volume from PipeWire. Runs wpctl, so only at start and when osd.sh says it changed. */
static void read_volume(void)
{
    FILE *p = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
    char line[128];
    float v;
    st.volume = -1;
    if (!p)
        return;
    if (fgets(line, sizeof line, p) && sscanf(line, "Volume: %f", &v) == 1) {
        st.volume = (int)(v * 100 + 0.5f);
        st.muted = strstr(line, "MUTED") != NULL;
    }
    pclose(p);
}

/* ---- output ---- */

enum sep { LINE, JOIN, LAST }; /* separator line after a block / keep with next / end */

static char out[2048];
static size_t len;

static void block(const char *color, enum sep sep, const char *fmt, ...)
{
    char text[128], esc[256], *e = esc;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    for (const char *t = text; *t && e < esc + sizeof esc - 2; t++) {
        if (*t == '"' || *t == '\\')
            *e++ = '\\';
        if ((unsigned char)*t >= ' ')
            *e++ = *t;
    }
    *e = 0;
    len += snprintf(out + len, sizeof out - len,
                    "%s{\"full_text\":\"%s\",\"color\":\"%s\",\"separator\":%s,\"separator_block_width\":%d}",
                    len > 1 ? "," : "", esc, color, sep == LINE ? "true" : "false",
                    sep == LINE ? 19 : sep == JOIN ? 8 : 6);
}

static void emit(void)
{
    static char last[sizeof out];
    char clock[32];
    time_t now = time(NULL);
    strftime(clock, sizeof clock, "%a %d %b  %H:%M", localtime(&now));

    len = 0;
    len += snprintf(out, sizeof out, "[");
    block(st.cpu_turbo ? TEXT : DIM, JOIN, "cpu %.1fG", st.cpu_ghz);
    if (st.gpu_mhz > 0)
        block(st.gpu_turbo ? TEXT : DIM, LINE, "gpu %dM", st.gpu_mhz);
    else
        block(st.gpu_turbo ? TEXT : DIM, LINE, "gpu idle");
    block(TEXT, LINE, "ram %.1f/%.1fG", st.ram_used, st.ram_total);
    if (!st.wifi_up)
        block(DIM, LINE, "wifi off");
    else if (st.signal >= 0)
        block(TEXT, LINE, "%s %d%%", st.ssid[0] ? st.ssid : "wifi", st.signal);
    else
        block(TEXT, LINE, "%s", st.ssid[0] ? st.ssid : "wifi");
    if (st.volume < 0)
        block(DIM, LINE, "vol ?");
    else
        block(st.muted ? DIM : TEXT, LINE, "%s %d%%", st.muted ? "muted" : "vol", st.volume);
    if (st.battery < 0)
        block(DIM, LINE, "bat ?");
    else
        block(st.charging ? TEXT : st.battery <= 15 ? RED : st.battery <= 30 ? YELLOW : TEXT, LINE,
              "bat %d%%%s", st.battery, st.charging ? "+" : "");
    block(TEXT, LAST, "%s", clock);
    len += snprintf(out + len, sizeof out - len, "],\n");

    if (strcmp(out, last) != 0) {
        fputs(out, stdout);
        fflush(stdout); /* swaybar gone -> SIGPIPE ends us, as it should */
        strcpy(last, out);
    }
}

/* ---- setup ---- */

static int netlink_socket(int protocol, unsigned groups)
{
    struct sockaddr_nl addr = { .nl_family = AF_NETLINK, .nl_groups = groups };
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, protocol);
    if (fd >= 0 && bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        fd = -1;
    }
    return fd;
}

/* Read everything queued on a netlink socket; returns 1 if any uevent mentions power_supply. */
static int drain(int fd)
{
    char buf[8192];
    ssize_t n;
    int power = 0;
    while ((n = recv(fd, buf, sizeof buf - 1, 0)) > 0) {
        buf[n] = 0;
        if (strstr(buf, "/power_supply/")) /* uevent header: "change@/devices/.../power_supply/..." */
            power = 1;
    }
    return power;
}

static void read_all(void)
{
    read_ram();
    read_turbo();
    read_battery();
    read_wifi_link();
    read_signal();
    read_volume();
}

int main(void)
{
    glob_t g;
    if (glob("/sys/class/drm/card*/gt_act_freq_mhz", 0, NULL, &g) == 0) {
        snprintf(gpu, sizeof gpu, "%.*s", (int)(strrchr(g.gl_pathv[0], '/') - g.gl_pathv[0] + 1),
                 g.gl_pathv[0]);
        globfree(&g);
        gpu_rp0 = gpu_int("gt_RP0_freq_mhz");
    }

    sigset_t sigs;
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGUSR1);
    sigaddset(&sigs, SIGCONT);
    sigprocmask(SIG_BLOCK, &sigs, NULL);

    struct itimerspec every_second = { .it_interval = { 1, 0 }, .it_value = { 1, 0 } };
    int timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    timerfd_settime(timer, 0, &every_second, NULL);

    struct pollfd fds[] = {
        { .fd = timer, .events = POLLIN },
        { .fd = signalfd(-1, &sigs, SFD_CLOEXEC), .events = POLLIN },
        { .fd = netlink_socket(NETLINK_KOBJECT_UEVENT, 1), .events = POLLIN },
        { .fd = netlink_socket(NETLINK_ROUTE, RTMGRP_LINK), .events = POLLIN },
    };

    printf("{\"version\":1}\n[\n");
    read_all();
    for (unsigned tick = 0;;) {
        read_cpu();
        read_gpu();
        emit();

        if (poll(fds, 4, -1) < 0)
            continue;
        if (fds[0].revents) {
            uint64_t expirations;
            read(timer, &expirations, sizeof expirations);
            tick++;
            if (tick % RAM_EVERY == 0)
                read_ram();
            if (tick % SIGNAL_EVERY == 0) {
                read_signal();
                if (st.wifi_up && !st.ssid[0]) /* iwd may name the network just after link up */
                    read_wifi_link();
            }
            if (tick % TURBO_EVERY == 0)
                read_turbo();
            if (tick % BATTERY_EVERY == 0)
                read_battery();
            if (st.volume < 0 && tick % VOLUME_RETRY == 0)
                read_volume();
        }
        if (fds[1].revents) {
            struct signalfd_siginfo si;
            read(fds[1].fd, &si, sizeof si);
            if (si.ssi_signo == SIGCONT) {
                read_all();
            } else {
                read_volume();
                read_turbo();
            }
        }
        if (fds[2].revents && drain(fds[2].fd))
            read_battery();
        if (fds[3].revents) {
            drain(fds[3].fd);
            read_wifi_link();
        }
    }
}

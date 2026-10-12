/*
 * statusbar: the swaybar status line (i3bar JSON on stdout).
 *
 *    2.4G | 󰢮 400M | 󱐋 6W | 󰔏 55° | 󰍛 0.6/3.7G | 󰤨 <ssid> 100% | 󰕾 50% | 󰂄 99% | Wed 07 Oct  15:29
 *
 * Icons: Symbols Nerd Font (ttf-nerd-fonts-symbols; Pango falls back to it from the bar's DejaVu Sans), Material
 * Design and Octicons glyphs by name from nerd-fonts' glyphnames.json (ICON_* below).
 *
 * Battery: level icon + "80%" on battery (yellow <=30%, red <=15%), charging icon while charging, full icon at
 * >= 99% with the charger, plug icon when held at the charge limit (docs/power.md), alert icon + "not charging"
 * (yellow) when connected but stuck. Wi-Fi: strength icon (4 levels), network name and signal; crossed out when
 * off. Volume: speaker by level, crossed out (dimmed) when muted.
 *
 * CPU/GPU show the current clock and are dimmed while turbo is capped (~/.config/sway/turbo.sh).
 * "6W" is the package power limit (RAPL PL1), white at the firmware's 6 W, yellow while a game raised it
 * (scripts/game-wrap GPD_TDP_W, docs/power.md). "55°C" is the hottest CPU core (coretemp): green, peach from
 * 75 C (normal in games at 8 W), red from 85 C (the chip throttles at 90 C).
 * CPU is the fastest core (/proc/cpuinfo, sampled by the kernel on its timer tick). GPU is "idle"
 * when it spent most of the last second in RC6 (its frequency register keeps the last value).
 *
 * Everything happens in one poll() loop, so the program sleeps until something can have changed.
 * Each value is read only as often as it is worth (measured cost per read on this device):
 *   every 1 s          CPU and GPU clock (~0.15 ms), and the clock text
 *   every 5 s          RAM (0.05 ms), CPU temperature (coretemp, 4 sysfs reads)
 *   every 30 s         WiFi signal (2.3 ms: asks the WiFi firmware); turbo state as a fallback
 *   every 60 s         battery (9 ms: I2C to the fuel gauge and the charger)
 *   kernel uevents     charger plugged in/out, battery events -> battery at once
 *   rtnetlink          WiFi link up/down -> link state and network name at once
 *   SIGUSR1            volume, turbo, power or charge limit changed (osd.sh, turbo.sh, game-wrap, charge.sh)
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
#define PL1 "/sys/class/powercap/intel-rapl:0/constraint_0_power_limit_uw"
#define PL1_DEFAULT 6016000
#define WIFI     "wlan0"

/* Colours from the sway theme (Catppuccin Mocha) */
#define TEXT   "#cdd6f4"
#define DIM    "#7f849c"
#define YELLOW "#f9e2af"
#define RED    "#f38ba8"
#define GREEN  "#a6e3a1"

/* Nerd Font glyphs (UTF-8), names as in nerd-fonts glyphnames.json */
#define ICON_CPU     "\uf4bc"      /* oct-cpu */
#define ICON_GPU     "\U000f08ae"  /* md-expansion_card */
#define ICON_POWER   "\U000f140b"  /* md-lightning_bolt */
#define ICON_TEMP    "\U000f050f"  /* md-thermometer */
#define ICON_RAM     "\U000f035b"  /* md-memory */
#define ICON_WIFIOFF "\U000f05aa"  /* md-wifi_off */
#define ICON_VOLOFF  "\U000f075f"  /* md-volume_mute */
#define ICON_BATCHG  "\U000f0084"  /* md-battery_charging */
#define ICON_BATPLUG "\U000f06a5"  /* md-power_plug: held at the charge limit */
#define ICON_BATWARN "\U000f0083"  /* md-battery_alert */

/* one Unicode code point as UTF-8 (the level icons are computed) */
static const char *glyph(unsigned cp)
{
    static char buf[8][5];
    static int n;
    char *b = buf[n++ % 8];
    b[0] = 0xf0 | (cp >> 18), b[1] = 0x80 | ((cp >> 12) & 0x3f), b[2] = 0x80 | ((cp >> 6) & 0x3f);
    b[3] = 0x80 | (cp & 0x3f), b[4] = 0;
    return b;
}

/* md-battery_10..90 are U+F007A..U+F0082, md-battery (full) U+F0079 */
static const char *battery_icon(int pct)
{
    return pct >= 95 ? glyph(0xf0079) : glyph(0xf007a + (pct < 10 ? 0 : pct / 10 - 1));
}

/* md-wifi_strength_1..4 */
static const char *wifi_icon(int pct)
{
    static const unsigned cp[] = { 0xf091f, 0xf0922, 0xf0925, 0xf0928 };
    return glyph(cp[pct < 25 ? 0 : pct < 50 ? 1 : pct < 75 ? 2 : 3]);
}

/* md-volume_low / medium / high */
static const char *volume_icon(int pct)
{
    return glyph(pct < 34 ? 0xf057f : pct < 67 ? 0xf0580 : 0xf057e);
}
#define PEACH  "#fab387"

/* seconds between reads of the slower values */
#define RAM_EVERY     5
#define SIGNAL_EVERY  30
#define TURBO_EVERY   30
#define BATTERY_EVERY 60
#define VOLUME_RETRY  10 /* while PipeWire isn't up yet after login */

static char gpu[64]; /* "/sys/class/drm/cardN/", found at start */
static glob_t coretemp; /* the coretemp hwmon's temp*_input files, found at start */
static int gpu_rp0;  /* the GPU's highest (turbo) clock, MHz */

static struct {
    double cpu_ghz;
    int cpu_turbo;
    int gpu_mhz;
    int gpu_turbo;
    int tdp_uw;                 /* package power limit, 0 = unknown */
    int temp_c;                 /* hottest CPU core, -1 = unknown */
    double ram_used, ram_total; /* GiB */
    int wifi_up;
    char ssid[64];
    int signal;                 /* percent, -1 = unknown */
    int volume;                 /* percent, -1 = unknown */
    int muted;
    int battery;                /* percent, -1 = unknown */
    int plugged;                /* charger connected */
    char charge[32];            /* the charger's status: "Charging", "Full", "Not charging"... */
    int at_limit;               /* battery at/above the charge voltage limit */
} st = { .signal = -1, .volume = -1, .battery = -1, .temp_c = -1 };

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
    st.tdp_uw = read_int(PL1, 0);
}

static void read_temp(void)
{
    int max = -1;
    for (size_t i = 0; i < coretemp.gl_pathc; i++) {
        int t = read_int(coretemp.gl_pathv[i], -1);
        if (t > max)
            max = t;
    }
    st.temp_c = max < 0 ? -1 : (max + 500) / 1000;
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

/* The cable is read from the charger's "online": its status says "Full" rather than "Charging"
 * once the battery is topped up, and "Not charging" when it is stuck (docs/power.md). */
static void read_battery(void)
{
    st.battery = read_int(BATTERY "capacity", -1);
    st.plugged = read_int(CHARGER "online", 0) == 1;
    read_line(CHARGER "status", st.charge, sizeof st.charge);
    /* Above the limit (charge-limit) the charger says "Not charging", or "Charging" with no current
     * going in: neither is right. Within 100 mV of it counts, the chip's own recharge threshold. */
    int volt = read_int(BATTERY "voltage_now", 0), limit = read_int(CHARGER "constant_charge_voltage", 0);
    st.at_limit = volt > 0 && limit > 0 && volt >= limit - 100000;
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

enum sep { LINE, LAST }; /* separator line after a block / end of the bar */

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
                    sep == LINE ? 19 : 6);
}

static void emit(void)
{
    static char last[sizeof out];
    char clock[32];
    time_t now = time(NULL);
    strftime(clock, sizeof clock, "%a %d %b  %H:%M", localtime(&now));

    len = 0;
    len += snprintf(out, sizeof out, "[");
    block(st.cpu_turbo ? TEXT : DIM, LINE, ICON_CPU " %.1fG", st.cpu_ghz);
    if (st.gpu_mhz > 0)
        block(st.gpu_turbo ? TEXT : DIM, LINE, ICON_GPU " %dM", st.gpu_mhz);
    else
        block(st.gpu_turbo ? TEXT : DIM, LINE, ICON_GPU " idle");
    if (st.tdp_uw > 0)
        block(st.tdp_uw > PL1_DEFAULT ? YELLOW : TEXT, LINE, ICON_POWER " %dW", (st.tdp_uw + 500000) / 1000000);
    if (st.temp_c >= 0)
        block(st.temp_c >= 85 ? RED : st.temp_c >= 75 ? PEACH : GREEN, LINE, ICON_TEMP " %d°", st.temp_c);
    block(TEXT, LINE, ICON_RAM " %.1f/%.1fG", st.ram_used, st.ram_total);
    if (!st.wifi_up)
        block(DIM, LINE, ICON_WIFIOFF);
    else if (st.signal >= 0)
        block(TEXT, LINE, "%s %s %d%%", wifi_icon(st.signal), st.ssid[0] ? st.ssid : "", st.signal);
    else
        block(TEXT, LINE, "%s %s", wifi_icon(100), st.ssid);
    if (st.volume < 0)
        block(DIM, LINE, "%s ?", volume_icon(0));
    else if (st.muted)
        block(DIM, LINE, ICON_VOLOFF " %d%%", st.volume);
    else
        block(TEXT, LINE, "%s %d%%", volume_icon(st.volume), st.volume);
    if (st.battery < 0)
        block(DIM, LINE, "%s ?", battery_icon(0));
    else if (st.plugged && strcmp(st.charge, "Full") == 0 && st.battery >= 99)
        block(TEXT, LINE, "%s %d%%", battery_icon(100), st.battery);
    /* at the charge limit: the chip says "Full" there too (at ~89%), or above it "Charging"/"Not
     * charging" with no current going in */
    else if (st.plugged && (st.at_limit || strcmp(st.charge, "Full") == 0))
        block(TEXT, LINE, ICON_BATPLUG " %d%%", st.battery);
    else if (st.plugged && strcmp(st.charge, "Charging") == 0)
        block(TEXT, LINE, ICON_BATCHG " %d%%", st.battery);
    else if (st.plugged) /* connected but not charging: stuck charger */
        block(YELLOW, LINE, ICON_BATWARN " %d%% not charging", st.battery);
    else
        block(st.battery <= 15 ? RED : st.battery <= 30 ? YELLOW : TEXT, LINE, "%s %d%%", battery_icon(st.battery), st.battery);
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
    read_temp();
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
    if (glob("/sys/class/hwmon/hwmon*/name", 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) {
            char name[32] = "", pat[300];
            read_line(g.gl_pathv[i], name, sizeof name);
            if (strcmp(name, "coretemp") == 0) {
                snprintf(pat, sizeof pat, "%.*s/temp*_input", (int)(strrchr(g.gl_pathv[i], '/') - g.gl_pathv[i]),
                         g.gl_pathv[i]);
                glob(pat, 0, NULL, &coretemp);
                break;
            }
        }
        globfree(&g);
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
            if (tick % RAM_EVERY == 0) {
                read_ram();
                read_temp();
            }
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
                read_battery();
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

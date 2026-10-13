/*
 * devconf.h: the device's hardware facts for the programs in src/ (included by each; header-only).
 *
 * devices/<name>/device.conf in the repo, linked to ~/.config/gpd/device.conf by scripts/sync. Lines
 * "key=value", "#" comments, no quoting (shell scripts source the same file). Every key is optional:
 * a missing file or key means the caller's default, which is auto-detection where possible, so the
 * programs also run on a device nobody has described yet. Keys: docs/repo-layout.md "device.conf".
 */
#ifndef DEVCONF_H
#define DEVCONF_H
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEVCONF_MAX 64

static struct { char *key, *value; } devconf_kv[DEVCONF_MAX];
static int devconf_n = -1;

static inline void devconf_load(void)
{
    char path[512], line[512];
    const char *x = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    devconf_n = 0;
    if (x && *x)
        snprintf(path, sizeof path, "%s/gpd/device.conf", x);
    else
        snprintf(path, sizeof path, "%s/.config/gpd/device.conf", home ? home : "");
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    while (devconf_n < DEVCONF_MAX && fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (line[0] == '#' || !eq)
            continue;
        *eq = 0;
        char *v = eq + 1;
        v[strcspn(v, "\n")] = 0;
        /* a trailing comment ("key=value  # why") */
        char *c = strstr(v, " #");
        if (c)
            *c = 0;
        for (char *e = v + strlen(v); e > v && (e[-1] == ' ' || e[-1] == '\t');)
            *--e = 0;
        if (*v == '"' || *v == '\'') { /* tolerate shell quoting */
            v++;
            if (*v && (v[strlen(v) - 1] == '"' || v[strlen(v) - 1] == '\''))
                v[strlen(v) - 1] = 0;
        }
        devconf_kv[devconf_n].key = strdup(line);
        devconf_kv[devconf_n].value = strdup(v);
        devconf_n++;
    }
    fclose(f);
}

/* The value of key, or fallback when it is missing or empty. */
static const char *devconf(const char *key, const char *fallback)
{
    if (devconf_n < 0)
        devconf_load();
    for (int i = 0; i < devconf_n; i++)
        if (strcmp(devconf_kv[i].key, key) == 0 && devconf_kv[i].value[0])
            return devconf_kv[i].value;
    return fallback;
}

static inline double devconf_num(const char *key, double fallback)
{
    const char *v = devconf(key, NULL);
    return v ? strtod(v, NULL) : fallback;
}

/* The first power supply (name under /sys/class/power_supply) whose type is one of types (space-separated,
 * e.g. "Battery" or "Mains USB") and that has the file need (NULL: any). Writes "" when there is none. */
static inline void devconf_find_supply(const char *types, const char *need, char *out, size_t size)
{
    DIR *d = opendir("/sys/class/power_supply");
    struct dirent *e;
    out[0] = 0;
    while (d && !out[0] && (e = readdir(d))) {
        char path[512], type[32] = "";
        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/type", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        if (fgets(type, sizeof type, f))
            type[strcspn(type, "\n")] = 0;
        fclose(f);
        char want[128];
        snprintf(want, sizeof want, " %s ", types);
        char t[40];
        snprintf(t, sizeof t, " %s ", type);
        if (!type[0] || !strstr(want, t))
            continue;
        if (need) {
            snprintf(path, sizeof path, "/sys/class/power_supply/%s/%s", e->d_name, need);
            FILE *n = fopen(path, "r");
            if (!n)
                continue;
            fclose(n);
        }
        snprintf(out, size, "%s", e->d_name);
    }
    if (d)
        closedir(d);
}

/* Battery and charger directories ("/sys/class/power_supply/NAME/"), from device.conf (battery=, charger=)
 * or the first supply of type Battery / of type Mains or USB with an "online" file. "" if none. */
static inline void devconf_battery_dir(char *out, size_t size)
{
    char name[256];
    const char *v = devconf("battery", NULL);
    if (v)
        snprintf(name, sizeof name, "%s", v);
    else
        devconf_find_supply("Battery", NULL, name, sizeof name);
    if (name[0])
        snprintf(out, size, "/sys/class/power_supply/%s/", name);
    else
        out[0] = 0;
}

static inline void devconf_charger_dir(char *out, size_t size)
{
    char name[256];
    const char *v = devconf("charger", NULL);
    if (v)
        snprintf(name, sizeof name, "%s", v);
    else
        devconf_find_supply("Mains USB", "online", name, sizeof name);
    if (name[0])
        snprintf(out, size, "/sys/class/power_supply/%s/", name);
    else
        out[0] = 0;
}

#endif

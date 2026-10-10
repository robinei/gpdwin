/*
 * shelf: a lightweight game launcher that reads and writes Pegasus Frontend's files, so the two can be
 * swapped at any time (docs/frontend.md "shelf").
 *
 * Data, all shared with Pegasus (exact rules: docs/pegasus-format.md):
 *   ~/.config/pegasus-frontend/game_dirs.txt          game dirs (read)
 *   <game dir>/metadata.pegasus.txt (and variants)     collections, games, launch commands (read)
 *   <game dir>/media/<file stem or title>/boxFront.*   box art (read)
 *   ~/.config/pegasus-frontend/stats.db                play time: read, and one row per finished game
 *                                                      written with Pegasus' exact SQL
 *   ~/.config/pegasus-frontend/favorites.txt           favourites (read; rewritten in Pegasus' format)
 *   ~/.config/pegasus-frontend/theme_settings/pegasus-theme-grid.json
 *                                                      last collection + game (read at start, written
 *                                                      at launch, so either frontend opens on it)
 * Paths are cleaned lexically and never symlink-resolved, like Pegasus, so stats and favourites match.
 *
 * UI: collections as tabs on top ("Recent" and "Favourites" first), games as a list on the left,
 * box art and details on the right. No animations; it draws only when something changed, so it
 * sleeps while untouched. After a game (or utility) exits it rescans the metadata, so new games show
 * up without a restart.
 *
 * Input: keyboard through SDL. The gamepad is read directly from evdev in a thread (inotify for
 * hotplug), not through SDL's gamepad layer: with a joystick open SDL polls about every millisecond,
 * and the pad disconnects whenever the screen goes off. Pad: d-pad/stick up/down move (held: repeat),
 * left/right or LB/RB switch collection, triggers page, A launch, Y favourite.
 * Keys: arrows, Page Up/Down, Home/End, Tab/Shift+Tab, Enter launch, F favourite.
 *
 * Options: --list prints what was found (no window) and exits.
 *
 * Built by `scripts/sync install` (manifest "build" entry with pkg-config packages); started by
 * dotfiles/pegasus-frontend/run when ~/.config/gpd/frontend says "shelf".
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <sqlite3.h>
#include <unicode/ucol.h>

#define JSMN_STATIC
#include "vendor/jsmn.h"

#define PAD_NAME "Microsoft X-Box 360 pad"
#define FONT_REGULAR "/usr/share/fonts/TTF/DejaVuSans.ttf"
#define FONT_BOLD "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"
#define RECENT_MAX 20
#define REPEAT_DELAY_MS 300
#define REPEAT_RATE_MS 70
#define BIG_IMAGE_DELAY_MS 120 /* while scrolling, load the large box art once movement pauses */

/* ------------------------------------------------------------------ small helpers */

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "shelf: out of memory\n");
        exit(1);
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (!p) {
        fprintf(stderr, "shelf: out of memory\n");
        exit(1);
    }
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

static char *xstrndup(const char *s, size_t n)
{
    char *d = xmalloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

static char *fmt(const char *f, ...)
{
    va_list ap;
    char *s;
    va_start(ap, f);
    if (vasprintf(&s, f, ap) < 0) {
        fprintf(stderr, "shelf: out of memory\n");
        exit(1);
    }
    va_end(ap);
    return s;
}

static void warn(const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    fputs("shelf: ", stderr);
    vfprintf(stderr, f, ap);
    fputc('\n', stderr);
    va_end(ap);
}

typedef struct {
    char **v;
    int n, cap;
} StrList;

static void sl_add(StrList *l, const char *s)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->v = xrealloc(l->v, l->cap * sizeof *l->v);
    }
    l->v[l->n++] = s ? xstrdup(s) : NULL;
}

static void sl_add_unique(StrList *l, const char *s)
{
    for (int i = 0; i < l->n; i++)
        if (strcmp(l->v[i], s) == 0)
            return;
    sl_add(l, s);
}

static bool sl_has(const StrList *l, const char *s)
{
    for (int i = 0; i < l->n; i++)
        if (l->v[i] && strcmp(l->v[i], s) == 0)
            return true;
    return false;
}

static void sl_free(StrList *l)
{
    for (int i = 0; i < l->n; i++)
        free(l->v[i]);
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

static char *trim_dup(const char *s, size_t n)
{
    while (n && isspace((unsigned char)*s))
        s++, n--;
    while (n && isspace((unsigned char)s[n - 1]))
        n--;
    return xstrndup(s, n);
}

/* QDir::cleanPath of an absolute path: no symlink resolution, "." and ".." resolved lexically,
 * repeated and trailing slashes removed. */
static char *clean_path(const char *p)
{
    size_t n = strlen(p);
    char *out = xmalloc(n + 2);
    size_t o = 0;
    const char *s = p;
    if (*s == '/')
        out[o++] = '/';
    while (*s) {
        while (*s == '/')
            s++;
        const char *e = s;
        while (*e && *e != '/')
            e++;
        size_t len = e - s;
        if (len == 0)
            break;
        if (len == 1 && s[0] == '.') {
            /* skip */
        } else if (len == 2 && s[0] == '.' && s[1] == '.') {
            if (o > 1) { /* drop the last component */
                if (out[o - 1] == '/')
                    o--;
                while (o > 0 && out[o - 1] != '/')
                    o--;
                if (o == 0 && p[0] == '/')
                    out[o++] = '/';
            } else if (o == 0) {
                memcpy(out + o, "..", 2), o += 2;
            }
        } else {
            if (o > 0 && out[o - 1] != '/')
                out[o++] = '/';
            memcpy(out + o, s, len);
            o += len;
        }
        s = e;
    }
    if (o > 1 && out[o - 1] == '/')
        o--;
    if (o == 0)
        out[o++] = p[0] == '/' ? '/' : '.';
    out[o] = 0;
    return out;
}

/* Absolute, cleaned: relative paths are relative to base. */
static char *abs_path(const char *base, const char *p)
{
    if (p[0] == '/')
        return clean_path(p);
    char *j = fmt("%s/%s", base, p);
    char *c = clean_path(j);
    free(j);
    return c;
}

static char *dir_of(const char *path)
{
    const char *sl = strrchr(path, '/');
    if (!sl)
        return xstrdup(".");
    if (sl == path)
        return xstrdup("/");
    return xstrndup(path, sl - path);
}

static const char *name_of(const char *path)
{
    const char *sl = strrchr(path, '/');
    return sl ? sl + 1 : path;
}

/* QFileInfo::completeBaseName: the name up to its last dot. */
static char *stem_of(const char *path)
{
    const char *n = name_of(path);
    const char *dot = strrchr(n, '.');
    return dot ? xstrndup(n, dot - n) : xstrdup(n);
}

/* QFileInfo::suffix: after the last dot ("" if none). */
static const char *suffix_of(const char *path)
{
    const char *n = name_of(path);
    const char *dot = strrchr(n, '.');
    return dot ? dot + 1 : "";
}

static bool exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static bool is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_file(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 4096, n = 0;
    char *b = xmalloc(cap);
    size_t r;
    while ((r = fread(b + n, 1, cap - n - 1, f)) > 0) {
        n += r;
        if (cap - n - 1 == 0)
            b = xrealloc(b, cap *= 2);
    }
    fclose(f);
    b[n] = 0;
    if (len)
        *len = n;
    return b;
}

/* Write via a temporary file and rename, so a crash never leaves a half-written file. */
static bool write_file(const char *path, const char *data)
{
    char *tmp = fmt("%s.shelf-tmp", path);
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fputs(data, f) >= 0;
    if (f && fclose(f) != 0)
        ok = false;
    if (ok && rename(tmp, path) != 0)
        ok = false;
    if (!ok) {
        warn("could not write %s: %s", path, strerror(errno));
        unlink(tmp);
    }
    free(tmp);
    return ok;
}

/* ------------------------------------------------------------------ data model */

typedef struct Collection Collection;

typedef struct Game {
    char *title, *sort_by;
    StrList files; /* clean absolute paths */
    StrList developers, publishers, genres;
    int players;
    char *release; /* "YYYY", "YYYY-MM" or "YYYY-MM-DD" */
    char *summary, *description;
    char *boxfront;
    char *launch, *workdir, *basedir;
    Collection **colls;
    int ncolls;
    bool favorite;
    int play_count;
    long long play_time, last_played;
} Game;

struct Collection {
    char *name, *shortname, *launch, *workdir, *basedir, *summary, *boxfront;
    Game **games;
    int ngames, cap;
    bool virtual_;
};

typedef struct {
    Collection *coll;
    StrList dirs, exts;
} Filter;

typedef struct {
    Game **games;
    int ngames, gcap;
    Collection **colls; /* real collections, sorted */
    int ncolls, ccap;
    Filter *filters;
    int nfilters, fcap;
    /* file path -> game */
    char **map_keys;
    Game **map_vals;
    size_t map_cap, map_n;
} Library;

static unsigned long hash_str(const char *s)
{
    unsigned long h = 5381;
    while (*s)
        h = h * 33 ^ (unsigned char)*s++;
    return h;
}

static void map_put(char ***keys, Game ***vals, size_t *cap, size_t *n, const char *k, Game *g, bool keep_first);

static void map_grow(char ***keys, Game ***vals, size_t *cap, size_t *n)
{
    size_t oc = *cap;
    char **ok = *keys;
    Game **ov = *vals;
    *cap = oc ? oc * 2 : 256;
    *keys = calloc(*cap, sizeof **keys);
    *vals = calloc(*cap, sizeof **vals);
    if (!*keys || !*vals) {
        fprintf(stderr, "shelf: out of memory\n");
        exit(1);
    }
    *n = 0;
    for (size_t i = 0; i < oc; i++)
        if (ok[i]) {
            map_put(keys, vals, cap, n, ok[i], ov[i], true);
            free(ok[i]);
        }
    free(ok);
    free(ov);
}

static void map_put(char ***keys, Game ***vals, size_t *cap, size_t *n, const char *k, Game *g, bool keep_first)
{
    if ((*n + 1) * 2 > *cap)
        map_grow(keys, vals, cap, n);
    size_t i = hash_str(k) & (*cap - 1);
    while ((*keys)[i]) {
        if (strcmp((*keys)[i], k) == 0) {
            if (!keep_first)
                (*vals)[i] = g;
            return;
        }
        i = (i + 1) & (*cap - 1);
    }
    (*keys)[i] = xstrdup(k);
    (*vals)[i] = g;
    (*n)++;
}

static Game *map_get(char **keys, Game **vals, size_t cap, const char *k)
{
    if (!cap)
        return NULL;
    size_t i = hash_str(k) & (cap - 1);
    while (keys[i]) {
        if (strcmp(keys[i], k) == 0)
            return vals[i];
        i = (i + 1) & (cap - 1);
    }
    return NULL;
}

static void map_free(char **keys, Game **vals, size_t cap)
{
    for (size_t i = 0; i < cap; i++)
        free(keys[i]);
    free(keys);
    free(vals);
}

static Game *lib_file_game(Library *L, const char *path)
{
    return map_get(L->map_keys, L->map_vals, L->map_cap, path);
}

static Game *new_game(Library *L, const char *title)
{
    Game *g = calloc(1, sizeof *g);
    if (!g)
        exit(1);
    g->players = 1;
    if (title) {
        g->title = xstrdup(title);
        g->sort_by = xstrdup(title);
    }
    if (L->ngames == L->gcap) {
        L->gcap = L->gcap ? L->gcap * 2 : 64;
        L->games = xrealloc(L->games, L->gcap * sizeof *L->games);
    }
    L->games[L->ngames++] = g;
    return g;
}

static void free_game(Game *g)
{
    free(g->title);
    free(g->sort_by);
    sl_free(&g->files);
    sl_free(&g->developers);
    sl_free(&g->publishers);
    sl_free(&g->genres);
    free(g->release);
    free(g->summary);
    free(g->description);
    free(g->boxfront);
    free(g->launch);
    free(g->workdir);
    free(g->basedir);
    free(g->colls);
    free(g);
}

static void free_collection(Collection *c)
{
    free(c->name);
    free(c->shortname);
    free(c->launch);
    free(c->workdir);
    free(c->basedir);
    free(c->summary);
    free(c->boxfront);
    free(c->games);
    free(c);
}

static void lib_free(Library *L)
{
    for (int i = 0; i < L->ngames; i++)
        free_game(L->games[i]);
    free(L->games);
    for (int i = 0; i < L->ncolls; i++)
        free_collection(L->colls[i]);
    free(L->colls);
    for (int i = 0; i < L->nfilters; i++) {
        sl_free(&L->filters[i].dirs);
        sl_free(&L->filters[i].exts);
    }
    free(L->filters);
    map_free(L->map_keys, L->map_vals, L->map_cap);
    memset(L, 0, sizeof *L);
}

static Collection *get_collection(Library *L, const char *name)
{
    for (int i = 0; i < L->ncolls; i++)
        if (strcmp(L->colls[i]->name, name) == 0)
            return L->colls[i];
    Collection *c = calloc(1, sizeof *c);
    if (!c)
        exit(1);
    c->name = xstrdup(name);
    if (L->ncolls == L->ccap) {
        L->ccap = L->ccap ? L->ccap * 2 : 8;
        L->colls = xrealloc(L->colls, L->ccap * sizeof *L->colls);
    }
    L->colls[L->ncolls++] = c;
    return c;
}

static void set_str(char **dst, const char *v)
{
    free(*dst);
    *dst = v ? xstrdup(v) : NULL;
}

/* SearchContext::game_add_to: membership plus inheritance of empty launch settings. */
static void game_add_to(Game *g, Collection *c)
{
    for (int i = 0; i < c->ngames; i++)
        if (c->games[i] == g)
            return;
    if (c->ngames == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 16;
        c->games = xrealloc(c->games, c->cap * sizeof *c->games);
    }
    c->games[c->ngames++] = g;
    g->colls = xrealloc(g->colls, (g->ncolls + 1) * sizeof *g->colls);
    g->colls[g->ncolls++] = c;
    if (!g->launch && c->launch)
        g->launch = xstrdup(c->launch);
    if (!g->workdir && c->workdir)
        g->workdir = xstrdup(c->workdir);
    if (!g->basedir && c->basedir)
        g->basedir = xstrdup(c->basedir);
}

static void game_add_file(Library *L, Game *g, const char *path)
{
    sl_add(&g->files, path);
    map_put(&L->map_keys, &L->map_vals, &L->map_cap, &L->map_n, path, g, true);
}

/* GameFile pretty name: completeBaseName with '_' and '.' as spaces. */
static char *pretty_name(const char *path)
{
    char *s = stem_of(path);
    for (char *p = s; *p; p++)
        if (*p == '_' || *p == '.')
            *p = ' ';
    return s;
}

/* ------------------------------------------------------------------ metadata parser */

static const char *BOXFRONT_ALIASES[] = { "boxfront", "boxFront", "box_front", "boxart2D", "boxart2d", NULL };

static bool is_boxfront_name(const char *n)
{
    for (int i = 0; BOXFRONT_ALIASES[i]; i++)
        if (strcmp(n, BOXFRONT_ALIASES[i]) == 0)
            return true;
    return false;
}

typedef struct {
    Library *L;
    const char *path, *dir;
    Collection *coll;
    Game *game;
    Collection **file_colls; /* collections declared so far in this file */
    int nfile_colls;
    Filter *filter; /* the last collection: line's filter (index, since filters may move) */
    int filter_idx;
} ParseState;

/* MetaFile merge_lines: values joined by spaces; a "." line (NULL) is a paragraph break. */
static char *merge_lines(const StrList *v)
{
    size_t cap = 64, n = 0;
    char *s = xmalloc(cap);
    bool after_break = true;
    for (int i = 0; i < v->n; i++) {
        const char *add = v->v[i] ? v->v[i] : "\n\n";
        size_t len = strlen(add);
        if (n + len + 2 >= cap)
            s = xrealloc(s, cap = (n + len + 2) * 2);
        if (v->v[i] && !after_break && n > 0)
            s[n++] = ' ';
        memcpy(s + n, add, len);
        n += len;
        after_break = !v->v[i];
    }
    s[n] = 0;
    char *t = trim_dup(s, n);
    free(s);
    return t;
}

/* replace_newlines: the escape "\n" becomes a newline, "\\n" a literal "\n". */
static char *unescape_newlines(char *s)
{
    char *o = s;
    for (char *p = s; *p;) {
        if (p[0] == '\\' && p[1] == '\\' && p[2] == 'n') {
            *o++ = '\\', *o++ = 'n', p += 3;
        } else if (p[0] == '\\' && p[1] == 'n') {
            *o++ = '\n', p += 2;
        } else {
            *o++ = *p++;
        }
    }
    *o = 0;
    return s;
}

static const char *first_line(const StrList *v, const char *key, const ParseState *ps)
{
    if (v->n > 1)
        warn("%s: '%s' expects one line, extra lines ignored", ps->path, key);
    return v->v[0] ? v->v[0] : "";
}

static bool key_is(const char *k, const char *a, const char *b)
{
    return strcmp(k, a) == 0 || (b && strcmp(k, b) == 0);
}

static void add_extensions(StrList *exts, const char *line)
{
    const char *s = line;
    while (*s) {
        const char *e = strchr(s, ',');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        char *t = trim_dup(s, n);
        for (char *p = t; *p; p++)
            *p = tolower((unsigned char)*p);
        if (*t)
            sl_add_unique(exts, t);
        free(t);
        s += n;
        if (*s == ',')
            s++;
    }
}

static bool uri_like(const char *s)
{
    /* ^[a-zA-Z][a-zA-Z0-9+\-.]+:.+ */
    if (!isalpha((unsigned char)s[0]))
        return false;
    int i = 1;
    while (isalnum((unsigned char)s[i]) || s[i] == '+' || s[i] == '-' || s[i] == '.')
        i++;
    return i >= 2 && s[i] == ':' && s[i + 1];
}

static void parse_players(Game *g, const char *v)
{
    int a = 0, b = 0, n = 0;
    if (sscanf(v, "%d%n", &a, &n) != 1 || !isdigit((unsigned char)v[0]))
        return;
    const char *r = v + n;
    if (*r == '-') {
        int m = 0;
        if (!isdigit((unsigned char)r[1]) || sscanf(r + 1, "%d%n", &b, &m) != 1 || r[1 + m])
            return;
    } else if (*r) {
        return;
    }
    int p = a > b ? a : b;
    g->players = p > 1 ? p : 1;
}

static void dispatch(ParseState *ps, const char *key, StrList *vals)
{
    Library *L = ps->L;
    if (strcmp(key, "collection") == 0) {
        Collection *c = get_collection(L, first_line(vals, key, ps));
        set_str(&c->basedir, ps->dir);
        ps->coll = c;
        ps->game = NULL;
        ps->file_colls = xrealloc(ps->file_colls, (ps->nfile_colls + 1) * sizeof *ps->file_colls);
        ps->file_colls[ps->nfile_colls++] = c;
        if (L->nfilters == L->fcap) {
            L->fcap = L->fcap ? L->fcap * 2 : 8;
            L->filters = xrealloc(L->filters, L->fcap * sizeof *L->filters);
        }
        Filter *f = &L->filters[L->nfilters];
        memset(f, 0, sizeof *f);
        f->coll = c;
        sl_add(&f->dirs, ps->dir);
        ps->filter_idx = L->nfilters++;
        return;
    }
    if (strcmp(key, "game") == 0) {
        Game *g = new_game(L, first_line(vals, key, ps));
        g->basedir = xstrdup(ps->dir);
        for (int i = 0; i < ps->nfile_colls; i++)
            game_add_to(g, ps->file_colls[i]);
        ps->game = g;
        return;
    }
    if (!ps->coll && !ps->game) {
        warn("%s: '%s' before any collection or game, ignored", ps->path, key);
        return;
    }
    if (strncmp(key, "x-", 2) == 0)
        return; /* extras: not shown */
    const char *dot = strchr(key, '.');
    if (dot && (strncmp(key, "assets.", 7) == 0 || strncmp(key, "asset.", 6) == 0)) {
        if (!is_boxfront_name(dot + 1))
            return; /* other asset types are not shown */
        char **dst = ps->game ? &ps->game->boxfront : &ps->coll->boxfront;
        for (int i = 0; i < vals->n; i++) {
            const char *v = vals->v[i];
            if (!v || strncmp(v, "http://", 7) == 0 || strncmp(v, "https://", 8) == 0)
                continue;
            char *p = abs_path(ps->dir, v);
            if (!exists(p)) {
                warn("%s: asset '%s' not found", ps->path, v);
                free(p);
            } else if (!*dst) {
                *dst = p;
            } else {
                free(p);
            }
        }
        return;
    }
    if (ps->game) {
        Game *g = ps->game;
        if (key_is(key, "file", "files")) {
            for (int i = 0; i < vals->n; i++) {
                const char *v = vals->v[i];
                if (!v || uri_like(v))
                    continue;
                char *p = abs_path(ps->dir, v);
                Game *owner = lib_file_game(L, p);
                if (!exists(p))
                    warn("%s: file '%s' not found", ps->path, v);
                else if (owner)
                    warn("%s: file '%s' already belongs to %s", ps->path, v,
                         owner == g ? "this game" : "a different game");
                else
                    game_add_file(L, g, p);
                free(p);
            }
        } else if (key_is(key, "launch", "command")) {
            free(g->launch);
            g->launch = merge_lines(vals);
        } else if (key_is(key, "workdir", "cwd")) {
            set_str(&g->workdir, first_line(vals, key, ps));
        } else if (key_is(key, "developer", "developers") || key_is(key, "publisher", "publishers") ||
                   key_is(key, "genre", "genres") || key_is(key, "tag", "tags")) {
            StrList *l = key[0] == 'd' ? &g->developers : key[0] == 'p' ? &g->publishers
                       : key[0] == 'g' ? &g->genres : NULL;
            for (int i = 0; l && i < vals->n; i++)
                if (vals->v[i])
                    sl_add_unique(l, vals->v[i]);
        } else if (strcmp(key, "players") == 0) {
            parse_players(g, first_line(vals, key, ps));
        } else if (strcmp(key, "summary") == 0) {
            free(g->summary);
            g->summary = unescape_newlines(merge_lines(vals));
        } else if (strcmp(key, "description") == 0) {
            free(g->description);
            g->description = unescape_newlines(merge_lines(vals));
        } else if (strcmp(key, "release") == 0) {
            set_str(&g->release, first_line(vals, key, ps));
        } else if (strcmp(key, "rating") == 0) {
            /* not shown */
        } else if (!strcmp(key, "sorttitle") || !strcmp(key, "sortname") || !strcmp(key, "sort_title") ||
                   !strcmp(key, "sort_name") || !strcmp(key, "sort-title") || !strcmp(key, "sort-name") ||
                   !strcmp(key, "sortby") || !strcmp(key, "sort_by") || !strcmp(key, "sort-by")) {
            set_str(&g->sort_by, first_line(vals, key, ps));
        } else {
            warn("%s: unknown game key '%s'", ps->path, key);
        }
        return;
    }
    Collection *c = ps->coll;
    Filter *f = &L->filters[ps->filter_idx];
    if (strcmp(key, "shortname") == 0) {
        set_str(&c->shortname, first_line(vals, key, ps));
        for (char *p = c->shortname; *p; p++)
            *p = tolower((unsigned char)*p);
    } else if (key_is(key, "launch", "command")) {
        free(c->launch);
        c->launch = merge_lines(vals);
    } else if (key_is(key, "workdir", "cwd")) {
        set_str(&c->workdir, first_line(vals, key, ps));
    } else if (key_is(key, "extension", "extensions")) {
        add_extensions(&f->exts, first_line(vals, key, ps));
    } else if (key_is(key, "directory", "directories")) {
        for (int i = 0; i < vals->n; i++) {
            if (!vals->v[i])
                continue;
            char *p = abs_path(ps->dir, vals->v[i]);
            if (is_dir(p))
                sl_add(&f->dirs, p);
            else
                warn("%s: directory '%s' not found", ps->path, vals->v[i]);
            free(p);
        }
    } else if (strcmp(key, "summary") == 0) {
        free(c->summary);
        c->summary = unescape_newlines(merge_lines(vals));
    } else if (strcmp(key, "description") == 0 || !strcmp(key, "sortby") || !strcmp(key, "sort_by") ||
               !strcmp(key, "sort-by")) {
        /* not shown */
    } else {
        warn("%s: unsupported collection key '%s' ignored", ps->path, key);
    }
}

static void parse_metafile(Library *L, const char *path)
{
    size_t len;
    char *text = read_file(path, &len);
    if (!text) {
        warn("cannot read %s", path);
        return;
    }
    char *dir = dir_of(path);
    ParseState ps = { .L = L, .path = path, .dir = dir };
    char *key = NULL;
    StrList vals = { 0 };
    char *s = text;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s += 3;
#define CLOSE_ENTRY()                                                    \
    do {                                                                 \
        if (key) {                                                       \
            if (vals.n == 0)                                             \
                warn("%s: attribute value missing for '%s'", path, key); \
            else                                                         \
                dispatch(&ps, key, &vals);                               \
        }                                                                \
        free(key);                                                       \
        key = NULL;                                                      \
        sl_free(&vals);                                                  \
    } while (0)
    while (*s) {
        char *nl = strchr(s, '\n');
        size_t n = nl ? (size_t)(nl - s) : strlen(s);
        char *line = xstrndup(s, n);
        s += n + (nl ? 1 : 0);
        size_t ln = strlen(line);
        while (ln && (line[ln - 1] == '\r'))
            line[--ln] = 0;
        char *t = trim_dup(line, ln);
        if (line[0] == '#') {
            /* comment */
        } else if (!*t) {
            CLOSE_ENTRY();
        } else if (isspace((unsigned char)line[0])) {
            if (!key)
                warn("%s: continuation line without an entry ignored", path);
            else
                sl_add(&vals, strcmp(t, ".") == 0 ? NULL : t);
        } else {
            CLOSE_ENTRY();
            char *colon = strchr(t, ':');
            if (!colon || colon == t) {
                warn("%s: line invalid: %s", path, t);
            } else {
                key = trim_dup(t, colon - t);
                for (char *p = key; *p; p++)
                    *p = tolower((unsigned char)*p);
                char *v = trim_dup(colon + 1, strlen(colon + 1));
                if (*v)
                    sl_add(&vals, v);
                free(v);
            }
        }
        free(t);
        free(line);
    }
    CLOSE_ENTRY();
#undef CLOSE_ENTRY
    free(ps.file_colls);
    free(dir);
    free(text);
}

/* ------------------------------------------------------------------ scanning */

static char *config_dir(void)
{
    const char *x = getenv("XDG_CONFIG_HOME");
    if (x && *x)
        return fmt("%s/pegasus-frontend", x);
    return fmt("%s/.config/pegasus-frontend", getenv("HOME") ? getenv("HOME") : "");
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static bool is_metafile_name(const char *n)
{
    size_t l = strlen(n);
    return !strcmp(n, "metadata.pegasus.txt") || !strcmp(n, "metadata.txt") ||
           (l > 21 && !strcmp(n + l - 21, ".metadata.pegasus.txt")) ||
           (l > 13 && !strcmp(n + l - 13, ".metadata.txt"));
}

static void accept_file(Library *L, Filter *f, const char *raw)
{
    char *p = clean_path(raw);
    Game *g = lib_file_game(L, p);
    if (!g) {
        char *t = pretty_name(p);
        g = new_game(L, t);
        free(t);
        game_add_file(L, g, p);
    }
    game_add_to(g, f->coll);
    free(p);
}

static void scan_entry(Library *L, Filter *f, const char *path)
{
    char ext[64];
    snprintf(ext, sizeof ext, "%s", suffix_of(path));
    for (char *p = ext; *p; p++)
        *p = tolower((unsigned char)*p);
    if (sl_has(&f->exts, ext))
        accept_file(L, f, path);
}

static void scan_recursive(Library *L, Filter *f, const char *dir, int depth)
{
    if (depth > 32)
        return;
    DIR *d = opendir(dir);
    if (!d)
        return;
    StrList names = { 0 };
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.')
            sl_add(&names, e->d_name);
    closedir(d);
    qsort(names.v, names.n, sizeof *names.v, cmp_str);
    for (int i = 0; i < names.n; i++) {
        char *p = fmt("%s/%s", dir, names.v[i]);
        struct stat st;
        if (stat(p, &st) == 0) {
            if (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))
                scan_entry(L, f, p);
            if (S_ISDIR(st.st_mode))
                scan_recursive(L, f, p, depth + 1);
        }
        free(p);
    }
    sl_free(&names);
}

static void run_filter(Library *L, Filter *f)
{
    if (f->exts.n == 0)
        return;
    for (int i = 0; i < f->dirs.n; i++) {
        const char *dir = f->dirs.v[i];
        DIR *d = opendir(dir);
        if (!d)
            continue;
        StrList names = { 0 };
        struct dirent *e;
        while ((e = readdir(d)))
            if (e->d_name[0] != '.')
                sl_add(&names, e->d_name);
        closedir(d);
        qsort(names.v, names.n, sizeof *names.v, cmp_str);
        for (int k = 0; k < names.n; k++) {
            char *p = fmt("%s/%s", dir, names.v[k]);
            struct stat st;
            if (stat(p, &st) == 0) {
                if (S_ISREG(st.st_mode))
                    scan_entry(L, f, p);
                else if (S_ISDIR(st.st_mode) && strcmp(names.v[k], "media") != 0)
                    scan_recursive(L, f, p, 0);
            }
            free(p);
        }
        sl_free(&names);
    }
}

/* media/<file stem or title>/boxFront.{png,jpg,webp,apng} */
static void scan_media(Library *L, char ***keys, Game ***vals, size_t *cap, const char *root, const char *base,
                       const char *dir, int depth)
{
    if (depth > 16)
        return;
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char *p = fmt("%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(p, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                scan_media(L, keys, vals, cap, root, base, p, depth + 1);
            } else if (S_ISREG(st.st_mode)) {
                const char *suf = suffix_of(p);
                char *stem = stem_of(p);
                if (is_boxfront_name(stem) && (!strcmp(suf, "png") || !strcmp(suf, "jpg") ||
                                               !strcmp(suf, "webp") || !strcmp(suf, "apng"))) {
                    /* key: the file's dir with the media root replaced by its parent */
                    char *key = fmt("%s%s", base, dir + strlen(root));
                    Game *g = map_get(*keys, *vals, *cap, key);
                    if (g && !g->boxfront)
                        g->boxfront = clean_path(p);
                    free(key);
                }
                free(stem);
            }
        }
        free(p);
    }
    closedir(d);
}

static void find_media(Library *L)
{
    char **keys = NULL;
    Game **vals = NULL;
    size_t cap = 0, n = 0;
    for (int i = 0; i < L->ngames; i++) {
        Game *g = L->games[i];
        for (int k = 0; k < g->files.n; k++) {
            char *dir = dir_of(g->files.v[k]);
            char *stem = stem_of(g->files.v[k]);
            char *a = fmt("%s/%s", dir, stem);
            map_put(&keys, &vals, &cap, &n, a, g, true);
            if (g->title) {
                char *b = fmt("%s/%s", dir, g->title);
                map_put(&keys, &vals, &cap, &n, b, g, true);
                free(b);
            }
            free(a);
            free(stem);
            free(dir);
        }
    }
    StrList roots = { 0 };
    for (int i = 0; i < L->nfilters; i++)
        for (int k = 0; k < L->filters[i].dirs.n; k++)
            sl_add_unique(&roots, L->filters[i].dirs.v[k]);
    for (int i = 0; i < roots.n; i++) {
        const char *names[] = { "media", ".media" };
        for (int k = 0; k < 2; k++) {
            char *root = fmt("%s/%s", roots.v[i], names[k]);
            if (is_dir(root))
                scan_media(L, &keys, &vals, &cap, root, roots.v[i], root, 0);
            free(root);
        }
    }
    sl_free(&roots);
    map_free(keys, vals, cap);
}

static UCollator *collator;

static int coll_cmp(const char *a, const char *b)
{
    if (!a)
        a = "";
    if (!b)
        b = "";
    if (collator) {
        UErrorCode st = U_ZERO_ERROR;
        UCollationResult r = ucol_strcollUTF8(collator, a, -1, b, -1, &st);
        if (U_SUCCESS(st))
            return r == UCOL_LESS ? -1 : r == UCOL_GREATER ? 1 : 0;
    }
    return strcoll(a, b);
}

static int cmp_game(const void *a, const void *b)
{
    const Game *x = *(Game *const *)a, *y = *(Game *const *)b;
    return coll_cmp(x->sort_by, y->sort_by);
}

static int cmp_coll(const void *a, const void *b)
{
    return coll_cmp((*(Collection *const *)a)->name, (*(Collection *const *)b)->name);
}

static void load_stats(Library *L, const char *cfg)
{
    char *path = fmt("%s/stats.db", cfg);
    sqlite3 *db;
    if (exists(path) && sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
        sqlite3_stmt *st;
        if (sqlite3_prepare_v2(db,
                               "SELECT paths.path, plays.start_time, plays.duration FROM plays "
                               "INNER JOIN paths ON plays.path_id = paths.id",
                               -1, &st, NULL) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                const char *p = (const char *)sqlite3_column_text(st, 0);
                Game *g = p ? lib_file_game(L, p) : NULL;
                if (!g)
                    continue;
                long long start = sqlite3_column_int64(st, 1), dur = sqlite3_column_int64(st, 2);
                g->play_count++;
                g->play_time += dur > 0 ? dur : 0;
                if (start + dur > g->last_played)
                    g->last_played = start + dur;
            }
            sqlite3_finalize(st);
        } else {
            warn("stats.db: %s", sqlite3_errmsg(db));
        }
        sqlite3_close(db);
    }
    free(path);
}

static void load_favorites(Library *L, const char *cfg)
{
    char *path = fmt("%s/favorites.txt", cfg);
    char *text = read_file(path, NULL);
    for (char *s = text; s && *s;) {
        char *nl = strchr(s, '\n');
        size_t n = nl ? (size_t)(nl - s) : strlen(s);
        if (n && s[0] != '#') {
            char *line = xstrndup(s, n);
            char *p = abs_path(cfg, line);
            Game *g = lib_file_game(L, p);
            if (g)
                g->favorite = true;
            free(p);
            free(line);
        }
        s += n + (nl ? 1 : 0);
    }
    free(text);
    free(path);
}

static void save_favorites(Library *L, const char *cfg)
{
    size_t cap = 256, n = 0;
    char *b = xmalloc(cap);
    const char *head = "# List of favorites, one path per line\n";
    strcpy(b, head);
    n = strlen(head);
    for (int i = 0; i < L->ngames; i++) {
        Game *g = L->games[i];
        if (!g->favorite)
            continue;
        for (int k = 0; k < g->files.n; k++) {
            size_t l = strlen(g->files.v[k]);
            if (n + l + 2 > cap)
                b = xrealloc(b, cap = (n + l + 2) * 2);
            memcpy(b + n, g->files.v[k], l);
            n += l;
            b[n++] = '\n';
            b[n] = 0;
        }
    }
    char *path = fmt("%s/favorites.txt", cfg);
    write_file(path, b);
    free(path);
    free(b);
}

static void load_library(Library *L)
{
    char *cfg = config_dir();
    char *gd = fmt("%s/game_dirs.txt", cfg);
    char *text = read_file(gd, NULL);
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd))
        strcpy(cwd, "/");
    StrList dirs = { 0 }, metafiles = { 0 };
    for (char *s = text; s && *s;) {
        char *nl = strchr(s, '\n');
        size_t n = nl ? (size_t)(nl - s) : strlen(s);
        if (n && s[0] != '#') {
            char *line = xstrndup(s, n);
            char *p = abs_path(cwd, line);
            if (is_dir(p))
                sl_add_unique(&dirs, p);
            free(p);
            free(line);
        }
        s += n + (nl ? 1 : 0);
    }
    free(text);
    for (int i = 0; i < dirs.n; i++) {
        DIR *d = opendir(dirs.v[i]);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.' || !is_metafile_name(e->d_name))
                continue;
            char *p = fmt("%s/%s", dirs.v[i], e->d_name);
            if (is_file(p))
                sl_add_unique(&metafiles, p);
            free(p);
        }
        if (d)
            closedir(d);
    }
    qsort(metafiles.v, metafiles.n, sizeof *metafiles.v, cmp_str);
    for (int i = 0; i < metafiles.n; i++)
        parse_metafile(L, metafiles.v[i]);
    for (int i = 0; i < L->nfilters; i++)
        run_filter(L, &L->filters[i]);

    /* drop games without files, then empty collections */
    for (int i = 0; i < L->ncolls; i++) {
        Collection *c = L->colls[i];
        int o = 0;
        for (int k = 0; k < c->ngames; k++)
            if (c->games[k]->files.n)
                c->games[o++] = c->games[k];
        c->ngames = o;
    }
    int o = 0;
    for (int i = 0; i < L->ncolls; i++) {
        if (L->colls[i]->ngames) {
            L->colls[o++] = L->colls[i];
        } else {
            for (int k = 0; k < L->ngames; k++) { /* forget the pointer in its games */
                Game *g = L->games[k];
                for (int j = 0; j < g->ncolls; j++)
                    if (g->colls[j] == L->colls[i])
                        g->colls[j--] = g->colls[--g->ncolls];
            }
            free_collection(L->colls[i]);
        }
    }
    L->ncolls = o;
    for (int i = 0; i < L->ncolls; i++)
        if (!L->colls[i]->shortname) {
            L->colls[i]->shortname = xstrdup(L->colls[i]->name);
            for (char *p = L->colls[i]->shortname; *p; p++)
                *p = tolower((unsigned char)*p);
        }

    find_media(L);
    load_stats(L, cfg);
    load_favorites(L, cfg);

    qsort(L->colls, L->ncolls, sizeof *L->colls, cmp_coll);
    for (int i = 0; i < L->ncolls; i++)
        qsort(L->colls[i]->games, L->colls[i]->ngames, sizeof *L->colls[i]->games, cmp_game);

    sl_free(&dirs);
    sl_free(&metafiles);
    free(gd);
    free(cfg);
}

/* ------------------------------------------------------------------ theme memory (Pegasus grid theme) */

static char *json_unescape(const char *s, int n)
{
    char *o = xmalloc(n * 3 + 1), *w = o;
    for (int i = 0; i < n; i++) {
        if (s[i] != '\\' || i + 1 >= n) {
            *w++ = s[i];
            continue;
        }
        char c = s[++i];
        switch (c) {
        case 'n': *w++ = '\n'; break;
        case 't': *w++ = '\t'; break;
        case 'r': *w++ = '\r'; break;
        case 'b': *w++ = '\b'; break;
        case 'f': *w++ = '\f'; break;
        case 'u': {
            unsigned cp = 0;
            if (i + 4 < n && sscanf(s + i + 1, "%4x", &cp) == 1) {
                i += 4;
                if (cp < 0x80) {
                    *w++ = cp;
                } else if (cp < 0x800) {
                    *w++ = 0xC0 | cp >> 6, *w++ = 0x80 | (cp & 0x3F);
                } else {
                    *w++ = 0xE0 | cp >> 12, *w++ = 0x80 | (cp >> 6 & 0x3F), *w++ = 0x80 | (cp & 0x3F);
                }
            }
            break;
        }
        default: *w++ = c;
        }
    }
    *w = 0;
    return o;
}

static void read_memory(char **coll, char **game)
{
    char *cfg = config_dir();
    char *path = fmt("%s/theme_settings/pegasus-theme-grid.json", cfg);
    size_t len;
    char *js = read_file(path, &len);
    if (js) {
        jsmn_parser p;
        jsmntok_t tok[64];
        jsmn_init(&p);
        int n = jsmn_parse(&p, js, len, tok, 64);
        if (n > 0 && tok[0].type == JSMN_OBJECT) {
            for (int i = 1; i + 1 < n; i += 2) {
                jsmntok_t *k = &tok[i], *v = &tok[i + 1];
                if (k->type != JSMN_STRING || v->type != JSMN_STRING) {
                    if (v->type == JSMN_OBJECT || v->type == JSMN_ARRAY)
                        break; /* nested values: not ours, stop */
                    continue;
                }
                char *key = json_unescape(js + k->start, k->end - k->start);
                char *val = json_unescape(js + v->start, v->end - v->start);
                if (!strcmp(key, "collection"))
                    set_str(coll, val);
                else if (!strcmp(key, "game"))
                    set_str(game, val);
                free(key);
                free(val);
            }
        }
        free(js);
    }
    free(path);
    free(cfg);
}

static void json_escape(char **b, size_t *n, size_t *cap, const char *s)
{
    for (; *s; s++) {
        if (*n + 8 > *cap)
            *b = xrealloc(*b, *cap = *cap * 2 + 16);
        unsigned char c = *s;
        if (c == '"' || c == '\\') {
            (*b)[(*n)++] = '\\', (*b)[(*n)++] = c;
        } else if (c < 0x20) {
            *n += sprintf(*b + *n, "\\u%04x", c);
        } else {
            (*b)[(*n)++] = c;
        }
    }
    (*b)[*n] = 0;
}

/* Same as Pegasus' QJsonDocument::Compact: {"collection":"...","game":"..."} (sorted keys, no newline). */
static void write_memory(const char *coll, const char *game)
{
    char *cfg = config_dir();
    char *dir = fmt("%s/theme_settings", cfg);
    mkdir(dir, 0755);
    size_t cap = 128, n = 0;
    char *b = xmalloc(cap);
    const char *parts[] = { "{\"collection\":\"", coll, "\",\"game\":\"", game, "\"}" };
    for (int i = 0; i < 5; i++) {
        if (i % 2) {
            json_escape(&b, &n, &cap, parts[i]);
        } else {
            size_t l = strlen(parts[i]);
            if (n + l + 1 > cap)
                b = xrealloc(b, cap = (n + l + 1) * 2);
            memcpy(b + n, parts[i], l + 1);
            n += l;
        }
    }
    char *path = fmt("%s/pegasus-theme-grid.json", dir);
    write_file(path, b);
    free(path);
    free(b);
    free(dir);
    free(cfg);
}

/* ------------------------------------------------------------------ --list */

static void list_library(Library *L)
{
    for (int i = 0; i < L->ncolls; i++) {
        Collection *c = L->colls[i];
        printf("== %s (%s): %d games\n", c->name, c->shortname, c->ngames);
        for (int k = 0; k < c->ngames; k++) {
            Game *g = c->games[k];
            printf("  %-40s %s\n", g->title, g->files.v[0]);
            printf("      launch: %s\n", g->launch ? g->launch : "(none)");
            printf("      art: %s\n", g->boxfront ? g->boxfront : "(none)");
            if (g->play_count)
                printf("      played %d times, %lld s, last %lld\n", g->play_count, g->play_time, g->last_played);
            if (g->favorite)
                printf("      favourite\n");
        }
    }
}

/* ------------------------------------------------------------------ launching */

/* Pegasus' CommandTokenizer: whitespace-separated; a token starting with ' or " runs to the next
 * identical quote; quotes elsewhere are literal; tokens are trimmed; no escapes. */
static void tokenize(const char *s, StrList *out)
{
    size_t n = strlen(s), i = 0;
    while (i < n) {
        while (i < n && isspace((unsigned char)s[i]))
            i++;
        if (i >= n)
            break;
        size_t start = i;
        char *tok;
        if (s[i] == '"' || s[i] == '\'') {
            char q = s[i];
            const char *end = strchr(s + i + 1, q);
            size_t e = end ? (size_t)(end - s) + 1 : n;
            if (e - start > 1 && s[e - 1] == q && end)
                tok = trim_dup(s + start + 1, e - start - 2);
            else
                tok = trim_dup(s + start, e - start);
            i = e;
        } else {
            while (i < n && !isspace((unsigned char)s[i]))
                i++;
            tok = trim_dup(s + start, i - start);
        }
        sl_add(out, tok);
        free(tok);
    }
}

static char *replace_all(const char *s, const char *from, const char *to)
{
    size_t fl = strlen(from), tl = strlen(to), cap = strlen(s) + 1, n = 0;
    char *o = xmalloc(cap);
    while (*s) {
        if (strncmp(s, from, fl) == 0) {
            if (n + tl + 1 > cap)
                o = xrealloc(o, cap = (n + tl + 1) * 2);
            memcpy(o + n, to, tl);
            n += tl;
            s += fl;
        } else {
            if (n + 2 > cap)
                o = xrealloc(o, cap *= 2);
            o[n++] = *s++;
        }
    }
    o[n] = 0;
    return o;
}

static char *expand_vars(const char *tok, const char *file)
{
    char *dir = dir_of(file), *stem = stem_of(file);
    char *a = replace_all(tok, "{file.path}", file);
    char *b = replace_all(a, "{file.name}", name_of(file));
    char *c = replace_all(b, "{file.basename}", stem);
    char *d = replace_all(c, "{file.dir}", dir);
    free(a), free(b), free(c), free(dir), free(stem);
    /* {env.NAME} */
    char *p;
    while ((p = strstr(d, "{env."))) {
        char *e = strchr(p, '}');
        if (!e)
            break;
        char *name = xstrndup(p + 5, e - p - 5);
        const char *v = getenv(name);
        char *r = fmt("%.*s%s%s", (int)(p - d), d, v ? v : "", e + 1);
        free(name);
        free(d);
        d = r;
    }
    return d;
}

typedef struct {
    pid_t pid;
    Game *game;
    long long start;
    int fail_fd;
} Running;

static Uint32 EV_PAD, EV_EXIT;

static int waiter(void *arg)
{
    Running *r = arg;
    int status;
    while (waitpid(r->pid, &status, 0) < 0 && errno == EINTR)
        ;
    SDL_Event ev = { 0 };
    ev.type = EV_EXIT;
    SDL_PushEvent(&ev);
    return 0;
}

static bool launch(Running *r, Game *g)
{
    if (!g->launch || !g->files.n) {
        warn("%s: no launch command", g->title);
        return false;
    }
    const char *file = g->files.v[0];
    StrList toks = { 0 };
    tokenize(g->launch, &toks);
    if (!toks.n || !*toks.v[0]) {
        warn("%s: no launch command", g->title);
        sl_free(&toks);
        return false;
    }
    char **argv = xmalloc((toks.n + 1) * sizeof *argv);
    for (int i = 0; i < toks.n; i++)
        argv[i] = expand_vars(toks.v[i], file);
    argv[toks.n] = NULL;
    const char *base = g->basedir ? g->basedir : "/";
    if (strchr(argv[0], '/')) {
        char *a = abs_path(base, argv[0]);
        free(argv[0]);
        argv[0] = a;
    }
    char *wd;
    if (g->workdir && *g->workdir) {
        char *w = expand_vars(g->workdir, file);
        wd = abs_path(base, w);
        free(w);
    } else {
        wd = dir_of(strchr(argv[0], '/') ? argv[0] : file);
    }
    int pfd[2];
    if (pipe2(pfd, O_CLOEXEC) < 0)
        pfd[0] = pfd[1] = -1;
    pid_t pid = fork();
    if (pid == 0) {
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        if (chdir(wd) != 0) { /* like QProcess: start anyway */ }
        execvp(argv[0], argv);
        int e = errno;
        if (pfd[1] >= 0 && write(pfd[1], &e, sizeof e) < 0) { /* nothing to do */ }
        _exit(127);
    }
    if (pfd[1] >= 0)
        close(pfd[1]);
    bool ok = pid > 0;
    if (ok && pfd[0] >= 0) {
        int e;
        ssize_t got;
        while ((got = read(pfd[0], &e, sizeof e)) < 0 && errno == EINTR)
            ;
        if (got == sizeof e) {
            warn("%s: cannot start %s: %s", g->title, argv[0], strerror(e));
            waitpid(pid, NULL, 0);
            ok = false;
        }
    }
    if (pfd[0] >= 0)
        close(pfd[0]);
    if (ok) {
        r->pid = pid;
        r->game = g;
        r->start = time(NULL);
        SDL_Thread *t = SDL_CreateThread(waiter, "waiter", r);
        if (t)
            SDL_DetachThread(t);
    }
    for (int i = 0; i < toks.n; i++)
        free(argv[i]);
    free(argv);
    free(wd);
    sl_free(&toks);
    return ok;
}

/* Exactly Pegasus' statements (PlaytimeStats.cpp), so both write identical rows. */
static void record_play(const char *path, long long start, long long duration)
{
    char *cfg = config_dir();
    char *dbp = fmt("%s/stats.db", cfg);
    sqlite3 *db;
    if (sqlite3_open(dbp, &db) != SQLITE_OK) {
        warn("stats.db: cannot open");
        goto out;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
    sqlite3_exec(db,
                 "CREATE TABLE IF NOT EXISTS paths(id INTEGER PRIMARY KEY,path TEXT UNIQUE NOT NULL);"
                 "CREATE TABLE IF NOT EXISTS plays(id INTEGER PRIMARY KEY,path_id INTEGER NOT NULL "
                 "REFERENCES plays(id),start_time INTEGER NOT NULL,duration INTEGER NOT NULL)",
                 NULL, NULL, NULL);
    sqlite3_stmt *st;
    long long id = -1;
    if (sqlite3_prepare_v2(db, "SELECT id FROM paths WHERE path = ?", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW)
            id = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    if (id < 0 && sqlite3_prepare_v2(db, "INSERT INTO paths VALUES(null, ?)", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_DONE)
            id = sqlite3_last_insert_rowid(db);
        sqlite3_finalize(st);
    }
    bool ok = false;
    if (id >= 0 && sqlite3_prepare_v2(db, "INSERT INTO plays VALUES(null, ?, ?, ?)", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, id);
        sqlite3_bind_int64(st, 2, start);
        sqlite3_bind_int64(st, 3, duration);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
    }
    sqlite3_exec(db, ok ? "COMMIT" : "ROLLBACK", NULL, NULL, NULL);
    if (!ok)
        warn("stats.db: could not record play: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
out:
    free(dbp);
    free(cfg);
}

/* ------------------------------------------------------------------ gamepad (evdev thread) */

enum { A_NONE, A_UP, A_DOWN, A_LEFT, A_RIGHT, A_PGUP, A_PGDN, A_LAUNCH, A_FAV, A_BACK };

static int pad_quit_pipe[2] = { -1, -1 };

static void push_pad(int action, int pressed)
{
    SDL_Event ev = { 0 };
    ev.type = EV_PAD;
    ev.user.code = action;
    ev.user.data1 = (void *)(intptr_t)pressed;
    SDL_PushEvent(&ev);
}

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
        if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) >= 0 && strcmp(name, PAD_NAME) == 0)
            found = fd;
        else
            close(fd);
    }
    if (d)
        closedir(d);
    return found;
}

static int pad_thread(void *unused)
{
    (void)unused;
    int watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    inotify_add_watch(watch, "/dev/input", IN_CREATE | IN_ATTRIB);
    int pad = open_pad();
    int hat_y = 0, hat_x = 0, stick = 0, lt = 0, rt = 0;
    for (;;) {
        struct pollfd fds[3] = {
            { .fd = pad_quit_pipe[0], .events = POLLIN },
            { .fd = watch, .events = POLLIN },
            { .fd = pad, .events = POLLIN },
        };
        if (poll(fds, 3, -1) < 0)
            continue;
        if (fds[0].revents)
            break;
        if (fds[1].revents) {
            char buf[4096];
            while (read(watch, buf, sizeof buf) > 0)
                ;
            if (pad < 0)
                pad = open_pad();
        }
        if (pad >= 0 && fds[2].revents) {
            struct input_event ev;
            ssize_t n;
            while ((n = read(pad, &ev, sizeof ev)) == sizeof ev) {
                if (ev.type == EV_KEY && ev.value != 2) {
                    int a = ev.code == BTN_A ? A_LAUNCH : ev.code == BTN_Y ? A_FAV : ev.code == BTN_B ? A_BACK
                          : ev.code == BTN_TL ? A_LEFT : ev.code == BTN_TR ? A_RIGHT : A_NONE;
                    if (a)
                        push_pad(a, ev.value);
                } else if (ev.type == EV_ABS) {
                    if (ev.code == ABS_HAT0Y && ev.value != hat_y) {
                        if (hat_y)
                            push_pad(hat_y < 0 ? A_UP : A_DOWN, 0);
                        if (ev.value)
                            push_pad(ev.value < 0 ? A_UP : A_DOWN, 1);
                        hat_y = ev.value;
                    } else if (ev.code == ABS_HAT0X && ev.value != hat_x) {
                        if (hat_x)
                            push_pad(hat_x < 0 ? A_LEFT : A_RIGHT, 0);
                        if (ev.value)
                            push_pad(ev.value < 0 ? A_LEFT : A_RIGHT, 1);
                        hat_x = ev.value;
                    } else if (ev.code == ABS_Y) { /* left stick, with hysteresis */
                        int s = ev.value < -20000 ? -1 : ev.value > 20000 ? 1 : (ev.value > -12000 && ev.value < 12000) ? 0 : stick;
                        if (s != stick) {
                            if (stick)
                                push_pad(stick < 0 ? A_UP : A_DOWN, 0);
                            if (s)
                                push_pad(s < 0 ? A_UP : A_DOWN, 1);
                            stick = s;
                        }
                    } else if (ev.code == ABS_Z || ev.code == ABS_RZ) {
                        int *t = ev.code == ABS_Z ? &lt : &rt;
                        int on = ev.value > 160 ? 1 : ev.value < 64 ? 0 : *t;
                        if (on != *t) {
                            push_pad(ev.code == ABS_Z ? A_PGUP : A_PGDN, on);
                            *t = on;
                        }
                    }
                }
            }
            if (n < 0 && errno != EAGAIN) { /* pad gone (screen off, resume): inotify brings it back */
                close(pad);
                pad = -1;
                hat_x = hat_y = stick = lt = rt = 0;
            }
        }
    }
    if (pad >= 0)
        close(pad);
    close(watch);
    return 0;
}

/* ------------------------------------------------------------------ UI */

typedef struct {
    SDL_Color base, mantle, surface0, surface1, text, subtext, overlay, accent, fav;
} Palette;

static const Palette P = {
    .base = { 0x1e, 0x1e, 0x2e, 255 },
    .mantle = { 0x11, 0x11, 0x1b, 255 },
    .surface0 = { 0x31, 0x32, 0x44, 255 },
    .surface1 = { 0x45, 0x47, 0x5a, 255 },
    .text = { 0xcd, 0xd6, 0xf4, 255 },
    .subtext = { 0xa6, 0xad, 0xc8, 255 },
    .overlay = { 0x6c, 0x70, 0x86, 255 },
    .accent = { 0x89, 0xb4, 0xfa, 255 },
    .fav = { 0xf9, 0xe2, 0xaf, 255 },
};

typedef struct {
    char *path;
    SDL_Texture *tex; /* NULL: failed */
    int w, h;
    int max; /* the size it was scaled to fit */
    unsigned long used;
} ImgEntry;

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    TTF_TextEngine *eng;
    TTF_Font *f_tab, *f_row, *f_title, *f_meta, *f_desc;
    Library lib;
    Collection recent, favs;
    Collection **tabs;
    int ntabs, tab;
    int *sel, *top; /* per tab */
    ImgEntry thumbs[256], bigs[4];
    unsigned long tick;
    Running run;
    bool running;
    int held; /* repeating action */
    Uint64 next_repeat;
    Uint64 last_move;
    bool dirty;
} App;

static SDL_Texture *load_image(App *a, const char *path, int max_w, int max_h, int *w, int *h)
{
    SDL_Surface *s = IMG_Load(path);
    if (!s) {
        warn("%s: %s", path, SDL_GetError());
        return NULL;
    }
    float k = SDL_min((float)max_w / s->w, (float)max_h / s->h);
    if (k < 1.0f) {
        int nw = SDL_max(1, (int)(s->w * k)), nh = SDL_max(1, (int)(s->h * k));
        SDL_Surface *t = SDL_ScaleSurface(s, nw, nh, SDL_SCALEMODE_LINEAR);
        if (t) {
            SDL_DestroySurface(s);
            s = t;
        }
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(a->ren, s);
    *w = s->w;
    *h = s->h;
    SDL_DestroySurface(s);
    return tex;
}

/* Image cache: thumbs (many, small) and bigs (few, large); least recently used is evicted. */
static ImgEntry *cache_get(App *a, ImgEntry *tab, int n, const char *path, int max, int max_h, bool load)
{
    ImgEntry *lru = &tab[0];
    for (int i = 0; i < n; i++) {
        if (tab[i].path && tab[i].max == max && strcmp(tab[i].path, path) == 0) {
            tab[i].used = ++a->tick;
            return &tab[i];
        }
        if (!tab[i].path || tab[i].used < lru->used)
            lru = &tab[i];
    }
    if (!load)
        return NULL;
    free(lru->path);
    if (lru->tex)
        SDL_DestroyTexture(lru->tex);
    lru->path = xstrdup(path);
    lru->max = max;
    lru->tex = load_image(a, path, max, max_h, &lru->w, &lru->h);
    lru->used = ++a->tick;
    return lru;
}

static void cache_clear(ImgEntry *tab, int n)
{
    for (int i = 0; i < n; i++) {
        free(tab[i].path);
        if (tab[i].tex)
            SDL_DestroyTexture(tab[i].tex);
        memset(&tab[i], 0, sizeof tab[i]);
    }
}

static void fill(App *a, SDL_Color c, float x, float y, float w, float h)
{
    SDL_SetRenderDrawColor(a->ren, c.r, c.g, c.b, c.a);
    SDL_FRect r = { x, y, w, h };
    SDL_RenderFillRect(a->ren, &r);
}

/* Draws text; returns its height. wrap > 0 wraps at that width. */
static int text(App *a, TTF_Font *f, const char *s, SDL_Color c, float x, float y, int wrap, int *out_w)
{
    if (!s || !*s) {
        if (out_w)
            *out_w = 0;
        return 0;
    }
    TTF_Text *t = TTF_CreateText(a->eng, f, s, 0);
    if (!t)
        return 0;
    if (wrap > 0)
        TTF_SetTextWrapWidth(t, wrap);
    TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
    int w = 0, h = 0;
    TTF_GetTextSize(t, &w, &h);
    TTF_DrawRendererText(t, x, y);
    TTF_DestroyText(t);
    if (out_w)
        *out_w = w;
    return h;
}

static int text_width(TTF_Font *f, const char *s)
{
    int w = 0, h = 0;
    TTF_GetStringSize(f, s, 0, &w, &h);
    return w;
}

static Collection *cur_coll(App *a)
{
    return a->ntabs ? a->tabs[a->tab] : NULL;
}

static Game *cur_game(App *a)
{
    Collection *c = cur_coll(a);
    return c && c->ngames ? c->games[a->sel[a->tab]] : NULL;
}

static int cmp_recent(const void *x, const void *y)
{
    const Game *a = *(Game *const *)x, *b = *(Game *const *)y;
    return a->last_played < b->last_played ? 1 : a->last_played > b->last_played ? -1 : 0;
}

static void build_tabs(App *a)
{
    Library *L = &a->lib;
    free(a->recent.games);
    free(a->favs.games);
    a->recent = (Collection){ .name = "Recent", .virtual_ = true };
    a->favs = (Collection){ .name = "Favourites", .virtual_ = true };
    for (int i = 0; i < L->ncolls; i++) /* games in a collection: each once */
        for (int k = 0; k < L->colls[i]->ngames; k++) {
            Game *g = L->colls[i]->games[k];
            if (g->colls[0] != L->colls[i])
                continue;
            Collection *v[2] = { g->last_played ? &a->recent : NULL, g->favorite ? &a->favs : NULL };
            for (int j = 0; j < 2; j++)
                if (v[j]) {
                    if (v[j]->ngames == v[j]->cap) {
                        v[j]->cap = v[j]->cap ? v[j]->cap * 2 : 16;
                        v[j]->games = xrealloc(v[j]->games, v[j]->cap * sizeof *v[j]->games);
                    }
                    v[j]->games[v[j]->ngames++] = g;
                }
        }
    qsort(a->recent.games, a->recent.ngames, sizeof(Game *), cmp_recent);
    if (a->recent.ngames > RECENT_MAX)
        a->recent.ngames = RECENT_MAX;
    qsort(a->favs.games, a->favs.ngames, sizeof(Game *), cmp_game);
    free(a->tabs);
    free(a->sel);
    free(a->top);
    a->tabs = xmalloc((L->ncolls + 2) * sizeof *a->tabs);
    a->ntabs = 0;
    if (a->recent.ngames)
        a->tabs[a->ntabs++] = &a->recent;
    if (a->favs.ngames)
        a->tabs[a->ntabs++] = &a->favs;
    for (int i = 0; i < L->ncolls; i++)
        a->tabs[a->ntabs++] = L->colls[i];
    a->sel = calloc(a->ntabs + 1, sizeof *a->sel);
    a->top = calloc(a->ntabs + 1, sizeof *a->top);
    a->tab = 0;
}

/* Select a collection by name and a game in it by title or file path (either may be NULL). */
static void select_game(App *a, const char *coll, const char *title, const char *file)
{
    for (int i = 0; i < a->ntabs; i++) {
        Collection *c = a->tabs[i];
        if (!coll || strcmp(c->name, coll) != 0)
            continue;
        a->tab = i;
        for (int k = 0; k < c->ngames; k++) {
            Game *g = c->games[k];
            if ((title && g->title && !strcmp(g->title, title)) || (file && !strcmp(g->files.v[0], file))) {
                a->sel[i] = k;
                break;
            }
        }
        return;
    }
}

static void reload(App *a)
{
    char *coll = NULL, *file = NULL;
    if (cur_coll(a))
        coll = xstrdup(cur_coll(a)->name);
    if (cur_game(a))
        file = xstrdup(cur_game(a)->files.v[0]);
    lib_free(&a->lib);
    load_library(&a->lib);
    build_tabs(a);
    select_game(a, coll, NULL, file);
    free(coll);
    free(file);
    a->dirty = true;
}

static void move(App *a, int d)
{
    Collection *c = cur_coll(a);
    if (!c || !c->ngames)
        return;
    int s = a->sel[a->tab] + d;
    if (s < 0)
        s = d < -1 ? 0 : c->ngames - 1; /* single steps wrap, pages stop */
    if (s >= c->ngames)
        s = d > 1 ? c->ngames - 1 : 0;
    a->sel[a->tab] = s;
    a->last_move = SDL_GetTicks();
    a->dirty = true;
}

static void switch_tab(App *a, int d)
{
    if (!a->ntabs)
        return;
    a->tab = (a->tab + d + a->ntabs) % a->ntabs;
    a->last_move = 0;
    a->dirty = true;
}

static void format_duration(char *b, size_t n, long long s)
{
    if (s < 60)
        snprintf(b, n, "under a minute");
    else if (s < 3600)
        snprintf(b, n, "%lld min", s / 60);
    else
        snprintf(b, n, "%lld h %lld min", s / 3600, s / 60 % 60);
}

static void format_date(char *b, size_t n, long long t)
{
    time_t now = time(NULL), tt = t;
    struct tm a, c;
    localtime_r(&now, &c);
    localtime_r(&tt, &a);
    if (a.tm_year == c.tm_year && a.tm_yday == c.tm_yday)
        snprintf(b, n, "today");
    else if (a.tm_year == c.tm_year && a.tm_yday == c.tm_yday - 1)
        snprintf(b, n, "yesterday");
    else
        strftime(b, n, a.tm_year == c.tm_year ? "%-d %b" : "%-d %b %Y", &a);
}

static void join(char *b, size_t n, const StrList *l)
{
    b[0] = 0;
    for (int i = 0; i < l->n; i++) {
        if (i)
            strncat(b, ", ", n - strlen(b) - 1);
        strncat(b, l->v[i], n - strlen(b) - 1);
    }
}

static int render(App *a)
{
    int W, H;
    SDL_GetRenderOutputSize(a->ren, &W, &H);
    int need_more = 0; /* ms until something else should be drawn (0: nothing pending) */
    fill(a, P.base, 0, 0, W, H);

    /* tabs */
    const int TAB_H = 56;
    fill(a, P.mantle, 0, 0, W, TAB_H);
    int x = 12, sel_x0 = 0, sel_x1 = 0;
    int *tw = xmalloc((a->ntabs + 1) * sizeof *tw);
    for (int i = 0; i < a->ntabs; i++) {
        tw[i] = text_width(a->f_tab, a->tabs[i]->name) + 32;
        if (i == a->tab)
            sel_x0 = x, sel_x1 = x + tw[i];
        x += tw[i] + 6;
    }
    int shift = 0;
    if (sel_x1 > W - 12)
        shift = sel_x1 - (W - 12);
    x = 12 - shift;
    for (int i = 0; i < a->ntabs; i++) {
        bool on = i == a->tab;
        if (on)
            fill(a, P.surface1, x, 8, tw[i], TAB_H - 16);
        text(a, a->f_tab, a->tabs[i]->name, on ? P.text : P.overlay, x + 16, 14, 0, NULL);
        x += tw[i] + 6;
    }
    free(tw);
    (void)sel_x0;

    Collection *c = cur_coll(a);
    if (!c || !c->ngames) {
        text(a, a->f_title, "No games found", P.subtext, 40, TAB_H + 40, 0, NULL);
        SDL_RenderPresent(a->ren);
        return 0;
    }

    /* list */
    const int ROW = 52, LW = W * 46 / 100, TOP = TAB_H + 8, THUMB = 40;
    int rows = (H - TOP - 8) / ROW;
    if (rows < 1)
        rows = 1;
    int *sel = &a->sel[a->tab], *top = &a->top[a->tab];
    if (*sel >= c->ngames)
        *sel = c->ngames - 1;
    if (*sel < *top)
        *top = *sel;
    if (*sel >= *top + rows)
        *top = *sel - rows + 1;
    if (*top > c->ngames - rows)
        *top = SDL_max(0, c->ngames - rows);
    int to_load = 0;
    for (int r = 0; r < rows && *top + r < c->ngames; r++) {
        Game *g = c->games[*top + r];
        int y = TOP + r * ROW;
        bool on = *top + r == *sel;
        if (on) {
            fill(a, P.surface0, 0, y, LW, ROW);
            fill(a, P.accent, 0, y, 5, ROW);
        }
        if (g->boxfront) {
            ImgEntry *e = cache_get(a, a->thumbs, SDL_arraysize(a->thumbs), g->boxfront, THUMB, THUMB + 4, false);
            if (!e && !to_load) { /* one decode per frame keeps the list responsive */
                e = cache_get(a, a->thumbs, SDL_arraysize(a->thumbs), g->boxfront, THUMB, THUMB + 4, true);
                to_load = 1;
            } else if (!e) {
                need_more = 1;
            }
            if (e && e->tex) {
                SDL_FRect d = { 16 + (THUMB - e->w) / 2.0f, y + (ROW - e->h) / 2.0f, e->w, e->h };
                SDL_RenderTexture(a->ren, e->tex, NULL, &d);
            }
        }
        SDL_Rect clip = { 0, y, LW - 14, ROW };
        SDL_SetRenderClipRect(a->ren, &clip);
        int th = TTF_GetFontHeight(a->f_row);
        text(a, a->f_row, g->title, on ? P.text : P.subtext, 16 + THUMB + 14, y + (ROW - th) / 2.0f, 0, NULL);
        SDL_SetRenderClipRect(a->ren, NULL);
        if (g->favorite)
            fill(a, P.fav, LW - 10, y + ROW / 2 - 3, 6, 6);
    }
    if (c->ngames > rows) { /* scroll position */
        float bh = (float)(H - TOP) * rows / c->ngames, by = TOP + (float)(H - TOP) * *top / c->ngames;
        fill(a, P.surface1, LW - 3, by, 3, bh);
    }

    /* details */
    Game *g = c->games[*sel];
    int PX = LW + 28, PW = W - PX - 28, y = TOP + 12;
    int IW = PW * 44 / 100, IH = (H - TOP) * 58 / 100;
    int img_h = 0;
    if (g->boxfront) {
        Uint64 now = SDL_GetTicks();
        bool settled = !a->last_move || now - a->last_move >= BIG_IMAGE_DELAY_MS;
        ImgEntry *e = cache_get(a, a->bigs, SDL_arraysize(a->bigs), g->boxfront, IW, IH, settled);
        if (!e)
            need_more = SDL_max(need_more, (int)(BIG_IMAGE_DELAY_MS - (now - a->last_move)) + 1);
        if (e && e->tex) {
            float k = SDL_min((float)IW / e->w, (float)IH / e->h);
            if (k > 1.0f)
                k = SDL_min(k, 2.0f);
            SDL_FRect d = { PX, y, e->w * k, e->h * k };
            SDL_RenderTexture(a->ren, e->tex, NULL, &d);
            img_h = d.h;
        } else {
            img_h = IH;
            fill(a, P.surface0, PX, y, IW * 0.7f, IH);
        }
    }
    int tx = g->boxfront ? PX + IW + 24 : PX, tw2 = PX + PW - tx, ty = y;
    ty += text(a, a->f_title, g->title, P.text, tx, ty, tw2, NULL) + 10;
    char line[512], part[256];
    line[0] = 0;
    join(part, sizeof part, &g->developers);
    if (*part)
        snprintf(line, sizeof line, "%s", part);
    if (g->release && strlen(g->release) >= 4)
        snprintf(line + strlen(line), sizeof line - strlen(line), "%s%.4s", *line ? "  ·  " : "", g->release);
    if (g->players > 1)
        snprintf(line + strlen(line), sizeof line - strlen(line), "%s1-%d players", *line ? "  ·  " : "",
                 g->players);
    if (*line)
        ty += text(a, a->f_meta, line, P.subtext, tx, ty, tw2, NULL) + 6;
    join(part, sizeof part, &g->genres);
    if (*part)
        ty += text(a, a->f_meta, part, P.subtext, tx, ty, tw2, NULL) + 6;
    if (g->play_count) {
        char d1[64], d2[64];
        format_duration(d1, sizeof d1, g->play_time);
        format_date(d2, sizeof d2, g->last_played);
        snprintf(line, sizeof line, "Played %s, last %s", d1, d2);
    } else {
        snprintf(line, sizeof line, "Not played yet");
    }
    ty += text(a, a->f_meta, line, P.overlay, tx, ty, tw2, NULL) + 6;
    if (g->favorite)
        ty += text(a, a->f_meta, "Favourite", P.fav, tx, ty, tw2, NULL) + 6;
    const char *desc = g->description ? g->description : g->summary;
    if (desc) {
        int dy = SDL_max(y + img_h, ty) + 20;
        SDL_Rect clip = { PX, dy, PW, H - dy - 12 };
        if (clip.h > 20) {
            SDL_SetRenderClipRect(a->ren, &clip);
            text(a, a->f_desc, desc, P.subtext, PX, dy, PW, NULL);
            SDL_SetRenderClipRect(a->ren, NULL);
        }
    }
    SDL_RenderPresent(a->ren);
    return to_load ? 1 : need_more;
}

static void game_finished(App *a)
{
    long long dur = time(NULL) - a->run.start;
    if (dur < 0)
        dur = 0;
    record_play(a->run.game->files.v[0], a->run.start, dur);
    a->running = false;
    reload(a); /* metadata may have changed (installer, box art tool), and stats did */
}

static void start_game(App *a)
{
    Game *g = cur_game(a);
    if (!g || a->running)
        return;
    Collection *c = cur_coll(a);
    write_memory(c->virtual_ ? g->colls[0]->name : c->name, g->title);
    if (launch(&a->run, g))
        a->running = true;
    a->held = A_NONE;
}

static void toggle_favorite(App *a)
{
    Game *g = cur_game(a);
    if (!g)
        return;
    g->favorite = !g->favorite;
    char *cfg = config_dir();
    save_favorites(&a->lib, cfg);
    free(cfg);
    char *coll = xstrdup(cur_coll(a)->name), *file = xstrdup(g->files.v[0]);
    build_tabs(a);
    select_game(a, coll, NULL, file);
    if (!cur_coll(a) || strcmp(cur_coll(a)->name, coll) != 0)
        select_game(a, g->colls[0]->name, NULL, file);
    free(coll);
    free(file);
    a->dirty = true;
}

static void action(App *a, int act, bool repeat)
{
    switch (act) {
    case A_UP: move(a, -1); break;
    case A_DOWN: move(a, 1); break;
    case A_PGUP: move(a, -8); break;
    case A_PGDN: move(a, 8); break;
    case A_LEFT: if (!repeat) switch_tab(a, -1); break;
    case A_RIGHT: if (!repeat) switch_tab(a, 1); break;
    case A_LAUNCH: if (!repeat) start_game(a); break;
    case A_FAV: if (!repeat) toggle_favorite(a); break;
    }
}

int main(int argc, char **argv)
{
    UErrorCode st = U_ZERO_ERROR;
    collator = ucol_open("en_US", &st);
    if (U_FAILURE(st))
        collator = NULL;

    if (argc > 1 && strcmp(argv[1], "--list") == 0) {
        Library L = { 0 };
        load_library(&L);
        list_library(&L);
        lib_free(&L);
        return 0;
    }

    SDL_SetHint(SDL_HINT_APP_ID, "shelf");
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR, "0");
    if (!SDL_Init(SDL_INIT_VIDEO) || !TTF_Init()) {
        fprintf(stderr, "shelf: %s\n", SDL_GetError());
        return 1;
    }
    EV_PAD = SDL_RegisterEvents(2);
    EV_EXIT = EV_PAD + 1;

    static App app;
    App *a = &app;
    a->win = SDL_CreateWindow("Shelf", 1280, 720, SDL_WINDOW_RESIZABLE);
    a->ren = a->win ? SDL_CreateRenderer(a->win, NULL) : NULL;
    a->eng = a->ren ? TTF_CreateRendererTextEngine(a->ren) : NULL;
    if (!a->eng) {
        fprintf(stderr, "shelf: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(a->ren, 1);
    a->f_tab = TTF_OpenFont(FONT_BOLD, 22);
    a->f_row = TTF_OpenFont(FONT_REGULAR, 22);
    a->f_title = TTF_OpenFont(FONT_BOLD, 28);
    a->f_meta = TTF_OpenFont(FONT_REGULAR, 18);
    a->f_desc = TTF_OpenFont(FONT_REGULAR, 18);
    if (!a->f_tab || !a->f_row || !a->f_title || !a->f_meta || !a->f_desc) {
        fprintf(stderr, "shelf: fonts: %s\n", SDL_GetError());
        return 1;
    }

    load_library(&a->lib);
    build_tabs(a);
    char *mc = NULL, *mg = NULL;
    read_memory(&mc, &mg);
    select_game(a, mc, mg, NULL);
    free(mc);
    free(mg);

    if (pipe2(pad_quit_pipe, O_CLOEXEC) == 0) {
        SDL_Thread *t = SDL_CreateThread(pad_thread, "pad", NULL);
        if (t)
            SDL_DetachThread(t);
    }

    a->dirty = true;
    int pending = 0;
    for (bool quit = false; !quit;) {
        SDL_Event ev;
        int timeout = -1;
        Uint64 now = SDL_GetTicks();
        if (a->held && !a->running)
            timeout = a->next_repeat > now ? (int)(a->next_repeat - now) : 0;
        if (pending && !a->running)
            timeout = timeout < 0 ? pending : SDL_min(timeout, pending);
        if (a->dirty && !a->running)
            timeout = 0;
        bool got = timeout < 0 ? SDL_WaitEvent(&ev) : SDL_WaitEventTimeout(&ev, timeout);
        while (got) {
            if (ev.type == SDL_EVENT_QUIT) {
                quit = true;
            } else if (ev.type == EV_EXIT) {
                if (a->running)
                    game_finished(a);
            } else if (ev.type == EV_PAD) {
                int act = ev.user.code, down = (int)(intptr_t)ev.user.data1;
                if (a->running) {
                    /* the game has the pad */
                } else if (down) {
                    action(a, act, false);
                    if (act == A_UP || act == A_DOWN || act == A_PGUP || act == A_PGDN) {
                        a->held = act;
                        a->next_repeat = SDL_GetTicks() + REPEAT_DELAY_MS;
                    }
                } else if (act == a->held) {
                    a->held = A_NONE;
                }
            } else if (ev.type == SDL_EVENT_KEY_DOWN && !a->running) {
                SDL_Keycode k = ev.key.key;
                bool shift = ev.key.mod & SDL_KMOD_SHIFT;
                int act = k == SDLK_UP ? A_UP : k == SDLK_DOWN ? A_DOWN : k == SDLK_PAGEUP ? A_PGUP
                        : k == SDLK_PAGEDOWN ? A_PGDN : k == SDLK_LEFT ? A_LEFT : k == SDLK_RIGHT ? A_RIGHT
                        : (k == SDLK_RETURN || k == SDLK_KP_ENTER) ? A_LAUNCH : k == SDLK_F ? A_FAV : A_NONE;
                if (k == SDLK_TAB)
                    act = shift ? A_LEFT : A_RIGHT;
                if (k == SDLK_HOME && cur_coll(a))
                    move(a, -cur_coll(a)->ngames);
                else if (k == SDLK_END && cur_coll(a))
                    move(a, cur_coll(a)->ngames);
                else if (act)
                    action(a, act, ev.key.repeat && (act == A_LEFT || act == A_RIGHT || act == A_LAUNCH || act == A_FAV));
            } else if (ev.type == SDL_EVENT_MOUSE_WHEEL && !a->running) {
                move(a, ev.wheel.y > 0 ? -1 : 1);
            } else if (ev.type == SDL_EVENT_WINDOW_RESIZED || ev.type == SDL_EVENT_WINDOW_EXPOSED ||
                       ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                a->dirty = true;
            }
            got = SDL_PollEvent(&ev);
        }
        now = SDL_GetTicks();
        if (a->held && !a->running && now >= a->next_repeat) {
            action(a, a->held, true);
            a->next_repeat = now + REPEAT_RATE_MS;
        }
        if ((a->dirty || pending) && !a->running) {
            a->dirty = false;
            pending = render(a);
        }
    }

    if (pad_quit_pipe[1] >= 0 && write(pad_quit_pipe[1], "q", 1) < 0) { /* thread dies with us */ }
    cache_clear(a->thumbs, SDL_arraysize(a->thumbs));
    cache_clear(a->bigs, SDL_arraysize(a->bigs));
    TTF_DestroyRendererTextEngine(a->eng);
    SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    TTF_Quit();
    SDL_Quit();
    lib_free(&a->lib);
    if (collator)
        ucol_close(collator);
    return 0;
}

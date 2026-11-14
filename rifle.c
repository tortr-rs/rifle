/* rifle: coreutils in one C file, on c.h. cat, ls, cp and the rest are links to this program, it runs
 * whichever name it was called by. build.sh makes them in bin/, "rifle --install DIR" anywhere else. */
#include "c.h"

#define VERSION "0.2"

/* ---- messages ---- */

static const char *prog = "rifle";
static const char *tool_usage;
static const char *tool_about;
static int status;                   /* 1 once something failed; tools go on with the next file */
static int usage_status = 1;           /* env and a few others exit 125 on a bad option */

static void say(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s: ", prog);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
}
static void perr(const char *fmt, ...)
{
    va_list ap;
    int e = errno;
    fflush(stdout);
    fprintf(stderr, "%s: ", prog);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, ": %s\n", strerror(e));
}
static __attribute__((noreturn)) void die(int code, const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s: ", prog);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    exit(code);
}
static void try_help(void) { fprintf(stderr, "Try '%s --help' for more information.\n", prog); }
static __attribute__((noreturn)) void usage_error(void) { try_help(); exit(usage_status); }
static __attribute__((noreturn)) void missing_operand(void) { say("missing operand"); usage_error(); }
static __attribute__((noreturn)) void extra_operand(const char *s) { say("extra operand '%s'", s); usage_error(); }
static void print_usage(void)
{
    printf("Usage: %s %s\n%s\n", prog, tool_usage, tool_about);
}

static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) die(1, "memory exhausted"); return p; }
static void *xrealloc(void *p, size_t n) { p = realloc(p, n ? n : 1); if (!p) die(1, "memory exhausted"); return p; }
static char *xstrdup(const char *s) { char *d = xmalloc(strlen(s) + 1); return strcpy(d, s); }

static int write_all(int fd, const char *buf, size_t n)
{
    while (n) {
        ssize_t w = write(fd, buf, n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        buf += w; n -= w;
    }
    return 0;
}
static int finish_stdout(void)
{
    if (fflush(stdout) == EOF || ferror(stdout)) { say("write error: %s", strerror(errno ? errno : EIO)); return 1; }
    return 0;
}

/* ---- options ----
 * getopt_long, more or less: options between operands, -abc and -s5, --name=x, short forms of long names.
 * a leading + in the short list stops at the first operand. what's left lands in ops */

struct opt { const char *name; int arg; int val; };
static char **ops;
static int nops;
static char *optarg;
static int opt_i = 1, opt_c, opt_stop;

static void opts_reset(void) { opt_i = 1; opt_c = 0; opt_stop = 0; nops = 0; optarg = NULL; }

static int getopts(int argc, char **argv, const char *shorts, const struct opt *longs)
{
    int posix = shorts[0] == '+';
    if (posix) shorts++;
    if (!ops) ops = xmalloc((argc + 1) * sizeof *ops);
    for (;;) {
        char *a;
        if (opt_c == 0) {
            if (opt_i >= argc) { ops[nops] = NULL; return 0; }
            a = argv[opt_i];
            if (opt_stop || a[0] != '-' || !a[1]) {
                if (posix) opt_stop = 1;
                ops[nops++] = a; opt_i++;
                continue;
            }
            if (a[1] == '-') {
                static const struct opt builtin[] = { { "help", 0, -100 }, { "version", 0, -101 }, { NULL, 0, 0 } };
                const struct opt *o = NULL, *hit;
                char *eq = strchr(a + 2, '=');
                size_t l = eq ? (size_t)(eq - a - 2) : strlen(a + 2);
                int found = 0, exact = 0, pass;
                if (!a[2]) { opt_stop = 1; opt_i++; continue; }
                /* the tool's own options, then --help and --version. an exact name wins, else a start that fits
                 * exactly one option (two names for the same option don't count as two) */
                for (pass = 0; pass < 2 && !exact; pass++)
                    for (hit = pass ? builtin : longs; hit && hit->name; hit++)
                        if (!strncmp(hit->name, a + 2, l)) {
                            if (!hit->name[l]) { o = hit; exact = 1; found = 1; break; }
                            if (!found) { o = hit; found = 1; }
                            else if (hit->val != o->val) found++;
                        }
                if (!found) { say("unrecognized option '%.*s'", (int)(eq ? eq - a : (long)strlen(a)), a); usage_error(); }
                if (found > 1) { say("option '%.*s' is ambiguous", (int)(eq ? eq - a : (long)strlen(a)), a); usage_error(); }
                if (o->val == -100) { print_usage(); exit(0); }
                if (o->val == -101) { printf("%s (rifle) " VERSION "\n", prog); exit(0); }
                opt_i++;
                if (o->arg == 0) {
                    if (eq) { say("option '--%s' doesn't allow an argument", o->name); usage_error(); }
                    optarg = NULL;
                } else if (eq) optarg = eq + 1;
                else if (o->arg == 1) {
                    if (opt_i >= argc) { say("option '--%s' requires an argument", o->name); usage_error(); }
                    optarg = argv[opt_i++];
                } else optarg = NULL;
                return o->val;
            }
            opt_c = 1;
        }
        a = argv[opt_i];
        {
            char c = a[opt_c++];
            const char *p = strchr(shorts, c);
            int last = !a[opt_c];
            if (!p || c == ':') { say("invalid option -- '%c'", c); usage_error(); }
            if (p[1] == ':' && p[2] == ':') {
                optarg = last ? NULL : a + opt_c;
                opt_c = 0; opt_i++;
            } else if (p[1] == ':') {
                if (!last) { optarg = a + opt_c; opt_c = 0; opt_i++; }
                else {
                    opt_c = 0; opt_i++;
                    if (opt_i >= argc) { say("option requires an argument -- '%c'", c); usage_error(); }
                    optarg = argv[opt_i++];
                }
            } else {
                optarg = NULL;
                if (last) { opt_c = 0; opt_i++; }
            }
            return (unsigned char)c;
        }
    }
}

/* ---- small helpers ---- */

static int parse_ulong(const char *s, unsigned long *out)
{
    char *end;
    if (!isdigit((unsigned char)*s)) return 0;
    errno = 0;
    *out = strtoul(s, &end, 10);
    return !*end && errno != ERANGE;
}

static const char *user_name(uid_t u)
{
    static char buf[24];
    struct passwd *p = getpwuid(u);
    if (p) return p->pw_name;
    snprintf(buf, sizeof buf, "%u", u);
    return buf;
}
static const char *group_name(gid_t g)
{
    static char buf[24];
    struct group *p = getgrgid(g);
    if (p) return p->gr_name;
    snprintf(buf, sizeof buf, "%u", g);
    return buf;
}
static int lookup_user(const char *s, uid_t *u)
{
    unsigned long n;
    struct passwd *p;
    if (!*s) return -1;
    if ((p = getpwnam(s))) { *u = p->pw_uid; return 0; }
    if (parse_ulong(s, &n)) { *u = n; return 0; }
    return -1;
}
static int lookup_group(const char *s, gid_t *g)
{
    unsigned long n;
    struct group *p;
    if (!*s) return -1;
    if ((p = getgrnam(s))) { *g = p->gr_gid; return 0; }
    if (parse_ulong(s, &n)) { *g = n; return 0; }
    return -1;
}

/* ---- modes ----
 * chmod's language (755, u=rwx,go=rx, g=u, +X ...), compiled to a list of changes and applied to the old mode */

struct change { char op; char flag; mode_t affected, value, mentioned; };
#define MODE_ORDINARY 0
#define MODE_COPY 1
#define MODE_X_IF_ANY 2
#define MODE_BITS 07777

static struct change *mode_compile(const char *s)
{
    struct change *list;
    int n = 0;
    if (*s >= '0' && *s <= '7') {
        unsigned long v = 0; const char *p = s;
        for (; *p >= '0' && *p <= '7'; p++) { v = v * 8 + (*p - '0'); if (v > 07777) return NULL; }
        if (*p) return NULL;
        list = xmalloc(2 * sizeof *list);
        list[0].op = '='; list[0].flag = MODE_ORDINARY; list[0].affected = MODE_BITS; list[0].value = v;
        /* with fewer than 5 digits the setuid and setgid bits are only touched if the mode has them */
        list[0].mentioned = (p - s < 5) ? ((v & (S_ISUID | S_ISGID)) | S_ISVTX | 0777) : MODE_BITS;
        list[1].op = 0;
        return list;
    }
    list = xmalloc((strlen(s) + 2) * sizeof *list);
    for (;;) {
        mode_t affected = 0;
        char op;
        for (;; s++) {
            if (*s == 'u') affected |= S_ISUID | S_IRWXU;
            else if (*s == 'g') affected |= S_ISGID | S_IRWXG;
            else if (*s == 'o') affected |= S_ISVTX | S_IRWXO;
            else if (*s == 'a') affected |= MODE_BITS;
            else break;
        }
        if (*s != '+' && *s != '-' && *s != '=') { free(list); return NULL; }
        while (*s == '+' || *s == '-' || *s == '=') {
            mode_t value = 0;
            char flag = MODE_ORDINARY;
            op = *s++;
            if (*s == 'u' || *s == 'g' || *s == 'o') {
                value = *s == 'u' ? S_IRWXU : *s == 'g' ? S_IRWXG : S_IRWXO;
                flag = MODE_COPY; s++;
            } else {
                for (;; s++) {
                    if (*s == 'r') value |= S_IRUSR | S_IRGRP | S_IROTH;
                    else if (*s == 'w') value |= S_IWUSR | S_IWGRP | S_IWOTH;
                    else if (*s == 'x') value |= S_IXUSR | S_IXGRP | S_IXOTH;
                    else if (*s == 'X') flag = MODE_X_IF_ANY;
                    else if (*s == 's') value |= S_ISUID | S_ISGID;
                    else if (*s == 't') value |= S_ISVTX;
                    else break;
                }
            }
            list[n].op = op; list[n].flag = flag; list[n].affected = affected; list[n].value = value;
            list[n].mentioned = affected ? (affected & value) : value;
            n++;
        }
        if (*s == ',') { s++; continue; }
        if (*s) { free(list); return NULL; }
        break;
    }
    list[n].op = 0;
    return list;
}

/* the mode a file gets: old is what it has, dir says whether it's a directory (setuid and setgid on those stay unless
 * asked about), umask is applied to changes that don't say who they're for */
static mode_t mode_apply(mode_t old, int dir, mode_t um, const struct change *c)
{
    mode_t newmode = old & MODE_BITS;
    for (; c->op; c++) {
        mode_t affected = c->affected, value = c->value;
        mode_t omit = (dir ? S_ISUID | S_ISGID : 0) & ~c->mentioned;
        if (c->flag == MODE_COPY) {
            value &= newmode;
            value |= ((value & (S_IRUSR | S_IRGRP | S_IROTH)) ? S_IRUSR | S_IRGRP | S_IROTH : 0)
                   | ((value & (S_IWUSR | S_IWGRP | S_IWOTH)) ? S_IWUSR | S_IWGRP | S_IWOTH : 0)
                   | ((value & (S_IXUSR | S_IXGRP | S_IXOTH)) ? S_IXUSR | S_IXGRP | S_IXOTH : 0);
        } else if (c->flag == MODE_X_IF_ANY) {
            if ((newmode & (S_IXUSR | S_IXGRP | S_IXOTH)) || dir) value |= S_IXUSR | S_IXGRP | S_IXOTH;
        }
        value &= (affected ? affected : (mode_t)~um) & ~omit;
        if (c->op == '=') {
            mode_t keep = (affected ? ~affected : 0) | omit;
            newmode = (newmode & keep) | value;
        } else if (c->op == '+') newmode |= value;
        else newmode &= ~value;
    }
    return newmode;
}

static void mode_string(mode_t m, char *s)
{
    s[0] = m & S_IRUSR ? 'r' : '-'; s[1] = m & S_IWUSR ? 'w' : '-';
    s[2] = m & S_ISUID ? (m & S_IXUSR ? 's' : 'S') : (m & S_IXUSR ? 'x' : '-');
    s[3] = m & S_IRGRP ? 'r' : '-'; s[4] = m & S_IWGRP ? 'w' : '-';
    s[5] = m & S_ISGID ? (m & S_IXGRP ? 's' : 'S') : (m & S_IXGRP ? 'x' : '-');
    s[6] = m & S_IROTH ? 'r' : '-'; s[7] = m & S_IWOTH ? 'w' : '-';
    s[8] = m & S_ISVTX ? (m & S_IXOTH ? 't' : 'T') : (m & S_IXOTH ? 'x' : '-');
    s[9] = 0;
}

static mode_t current_umask(void) { mode_t m = umask(0); umask(m); return m; }

static char *strtok_r_simple(char **p)
{
    char *s = *p, *w;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (!*s) { *p = s; return NULL; }
    w = s;
    while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') s++;
    if (*s) *s++ = 0;
    *p = s;
    return w;
}

static const char *base_of(const char *p)
{
    const char *s = strrchr(p, '/');
    return s && s[1] ? s + 1 : p;
}

/* ---- true false ---- */

static int t_true(int argc, char **argv)
{
    /* they ignore everything, except as the only argument: --help and --version still work */
    if (argc == 2 && !strcmp(argv[1], "--help")) { print_usage(); return prog[0] == 'f'; }
    if (argc == 2 && !strcmp(argv[1], "--version")) { printf("%s (rifle) " VERSION "\n", prog); return prog[0] == 'f'; }
    return prog[0] == 'f';
}

/* ---- echo ---- */

static int t_echo(int argc, char **argv)
{
    int nl = 1, esc = 0, i = 1, first;
    if (argc == 2 && !strcmp(argv[1], "--help")) { print_usage(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--version")) { printf("echo (rifle) " VERSION "\n"); return 0; }
    for (; i < argc; i++) {
        char *a = argv[i]; int j;
        if (a[0] != '-' || !a[1]) break;
        for (j = 1; a[j] && strchr("neE", a[j]); j++) ;
        if (a[j]) break;
        for (j = 1; a[j]; j++) { if (a[j] == 'n') nl = 0; else if (a[j] == 'e') esc = 1; else esc = 0; }
    }
    first = i;
    for (; i < argc; i++) {
        const char *s = argv[i];
        if (i > first) putchar(' ');
        if (!esc) { fputs(s, stdout); continue; }
        while (*s) {
            int c = (unsigned char)*s++;
            if (c != '\\' || !*s) { putchar(c); continue; }
            c = *s++;
            switch (c) {
            case 'a': putchar('\a'); break;
            case 'b': putchar('\b'); break;
            case 'c': return finish_stdout();
            case 'e': putchar(27); break;
            case 'f': putchar('\f'); break;
            case 'n': putchar('\n'); break;
            case 'r': putchar('\r'); break;
            case 't': putchar('\t'); break;
            case 'v': putchar('\v'); break;
            case '\\': putchar('\\'); break;
            case 'x':
                if (!isxdigit((unsigned char)*s)) { putchar('\\'); putchar('x'); break; }
                c = digit_of(*s++);
                if (isxdigit((unsigned char)*s)) c = c * 16 + digit_of(*s++);
                putchar(c);
                break;
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
                if (c == '0') { if (!(*s >= '0' && *s <= '7')) { putchar(0); break; } c = *s++; }
                c -= '0';
                if (*s >= '0' && *s <= '7') c = c * 8 + (*s++ - '0');
                if (*s >= '0' && *s <= '7') c = c * 8 + (*s++ - '0');
                putchar(c & 255);
                break;
            default: putchar('\\'); putchar(c); break;
            }
        }
    }
    if (nl) putchar('\n');
    return finish_stdout();
}

/* ---- yes ---- */

static int t_yes(int argc, char **argv)
{
    static char buf[16384];
    size_t len = 0, i;
    int c;
    opts_reset();
    while ((c = getopts(argc, argv, "", NULL))) ;
    if (!nops) { memcpy(buf, "y\n", 2); len = 2; }
    else {
        for (i = 0; i < (size_t)nops; i++) {
            size_t l = strlen(ops[i]);
            if (len + l + 1 > sizeof buf) break;
            if (i) buf[len++] = ' ';
            memcpy(buf + len, ops[i], l); len += l;
        }
        buf[len++] = '\n';
    }
    for (i = len; i + len <= sizeof buf; i += len) memcpy(buf + i, buf, len);
    len = i;
    for (;;) if (write_all(1, buf, len) < 0) { perr("standard output"); return 1; }
}

/* ---- cat ---- */

static char cat_out[1 << 16];
static size_t cat_n;
static int cat_flush(void)
{
    int r = cat_n ? write_all(1, cat_out, cat_n) : 0;
    cat_n = 0;
    return r;
}

static int t_cat(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "show-all", 0, 'A' }, { "number-nonblank", 0, 'b' }, { "show-ends", 0, 'E' }, { "number", 0, 'n' },
        { "squeeze-blank", 0, 's' }, { "show-tabs", 0, 'T' }, { "show-nonprinting", 0, 'v' }, { NULL, 0, 0 } };
    int c, number = 0, nonblank = 0, ends = 0, tabs = 0, nonprint = 0, squeeze = 0, i;
    static char in[1 << 17];
    struct stat out_st; int out_reg;
    static unsigned long line_no;
    opts_reset();
    while ((c = getopts(argc, argv, "AbeEnstTuv", lo)))
        switch (c) {
        case 'A': nonprint = ends = tabs = 1; break;
        case 'b': number = nonblank = 1; break;
        case 'e': nonprint = ends = 1; break;
        case 'E': ends = 1; break;
        case 'n': number = 1; break;
        case 's': squeeze = 1; break;
        case 't': nonprint = tabs = 1; break;
        case 'T': tabs = 1; break;
        case 'v': nonprint = 1; break;
        default: break;
        }
    if (!nops) ops[nops++] = "-";
    out_reg = fstat(1, &out_st) == 0 && S_ISREG(out_st.st_mode);
    for (i = 0; i < nops; i++) {
        int fd = strcmp(ops[i], "-") ? open(ops[i], O_RDONLY) : 0;
        struct stat st;
        ssize_t n;
        int at_bol = 1, blank_run = 0;
        if (fd < 0) { perr("%s", ops[i]); status = 1; continue; }
        if (fstat(fd, &st) == 0 && out_reg && st.st_dev == out_st.st_dev && st.st_ino == out_st.st_ino &&
            lseek(fd, 0, SEEK_CUR) < st.st_size) {
            say("%s: input file is output file", ops[i]); status = 1; if (fd) close(fd); continue;
        }
        while ((n = read(fd, in, sizeof in)) != 0) {
            ssize_t k;
            if (n < 0) { if (errno == EINTR) continue; perr("%s", ops[i]); status = 1; break; }
            if (!(number || ends || tabs || nonprint || squeeze)) {
                if (cat_flush() < 0 || write_all(1, in, n) < 0) { perr("write error"); exit(1); }
                continue;
            }
            for (k = 0; k < n; k++) {
                int ch = (unsigned char)in[k];
                if (cat_n > sizeof cat_out - 32) if (cat_flush() < 0) { perr("write error"); exit(1); }
                if (at_bol) {
                    if (ch == '\n') {
                        if (squeeze && blank_run) continue;
                        blank_run++;
                        if (number && !nonblank) cat_n += snprintf(cat_out + cat_n, 32, "%6lu\t", ++line_no);
                        if (ends) cat_out[cat_n++] = '$';
                        cat_out[cat_n++] = '\n';
                        continue;
                    }
                    blank_run = 0;
                    if (number) cat_n += snprintf(cat_out + cat_n, 32, "%6lu\t", ++line_no);
                    at_bol = 0;
                }
                if (ch == '\n') { if (ends) cat_out[cat_n++] = '$'; cat_out[cat_n++] = '\n'; at_bol = 1; continue; }
                if (ch == '\t') { if (tabs) { cat_out[cat_n++] = '^'; cat_out[cat_n++] = 'I'; } else cat_out[cat_n++] = '\t'; continue; }
                if (nonprint) {
                    if (ch >= 128) { cat_out[cat_n++] = 'M'; cat_out[cat_n++] = '-'; ch -= 128; }
                    if (ch < 32) { cat_out[cat_n++] = '^'; cat_out[cat_n++] = ch + 64; continue; }
                    if (ch == 127) { cat_out[cat_n++] = '^'; cat_out[cat_n++] = '?'; continue; }
                }
                cat_out[cat_n++] = ch;
            }
        }
        if (fd) close(fd);
    }
    if (cat_flush() < 0) { perr("write error"); return 1; }
    return status;
}

/* ---- pwd ---- */

static int t_pwd(int argc, char **argv)
{
    static const struct opt lo[] = { { "logical", 0, 'L' }, { "physical", 0, 'P' }, { NULL, 0, 0 } };
    int c, logical = getenv("POSIXLY_CORRECT") != NULL;
    char buf[PATH_MAX], *w;
    opts_reset();
    while ((c = getopts(argc, argv, "LP", lo))) logical = c == 'L';
    if (nops) say("ignoring non-option arguments");
    if (logical && (w = getenv("PWD")) && w[0] == '/') {
        /* $PWD is good if it's this directory and has no . or .. in it */
        struct stat a, b; const char *p;
        int ok = stat(w, &a) == 0 && stat(".", &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
        for (p = w; ok && *p; ) {
            if (*p == '/') { p++; continue; }
            if (p[0] == '.' && (p[1] == '/' || !p[1])) ok = 0;
            if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || !p[2])) ok = 0;
            while (*p && *p != '/') p++;
        }
        if (ok) { puts(w); return finish_stdout(); }
    }
    if (!getcwd(buf, sizeof buf)) { perr("cannot get current directory"); return 1; }
    puts(buf);
    return finish_stdout();
}

/* ---- sleep ---- */

static int t_sleep(int argc, char **argv)
{
    double total = 0;
    int i, bad = 0, c;
    struct timespec t;
    opts_reset();
    while ((c = getopts(argc, argv, "", NULL))) ;
    if (!nops) missing_operand();
    for (i = 0; i < nops; i++) {
        char *end;
        double v = strtod(ops[i], &end);
        double mult = 1;
        if (end == ops[i] || v < 0 || ops[i][0] == '-') { bad = 1; say("invalid time interval '%s'", ops[i]); continue; }
        if (*end == 's') end++;
        else if (*end == 'm') { mult = 60; end++; }
        else if (*end == 'h') { mult = 3600; end++; }
        else if (*end == 'd') { mult = 86400; end++; }
        if (*end) { bad = 1; say("invalid time interval '%s'", ops[i]); continue; }
        total += v * mult;
    }
    if (bad) usage_error();
    t.tv_sec = (long)total;
    t.tv_nsec = (long)((total - (double)t.tv_sec) * 1e9);
    while (nanosleep(&t, &t) < 0 && errno == EINTR) ;
    return 0;
}

/* ---- basename dirname ---- */

static int t_basename(int argc, char **argv)
{
    static const struct opt lo[] = { { "multiple", 0, 'a' }, { "suffix", 1, 's' }, { "zero", 0, 'z' }, { NULL, 0, 0 } };
    int c, multiple = 0, zero = 0, i;
    const char *suffix = NULL;
    opts_reset();
    while ((c = getopts(argc, argv, "as:z", lo)))
        switch (c) {
        case 'a': multiple = 1; break;
        case 's': multiple = 1; suffix = optarg; break;
        case 'z': zero = 1; break;
        }
    if (!nops) missing_operand();
    if (!multiple) {
        if (nops > 2) extra_operand(ops[2]);
        if (nops == 2) suffix = ops[1];
        nops = 1;
    }
    for (i = 0; i < nops; i++) {
        char *s = xstrdup(ops[i]), *b;
        size_t n = strlen(s);
        while (n > 1 && s[n - 1] == '/') s[--n] = 0;
        b = n > 1 || s[0] != '/' ? (strrchr(s, '/') ? strrchr(s, '/') + 1 : s) : s;
        if (suffix && *suffix) {
            size_t bl = strlen(b), sl = strlen(suffix);
            if (bl > sl && !memcmp(b + bl - sl, suffix, sl)) b[bl - sl] = 0;
        }
        fputs(b, stdout);
        putchar(zero ? 0 : '\n');
        free(s);
    }
    return finish_stdout();
}

static int t_dirname(int argc, char **argv)
{
    static const struct opt lo[] = { { "zero", 0, 'z' }, { NULL, 0, 0 } };
    int c, zero = 0, i;
    opts_reset();
    while ((c = getopts(argc, argv, "z", lo))) zero = 1;
    if (!nops) missing_operand();
    for (i = 0; i < nops; i++) {
        const char *s = ops[i];
        size_t n = strlen(s), k;
        while (n > 1 && s[n - 1] == '/') n--;
        k = n;
        while (k > 0 && s[k - 1] != '/') k--;
        if (k == 0) fputs(".", stdout);
        else {
            while (k > 1 && s[k - 1] == '/') k--;
            fwrite(s, 1, k, stdout);
        }
        putchar(zero ? 0 : '\n');
    }
    return finish_stdout();
}

/* ---- sync ---- */

static int t_sync(int argc, char **argv)
{
    static const struct opt lo[] = { { "data", 0, 'd' }, { "file-system", 0, 'f' }, { NULL, 0, 0 } };
    int c, data = 0, fs = 0, i, st = 0;
    opts_reset();
    while ((c = getopts(argc, argv, "df", lo))) { if (c == 'd') data = 1; else fs = 1; }
    if (data && fs) { say("cannot specify both --data and --file-system"); usage_error(); }
    if (data && !nops) { say("--data needs at least one argument"); usage_error(); }
    if (!nops) { sync(); return 0; }
    for (i = 0; i < nops; i++) {
        int fd = open(ops[i], O_RDONLY | O_NONBLOCK);
        if (fd < 0) fd = open(ops[i], O_WRONLY | O_NONBLOCK);
        if (fd < 0) { perr("error opening '%s'", ops[i]); st = 1; continue; }
        if ((fs ? syncfs(fd) : data ? fdatasync(fd) : fsync(fd)) < 0) { perr("error syncing '%s'", ops[i]); st = 1; }
        close(fd);
    }
    return st;
}

/* ---- uname arch hostname ---- */

static int t_uname(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "all", 0, 'a' }, { "kernel-name", 0, 's' }, { "nodename", 0, 'n' }, { "kernel-release", 0, 'r' },
        { "kernel-version", 0, 'v' }, { "machine", 0, 'm' }, { "processor", 0, 'p' },
        { "hardware-platform", 0, 'i' }, { "operating-system", 0, 'o' }, { NULL, 0, 0 } };
    struct utsname u;
    int c, all = 0, s = 0, n = 0, r = 0, v = 0, m = 0, p = 0, hw = 0, o = 0, any, first = 1;
    opts_reset();
    if (!strcmp(prog, "arch")) { m = 1; while ((c = getopts(argc, argv, "", NULL))) ; if (nops) extra_operand(ops[0]); }
    else {
        while ((c = getopts(argc, argv, "asnrvmpio", lo)))
            switch (c) {
            case 'a': all = 1; break; case 's': s = 1; break; case 'n': n = 1; break; case 'r': r = 1; break;
            case 'v': v = 1; break; case 'm': m = 1; break; case 'p': p = 1; break; case 'i': hw = 1; break;
            case 'o': o = 1; break;
            }
        if (nops) extra_operand(ops[0]);
        if (all) s = n = r = v = m = o = 1;
        any = s | n | r | v | m | p | hw | o;
        if (!any) s = 1;
    }
    if (uname(&u) < 0) { perr("cannot get system name"); return 1; }
#define PUT(flag, str) if (flag) { if (!first) putchar(' '); fputs(str, stdout); first = 0; }
    PUT(s, u.sysname) PUT(n, u.nodename) PUT(r, u.release) PUT(v, u.version) PUT(m, u.machine)
    PUT(p, "unknown") PUT(hw, "unknown") PUT(o, "Linux")
    putchar('\n');
    return finish_stdout();
}

static int t_hostname(int argc, char **argv)
{
    struct utsname u;
    int c;
    opts_reset();
    while ((c = getopts(argc, argv, "", NULL))) ;
    if (nops > 1) extra_operand(ops[1]);
    if (nops == 1) {
        if (sethostname(ops[0], strlen(ops[0])) < 0) { perr("cannot set name to '%s'", ops[0]); return 1; }
        return 0;
    }
    if (uname(&u) < 0) { perr("cannot determine hostname"); return 1; }
    puts(u.nodename);
    return finish_stdout();
}

/* ---- printenv env ---- */

static int t_printenv(int argc, char **argv)
{
    static const struct opt lo[] = { { "null", 0, '0' }, { NULL, 0, 0 } };
    int c, nul = 0, i, st = 0;
    char **e;
    opts_reset();
    while ((c = getopts(argc, argv, "0", lo))) nul = 1;
    if (!nops) {
        for (e = environ; e && *e; e++) { fputs(*e, stdout); putchar(nul ? 0 : '\n'); }
        return finish_stdout();
    }
    for (i = 0; i < nops; i++) {
        char *v = strchr(ops[i], '=') ? NULL : getenv(ops[i]);
        if (!v) { st = 1; continue; }
        fputs(v, stdout); putchar(nul ? 0 : '\n');
    }
    return finish_stdout() ? 2 : st;
}

static int t_env(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "ignore-environment", 0, 'i' }, { "unset", 1, 'u' }, { "null", 0, '0' }, { "chdir", 1, 'C' }, { NULL, 0, 0 } };
    int c, nul = 0, i;
    const char *dir = NULL;
    usage_status = 125;
    opts_reset();
    for (i = 1; i < argc && !strcmp(argv[i], "-"); i++) argv[i] = "-i";
    while ((c = getopts(argc, argv, "+iu:0C:", lo)))
        switch (c) {
        case 'i': clearenv(); break;
        case 'u': unsetenv(optarg); break;
        case '0': nul = 1; break;
        case 'C': dir = optarg; break;
        }
    for (i = 0; i < nops && strchr(ops[i], '=') && ops[i][0] != '='; i++)
        if (putenv(ops[i]) < 0) die(125, "cannot set '%s'", ops[i]);
    if (i == nops) {
        char **e;
        if (dir && chdir(dir) < 0) { perr("cannot change directory to '%s'", dir); return 125; }
        for (e = environ; e && *e; e++) { fputs(*e, stdout); putchar(nul ? 0 : '\n'); }
        return finish_stdout();
    }
    if (nul) { say("cannot specify --null (-0) with command"); usage_error(); }
    if (dir && chdir(dir) < 0) { perr("cannot change directory to '%s'", dir); return 125; }
    fflush(stdout);
    execvp(ops[i], ops + i);
    perr("'%s'", ops[i]);
    return errno == ENOENT ? 127 : 126;
}

/* ---- link unlink ---- */

static int t_link(int argc, char **argv)
{
    int c;
    opts_reset();
    while ((c = getopts(argc, argv, "", NULL))) ;
    if (nops < 1) missing_operand();
    if (nops < 2) { say("missing operand after '%s'", ops[0]); usage_error(); }
    if (nops > 2) extra_operand(ops[2]);
    if (link(ops[0], ops[1]) < 0) { perr("cannot create link '%s' to '%s'", ops[1], ops[0]); return 1; }
    return 0;
}

static int t_unlink(int argc, char **argv)
{
    int c;
    opts_reset();
    while ((c = getopts(argc, argv, "", NULL))) ;
    if (nops < 1) missing_operand();
    if (nops > 1) extra_operand(ops[1]);
    if (unlink(ops[0]) < 0) { perr("cannot unlink '%s'", ops[0]); return 1; }
    return 0;
}

/* ---- tty nproc whoami logname hostid ---- */

static int t_tty(int argc, char **argv)
{
    static const struct opt lo[] = { { "silent", 0, 's' }, { "quiet", 0, 's' }, { NULL, 0, 0 } };
    int c, silent = 0;
    char name[PATH_MAX];
    ssize_t n;
    usage_status = 2;
    opts_reset();
    while ((c = getopts(argc, argv, "s", lo))) silent = 1;
    if (nops) extra_operand(ops[0]);
    if (isatty(0) && (n = readlink("/proc/self/fd/0", name, sizeof name - 1)) > 0) {
        name[n] = 0;
        if (!silent) puts(name);
        return finish_stdout() ? 3 : 0;
    }
    if (!silent) puts("not a tty");
    return finish_stdout() ? 3 : 1;
}

static unsigned long omp_value(const char *name)
{
    const char *s = getenv(name);
    unsigned long v = 0;
    char *end;
    if (!s) return 0;
    v = strtoul(s, &end, 10);
    return end != s && (*end == 0 || *end == ',') ? v : 0;
}

static int t_nproc(int argc, char **argv)
{
    static const struct opt lo[] = { { "all", 0, 'a' }, { "ignore", 1, 'i' }, { NULL, 0, 0 } };
    int c, all = 0;
    unsigned long ignore = 0, n = 0;
    opts_reset();
    while ((c = getopts(argc, argv, "", lo)))
        switch (c) {
        case 'a': all = 1; break;
        case 'i': if (!parse_ulong(optarg, &ignore)) die(1, "invalid number: '%s'", optarg); break;
        }
    if (nops) extra_operand(ops[0]);
    if (all) {
        DIR *d = opendir("/sys/devices/system/cpu");
        struct dirent *e;
        if (d) {
            while ((e = readdir(d)))
                if (!strncmp(e->d_name, "cpu", 3) && isdigit((unsigned char)e->d_name[3])) n++;
            closedir(d);
        }
    }
    if (!n) {
        unsigned long mask[16], limit;
        int i, k;
        if (!all && (n = omp_value("OMP_NUM_THREADS"))) goto done;
        memset(mask, 0, sizeof mask);
        if (sched_getaffinity(0, sizeof mask, mask) < 0) n = 1;
        else for (i = 0; i < 16; i++) for (k = 0; k < 64; k++) if (mask[i] >> k & 1) n++;
        if (!all && (limit = omp_value("OMP_THREAD_LIMIT")) && limit < n) n = limit;
        if (!n) n = 1;
    }
done:
    n = ignore < n ? n - ignore : 1;
    printf("%lu\n", n);
    return finish_stdout();
}

static int t_whoami(int argc, char **argv)
{
    int c;
    struct passwd *p;
    opts_reset();
    while ((c = getopts(argc, argv, "", NULL))) ;
    if (nops) extra_operand(ops[0]);
    if (!(p = getpwuid(geteuid()))) die(1, "cannot find name for user ID %u", geteuid());
    puts(p->pw_name);
    return finish_stdout();
}

static int t_logname(int argc, char **argv)
{
    int c;
    char buf[32];
    ssize_t n;
    int fd;
    unsigned long uid;
    opts_reset();
    while ((c = getopts(argc, argv, "", NULL))) ;
    if (nops) extra_operand(ops[0]);
    fd = open("/proc/self/loginuid", O_RDONLY);
    n = fd < 0 ? -1 : read(fd, buf, sizeof buf - 1);
    if (fd >= 0) close(fd);
    if (n > 0) {
        buf[n] = 0;
        if (parse_ulong(buf, &uid) || (buf[n - 1] == '\n' && (buf[n - 1] = 0, parse_ulong(buf, &uid))))
            if (uid != 4294967295UL && getpwuid(uid)) { puts(getpwuid(uid)->pw_name); return finish_stdout(); }
    }
    die(1, "no login name");
}

static int t_hostid(int argc, char **argv)
{
    int c;
    unsigned id = 0;
    int fd;
    opts_reset();
    while ((c = getopts(argc, argv, "", NULL))) ;
    if (nops) extra_operand(ops[0]);
    fd = open("/etc/hostid", O_RDONLY);
    if (fd >= 0 && read(fd, &id, 4) == 4) { close(fd); }
    else {
        /* like glibc without /etc/hostid: our name's IPv4 address from /etc/hosts, halves swapped */
        struct utsname u; FILE *f; char line[1024];
        if (fd >= 0) close(fd);
        id = 0;
        if (uname(&u) == 0 && (f = fopen("/etc/hosts", "r"))) {
            while (fgets(line, sizeof line, f)) {
                char *p = line, *tok, *ip = NULL, *hash = strchr(line, '#');
                int idx = 0, hit = 0;
                if (hash) *hash = 0;
                while ((tok = strtok_r_simple(&p))) {
                    if (idx++ == 0) { ip = tok; continue; }
                    if (!strcasecmp(tok, u.nodename)) hit = 1;
                }
                if (hit && ip) {
                    unsigned b[4] = { 0 }; int k = 0; char *q = ip;
                    for (; *q && k < 4; q++) { if (*q == '.') k++; else if (isdigit((unsigned char)*q)) b[k] = b[k] * 10 + *q - '0'; else { k = 9; break; } }
                    if (k == 3) { unsigned ipn = b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24; id = ipn << 16 | ipn >> 16; break; }
                }
            }
            fclose(f);
        }
    }
    printf("%08x\n", id);
    return finish_stdout();
}

/* ---- sizes, inputs, paths, dates ---- */

/* 10, 4K, 2M, 1kB: a number with the size suffixes the GNU tools take */
static int parse_size(const char *s, unsigned long long *out)
{
    char *end;
    unsigned long long v, mult = 1;
    errno = 0;
    if (!isdigit((unsigned char)*s)) return 0;
    v = strtoull(s, &end, 10);
    if (errno == ERANGE) return 0;
    if (*end) {
        static const struct { const char *s; unsigned long long m; } suf[] = {
            { "b", 512 }, { "kB", 1000 }, { "KB", 1000 }, { "K", 1024 }, { "KiB", 1024 }, { "k", 1024 },
            { "MB", 1000000ULL }, { "M", 1ULL << 20 }, { "MiB", 1ULL << 20 },
            { "GB", 1000000000ULL }, { "G", 1ULL << 30 }, { "GiB", 1ULL << 30 },
            { "TB", 1000000000000ULL }, { "T", 1ULL << 40 }, { "TiB", 1ULL << 40 },
            { "PB", 1000000000000000ULL }, { "P", 1ULL << 50 }, { "PiB", 1ULL << 50 },
            { "EB", 1000000000000000000ULL }, { "E", 1ULL << 60 }, { "EiB", 1ULL << 60 }, { NULL, 0 } };
        int i;
        for (i = 0; suf[i].s; i++) if (!strcmp(end, suf[i].s)) { mult = suf[i].m; break; }
        if (!suf[i].s) return 0;
    }
    if (mult > 1 && v > ~0ULL / mult) { *out = ~0ULL; return 1; }
    *out = v * mult;
    return 1;
}

/* the rest of fd in one block, with a NUL after it */
static int read_all(int fd, char **buf, size_t *len)
{
    size_t cap = 65536, n = 0;
    char *b = xmalloc(cap + 1);
    for (;;) {
        ssize_t r;
        if (n == cap) { cap *= 2; b = xrealloc(b, cap + 1); }
        r = read(fd, b + n, cap - n);
        if (r < 0) { if (errno == EINTR) continue; free(b); return -1; }
        if (!r) break;
        n += r;
    }
    b[n] = 0;
    *buf = b; *len = n;
    return 0;
}
static int open_input(const char *name) { return strcmp(name, "-") ? open(name, O_RDONLY) : 0; }

/* . and .. resolved and symlinks followed. CAN_EXISTING is readlink -e, CAN_ALL_BUT_LAST is -f, CAN_MISSING is -m */
#define CAN_EXISTING 0
#define CAN_ALL_BUT_LAST 1
#define CAN_MISSING 2
static char *canonicalize(const char *path, int mode, int nolinks)
{
    char *dest = xmalloc(PATH_MAX + 2), *rest = xstrdup(path), *p;
    size_t n = 0;               /* used part of dest. no slash at the end, n 0 is the root */
    int links = 0;
    dest[0] = 0;
    if (path[0] != '/') {
        if (!getcwd(dest, PATH_MAX)) goto fail;
        n = strlen(dest);
        if (n == 1) n = 0;
        dest[n] = 0;
    }
    p = rest;
    for (;;) {
        char *seg, *q;
        size_t l;
        int last;
        struct stat sb;
        while (*p == '/') p++;
        if (!*p) break;
        seg = p;
        while (*p && *p != '/') p++;
        l = p - seg;
        if (l == 1 && seg[0] == '.') continue;
        if (l == 2 && seg[0] == '.' && seg[1] == '.') { while (n && dest[n - 1] != '/') n--; if (n) n--; dest[n] = 0; continue; }
        if (n + l + 2 > PATH_MAX) { errno = ENAMETOOLONG; goto fail; }
        dest[n++] = '/'; memcpy(dest + n, seg, l); n += l; dest[n] = 0;
        for (q = p; *q == '/'; q++) ;
        last = !*q;
        if (nolinks) continue;
        if (lstat(dest, &sb) < 0) {
            if (errno == ENOENT && (mode == CAN_MISSING || (mode == CAN_ALL_BUT_LAST && last))) continue;
            goto fail;
        }
        if (S_ISLNK(sb.st_mode)) {
            char target[PATH_MAX], *nr;
            ssize_t k;
            if (++links > 40) { errno = ELOOP; goto fail; }
            if ((k = readlink(dest, target, sizeof target - 1)) < 0) goto fail;
            target[k] = 0;
            n -= l + 1; dest[n] = 0;
            if (target[0] == '/') { n = 0; dest[0] = 0; }
            nr = xmalloc(strlen(target) + strlen(p) + 2);
            strcpy(nr, target); strcat(nr, "/"); strcat(nr, p);
            free(rest); rest = nr; p = rest;
        } else if (!S_ISDIR(sb.st_mode) && !last) { errno = ENOTDIR; goto fail; }
    }
    if (!n) { dest[0] = '/'; dest[1] = 0; }
    free(rest);
    return dest;
fail:
    free(dest); free(rest);
    return NULL;
}

/* `to` as seen from the directory `from`, both canonical */
static char *relative_path(const char *from, const char *to)
{
    char *r = xmalloc(strlen(from) * 3 + strlen(to) + 8);
    const char *f = from, *t = to;
    size_t n = 0;
    for (;;) {
        const char *fe, *te;
        while (*f == '/') f++;
        while (*t == '/') t++;
        for (fe = f; *fe && *fe != '/'; fe++) ;
        for (te = t; *te && *te != '/'; te++) ;
        if (fe == f || te == t || fe - f != te - t || memcmp(f, t, fe - f)) break;
        f = fe; t = te;
    }
    for (;;) {
        while (*f == '/') f++;
        if (!*f) break;
        memcpy(r + n, "../", 3); n += 3;
        while (*f && *f != '/') f++;
    }
    while (*t == '/') t++;
    strcpy(r + n, t);
    n = strlen(r);
    if (n > 1 && r[n - 1] == '/') r[n - 1] = 0;
    if (!r[0]) strcpy(r, ".");
    return r;
}

/* @SECONDS, 2024-01-31, 2024-01-31 10:20[:30[.123]], now, today, yesterday, tomorrow, "3 days ago", "+2 hours".
 * UTC until rifle reads TZ. 0 on success */
static int parse_date(const char *s, struct timespec *ts)
{
    struct timespec now;
    struct tm t;
    long y, mo, d, h = 0, mi = 0, sec = 0;
    long nsec = 0;
    const char *p = s;
    clock_gettime(0, &now);
    while (*p == ' ') p++;
    if (*p == '@') {
        char *end; long long v = strtoll(p + 1, &end, 10);
        if (end == p + 1) return -1;
        ts->tv_sec = v; ts->tv_nsec = 0;
        if (*end == '.') { long f = 0, digits = 0; end++; while (isdigit((unsigned char)*end)) { if (digits < 9) { f = f * 10 + *end - '0'; digits++; } end++; } while (digits++ < 9) f *= 10; ts->tv_nsec = f; }
        return *end ? -1 : 0;
    }
    ts->tv_sec = now.tv_sec; ts->tv_nsec = now.tv_nsec;
    if (!*p) return 0;
    if (isdigit((unsigned char)*p)) {
        char *end;
        y = strtol(p, &end, 10);
        if (end - p == 4 && *end == '-') {
            p = end + 1; mo = strtol(p, &end, 10);
            if (*end != '-' || end == p) return -1;
            p = end + 1; d = strtol(p, &end, 10);
            if (end == p) return -1;
            p = end;
            if (*p == ' ' || *p == 'T') {
                p++;
                h = strtol(p, &end, 10);
                if (*end != ':' || end == p) return -1;
                p = end + 1; mi = strtol(p, &end, 10);
                if (end == p) return -1;
                p = end;
                if (*p == ':') { p++; sec = strtol(p, &end, 10); if (end == p) return -1; p = end; }
                if (*p == '.' || *p == ',') { long f = 0, digits = 0; p++; while (isdigit((unsigned char)*p)) { if (digits < 9) { f = f * 10 + *p - '0'; digits++; } p++; } while (digits++ < 9) f *= 10; nsec = f; }
            }
            if (*p == 'Z') p++;
            while (*p == ' ') p++;
            if (*p) return -1;
            if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 24 || mi > 59 || sec > 61) return -1;
            memset(&t, 0, sizeof t);
            t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = sec;
            ts->tv_sec = timegm(&t); ts->tv_nsec = nsec;
            return 0;
        }
    }
    {
        char word[64];
        long total = 0;
        int sign = 1, any = 0;
        while (*p) {
            char *end;
            long n;
            int i = 0;
            while (*p == ' ') p++;
            if (!*p) break;
            if (*p == '+' || *p == '-' || isdigit((unsigned char)*p)) {
                sign = *p == '-' ? -1 : 1;
                if (*p == '+' || *p == '-') p++;
                n = strtol(p, &end, 10);
                if (end == p) return -1;
                p = end;
                while (*p == ' ') p++;
                while (isalpha((unsigned char)*p) && i < 63) word[i++] = tolower(*p++);
                word[i] = 0;
                if (!strncmp(word, "sec", 3) || !strcmp(word, "s")) total += sign * n;
                else if (!strncmp(word, "min", 3)) total += sign * n * 60;
                else if (!strncmp(word, "hour", 4) || !strcmp(word, "h")) total += sign * n * 3600;
                else if (!strncmp(word, "day", 3)) total += sign * n * 86400;
                else if (!strncmp(word, "week", 4)) total += sign * n * 7 * 86400;
                else return -1;
                any = 1;
                continue;
            }
            while (isalpha((unsigned char)*p) && i < 63) word[i++] = tolower(*p++);
            word[i] = 0;
            if (!word[0]) return -1;
            if (!strcmp(word, "now") || !strcmp(word, "today")) { if (word[0] == 't') ts->tv_sec -= ts->tv_sec % 86400; }
            else if (!strcmp(word, "yesterday")) total -= 86400;
            else if (!strcmp(word, "tomorrow")) total += 86400;
            else if (!strcmp(word, "ago")) total = -total;
            else return -1;
            any = 1;
        }
        if (!any) return -1;
        ts->tv_sec += total;
        return 0;
    }
}

/* ---- walking trees ----
 * children first, then the directory, so chmod -R 000 works */

typedef int (*visit_fn)(const char *path, const struct stat *st, void *ctx);

static int walk(const char *path, int top, int follow_top, int follow_all, int recurse, visit_fn fn, void *ctx)
{
    struct stat st;
    int follow = top ? follow_top : follow_all, bad = 0;
    if ((follow ? stat(path, &st) : lstat(path, &st)) < 0) {
        perr("cannot access '%s'", path);
        return 1;
    }
    if (recurse && S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) { perr("cannot read directory '%s'", path); bad = 1; }
        else {
            struct dirent *e;
            char **names = NULL; int n = 0, cap = 0, i;
            while ((e = readdir(d))) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                if (n == cap) { cap = cap ? cap * 2 : 32; names = xrealloc(names, cap * sizeof *names); }
                names[n++] = xstrdup(e->d_name);
            }
            closedir(d);
            for (i = 0; i < n; i++) {
                size_t pl = strlen(path);
                char *child = xmalloc(pl + strlen(names[i]) + 2);
                memcpy(child, path, pl);
                if (pl && path[pl - 1] != '/') child[pl++] = '/';
                strcpy(child + pl, names[i]);
                bad |= walk(child, 0, follow_top, follow_all, recurse, fn, ctx);
                free(child); free(names[i]);
            }
            free(names);
        }
    }
    return bad | fn(path, &st, ctx);
}

/* ---- mkdir rmdir ---- */

static int t_mkdir(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "mode", 1, 'm' }, { "parents", 0, 'p' }, { "verbose", 0, 'v' }, { "context", 2, 'Z' }, { NULL, 0, 0 } };
    int c, parents = 0, verbose = 0, i, st = 0;
    struct change *change = NULL;
    mode_t um = current_umask(), mode = 0777;
    opts_reset();
    while ((c = getopts(argc, argv, "m:pvZ", lo)))
        switch (c) {
        case 'm': if (!(change = mode_compile(optarg))) die(1, "invalid mode '%s'", optarg); break;
        case 'p': parents = 1; break;
        case 'v': verbose = 1; break;
        default: break;
        }
    if (!nops) missing_operand();
    if (change) mode = mode_apply(0777 & ~um, 1, um, change);
    for (i = 0; i < nops; i++) {
        char *path = xstrdup(ops[i]), *p;
        struct stat sb;
        if (parents) {
            /* parents get the umask's mode plus u+wx so we can go on into them */
            for (p = path; *p == '/'; p++) ;
            for (; *p; p++) {
                if (*p != '/') continue;
                *p = 0;
                if (stat(path, &sb) < 0 && errno == ENOENT) {
                    if (mkdir(path, (0777 & ~um) | S_IWUSR | S_IXUSR) < 0) { perr("cannot create directory '%s'", path); st = 1; goto next; }
                    if (verbose) printf("%s: created directory '%s'\n", prog, path);
                }
                *p = '/';
                while (p[1] == '/') p++;
            }
            if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) goto next;
        }
        if (change) umask(0);
        if (mkdir(path, change ? (mode & 0777) : 0777) < 0) {
            if (change) umask(um);
            perr("cannot create directory '%s'", path); st = 1; goto next;
        }
        if (change) {
            umask(um);
            if (mode & ~0777) chmod(path, mode);
        }
        if (verbose) printf("%s: created directory '%s'\n", prog, path);
next:
        free(path);
    }
    return finish_stdout() ? 1 : st;
}

static int t_rmdir(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "ignore-fail-on-non-empty", 0, 'I' }, { "parents", 0, 'p' }, { "verbose", 0, 'v' }, { NULL, 0, 0 } };
    int c, ignore = 0, parents = 0, verbose = 0, i, st = 0;
    opts_reset();
    while ((c = getopts(argc, argv, "pv", lo))) { if (c == 'I') ignore = 1; else if (c == 'p') parents = 1; else verbose = 1; }
    if (!nops) missing_operand();
    for (i = 0; i < nops; i++) {
        char *dir = xstrdup(ops[i]);
        int first = 1;
        for (;;) {
            char *slash;
            if (verbose) printf("%s: removing directory, '%s'\n", prog, dir);
            if (rmdir(dir) < 0) {
                if (ignore && (errno == ENOTEMPTY || errno == EEXIST)) break;
                if (first) perr("failed to remove '%s'", dir); else perr("failed to remove directory '%s'", dir);
                st = 1;
                break;
            }
            if (!parents) break;
            first = 0;
            slash = dir + strlen(dir);
            while (slash > dir && slash[-1] == '/') slash--;
            while (slash > dir && slash[-1] != '/') slash--;
            while (slash > dir && slash[-1] == '/') slash--;
            if (slash == dir) break;
            *slash = 0;
        }
        free(dir);
    }
    return finish_stdout() ? 1 : st;
}

/* ---- rm ---- */

struct rm_opts { int force, ask, recursive, dir, verbose; };

static int rm_confirm(const char *what, const char *path)
{
    char line[16];
    fflush(stdout);
    fprintf(stderr, "%s: remove %s '%s'? ", prog, what, path);
    if (!fgets(line, sizeof line, stdin)) return 0;
    return line[0] == 'y' || line[0] == 'Y';
}

static int rm_entry(int dfd, const char *name, const char *path, const struct rm_opts *o)
{
    struct stat st;
    if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) < 0) {
        if (o->force && errno == ENOENT) return 0;
        perr("cannot remove '%s'", path);
        return 1;
    }
    if (S_ISDIR(st.st_mode)) {
        if (!o->recursive) {
            if (!o->dir) { say("cannot remove '%s': Is a directory", path); return 1; }
            if (o->ask && !rm_confirm("directory", path)) return 0;
            if (unlinkat(dfd, name, AT_REMOVEDIR) < 0) { perr("cannot remove '%s'", path); return 1; }
            if (o->verbose) printf("removed directory '%s'\n", path);
            return 0;
        } else {
            int fd, bad = 0, n = 0, cap = 0, i;
            DIR *d;
            struct dirent *e;
            char **names = NULL;
            if (o->ask && !rm_confirm("directory", path)) return 0;
            fd = openat(dfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (fd < 0 || !(d = fdopendir(fd))) { perr("cannot remove '%s'", path); if (fd >= 0) close(fd); return 1; }
            while ((e = readdir(d))) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                if (n == cap) { cap = cap ? cap * 2 : 32; names = xrealloc(names, cap * sizeof *names); }
                names[n++] = xstrdup(e->d_name);
            }
            for (i = 0; i < n; i++) {
                size_t pl = strlen(path);
                char *child = xmalloc(pl + strlen(names[i]) + 2);
                memcpy(child, path, pl);
                if (pl && path[pl - 1] != '/') child[pl++] = '/';
                strcpy(child + pl, names[i]);
                bad |= rm_entry(dirfd(d), names[i], child, o);
                free(child); free(names[i]);
            }
            free(names);
            closedir(d);
            if (bad) return 1;
            if (unlinkat(dfd, name, AT_REMOVEDIR) < 0) { perr("cannot remove '%s'", path); return 1; }
            if (o->verbose) printf("removed directory '%s'\n", path);
            return 0;
        }
    }
    if (o->ask && !rm_confirm(S_ISLNK(st.st_mode) ? "symbolic link" : st.st_size ? "regular file" : "regular empty file", path))
        return 0;
    if (unlinkat(dfd, name, 0) < 0) { perr("cannot remove '%s'", path); return 1; }
    if (o->verbose) printf("removed '%s'\n", path);
    return 0;
}

static int t_rm(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "force", 0, 'f' }, { "interactive", 2, 'i' }, { "recursive", 0, 'r' }, { "dir", 0, 'd' }, { "verbose", 0, 'v' },
        { "preserve-root", 2, 'P' }, { "no-preserve-root", 0, 'N' }, { "one-file-system", 0, 'x' }, { NULL, 0, 0 } };
    struct rm_opts o = { 0, 0, 0, 0, 0 };
    int c, preserve_root = 1, i, st = 0;
    opts_reset();
    while ((c = getopts(argc, argv, "fiIrRdv", lo)))
        switch (c) {
        case 'f': o.force = 1; o.ask = 0; break;
        case 'i': o.ask = !(optarg && (!strcmp(optarg, "never") || !strcmp(optarg, "no") || !strcmp(optarg, "none"))); o.force = 0; break;
        case 'I': break;
        case 'r': case 'R': o.recursive = 1; break;
        case 'd': o.dir = 1; break;
        case 'v': o.verbose = 1; break;
        case 'P': preserve_root = 1; break;
        case 'N': preserve_root = 0; break;
        default: break;
        }
    if (!nops) { if (o.force) return 0; missing_operand(); }
    for (i = 0; i < nops; i++) {
        const char *path = ops[i], *b = base_of(path);
        /* the root check comes before anything is touched */
        if (o.recursive && preserve_root) {
            struct stat a, r;
            if (stat(path, &a) == 0 && stat("/", &r) == 0 && a.st_dev == r.st_dev && a.st_ino == r.st_ino) {
                say("it is dangerous to operate recursively on '%s'", path);
                say("use --no-preserve-root to override this failsafe");
                st = 1; continue;
            }
        }
        if (o.recursive && (!strcmp(b, ".") || !strcmp(b, ".."))) {
            say("refusing to remove '.' or '..' directory: skipping '%s'", path);
            st = 1; continue;
        }
        st |= rm_entry(AT_FDCWD, path, path, &o);
    }
    return finish_stdout() ? 1 : st;
}

/* ---- chmod chown chgrp ---- */

struct chmod_ctx { const struct change *change; int verbose, changes_only, quiet; mode_t um; };

static int chmod_visit(const char *path, const struct stat *st, void *v)
{
    struct chmod_ctx *x = v;
    mode_t old = st->st_mode & MODE_BITS, new;
    char a[10], b[10];
    if (S_ISLNK(st->st_mode)) return 0;
    new = mode_apply(st->st_mode, S_ISDIR(st->st_mode), x->um, x->change) & MODE_BITS;
    if (chmod(path, new) < 0) {
        if (!x->quiet) perr("changing permissions of '%s'", path);
        return 1;
    }
    if (x->verbose || x->changes_only) {
        mode_string(old, a); mode_string(new, b);
        if (new != old) printf("mode of '%s' changed from %04o (%s) to %04o (%s)\n", path, old, a, new, b);
        else if (x->verbose) printf("mode of '%s' retained as %04o (%s)\n", path, old, a);
    }
    return 0;
}

static int t_chmod(int argc, char **argv)
{
    struct chmod_ctx x = { NULL, 0, 0, 0, 0 };
    char *modestr = NULL, *reference = NULL;
    char **files = xmalloc((argc + 1) * sizeof *files);
    int nfiles = 0, i, recurse = 0, st = 0, ends = 0;
    /* an argument starting with - that isn't an option is the mode: chmod -w f */
    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (ends || a[0] != '-' || !a[1]) {
            if (!modestr && !reference) modestr = a; else files[nfiles++] = a;
            continue;
        }
        if (!strcmp(a, "--")) { ends = 1; continue; }
        if (a[1] == '-') {
            if (!strcmp(a, "--recursive")) recurse = 1;
            else if (!strcmp(a, "--verbose")) x.verbose = 1;
            else if (!strcmp(a, "--changes")) x.changes_only = 1;
            else if (!strcmp(a, "--silent") || !strcmp(a, "--quiet")) x.quiet = 1;
            else if (!strncmp(a, "--reference=", 12)) reference = a + 12;
            else if (!strcmp(a, "--reference") && i + 1 < argc) reference = argv[++i];
            else if (!strcmp(a, "--no-preserve-root") || !strcmp(a, "--preserve-root")) ;
            else if (!strcmp(a, "--help")) { print_usage(); return 0; }
            else if (!strcmp(a, "--version")) { printf("chmod (rifle) " VERSION "\n"); return 0; }
            else { say("unrecognized option '%s'", a); usage_error(); }
            continue;
        }
        if (strchr("Rcfv", a[1])) {
            int j;
            for (j = 1; a[j]; j++) {
                if (a[j] == 'R') recurse = 1; else if (a[j] == 'c') x.changes_only = 1;
                else if (a[j] == 'f') x.quiet = 1; else if (a[j] == 'v') x.verbose = 1;
                else { say("invalid option -- '%c'", a[j]); usage_error(); }
            }
            continue;
        }
        if (!modestr && !reference) modestr = a; else files[nfiles++] = a;   /* -w, -x, -rw: it's the mode */
    }
    if (reference) {
        struct stat rs; struct change *list;
        if (stat(reference, &rs) < 0) die(1, "failed to get attributes of '%s': %s", reference, strerror(errno));
        list = xmalloc(2 * sizeof *list);
        list[0].op = '='; list[0].flag = MODE_ORDINARY; list[0].affected = MODE_BITS; list[0].value = rs.st_mode & MODE_BITS;
        list[0].mentioned = MODE_BITS; list[1].op = 0;
        x.change = list;
        if (modestr) files[nfiles++] = modestr;
    } else {
        if (!modestr) missing_operand();
        if (!(x.change = mode_compile(modestr))) { say("invalid mode: '%s'", modestr); usage_error(); }
    }
    if (!nfiles) { if (reference) missing_operand(); say("missing operand after '%s'", modestr); usage_error(); }
    x.um = current_umask();
    for (i = 0; i < nfiles; i++) st |= walk(files[i], 1, 1, 0, recurse, chmod_visit, &x);
    return finish_stdout() ? 1 : st;
}

struct chown_ctx {
    int have_u, have_g, verbose, changes_only, quiet, deref;
    uid_t u; gid_t g;
    int from_u, from_g; uid_t fu; gid_t fg;
};

static int chown_visit(const char *path, const struct stat *st, void *v)
{
    struct chown_ctx *x = v;
    uid_t nu = x->have_u ? x->u : st->st_uid;
    gid_t ng = x->have_g ? x->g : st->st_gid;
    int ch;
    if (x->from_u && st->st_uid != x->fu) return 0;
    if (x->from_g && st->st_gid != x->fg) return 0;
    if ((S_ISLNK(st->st_mode) ? lchown(path, x->have_u ? x->u : (uid_t)-1, x->have_g ? x->g : (gid_t)-1)
                              : chown(path, x->have_u ? x->u : (uid_t)-1, x->have_g ? x->g : (gid_t)-1)) < 0) {
        if (!x->quiet) perr("changing ownership of '%s'", path);
        return 1;
    }
    ch = nu != st->st_uid || ng != st->st_gid;
    if (x->verbose || (x->changes_only && ch)) {
        char old[100], new[100];
        const char *what = x->have_u ? "ownership" : "group";
        if (x->have_u && x->have_g) {
            snprintf(old, sizeof old, "%s:%s", user_name(st->st_uid), group_name(st->st_gid));
            snprintf(new, sizeof new, "%s:%s", user_name(nu), group_name(ng));
        } else if (x->have_u) {
            snprintf(old, sizeof old, "%s", user_name(st->st_uid)); snprintf(new, sizeof new, "%s", user_name(nu));
        } else {
            snprintf(old, sizeof old, "%s", group_name(st->st_gid)); snprintf(new, sizeof new, "%s", group_name(ng));
        }
        if (ch) printf("changed %s of '%s' from %s to %s\n", what, path, old, new);
        else printf("%s of '%s' retained as %s\n", what, path, new);
    }
    return 0;
}

/* "user", "user:group", "user:", ":group", or numbers. returns -1 after saying what was wrong */
static int parse_owner(const char *spec, struct chown_ctx *x, int group_only)
{
    char *s = xstrdup(spec), *colon = group_only ? NULL : strchr(s, ':'), *g = NULL, *u = s;
    if (group_only) g = s, u = NULL;
    else if (!colon && strchr(s, '.') && lookup_user(s, &x->u) < 0) colon = strchr(s, '.');
    if (colon) { *colon = 0; g = colon + 1; }
    if (u && *u) {
        if (lookup_user(u, &x->u) < 0) { say("invalid user: '%s'", spec); return -1; }
        x->have_u = 1;
    }
    if (g && *g) {
        if (lookup_group(g, &x->g) < 0) { say("invalid group: '%s'", spec); return -1; }
        x->have_g = 1;
    } else if (colon && x->have_u) {
        struct passwd *p = getpwuid(x->u);
        if (!p) { say("invalid spec: '%s'", spec); return -1; }
        x->g = p->pw_gid; x->have_g = 1;
    }
    free(s);
    return 0;
}

static int t_chown(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "recursive", 0, 'R' }, { "verbose", 0, 'v' }, { "changes", 0, 'c' }, { "silent", 0, 'f' }, { "quiet", 0, 'f' },
        { "no-dereference", 0, 'h' }, { "dereference", 0, 'D' }, { "from", 1, 'F' }, { "reference", 1, 'r' },
        { "preserve-root", 0, 'p' }, { "no-preserve-root", 0, 'n' }, { NULL, 0, 0 } };
    struct chown_ctx x; int c, recurse = 0, follow_top = 0, follow_all = 0, deref = -1, i, st = 0, isgrp = !strcmp(prog, "chgrp");
    const char *reference = NULL, *from = NULL;
    memset(&x, 0, sizeof x);
    opts_reset();
    while ((c = getopts(argc, argv, "RvcfhHLP", lo)))
        switch (c) {
        case 'R': recurse = 1; break;
        case 'v': x.verbose = 1; break;
        case 'c': x.changes_only = 1; break;
        case 'f': x.quiet = 1; break;
        case 'h': deref = 0; break;
        case 'D': deref = 1; break;
        case 'H': follow_top = 1; break;
        case 'L': follow_all = follow_top = 1; break;
        case 'P': follow_top = follow_all = 0; break;
        case 'F': from = optarg; break;
        case 'r': reference = optarg; break;
        default: break;
        }
    if (!nops) missing_operand();
    if (reference) {
        struct stat rs;
        if (stat(reference, &rs) < 0) die(1, "failed to get attributes of '%s': %s", reference, strerror(errno));
        if (!isgrp) { x.u = rs.st_uid; x.have_u = 1; }
        x.g = rs.st_gid; x.have_g = 1;
    } else {
        if (nops < 2) { say("missing operand after '%s'", ops[0]); usage_error(); }
        if (parse_owner(ops[0], &x, isgrp) < 0) return 1;
        ops++; nops--;
    }
    if (from) {
        struct chown_ctx f; memset(&f, 0, sizeof f);
        if (parse_owner(from, &f, 0) < 0) return 1;
        x.from_u = f.have_u; x.fu = f.u; x.from_g = f.have_g; x.fg = f.g;
    }
    if (!nops) missing_operand();
    if (!recurse) { follow_top = deref != 0; follow_all = 0; }
    else if (deref == 0) follow_top = follow_all = 0;
    for (i = 0; i < nops; i++) st |= walk(ops[i], 1, follow_top, follow_all, recurse, chown_visit, &x);
    return finish_stdout() ? 1 : st;
}

/* ---- mkfifo mknod ---- */

static int t_mkfifo(int argc, char **argv)
{
    static const struct opt lo[] = { { "mode", 1, 'm' }, { "context", 2, 'Z' }, { NULL, 0, 0 } };
    int c, i, st = 0;
    struct change *change = NULL;
    mode_t um = current_umask(), mode = 0666;
    opts_reset();
    while ((c = getopts(argc, argv, "m:Z", lo)))
        if (c == 'm' && !(change = mode_compile(optarg))) die(1, "invalid mode '%s'", optarg);
    if (!nops) missing_operand();
    if (change) mode = mode_apply(0666 & ~um, 0, um, change);
    for (i = 0; i < nops; i++) {
        if (change) umask(0);
        if (mkfifo(ops[i], mode) < 0) { perr("cannot create fifo '%s'", ops[i]); st = 1; }
        if (change) umask(um);
    }
    return st;
}

static int t_mknod(int argc, char **argv)
{
    static const struct opt lo[] = { { "mode", 1, 'm' }, { "context", 2, 'Z' }, { NULL, 0, 0 } };
    int c;
    struct change *change = NULL;
    mode_t um = current_umask(), mode = 0666, type;
    dev_t dev = 0;
    opts_reset();
    while ((c = getopts(argc, argv, "m:Z", lo)))
        if (c == 'm' && !(change = mode_compile(optarg))) die(1, "invalid mode '%s'", optarg);
    if (nops < 1) missing_operand();
    if (nops < 2) { say("missing operand after '%s'", ops[0]); usage_error(); }
    if (change) mode = mode_apply(0666 & ~um, 0, um, change);
    if (strlen(ops[1]) != 1 || !strchr("pbcu", ops[1][0])) { say("invalid device type '%s'", ops[1]); usage_error(); }
    if (ops[1][0] == 'p') {
        if (nops > 2) { say("Fifos do not have major and minor device numbers."); usage_error(); }
        type = S_IFIFO;
    } else {
        unsigned long ma, mi;
        if (nops < 4) { say("Special files require major and minor device numbers."); usage_error(); }
        if (nops > 4) extra_operand(ops[4]);
        if (!parse_ulong(ops[2], &ma)) die(1, "invalid major device number '%s'", ops[2]);
        if (!parse_ulong(ops[3], &mi)) die(1, "invalid minor device number '%s'", ops[3]);
        dev = makedev(ma, mi);
        type = ops[1][0] == 'b' ? S_IFBLK : S_IFCHR;
    }
    if (change) umask(0);
    c = mknod(ops[0], type | (mode & 07777), dev);
    if (change) umask(um);
    if (c < 0) { perr("'%s'", ops[0]); return 1; }
    return 0;
}

/* ---- touch ---- */

/* [[CC]YY]MMDDhhmm[.ss] */
static int parse_stamp(const char *s, struct timespec *ts)
{
    char buf[32];
    size_t n, i;
    long cc = 0, yy, mo, d, h, mi, sec = 0;
    struct tm t;
    const char *dot = strchr(s, '.');
    n = dot ? (size_t)(dot - s) : strlen(s);
    if (n != 8 && n != 10 && n != 12) return -1;
    for (i = 0; i < n; i++) if (!isdigit((unsigned char)s[i])) return -1;
    memcpy(buf, s, n); buf[n] = 0;
    if (dot) { if (strlen(dot) != 3 || !isdigit((unsigned char)dot[1]) || !isdigit((unsigned char)dot[2])) return -1; sec = (dot[1] - '0') * 10 + dot[2] - '0'; }
#define TWO(p) ((p)[0] - '0') * 10 + (p)[1] - '0'
    {
        const char *p = buf;
        long year;
        if (n == 12) { cc = TWO(p); p += 2; yy = TWO(p); p += 2; year = cc * 100 + yy; }
        else if (n == 10) { yy = TWO(p); p += 2; year = yy < 69 ? 2000 + yy : 1900 + yy; }
        else { clock_gettime(0, ts); { time_t now = ts->tv_sec; year = localtime(&now)->tm_year + 1900; } }
        mo = TWO(p); p += 2; d = TWO(p); p += 2; h = TWO(p); p += 2; mi = TWO(p);
        if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 61) return -1;
        memset(&t, 0, sizeof t);
        t.tm_year = year - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = sec;
    }
    ts->tv_sec = timegm(&t); ts->tv_nsec = 0;
    return 0;
}

static int t_touch(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "no-create", 0, 'c' }, { "date", 1, 'd' }, { "no-dereference", 0, 'h' }, { "reference", 1, 'r' }, { "time", 1, 'T' }, { NULL, 0, 0 } };
    int c, a_only = 0, m_only = 0, nocreate = 0, nofollow = 0, i, st = 0;
    const char *datestr = NULL, *stamp = NULL, *ref = NULL;
    struct timespec ts[2];
    opts_reset();
    while ((c = getopts(argc, argv, "acd:fhmr:t:", lo)))
        switch (c) {
        case 'a': a_only = 1; break;
        case 'm': m_only = 1; break;
        case 'c': nocreate = 1; break;
        case 'd': datestr = optarg; break;
        case 'h': nofollow = 1; break;
        case 'r': ref = optarg; break;
        case 't': stamp = optarg; break;
        case 'T':
            if (!strcmp(optarg, "atime") || !strcmp(optarg, "access") || !strcmp(optarg, "use")) a_only = 1;
            else if (!strcmp(optarg, "mtime") || !strcmp(optarg, "modify")) m_only = 1;
            else die(1, "invalid argument '%s' for '--time'", optarg);
            break;
        }
    if (!nops) { say("missing file operand"); usage_error(); }
    ts[0].tv_sec = ts[1].tv_sec = 0; ts[0].tv_nsec = ts[1].tv_nsec = UTIME_NOW;
    if (ref) {
        struct stat rs;
        if ((nofollow ? lstat(ref, &rs) : stat(ref, &rs)) < 0) die(1, "failed to get attributes of '%s': %s", ref, strerror(errno));
        ts[0].tv_sec = rs.st_atime; ts[0].tv_nsec = rs.st_atime_nsec;
        ts[1].tv_sec = rs.st_mtime; ts[1].tv_nsec = rs.st_mtime_nsec;
    }
    if (datestr) {
        struct timespec when;
        if (parse_date(datestr, &when) < 0) die(1, "invalid date format '%s'", datestr);
        ts[0] = ts[1] = when;
    }
    if (stamp) {
        struct timespec when;
        if (parse_stamp(stamp, &when) < 0) die(1, "invalid date format '%s'", stamp);
        ts[0] = ts[1] = when;
    }
    if (a_only && !m_only) ts[1].tv_nsec = UTIME_OMIT;
    if (m_only && !a_only) ts[0].tv_nsec = UTIME_OMIT;
    for (i = 0; i < nops; i++) {
        int flags = nofollow ? AT_SYMLINK_NOFOLLOW : 0, r;
        if (!strcmp(ops[i], "-")) { r = utimensat(1, NULL, ts, 0); if (r < 0) { perr("setting times of standard output"); st = 1; } continue; }
        r = utimensat(AT_FDCWD, ops[i], ts, flags);
        if (r < 0 && errno == ENOENT && !nocreate) {
            int fd = open(ops[i], O_WRONLY | O_CREAT | O_NOCTTY | O_NONBLOCK, 0666);
            if (fd >= 0) { close(fd); r = utimensat(AT_FDCWD, ops[i], ts, flags); }
        }
        if (r < 0 && !(nocreate && errno == ENOENT)) { perr("cannot touch '%s'", ops[i]); st = 1; }
    }
    return st;
}

/* ---- ln ---- */

static int t_ln(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "symbolic", 0, 's' }, { "force", 0, 'f' }, { "no-dereference", 0, 'n' }, { "verbose", 0, 'v' }, { "relative", 0, 'r' },
        { "no-target-directory", 0, 'T' }, { "target-directory", 1, 't' }, { "interactive", 0, 'i' }, { "logical", 0, 'L' },
        { "physical", 0, 'P' }, { NULL, 0, 0 } };
    int c, sym = 0, force = 0, nofollow = 0, verbose = 0, rel = 0, notdir = 0, ask = 0, i, st = 0, ntargets;
    const char *tdir = NULL;
    char **targets;
    struct stat sb;
    opts_reset();
    while ((c = getopts(argc, argv, "sfnvrTt:iLPdF", lo)))
        switch (c) {
        case 's': sym = 1; break; case 'f': force = 1; ask = 0; break; case 'n': nofollow = 1; break;
        case 'v': verbose = 1; break; case 'r': rel = 1; break; case 'T': notdir = 1; break;
        case 't': tdir = optarg; break; case 'i': ask = 1; force = 0; break;
        default: break;
        }
    if (!nops) missing_operand();
    if (tdir && notdir) { say("cannot combine --target-directory and --no-target-directory"); usage_error(); }
    if (tdir) { targets = ops; ntargets = nops; }
    else if (notdir) {
        if (nops < 2) { say("missing destination file operand after '%s'", ops[0]); usage_error(); }
        if (nops > 2) extra_operand(ops[2]);
        targets = ops; ntargets = 1; tdir = NULL;
    } else if (nops == 1) { targets = ops; ntargets = 1; tdir = "."; }
    else {
        const char *last = ops[nops - 1];
        int isdir = (nofollow ? lstat(last, &sb) : stat(last, &sb)) == 0 && S_ISDIR(sb.st_mode);
        if (isdir) { tdir = last; targets = ops; ntargets = nops - 1; }
        else if (nops == 2) { targets = ops; ntargets = 1; }
        else { say("target '%s' is not a directory", last); usage_error(); }
    }
    for (i = 0; i < ntargets; i++) {
        const char *src = targets[i];
        char *dest, *tmp = NULL;
        if (tdir) {
            char *b;
            tmp = xstrdup(src);
            { size_t n = strlen(tmp); while (n > 1 && tmp[n - 1] == '/') tmp[--n] = 0; }
            b = strrchr(tmp, '/'); b = b && b[1] ? b + 1 : tmp;
            dest = xmalloc(strlen(tdir) + strlen(b) + 2);
            sprintf(dest, "%s%s%s", tdir, tdir[strlen(tdir) - 1] == '/' ? "" : "/", b);
        } else dest = xstrdup(targets[ntargets]);
        if (!sym) {
            struct stat a, b;
            if (stat(src, &a) < 0 && lstat(src, &a) < 0) { perr("failed to access '%s'", src); st = 1; goto next; }
            if (lstat(dest, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino && !S_ISLNK(b.st_mode) && !S_ISLNK(a.st_mode)) {
                say("'%s' and '%s' are the same file", src, dest); st = 1; goto next;
            }
        }
        if (ask && lstat(dest, &sb) == 0) {
            char line[8];
            fprintf(stderr, "%s: replace '%s'? ", prog, dest);
            if (!fgets(line, sizeof line, stdin) || (line[0] != 'y' && line[0] != 'Y')) goto next;
            force = 1;
        }
        {
            const char *text = src;
            char *relp = NULL;
            if (sym && rel) {
                char *dd = xstrdup(dest), *s2, *d2, *dir;
                char *sl = strrchr(dd, '/');
                if (sl) { if (sl == dd) dd[1] = 0; else *sl = 0; } else strcpy(dd, ".");
                s2 = canonicalize(src, CAN_MISSING, 0); d2 = canonicalize(dd, CAN_MISSING, 0);
                if (s2 && d2) { dir = relative_path(d2, s2); relp = dir; text = relp; }
                free(s2); free(d2); free(dd);
            }
            for (;;) {
                int r = sym ? symlink(text, dest) : link(src, dest);
                if (r == 0) {
                    if (verbose) printf("'%s' %s '%s'\n", dest, sym ? "->" : "=>", text);
                    break;
                }
                if (errno == EEXIST && force && unlink(dest) == 0) continue;
                if (sym) perr("failed to create symbolic link '%s'", dest);
                else perr("failed to create hard link '%s' => '%s'", dest, src);
                st = 1;
                break;
            }
            free(relp);
        }
next:
        free(dest); free(tmp);
    }
    return finish_stdout() ? 1 : st;
}

/* ---- cp mv ---- */

/* where each operand goes: into a directory (*tdir) or onto one name (the last operand) */
static int plan_dest(const char *tdir_opt, int notdir, int follow, const char **tdir, char ***srcs, int *nsrcs)
{
    struct stat sb;
    *tdir = tdir_opt;
    *srcs = ops; *nsrcs = nops;
    if (!nops) missing_operand();
    if (tdir_opt && notdir) { say("cannot combine --target-directory and --no-target-directory"); usage_error(); }
    if (tdir_opt) return 0;
    if (nops == 1) { say("missing destination file operand after '%s'", ops[0]); usage_error(); }
    if (notdir) {
        if (nops > 2) extra_operand(ops[2]);
        *nsrcs = 1;
        return 0;
    }
    *nsrcs = nops - 1;
    if ((follow ? stat(ops[nops - 1], &sb) : lstat(ops[nops - 1], &sb)) == 0 && S_ISDIR(sb.st_mode)) *tdir = ops[nops - 1];
    else if (nops > 2) { say("target '%s' is not a directory", ops[nops - 1]); usage_error(); }
    return 0;
}

static char *dest_in(const char *dir, const char *src)
{
    char *t = xstrdup(src), *b, *d;
    size_t n = strlen(t);
    while (n > 1 && t[n - 1] == '/') t[--n] = 0;
    b = strrchr(t, '/'); b = b && b[1] ? b + 1 : t;
    d = xmalloc(strlen(dir) + strlen(b) + 2);
    sprintf(d, "%s%s%s", dir, dir[strlen(dir) - 1] == '/' ? "" : "/", b);
    free(t);
    return d;
}

static int yes_answer(void)
{
    char line[16];
    return fgets(line, sizeof line, stdin) && (line[0] == 'y' || line[0] == 'Y');
}

struct cp {
    int rec, force, ask, keep, verbose, preserve, follow_top, follow_all, hard, sym, update;
    mode_t um;
    dev_t top_dev; ino_t top_ino;           /* the destination directory we are filling */
    const char *top_src, *top_dest;
};

static int cp_data(int in, int out)
{
    char buf[65536];
    ssize_t n;
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write_all(out, buf, n) < 0) return -1;
    return n < 0 ? -1 : 0;
}

static void cp_keep(const struct cp *o, const char *dest, const struct stat *st, int fd)
{
    struct timespec t[2];
    if (!o->preserve) return;
    if (fd >= 0) { if (fchown(fd, st->st_uid, st->st_gid) < 0) fchown(fd, -1, st->st_gid); }
    else if (lchown(dest, st->st_uid, st->st_gid) < 0) lchown(dest, -1, st->st_gid);
    if (!S_ISLNK(st->st_mode)) {
        if (fd >= 0) fchmod(fd, st->st_mode & 07777); else chmod(dest, st->st_mode & 07777);
    }
    t[0].tv_sec = st->st_atime; t[0].tv_nsec = st->st_atime_nsec;
    t[1].tv_sec = st->st_mtime; t[1].tv_nsec = st->st_mtime_nsec;
    utimensat(AT_FDCWD, dest, t, S_ISLNK(st->st_mode) ? AT_SYMLINK_NOFOLLOW : 0);
}

static int cp_one(struct cp *o, const char *src, const char *dest, int top)
{
    struct stat st, ds;
    int follow = top ? o->follow_top : o->follow_all, have;
    if ((follow ? stat(src, &st) : lstat(src, &st)) < 0) { perr("cannot stat '%s'", src); return 1; }
    have = lstat(dest, &ds) == 0;
    if (have && !S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) && !o->hard) {
        struct stat dd;
        if (stat(dest, &dd) == 0 && dd.st_dev == st.st_dev && dd.st_ino == st.st_ino) {
            say("'%s' and '%s' are the same file", src, dest);
            return 1;
        }
    }
    if (S_ISDIR(st.st_mode)) {
        mode_t m;
        int bad = 0, n = 0, cap = 0, i;
        DIR *d;
        struct dirent *e;
        char **names = NULL;
        if (!o->rec) { say("-r not specified; omitting directory '%s'", src); return 1; }
        if (o->top_ino && st.st_dev == o->top_dev && st.st_ino == o->top_ino) {
            say("cannot copy a directory, '%s', into itself, '%s'", o->top_src, o->top_dest);
            return 1;
        }
        if (have && !S_ISDIR(ds.st_mode)) { say("cannot overwrite non-directory '%s' with directory '%s'", dest, src); return 1; }
        m = o->preserve ? st.st_mode & 07777 : st.st_mode & ~o->um & 0777;
        if (!have) {
            if (mkdir(dest, m | S_IRWXU) < 0) { perr("cannot create directory '%s'", dest); return 1; }
            if (o->verbose) printf("'%s' -> '%s'\n", src, dest);
        }
        if (top) {
            struct stat t;
            if (stat(dest, &t) == 0) { o->top_dev = t.st_dev; o->top_ino = t.st_ino; o->top_src = src; o->top_dest = dest; }
        }
        d = opendir(src);
        if (!d) { perr("cannot access '%s'", src); return 1; }
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (n == cap) { cap = cap ? cap * 2 : 32; names = xrealloc(names, cap * sizeof *names); }
            names[n++] = xstrdup(e->d_name);
        }
        closedir(d);
        for (i = 0; i < n; i++) {
            char *cs = xmalloc(strlen(src) + strlen(names[i]) + 2), *cd = xmalloc(strlen(dest) + strlen(names[i]) + 2);
            sprintf(cs, "%s%s%s", src, src[strlen(src) - 1] == '/' ? "" : "/", names[i]);
            sprintf(cd, "%s%s%s", dest, dest[strlen(dest) - 1] == '/' ? "" : "/", names[i]);
            bad |= cp_one(o, cs, cd, 0);
            free(cs); free(cd); free(names[i]);
        }
        free(names);
        if (!have) chmod(dest, m);
        cp_keep(o, dest, &st, -1);
        return bad;
    }
    if (have && S_ISDIR(ds.st_mode)) { say("cannot overwrite directory '%s' with non-directory", dest); return 1; }
    if (have) {
        if (o->update) {
            struct stat dd;
            if (stat(dest, &dd) == 0 && (dd.st_mtime > st.st_mtime || (dd.st_mtime == st.st_mtime && dd.st_mtime_nsec >= st.st_mtime_nsec))) return 0;
        }
        if (o->keep) return 0;
        if (o->ask) {
            fflush(stdout);
            fprintf(stderr, "%s: overwrite '%s'? ", prog, dest);
            if (!yes_answer()) return 0;
        }
    }
    if (o->hard || o->sym) {
        int r;
        if (have && (unlink(dest) < 0 && errno != ENOENT)) { perr("cannot remove '%s'", dest); return 1; }
        r = o->sym ? symlink(src, dest) : link(src, dest);
        if (r < 0) { perr("cannot create %s link '%s'", o->sym ? "symbolic" : "hard", dest); return 1; }
        if (o->verbose) printf("'%s' -> '%s'\n", src, dest);
        return 0;
    }
    if (S_ISLNK(st.st_mode)) {
        char tgt[PATH_MAX];
        ssize_t n = readlink(src, tgt, sizeof tgt - 1);
        if (n < 0) { perr("cannot read symbolic link '%s'", src); return 1; }
        tgt[n] = 0;
        if (have) unlink(dest);
        if (symlink(tgt, dest) < 0) { perr("cannot create symbolic link '%s'", dest); return 1; }
        if (o->verbose) printf("'%s' -> '%s'\n", src, dest);
        cp_keep(o, dest, &st, -1);
        return 0;
    }
    if (S_ISFIFO(st.st_mode) && o->rec) {
        if (have) unlink(dest);
        if (mkfifo(dest, st.st_mode & 0777 & ~o->um) < 0) { perr("cannot create fifo '%s'", dest); return 1; }
        if (o->verbose) printf("'%s' -> '%s'\n", src, dest);
        cp_keep(o, dest, &st, -1);
        return 0;
    }
    if ((S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) && o->rec) {
        if (have) unlink(dest);
        if (mknod(dest, st.st_mode, st.st_rdev) < 0) { perr("cannot create special file '%s'", dest); return 1; }
        if (o->verbose) printf("'%s' -> '%s'\n", src, dest);
        cp_keep(o, dest, &st, -1);
        return 0;
    }
    {
        int in = open(src, O_RDONLY), out;
        mode_t m = o->preserve ? st.st_mode & 07777 : st.st_mode & ~o->um & 0777;
        if (in < 0) { perr("cannot open '%s' for reading", src); return 1; }
        out = open(dest, O_WRONLY | O_CREAT | (have ? O_TRUNC : O_EXCL), m);
        if (out < 0 && have && o->force) { unlink(dest); out = open(dest, O_WRONLY | O_CREAT | O_EXCL, m); }
        if (out < 0) { perr("cannot create regular file '%s'", dest); close(in); return 1; }
        if (o->verbose) printf("'%s' -> '%s'\n", src, dest);
        if (cp_data(in, out) < 0) { perr("error copying '%s'", src); close(in); close(out); return 1; }
        cp_keep(o, dest, &st, out);
        close(in);
        if (close(out) < 0) { perr("failed to close '%s'", dest); return 1; }
    }
    return 0;
}

static int t_cp(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "recursive", 0, 'R' }, { "archive", 0, 'a' }, { "force", 0, 'f' }, { "interactive", 0, 'i' }, { "no-clobber", 0, 'n' },
        { "verbose", 0, 'v' }, { "preserve", 2, 'p' }, { "no-dereference", 0, 'P' }, { "dereference", 0, 'L' },
        { "link", 0, 'l' }, { "symbolic-link", 0, 's' }, { "update", 0, 'u' }, { "no-target-directory", 0, 'T' },
        { "target-directory", 1, 't' }, { NULL, 0, 0 } };
    struct cp o = { 0 };
    int c, notdir = 0, bad = 0, i, nsrc, h = 0, l = 0, p = 0;
    const char *tdir = NULL;
    char **srcs;
    opts_reset();
    while ((c = getopts(argc, argv, "rRafinvpPLHlsuTt:d", lo)))
        switch (c) {
        case 'r': case 'R': o.rec = 1; break;
        case 'a': o.rec = 1; o.preserve = 1; p = 1; break;
        case 'f': o.force = 1; o.ask = 0; break;
        case 'i': o.ask = 1; o.keep = 0; break;
        case 'n': o.keep = 1; o.ask = 0; break;
        case 'v': o.verbose = 1; break;
        case 'p': o.preserve = 1; break;
        case 'P': p = 1; l = 0; h = 0; break;
        case 'L': l = 1; p = 0; h = 0; break;
        case 'H': h = 1; l = 0; p = 0; break;
        case 'd': p = 1; l = 0; h = 0; break;
        case 'l': o.hard = 1; break;
        case 's': o.sym = 1; break;
        case 'u': o.update = 1; break;
        case 'T': notdir = 1; break;
        case 't': tdir = optarg; break;
        default: break;
        }
    o.um = current_umask();
    o.follow_all = l || (!o.rec && !p);
    o.follow_top = l || h || o.follow_all;
    plan_dest(tdir, notdir, 1, &tdir, &srcs, &nsrc);
    for (i = 0; i < nsrc; i++) {
        char *dest = tdir ? dest_in(tdir, srcs[i]) : xstrdup(ops[nops - 1]);
        o.top_ino = 0;
        bad |= cp_one(&o, srcs[i], dest, 1);
        free(dest);
    }
    return finish_stdout() ? 1 : bad;
}

static int t_mv(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "force", 0, 'f' }, { "interactive", 0, 'i' }, { "no-clobber", 0, 'n' }, { "verbose", 0, 'v' },
        { "update", 0, 'u' }, { "no-target-directory", 0, 'T' }, { "target-directory", 1, 't' }, { NULL, 0, 0 } };
    int c, notdir = 0, bad = 0, i, nsrc, force = 0, ask = 0, keep = 0, verbose = 0, update = 0;
    const char *tdir = NULL;
    char **srcs;
    opts_reset();
    while ((c = getopts(argc, argv, "finvuTt:", lo)))
        switch (c) {
        case 'f': force = 1; ask = 0; keep = 0; break;
        case 'i': ask = 1; force = 0; keep = 0; break;
        case 'n': keep = 1; ask = 0; force = 0; break;
        case 'v': verbose = 1; break;
        case 'u': update = 1; break;
        case 'T': notdir = 1; break;
        case 't': tdir = optarg; break;
        default: break;
        }
    plan_dest(tdir, notdir, 1, &tdir, &srcs, &nsrc);
    for (i = 0; i < nsrc; i++) {
        const char *src = srcs[i];
        char *dest = tdir ? dest_in(tdir, src) : xstrdup(ops[nops - 1]);
        struct stat s, d;
        int have;
        if (lstat(src, &s) < 0) { perr("cannot stat '%s'", src); bad = 1; goto next; }
        have = lstat(dest, &d) == 0;
        if (have && s.st_dev == d.st_dev && s.st_ino == d.st_ino && !S_ISLNK(s.st_mode)) {
            say("'%s' and '%s' are the same file", src, dest); bad = 1; goto next;
        }
        if (have && S_ISDIR(d.st_mode) && !S_ISDIR(s.st_mode)) { say("cannot overwrite directory '%s' with non-directory", dest); bad = 1; goto next; }
        if (have && !S_ISDIR(d.st_mode) && S_ISDIR(s.st_mode)) { say("cannot overwrite non-directory '%s' with directory '%s'", dest, src); bad = 1; goto next; }
        if (have && update && s.st_mtime < d.st_mtime) goto next;
        if (have && keep) goto next;
        if (have && (ask || (!force && access(dest, W_OK) < 0 && !S_ISLNK(d.st_mode) && isatty(0)))) {
            fflush(stdout);
            fprintf(stderr, ask ? "%s: overwrite '%s'? " : "%s: replace '%s', overriding mode? ", prog, dest);
            if (!yes_answer()) goto next;
        }
        if (rename(src, dest) == 0) {
            if (verbose) printf("renamed '%s' -> '%s'\n", src, dest);
        } else if (errno == EXDEV) {
            struct cp o = { 0 };
            struct rm_opts r = { 1, 0, 1, 0, 0 };
            o.rec = 1; o.preserve = 1; o.verbose = verbose; o.um = current_umask();
            if (have && !S_ISDIR(d.st_mode)) unlink(dest);
            if (cp_one(&o, src, dest, 1)) { bad = 1; goto next; }
            if (rm_entry(AT_FDCWD, src, src, &r)) bad = 1;
        } else if (errno == EINVAL && S_ISDIR(s.st_mode)) {
            say("cannot move '%s' to a subdirectory of itself, '%s'", src, dest); bad = 1;
        } else { perr("cannot move '%s' to '%s'", src, dest); bad = 1; }
next:
        free(dest);
    }
    return finish_stdout() ? 1 : bad;
}

/* ---- readlink realpath ---- */

static int t_readlink(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "canonicalize", 0, 'f' }, { "canonicalize-existing", 0, 'e' }, { "canonicalize-missing", 0, 'm' },
        { "no-newline", 0, 'n' }, { "quiet", 0, 'q' }, { "silent", 0, 's' }, { "verbose", 0, 'v' }, { "zero", 0, 'z' }, { NULL, 0, 0 } };
    int c, mode = -1, nonl = 0, verbose = 0, zero = 0, i, st = 0;
    opts_reset();
    while ((c = getopts(argc, argv, "femnqsvz", lo)))
        switch (c) {
        case 'f': mode = CAN_ALL_BUT_LAST; break; case 'e': mode = CAN_EXISTING; break; case 'm': mode = CAN_MISSING; break;
        case 'n': nonl = 1; break; case 'v': verbose = 1; break; case 'z': zero = 1; break;
        }
    if (!nops) missing_operand();
    if (nops > 1 && nonl) { say("ignoring --no-newline with multiple arguments"); nonl = 0; }
    for (i = 0; i < nops; i++) {
        char buf[PATH_MAX], *r;
        if (mode < 0) {
            ssize_t n = readlink(ops[i], buf, sizeof buf - 1);
            if (n < 0) { if (verbose) perr("%s", ops[i]); st = 1; continue; }
            buf[n] = 0; r = buf;
        } else {
            r = canonicalize(ops[i], mode, 0);
            if (!r) { if (verbose) perr("%s", ops[i]); st = 1; continue; }
        }
        fputs(r, stdout);
        if (!nonl) putchar(zero ? 0 : '\n');
        if (r != buf) free(r);
    }
    return finish_stdout() ? 1 : st;
}

static int t_realpath(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "canonicalize-existing", 0, 'e' }, { "canonicalize-missing", 0, 'm' }, { "logical", 0, 'L' }, { "physical", 0, 'P' },
        { "strip", 0, 's' }, { "no-symlinks", 0, 's' }, { "quiet", 0, 'q' }, { "zero", 0, 'z' }, { "relative-to", 1, 'R' },
        { "relative-base", 1, 'B' }, { NULL, 0, 0 } };
    int c, mode = CAN_ALL_BUT_LAST, nolinks = 0, quiet = 0, zero = 0, i, st = 0;
    const char *relto = NULL, *relbase = NULL;
    opts_reset();
    while ((c = getopts(argc, argv, "emLPsqz", lo)))
        switch (c) {
        case 'e': mode = CAN_EXISTING; break; case 'm': mode = CAN_MISSING; break; case 's': nolinks = 1; break;
        case 'L': nolinks = 0; break; case 'P': nolinks = 0; break; case 'q': quiet = 1; break; case 'z': zero = 1; break;
        case 'R': relto = optarg; break; case 'B': relbase = optarg; break;
        }
    if (!nops) missing_operand();
    {
        char *to = relto ? canonicalize(relto, mode == CAN_EXISTING ? CAN_EXISTING : CAN_MISSING, nolinks) : NULL;
        char *base = relbase ? canonicalize(relbase, CAN_MISSING, nolinks) : NULL;
        if (relto && !to) die(1, "%s: %s", relto, strerror(errno));
        if (relbase && !base) die(1, "%s: %s", relbase, strerror(errno));
        if (base && !to) to = base;
        for (i = 0; i < nops; i++) {
            char *r = canonicalize(ops[i], mode, nolinks), *out = r;
            if (r && mode == CAN_EXISTING && nolinks) { struct stat sb; if (lstat(r, &sb) < 0) { free(r); r = NULL; } }
            if (!r) { if (!quiet) perr("%s", ops[i]); st = 1; continue; }
            if (to) {
                size_t bl = base ? strlen(base) : 0;
                int inside = !base || (!strncmp(r, base, bl) && (r[bl] == '/' || !r[bl] || bl == 1));
                if (inside) out = relative_path(to, r);
            }
            fputs(out, stdout);
            putchar(zero ? 0 : '\n');
            if (out != r) free(out);
            free(r);
        }
    }
    return finish_stdout() ? 1 : st;
}

/* ---- mktemp ---- */

static int t_mktemp(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "directory", 0, 'd' }, { "dry-run", 0, 'u' }, { "quiet", 0, 'q' }, { "tmpdir", 2, 'p' }, { "suffix", 1, 'S' }, { NULL, 0, 0 } };
    int c, dir = 0, dry = 0, quiet = 0, use_tmpdir = 0, tries;
    const char *tmpdir = NULL, *suffix = "", *tmpl;
    char *path, *x;
    size_t len, nx = 0, sl;
    static const char alpha[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    opts_reset();
    while ((c = getopts(argc, argv, "dqp:tuV", lo)))
        switch (c) {
        case 'd': dir = 1; break; case 'u': dry = 1; break; case 'q': quiet = 1; break;
        case 'p': use_tmpdir = 1; tmpdir = optarg; break; case 't': use_tmpdir = 1; break;
        case 'S': suffix = optarg; break; default: break;
        }
    if (nops > 1) extra_operand(ops[1]);
    if (!nops) { tmpl = "tmp.XXXXXXXXXX"; use_tmpdir = 1; } else tmpl = ops[0];
    if (use_tmpdir && !tmpdir) tmpdir = getenv("TMPDIR") && *getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    if (strchr(suffix, '/')) die(1, "invalid suffix '%s', contains directory separator", suffix);
    len = strlen(tmpl); sl = strlen(suffix);
    if (use_tmpdir && strchr(tmpl, '/') && nops) die(1, "invalid template, '%s', contains directory separator", tmpl);
    path = xmalloc((tmpdir ? strlen(tmpdir) : 0) + len + sl + 4);
    path[0] = 0;
    if (use_tmpdir) { strcpy(path, tmpdir); if (path[0] && path[strlen(path) - 1] != '/') strcat(path, "/"); }
    strcat(path, tmpl); strcat(path, suffix);
    {
        size_t end = strlen(path) - sl;
        x = path + end;
        while (x > path && x[-1] == 'X') { x--; nx++; }
    }
    if (nx < 3) { if (!quiet) say("too few X's in template '%s'", tmpl); return 1; }
    for (tries = 0; tries < 100; tries++) {
        unsigned char rnd[64];
        size_t k;
        if (getrandom(rnd, nx, 0) < (ssize_t)nx) { size_t j; unsigned long long v = (unsigned long long)time(NULL) * 6364136223846793005ULL + tries; for (j = 0; j < nx; j++) { v = v * 6364136223846793005ULL + 1442695040888963407ULL; rnd[j] = v >> 33; } }
        for (k = 0; k < nx; k++) x[k] = alpha[rnd[k] % (sizeof alpha - 1)];
        if (dry) { struct stat sb; if (lstat(path, &sb) < 0 && errno == ENOENT) break; continue; }
        if (dir) { if (mkdir(path, 0700) == 0) break; }
        else { int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600); if (fd >= 0) { close(fd); break; } }
        if (errno != EEXIST) { if (!quiet) perr("failed to create %s via template '%s'", dir ? "directory" : "file", tmpl); return 1; }
    }
    if (tries == 100) { if (!quiet) say("too many files could not be created"); return 1; }
    puts(path);
    return finish_stdout();
}

/* ---- head tail ---- */

/* head -5 and fold -5 for -n 5 and -w 5: only as the first argument, digits and maybe a b, k or m */
static char **old_count_form(int *argc, char **argv, const char *opt)
{
    char **n;
    const char *a;
    int i;
    if (*argc < 2) return argv;
    a = argv[1];
    if (a[0] != '-' || !isdigit((unsigned char)a[1])) return argv;
    for (i = 1; isdigit((unsigned char)a[i]); i++) ;
    if (a[i] && !(strchr("bkm", a[i]) && !a[i + 1])) return argv;
    n = xmalloc((*argc + 2) * sizeof *n);
    n[0] = argv[0]; n[1] = (char *)opt; n[2] = (char *)a + 1;
    for (i = 2; i <= *argc; i++) n[i + 1] = argv[i];
    (*argc)++;
    return n;
}

static void print_header(const char *name, int *first)
{
    printf("%s==> %s <==\n", *first ? "" : "\n", !strcmp(name, "-") ? "'standard input'" : name);
    *first = 0;
}

static int t_head(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "bytes", 1, 'c' }, { "lines", 1, 'n' }, { "quiet", 0, 'q' }, { "silent", 0, 'q' }, { "verbose", 0, 'v' },
        { "zero-terminated", 0, 'z' }, { NULL, 0, 0 } };
    int c, bytes = 0, quiet = 0, verbose = 0, i, st = 0, first = 1, sep = '\n', from_end = 0;
    unsigned long long count = 10;
    static char buf[1 << 16];
    argv = old_count_form(&argc, argv, "-n");
    opts_reset();
    while ((c = getopts(argc, argv, "c:n:qvz", lo)))
        switch (c) {
        case 'c': case 'n': {
            const char *s = optarg;
            bytes = c == 'c'; from_end = 0;
            if (*s == '-') { from_end = 1; s++; } else if (*s == '+') s++;
            if (!parse_size(s, &count)) die(1, "invalid number of %s: '%s'", bytes ? "bytes" : "lines", optarg);
            break;
        }
        case 'q': quiet = 1; break;
        case 'v': verbose = 1; break;
        case 'z': sep = 0; break;
        }
    if (!nops) ops[nops++] = "-";
    for (i = 0; i < nops; i++) {
        int fd = open_input(ops[i]);
        unsigned long long left = count;
        ssize_t n;
        if (fd < 0) { perr("cannot open '%s' for reading", ops[i]); st = 1; continue; }
        if (verbose || (!quiet && nops > 1)) print_header(ops[i], &first);
        fflush(stdout);
        if (from_end) {
            char *data; size_t len, end;
            if (read_all(fd, &data, &len) < 0) { perr("error reading '%s'", ops[i]); st = 1; if (fd) close(fd); continue; }
            if (bytes) end = len > count ? len - count : 0;
            else if (count == 0) end = len;
            else {
                /* the newline at the very end doesn't start another line; the Nth one from the back ends what we keep */
                size_t q = len;
                unsigned long long seen = 0;
                if (len && data[len - 1] == sep) q = len - 1;
                end = 0;
                while (q > 0) {
                    q--;
                    if (data[q] == sep && ++seen == count) { end = q + 1; break; }
                }
            }
            if (write_all(1, data, end) < 0) { perr("error writing 'standard output'"); exit(1); }
            free(data);
        } else if (bytes) {
            while (left && (n = read(fd, buf, left < sizeof buf ? left : sizeof buf)) != 0) {
                if (n < 0) { if (errno == EINTR) continue; perr("error reading '%s'", ops[i]); st = 1; break; }
                if (write_all(1, buf, n) < 0) { perr("error writing 'standard output'"); exit(1); }
                left -= n;
            }
        } else {
            while (left && (n = read(fd, buf, sizeof buf)) != 0) {
                ssize_t k, upto = n;
                if (n < 0) { if (errno == EINTR) continue; perr("error reading '%s'", ops[i]); st = 1; break; }
                for (k = 0; k < n; k++)
                    if (buf[k] == sep && --left == 0) { upto = k + 1; break; }
                if (write_all(1, buf, upto) < 0) { perr("error writing 'standard output'"); exit(1); }
            }
        }
        if (fd) close(fd);
    }
    return st;
}

struct follow { char *name; int fd; off_t pos; dev_t dev; ino_t ino; };

static int t_tail(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "bytes", 1, 'c' }, { "lines", 1, 'n' }, { "quiet", 0, 'q' }, { "silent", 0, 'q' }, { "verbose", 0, 'v' },
        { "zero-terminated", 0, 'z' }, { "follow", 2, 'f' }, { "sleep-interval", 1, 's' },
        { "pid", 1, 'p' }, { NULL, 0, 0 } };
    int c, bytes = 0, quiet = 0, verbose = 0, i, st = 0, first = 1, sep = '\n', from_start = 0, follow = 0, byname = 0;
    unsigned long long count = 10;
    double interval = 0.25;
    struct follow *fl;
    argv = old_count_form(&argc, argv, "-n");
    opts_reset();
    while ((c = getopts(argc, argv, "c:n:qvzfFs:", lo)))
        switch (c) {
        case 'c': case 'n': {
            const char *s = optarg;
            bytes = c == 'c'; from_start = 0;
            if (*s == '+') { from_start = 1; s++; } else if (*s == '-') s++;
            if (!parse_size(s, &count)) die(1, "invalid number of %s: '%s'", bytes ? "bytes" : "lines", optarg);
            break;
        }
        case 'q': quiet = 1; break;
        case 'v': verbose = 1; break;
        case 'z': sep = 0; break;
        case 'f': follow = 1; if (optarg && !strcmp(optarg, "name")) byname = 1; break;
        case 'F': follow = 1; byname = 1; break;
        case 's': interval = strtod(optarg, NULL); if (interval <= 0) interval = 0.25; break;
        default: break;
        }
    if (!nops) ops[nops++] = "-";
    fl = xmalloc(nops * sizeof *fl);
    for (i = 0; i < nops; i++) {
        int fd = open_input(ops[i]);
        struct stat sb;
        char *data = NULL; size_t len = 0, start = 0;
        fl[i].name = ops[i]; fl[i].fd = fd; fl[i].pos = 0; fl[i].dev = fl[i].ino = 0;
        if (fd < 0) { perr("cannot open '%s' for reading", ops[i]); st = 1; continue; }
        if (verbose || (!quiet && nops > 1)) print_header(ops[i], &first);
        fflush(stdout);
        if (fstat(fd, &sb) == 0) { fl[i].dev = sb.st_dev; fl[i].ino = sb.st_ino; }
        if (fstat(fd, &sb) == 0 && S_ISREG(sb.st_mode) && !from_start) {
            /* a file we can seek in: look backwards from the end */
            off_t size = sb.st_size, pos = size;
            if (bytes) start = size > (off_t)count ? size - count : 0;
            else if (count == 0) start = size;
            else {
                char blk[8192];
                unsigned long long seen = 0;
                int skip_last = 0;
                if (size > 0) { char lastc; if (pread(fd, &lastc, 1, size - 1) == 1 && lastc == sep) skip_last = 1; }
                start = 0;
                while (pos > 0) {
                    off_t from = pos > (off_t)sizeof blk ? pos - sizeof blk : 0;
                    ssize_t got = pread(fd, blk, pos - from, from), k;
                    if (got <= 0) break;
                    for (k = got - 1; k >= 0; k--) {
                        if (from + k == size - 1 && skip_last) continue;
                        if (blk[k] == sep && ++seen == count) { start = from + k + 1; goto found; }
                    }
                    pos = from;
                }
found:
                ;
            }
            lseek(fd, start, SEEK_SET);
            fl[i].pos = start;
            {
                char buf[1 << 16]; ssize_t n;
                while ((n = read(fd, buf, sizeof buf)) != 0) {
                    if (n < 0) { if (errno == EINTR) continue; perr("error reading '%s'", ops[i]); st = 1; break; }
                    if (write_all(1, buf, n) < 0) { perr("error writing 'standard output'"); exit(1); }
                    fl[i].pos += n;
                }
            }
            continue;
        }
        if (read_all(fd, &data, &len) < 0) { perr("error reading '%s'", ops[i]); st = 1; continue; }
        if (from_start) {
            if (bytes) start = count > 1 ? (count - 1 < len ? count - 1 : len) : 0;
            else {
                unsigned long long k = 1;
                while (k < count && start < len) { char *nl = memchr(data + start, sep, len - start); if (!nl) { start = len; break; } start = nl - data + 1; k++; }
                if (k < count) start = len;
            }
        } else if (bytes) start = len > count ? len - count : 0;
        else if (count == 0) start = len;
        else {
            unsigned long long seen = 0;
            size_t q = len;
            int skip_last = len && data[len - 1] == sep;
            start = 0;
            while (q > 0) {
                q--;
                if (skip_last && q == len - 1) continue;
                if (data[q] == sep && ++seen == count) { start = q + 1; break; }
            }
        }
        if (write_all(1, data + start, len - start) < 0) { perr("error writing 'standard output'"); exit(1); }
        free(data);
        fl[i].pos = len;
    }
    if (follow) {
        int last = -1;
        for (;;) {
            struct timespec t;
            for (i = 0; i < nops; i++) {
                struct stat sb, ps;
                char buf[1 << 16];
                ssize_t n;
                if (byname && stat(fl[i].name, &ps) == 0 && (fl[i].fd < 0 || ps.st_ino != fl[i].ino || ps.st_dev != fl[i].dev)) {
                    int fd = open(fl[i].name, O_RDONLY);
                    if (fd >= 0) {
                        if (fl[i].fd > 0) close(fl[i].fd);
                        if (fl[i].fd >= 0) say("'%s' has been replaced;  following new file", fl[i].name);
                        else say("'%s' has appeared;  following new file", fl[i].name);
                        fl[i].fd = fd; fl[i].pos = 0; fl[i].dev = ps.st_dev; fl[i].ino = ps.st_ino;
                    }
                }
                if (fl[i].fd < 0) continue;
                if (fstat(fl[i].fd, &sb) == 0 && S_ISREG(sb.st_mode) && sb.st_size < fl[i].pos) {
                    say("%s: file truncated", fl[i].name);
                    lseek(fl[i].fd, 0, SEEK_SET); fl[i].pos = 0;
                }
                while ((n = read(fl[i].fd, buf, sizeof buf)) > 0) {
                    if (nops > 1 && !quiet && last != i) print_header(fl[i].name, &first);
                    last = i;
                    if (write_all(1, buf, n) < 0) { perr("error writing 'standard output'"); exit(1); }
                    fl[i].pos += n;
                }
            }
            fflush(stdout);
            t.tv_sec = (long)interval; t.tv_nsec = (long)((interval - (double)t.tv_sec) * 1e9);
            nanosleep(&t, NULL);
        }
    }
    return st;
}

/* ---- wc ---- */

static int utf8_locale(void)
{
    const char *v = getenv("LC_ALL");
    if (!v || !*v) v = getenv("LC_CTYPE");
    if (!v || !*v) v = getenv("LANG");
    return v && (strstr(v, "UTF-8") || strstr(v, "utf8") || strstr(v, "UTF8") || strstr(v, "utf-8"));
}

struct counts { unsigned long long lines, words, chars, bytes, maxlen; };

static int wc_count(int fd, struct counts *c, int utf8)
{
    static char buf[1 << 16];
    ssize_t n;
    int in_word = 0;
    unsigned long long col = 0;
    memset(c, 0, sizeof *c);
    while ((n = read(fd, buf, sizeof buf)) != 0) {
        ssize_t k;
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        c->bytes += n;
        for (k = 0; k < n; k++) {
            unsigned char ch = buf[k];
            if (utf8 && (ch & 0xc0) == 0x80) continue;
            c->chars++;
            if (ch == '\n') { c->lines++; if (col > c->maxlen) c->maxlen = col; col = 0; in_word = 0; continue; }
            if (ch == '\t') col = (col / 8 + 1) * 8;
            else if (ch == '\r' || ch == '\f') { if (col > c->maxlen) c->maxlen = col; col = 0; }
            else if (ch >= 32 && ch != 127) col++;
            if (isspace(ch)) in_word = 0;
            else if (!in_word) { in_word = 1; c->words++; }
        }
    }
    if (col > c->maxlen) c->maxlen = col;
    return 0;
}

static int t_wc(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "bytes", 0, 'c' }, { "chars", 0, 'm' }, { "lines", 0, 'l' }, { "words", 0, 'w' }, { "max-line-length", 0, 'L' },
        { "files0-from", 1, 'F' }, { "total", 1, 'T' }, { NULL, 0, 0 } };
    int c, pl = 0, pw = 0, pc = 0, pb = 0, pL = 0, i, st = 0, ncols, width = 1, utf8 = utf8_locale();
    int total_mode = 0;                     /* auto, always, only, never: 0 to 3 */
    const char *from0 = NULL;
    char **names; int nnames;
    struct counts total;
    unsigned long long reg_total = 0;
    int stdin_only;
    opts_reset();
    while ((c = getopts(argc, argv, "cmlwL", lo)))
        switch (c) {
        case 'c': pb = 1; break; case 'm': pc = 1; break; case 'l': pl = 1; break; case 'w': pw = 1; break; case 'L': pL = 1; break;
        case 'F': from0 = optarg; break;
        case 'T':
            if (!strcmp(optarg, "auto")) total_mode = 0; else if (!strcmp(optarg, "always")) total_mode = 1;
            else if (!strcmp(optarg, "only")) total_mode = 2; else if (!strcmp(optarg, "never")) total_mode = 3;
            else die(1, "invalid argument '%s' for '--total'", optarg);
            break;
        }
    if (!(pl | pw | pc | pb | pL)) pl = pw = pb = 1;
    ncols = pl + pw + pc + pb + pL;
    if (from0) {
        char *data; size_t len, k = 0;
        int fd;
        if (nops) { say("extra operand '%s'", ops[0]); fprintf(stderr, "file operands cannot be combined with --files0-from\n"); usage_error(); }
        fd = open_input(from0);
        if (fd < 0 || read_all(fd, &data, &len) < 0) die(1, "cannot open '%s' for reading: %s", from0, strerror(errno));
        names = xmalloc((len + 2) * sizeof *names); nnames = 0;
        while (k < len) { names[nnames++] = data + k; k += strlen(data + k) + 1; }
    } else { names = ops; nnames = nops; }
    stdin_only = nnames == 0;
    if (stdin_only) { names = (char *[]){ "-", NULL }; nnames = 1; }
    /* width as GNU does it: from the sizes of regular files, 7 if any input is something else (a pipe), 1 for one
     * input with one column */
    if (!(nnames <= 1 && ncols == 1) && total_mode != 2) {
        for (i = 0; i < nnames; i++) {
            struct stat sb;
            int r = !strcmp(names[i], "-") ? fstat(0, &sb) : stat(names[i], &sb);
            if (r < 0) continue;
            if (S_ISREG(sb.st_mode)) reg_total += sb.st_size; else { width = 7; break; }
        }
        if (width != 7) for (; reg_total >= 10; reg_total /= 10) width++;
    }
    memset(&total, 0, sizeof total);
#define SHOW(flag, v) if (flag) { printf("%s%*llu", first_col ? "" : " ", width, v); first_col = 0; }
    for (i = 0; i < nnames; i++) {
        struct counts k;
        int fd = open_input(names[i]), first_col = 1;
        struct stat sb;
        if (fd < 0) { perr("%s", names[i]); st = 1; continue; }
        if (fstat(fd, &sb) == 0 && S_ISDIR(sb.st_mode)) {
            memset(&k, 0, sizeof k);
            if (total_mode != 2) {
                SHOW(pl, 0) SHOW(pw, 0) SHOW(pc, 0) SHOW(pb, 0) SHOW(pL, 0)
                if (!stdin_only) printf(" %s", names[i]);
                putchar('\n');
            }
            perr("%s", names[i]); st = 1; if (fd) close(fd);
            continue;
        }
        if (wc_count(fd, &k, utf8) < 0) { perr("%s", names[i]); st = 1; if (fd) close(fd); continue; }
        if (fd) close(fd);
        total.lines += k.lines; total.words += k.words; total.chars += k.chars; total.bytes += k.bytes;
        if (k.maxlen > total.maxlen) total.maxlen = k.maxlen;
        if (total_mode == 2) continue;
        SHOW(pl, k.lines) SHOW(pw, k.words) SHOW(pc, utf8 ? k.chars : k.bytes) SHOW(pb, k.bytes) SHOW(pL, k.maxlen)
        if (!stdin_only) printf(" %s", names[i]);
        putchar('\n');
    }
    if (total_mode == 1 || total_mode == 2 || (total_mode == 0 && nnames > 1)) {
        int first_col = 1;
        SHOW(pl, total.lines) SHOW(pw, total.words) SHOW(pc, utf8 ? total.chars : total.bytes) SHOW(pb, total.bytes) SHOW(pL, total.maxlen)
        if (total_mode != 2) printf(" total");
        putchar('\n');
    }
    return finish_stdout() ? 1 : st;
}

/* ---- tee ---- */

static int t_tee(int argc, char **argv)
{
    static const struct opt lo[] = { { "append", 0, 'a' }, { "ignore-interrupts", 0, 'i' }, { "output-error", 2, 'e' }, { NULL, 0, 0 } };
    int c, append = 0, i, n, st = 0;
    int *fds;
    static char buf[1 << 16];
    ssize_t r;
    opts_reset();
    while ((c = getopts(argc, argv, "aip", lo)))
        switch (c) { case 'a': append = 1; break; case 'i': signal(SIGINT, SIG_IGN); break; }
    n = nops;
    fds = xmalloc((n + 1) * sizeof *fds);
    for (i = 0; i < n; i++) {
        fds[i] = open(ops[i], O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0666);
        if (fds[i] < 0) { perr("%s", ops[i]); st = 1; }
    }
    while ((r = read(0, buf, sizeof buf)) != 0) {
        if (r < 0) { if (errno == EINTR) continue; perr("standard input"); st = 1; break; }
        if (write_all(1, buf, r) < 0) { perr("standard output"); st = 1; break; }
        for (i = 0; i < n; i++)
            if (fds[i] >= 0 && write_all(fds[i], buf, r) < 0) { perr("%s", ops[i]); fds[i] = -1; st = 1; }
    }
    for (i = 0; i < n; i++) if (fds[i] > 1) close(fds[i]);
    return st;
}

/* ---- tr ---- */

static int is_blank_c(int c) { return c == ' ' || c == '\t'; }
static int is_cntrl_c(int c) { return c < 32 || c == 127; }
static int is_graph_c(int c) { return c > 32 && c < 127; }
static int is_punct_c(int c) { return is_graph_c(c) && !isalnum(c); }

static int tr_escape(const char **p)
{
    int c = (unsigned char)**p;
    if (c >= '0' && c <= '7') {
        int v = 0, n = 0;
        for (; n < 3 && **p >= '0' && **p <= '7'; n++, (*p)++) v = v * 8 + **p - '0';
        return v & 255;
    }
    (*p)++;
    switch (c) {
    case 'a': return 7;
    case 'b': return 8;
    case 'f': return 12;
    case 'n': return 10;
    case 'r': return 13;
    case 't': return 9;
    case 'v': return 11;
    case 0: (*p)--; return '\\';
    }
    return c;
}

static int in_class(const char *name, int ch)
{
    if (!strcmp(name, "alnum")) return isalnum(ch);
    if (!strcmp(name, "alpha")) return isalpha(ch);
    if (!strcmp(name, "blank")) return is_blank_c(ch);
    if (!strcmp(name, "cntrl")) return is_cntrl_c(ch);
    if (!strcmp(name, "digit")) return isdigit(ch);
    if (!strcmp(name, "graph")) return is_graph_c(ch);
    if (!strcmp(name, "lower")) return islower(ch);
    if (!strcmp(name, "print")) return isprint(ch);
    if (!strcmp(name, "punct")) return is_punct_c(ch);
    if (!strcmp(name, "space")) return isspace(ch);
    if (!strcmp(name, "upper")) return isupper(ch);
    if (!strcmp(name, "xdigit")) return isxdigit(ch);
    return -1;
}

/* a tr SET as a list of bytes. a [c*] with no count has to stretch to fit the other set, so its place is
 * returned in fill_at */
static unsigned char *tr_expand(const char *s, size_t *len, int *fill_at, int *fill_ch)
{
    size_t cap = strlen(s) * 4 + 256, n = 0;
    unsigned char *r = xmalloc(cap);
    *fill_at = *fill_ch = -1;
#define PUSH(ch) do { if (n == cap) { cap *= 2; r = xrealloc(r, cap); } r[n++] = (ch); } while (0)
    while (*s) {
        const char *e;
        if (s[0] == '[' && s[1] == ':' && (e = strstr(s + 2, ":]"))) {
            char name[16];
            int ch;
            if ((size_t)(e - s - 2) >= sizeof name) die(1, "invalid character class '%.*s'", (int)(e - s - 2), s + 2);
            memcpy(name, s + 2, e - s - 2);
            name[e - s - 2] = 0;
            if (in_class(name, 'a') < 0) die(1, "invalid character class '%s'", name);
            for (ch = 0; ch < 256; ch++) if (in_class(name, ch)) PUSH(ch);
            s = e + 2;
            continue;
        }
        if (s[0] == '[' && s[1] == '=' && s[2] && !strncmp(s + 3, "=]", 2)) { PUSH((unsigned char)s[2]); s += 5; continue; }
        if (s[0] == '[' && s[1]) {
            const char *q = s + 1;
            int ch;
            if (*q == '\\') { q++; ch = tr_escape(&q); } else ch = (unsigned char)*q++;
            if (*q == '*') {
                char *end;
                unsigned long cnt;
                if (q[1] == ']') { *fill_at = n; *fill_ch = ch; s = q + 2; continue; }
                cnt = strtoul(q + 1, &end, q[1] == '0' ? 8 : 10);
                if (end != q + 1 && *end == ']') { while (cnt--) PUSH(ch); s = end + 1; continue; }
            }
        }
        {
            int ch, hi;
            if (*s == '\\') { s++; ch = tr_escape(&s); } else ch = (unsigned char)*s++;
            if (*s == '-' && s[1]) {
                s++;
                if (*s == '\\') { s++; hi = tr_escape(&s); } else hi = (unsigned char)*s++;
                if (hi < ch) die(1, "range-endpoints of '%c-%c' are in reverse collating sequence order", ch, hi);
                for (; ch <= hi; ch++) PUSH(ch);
            } else PUSH(ch);
        }
    }
    *len = n;
    return r;
}

static int t_tr(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "complement", 0, 'c' }, { "delete", 0, 'd' }, { "squeeze-repeats", 0, 's' }, { "truncate-set1", 0, 't' }, { NULL, 0, 0 } };
    static unsigned char in[1 << 16], out[1 << 16];
    unsigned char *s1, *s2 = NULL, map[256], inset[256], sqset[256];
    size_t n1, n2 = 0, k;
    int c, comp = 0, del = 0, sq = 0, trunc = 0, i, f1, c1, f2 = -1, c2 = -1, translate, prev = -1;
    ssize_t r;
    opts_reset();
    while ((c = getopts(argc, argv, "cCdst", lo)))
        switch (c) {
        case 'c': case 'C': comp = 1; break;
        case 'd': del = 1; break;
        case 's': sq = 1; break;
        case 't': trunc = 1; break;
        }
    if (!nops) missing_operand();
    if (nops > 2) extra_operand(ops[2]);
    if (nops == 1 && !del && !sq) {
        say("missing operand after '%s'", ops[0]);
        fprintf(stderr, "Two strings must be given when translating.\n");
        usage_error();
    }
    if (nops == 2 && del && !sq) {
        say("extra operand '%s'", ops[1]);
        fprintf(stderr, "Only one string may be given when deleting without squeezing repeats.\n");
        usage_error();
    }
    s1 = tr_expand(ops[0], &n1, &f1, &c1);
    if (comp) {
        unsigned char seen[256], *nw = xmalloc(256);
        size_t m = 0;
        memset(seen, 0, sizeof seen);
        for (k = 0; k < n1; k++) seen[s1[k]] = 1;
        for (i = 0; i < 256; i++) if (!seen[i]) nw[m++] = i;
        free(s1); s1 = nw; n1 = m;
    }
    translate = !del && nops == 2;
    if (nops == 2) s2 = tr_expand(ops[1], &n2, &f2, &c2);
    memset(inset, 0, sizeof inset);
    memset(sqset, 0, sizeof sqset);
    for (i = 0; i < 256; i++) map[i] = i;
    for (k = 0; k < n1; k++) inset[s1[k]] = 1;
    if (translate) {
        if (f2 >= 0 && n1 > n2) {
            size_t want = n1 - n2;
            unsigned char *g = xmalloc(n1 + 1);
            memcpy(g, s2, f2);
            memset(g + f2, c2, want);
            memcpy(g + f2 + want, s2 + f2, n2 - f2);
            free(s2); s2 = g; n2 = n1;
        }
        if (!n2 && !trunc) die(1, "when not truncating set1, string2 must be non-empty");
        if (trunc && n1 > n2) n1 = n2;
        for (k = 0; k < n1; k++) map[s1[k]] = k < n2 ? s2[k] : s2[n2 - 1];
        if (sq) for (k = 0; k < n2; k++) sqset[s2[k]] = 1;
    } else if (sq) {
        if (nops == 2) for (k = 0; k < n2; k++) sqset[s2[k]] = 1;
        else memcpy(sqset, inset, sizeof sqset);
    }
    while ((r = read(0, in, sizeof in)) != 0) {
        size_t o = 0;
        if (r < 0) { if (errno == EINTR) continue; perr("read error"); return 1; }
        for (k = 0; k < (size_t)r; k++) {
            int ch = in[k];
            if (del && inset[ch]) continue;
            if (translate) ch = map[ch];
            if (sq && sqset[ch]) { if (ch == prev) continue; prev = ch; }
            else prev = -1;
            out[o++] = ch;
        }
        if (write_all(1, (char *)out, o) < 0) { perr("write error"); return 1; }
    }
    return 0;
}

/* ---- seq ---- */

static int all_digits(const char *s)
{
    if (*s == '+' || *s == '-') s++;
    if (!*s) return 0;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0;
    return 1;
}

static int decimals(const char *s)
{
    const char *dot = strchr(s, '.');
    int n = 0;
    if (strpbrk(s, "eE")) return -1;
    if (dot) for (dot++; isdigit((unsigned char)*dot); dot++) n++;
    return n;
}

/* a number starting with - is an operand: seq -5 5 */
static void guard_negative(int *argc, char ***argv)
{
    int i, j;
    char **n;
    for (i = 1; i < *argc; i++) {
        const char *a = (*argv)[i];
        if (!strcmp(a, "--")) return;
        if (a[0] == '-' && (isdigit((unsigned char)a[1]) || a[1] == '.')) break;
    }
    if (i == *argc) return;
    n = xmalloc((*argc + 2) * sizeof *n);
    for (j = 0; j < i; j++) n[j] = (*argv)[j];
    n[i] = "--";
    for (j = i; j <= *argc; j++) n[j + 1] = (*argv)[j];
    (*argc)++;
    *argv = n;
}

/* zeros between the sign and the digits, for -w */
static void put_padded(const char *t, size_t width)
{
    size_t l = strlen(t), neg = t[0] == '-';
    if (l < width) {
        if (neg) putchar('-');
        for (; l < width; l++) putchar('0');
        fputs(t + neg, stdout);
    } else fputs(t, stdout);
}

static int t_seq(int argc, char **argv)
{
    static const struct opt lo[] = { { "format", 1, 'f' }, { "separator", 1, 's' }, { "equal-width", 0, 'w' }, { NULL, 0, 0 } };
    const char *fmt = NULL, *sep = "\n", *sf, *si, *sl;
    char *end, t[512];
    size_t width = 0;
    int c, eq = 0;
    guard_negative(&argc, &argv);
    opts_reset();
    while ((c = getopts(argc, argv, "f:s:w", lo)))
        switch (c) {
        case 'f': fmt = optarg; break;
        case 's': sep = optarg; break;
        case 'w': eq = 1; break;
        }
    if (!nops) missing_operand();
    if (nops > 3) extra_operand(ops[3]);
    sf = nops > 1 ? ops[0] : "1";
    si = nops > 2 ? ops[1] : "1";
    sl = ops[nops - 1];
    if (all_digits(sf) && all_digits(si) && all_digits(sl) && !fmt) {
        long long first = strtoll(sf, NULL, 10), inc = strtoll(si, NULL, 10), last = strtoll(sl, NULL, 10), v;
        int started = 0;
        if (!inc) die(1, "invalid Zero increment value: '%s'", si);
        if (eq) {
            size_t a = snprintf(t, sizeof t, "%lld", first), b = snprintf(t, sizeof t, "%lld", last);
            width = a > b ? a : b;
        }
        for (v = first; inc > 0 ? v <= last : v >= last; v += inc) {
            if (started) fputs(sep, stdout);
            started = 1;
            snprintf(t, sizeof t, "%lld", v);
            put_padded(t, width);
            if (inc > 0 ? v > last - inc : v < last - inc) break;
        }
        if (started) putchar('\n');
    } else {
        double first = strtod(sf, &end), inc, last;
        int prec = 0, p, k, started = 0;
        if (*end) die(1, "invalid floating point argument: '%s'", sf);
        inc = strtod(si, &end);
        if (*end) die(1, "invalid floating point argument: '%s'", si);
        last = strtod(sl, &end);
        if (*end) die(1, "invalid floating point argument: '%s'", sl);
        if (inc == 0) die(1, "invalid Zero increment value: '%s'", si);
        p = decimals(sf); if (p > prec) prec = p;
        p = decimals(si); if (p > prec) prec = p;
        p = decimals(sl); if (p > prec) prec = p;
        if (prec < 0) prec = 6;
        if (eq && !fmt) {
            size_t a = snprintf(t, sizeof t, "%.*f", prec, first), b = snprintf(t, sizeof t, "%.*f", prec, last);
            width = a > b ? a : b;
        }
        for (k = 0; ; k++) {
            double v = first + k * inc;
            if (inc > 0 ? v > last + inc * 1e-9 : v < last + inc * 1e-9) break;
            if (started) fputs(sep, stdout);
            started = 1;
            if (fmt) snprintf(t, sizeof t, fmt, v);
            else snprintf(t, sizeof t, "%.*f", prec, v);
            put_padded(t, width);
        }
        if (started) putchar('\n');
    }
    return finish_stdout();
}

/* ---- tac nl fold ---- */

static int t_tac(int argc, char **argv)
{
    static const struct opt lo[] = { { "before", 0, 'b' }, { "regex", 0, 'r' }, { "separator", 1, 's' }, { NULL, 0, 0 } };
    const char *sep = "\n";
    int c, before = 0, i, st = 0;
    size_t sl;
    opts_reset();
    while ((c = getopts(argc, argv, "brs:", lo))) {
        if (c == 'b') before = 1;
        else if (c == 's') sep = optarg;
    }
    if (!*sep) die(1, "separator cannot be empty");
    sl = strlen(sep);
    if (!nops) ops[nops++] = "-";
    for (i = 0; i < nops; i++) {
        char *d;
        size_t len, end, k;
        int fd = open_input(ops[i]);
        if (fd < 0) { perr("failed to open '%s' for reading", ops[i]); st = 1; continue; }
        if (read_all(fd, &d, &len) < 0) { perr("%s: read error", ops[i]); st = 1; if (fd) close(fd); continue; }
        if (fd) close(fd);
        for (end = len; end; end = k) {
            k = end;
            if (before) {
                while (k >= sl && memcmp(d + k - sl, sep, sl)) k--;     /* back to a separator at the start of a record */
                k = k >= sl ? k - sl : 0;
            } else {
                if (k >= sl && !memcmp(d + k - sl, sep, sl)) k -= sl;    /* this record's own separator */
                while (k >= sl && memcmp(d + k - sl, sep, sl)) k--;
                if (k < sl) k = 0;
            }
            if (write_all(1, d + k, end - k) < 0) { perr("write error"); return 1; }
        }
        free(d);
    }
    return st;
}

static int t_nl(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "body-numbering", 1, 'b' }, { "header-numbering", 1, 'h' }, { "footer-numbering", 1, 'f' }, { "number-format", 1, 'n' },
        { "number-width", 1, 'w' }, { "number-separator", 1, 's' }, { "starting-line-number", 1, 'v' },
        { "line-increment", 1, 'i' }, { "join-blank-lines", 1, 'l' }, { "no-renumber", 0, 'p' }, { "section-delimiter", 1, 'd' }, { NULL, 0, 0 } };
    const char *style = "t", *fmt = "rn", *sep = "\t";
    long long no = 1, inc = 1;
    int c, i, st = 0, width = 6, join = 1;
    opts_reset();
    while ((c = getopts(argc, argv, "b:h:f:n:w:s:v:i:l:pd:", lo)))
        switch (c) {
        case 'b':
            style = optarg;
            if (strcmp(style, "a") && strcmp(style, "t") && strcmp(style, "n")) die(1, "invalid body numbering style: '%s'", style);
            break;
        case 'n':
            fmt = optarg;
            if (strcmp(fmt, "ln") && strcmp(fmt, "rn") && strcmp(fmt, "rz")) die(1, "invalid line numbering format: '%s'", fmt);
            break;
        case 'w': width = atoi(optarg); if (width < 1) die(1, "invalid line number field width: '%s'", optarg); break;
        case 's': sep = optarg; break;
        case 'v': no = atoll(optarg); break;
        case 'i': inc = atoll(optarg); break;
        case 'l': join = atoi(optarg); if (join < 1) die(1, "invalid line number of blank lines: '%s'", optarg); break;
        }
    if (nops > 1) extra_operand(ops[1]);
    if (!nops) ops[nops++] = "-";
    for (i = 0; i < nops; i++) {
        char *d;
        size_t len, p = 0;
        int fd = open_input(ops[i]), blanks = 0;
        if (fd < 0) { perr("%s", ops[i]); st = 1; continue; }
        if (read_all(fd, &d, &len) < 0) { perr("%s", ops[i]); st = 1; if (fd) close(fd); continue; }
        if (fd) close(fd);
        while (p < len) {
            char *nl = memchr(d + p, '\n', len - p);
            size_t l = nl ? (size_t)(nl - d) - p : len - p;
            int number = style[0] == 'a' || (style[0] == 't' && l > 0);
            if (style[0] == 'a' && l == 0) {
                if (++blanks < join) number = 0;
                else blanks = 0;
            } else if (l) blanks = 0;
            if (number) {
                printf(!strcmp(fmt, "ln") ? "%-*lld" : !strcmp(fmt, "rz") ? "%0*lld" : "%*lld", width, no);
                fputs(sep, stdout);
                no += inc;
            } else printf("%*s", width + (int)strlen(sep), "");
            fwrite(d + p, 1, l, stdout);
            putchar('\n');
            p += l + !!nl;
        }
        free(d);
    }
    return finish_stdout() ? 1 : st;
}

static unsigned long advance(unsigned long col, unsigned char ch, int bytes)
{
    if (bytes) return col + 1;
    if (ch == '\t') return (col / 8 + 1) * 8;
    if (ch == '\b') return col ? col - 1 : 0;
    if (ch == '\r') return 0;
    return col + 1;
}

static int t_fold(int argc, char **argv)
{
    static const struct opt lo[] = { { "bytes", 0, 'b' }, { "spaces", 0, 's' }, { "width", 1, 'w' }, { NULL, 0, 0 } };
    unsigned long width = 80;
    char *line = NULL;
    size_t cap = 0;
    int c, bytes = 0, spaces = 0, i, st = 0;
    argv = old_count_form(&argc, argv, "-w");
    opts_reset();
    while ((c = getopts(argc, argv, "bsw:", lo)))
        switch (c) {
        case 'b': bytes = 1; break;
        case 's': spaces = 1; break;
        case 'w': if (!parse_ulong(optarg, &width) || !width) die(1, "invalid number of columns: '%s'", optarg); break;
        }
    if (!nops) ops[nops++] = "-";
    for (i = 0; i < nops; i++) {
        char *d;
        size_t len, k, n = 0;
        unsigned long col = 0;
        int fd = open_input(ops[i]);
        if (fd < 0) { perr("%s", ops[i]); st = 1; continue; }
        if (read_all(fd, &d, &len) < 0) { perr("%s", ops[i]); st = 1; if (fd) close(fd); continue; }
        if (fd) close(fd);
        for (k = 0; k < len; k++) {
            unsigned char ch = d[k];
            unsigned long next;
            if (n + 2 > cap) { cap = cap ? cap * 2 : 256; line = xrealloc(line, cap); }
            if (ch == '\n') { fwrite(line, 1, n, stdout); putchar('\n'); n = 0; col = 0; continue; }
            next = advance(col, ch, bytes);
            if (next > width && ch != '\b' && ch != '\r') {
                size_t cut = n, j;
                if (spaces) {
                    for (j = n; j > 0 && line[j - 1] != ' ' && line[j - 1] != '\t'; j--) ;
                    if (j) cut = j;
                }
                fwrite(line, 1, cut, stdout);
                putchar('\n');
                memmove(line, line + cut, n - cut);
                n -= cut;
                for (col = 0, j = 0; j < n; j++) col = advance(col, line[j], bytes);
                next = advance(col, ch, bytes);
            }
            line[n++] = ch;
            col = next;
        }
        fwrite(line, 1, n, stdout);
        free(d);
    }
    return finish_stdout() ? 1 : st;
}

/* ---- cut paste ---- */

/* lines of a buffer, cut at sep. the last one may have no terminator */
static size_t split_lines(char *d, size_t len, int sep, char ***out)
{
    size_t n = 0, cap = 256, p = 0;
    char **v = xmalloc(cap * sizeof *v);
    while (p < len) {
        char *e = memchr(d + p, sep, len - p);
        if (n == cap) { cap *= 2; v = xrealloc(v, cap * sizeof *v); }
        v[n++] = d + p;
        if (!e) break;
        *e = 0;
        p = e - d + 1;
    }
    *out = v;
    return n;
}

struct ranges { unsigned char *sel; unsigned long limit, open_from; int complement; };

static int selected(const struct ranges *r, unsigned long i)
{
    int in = (i <= r->limit && r->sel[i]) || i >= r->open_from;
    return in != r->complement;
}

static void parse_list(const char *s, struct ranges *r, const char *what)
{
    const char *p;
    unsigned long max = 0;
    r->open_from = ~0UL;
    for (p = s; *p; ) {                                  /* first pass: how far do the numbers go */
        char *e;
        unsigned long lo = 0, hi = 0;
        if (*p != '-') { lo = strtoul(p, &e, 10); if (e == p || !lo) goto bad; p = e; } else lo = 1;
        if (*p == '-') {
            p++;
            if (isdigit((unsigned char)*p)) { hi = strtoul(p, &e, 10); p = e; if (hi < lo) die(1, "invalid decreasing range"); }
            else hi = ~0UL;
        } else hi = lo;
        if (hi != ~0UL && hi > max) max = hi;
        if (hi == ~0UL && lo > max) max = lo;
        if (*p == ',' || *p == ' ') p++;
        else if (*p) goto bad;
    }
    r->limit = max;
    r->sel = xmalloc(max + 2);
    memset(r->sel, 0, max + 2);
    for (p = s; *p; ) {
        char *e;
        unsigned long lo = 1, hi, i;
        if (*p != '-') { lo = strtoul(p, &e, 10); p = e; }
        if (*p == '-') {
            p++;
            if (isdigit((unsigned char)*p)) { hi = strtoul(p, &e, 10); p = e; } else hi = ~0UL;
        } else hi = lo;
        if (hi == ~0UL) { if (lo < r->open_from) r->open_from = lo; }
        else for (i = lo; i <= hi; i++) r->sel[i] = 1;
        if (*p == ',' || *p == ' ') p++;
    }
    return;
bad:
    die(1, "invalid %s list: '%s'", what, s);
}

static int t_cut(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "bytes", 1, 'b' }, { "characters", 1, 'c' }, { "fields", 1, 'f' }, { "delimiter", 1, 'd' }, { "only-delimited", 0, 's' },
        { "complement", 0, 'C' }, { "output-delimiter", 1, 'O' }, { "zero-terminated", 0, 'z' }, { NULL, 0, 0 } };
    struct ranges r = { NULL, 0, 0, 0 };
    const char *list = NULL, *odelim = NULL;
    int c, mode = 0, delim = '\t', onlydel = 0, sep = '\n', i, st = 0;
    opts_reset();
    while ((c = getopts(argc, argv, "b:c:f:d:nsz", lo)))
        switch (c) {
        case 'b': case 'c': case 'f':
            if (mode && mode != c) die(1, "only one type of list may be specified");
            mode = c; list = optarg;
            break;
        case 'd':
            if (strlen(optarg) > 1) die(1, "the delimiter must be a single character");
            delim = (unsigned char)optarg[0];
            break;
        case 's': onlydel = 1; break;
        case 'C': r.complement = 1; break;
        case 'O': odelim = optarg; break;
        case 'z': sep = 0; break;
        }
    if (!mode) { say("you must specify a list of bytes, characters, or fields"); usage_error(); }
    if (onlydel && mode != 'f') die(1, "suppressing non-delimited lines makes sense only when operating on fields");
    parse_list(list, &r, mode == 'f' ? "field" : "byte/character position");
    if (!nops) ops[nops++] = "-";
    for (i = 0; i < nops; i++) {
        char *d, **v;
        size_t len, n, k;
        int fd = open_input(ops[i]);
        if (fd < 0) { perr("%s", ops[i]); st = 1; continue; }
        if (read_all(fd, &d, &len) < 0) { perr("%s", ops[i]); st = 1; if (fd) close(fd); continue; }
        if (fd) close(fd);
        n = split_lines(d, len, sep, &v);
        for (k = 0; k < n; k++) {
            const char *line = v[k], *p;
            size_t ll = strlen(line), j;
            if (mode == 'f') {
                unsigned long f = 1;
                int printed = 0;
                if (!memchr(line, delim, ll)) {
                    if (!onlydel) { fputs(line, stdout); putchar(sep); }
                    continue;
                }
                for (p = line; ; f++) {
                    const char *e = memchr(p, delim, line + ll - p);
                    size_t fl = e ? (size_t)(e - p) : (size_t)(line + ll - p);
                    if (selected(&r, f)) {
                        if (printed) { if (odelim) fputs(odelim, stdout); else putchar(delim); }
                        fwrite(p, 1, fl, stdout);
                        printed = 1;
                    }
                    if (!e) break;
                    p = e + 1;
                }
                putchar(sep);
            } else {
                int printed = 0, gap = 0;
                for (j = 0; j < ll; j++) {
                    if (selected(&r, j + 1)) {
                        if (gap && printed && odelim) fputs(odelim, stdout);
                        putchar(line[j]);
                        printed = 1; gap = 0;
                    } else gap = 1;
                }
                putchar(sep);
            }
        }
        free(v); free(d);
    }
    return finish_stdout() ? 1 : st;
}

struct reader { char *d; size_t len, pos; int ok; };

static int next_line(struct reader *r, char **line, size_t *ll, int sep)
{
    char *e;
    if (r->pos >= r->len) return 0;
    e = memchr(r->d + r->pos, sep, r->len - r->pos);
    *line = r->d + r->pos;
    *ll = e ? (size_t)(e - r->d) - r->pos : r->len - r->pos;
    r->pos += *ll + (e != NULL);
    return 1;
}

static int t_paste(int argc, char **argv)
{
    static const struct opt lo[] = { { "delimiters", 1, 'd' }, { "serial", 0, 's' }, { "zero-terminated", 0, 'z' }, { NULL, 0, 0 } };
    char *delims = "\t", dl[256];
    int c, serial = 0, sep = '\n', i, nd, st = 0;
    struct reader *rd, stdin_rd;
    int stdin_loaded = 0;
    opts_reset();
    while ((c = getopts(argc, argv, "d:sz", lo)))
        switch (c) { case 'd': delims = optarg; break; case 's': serial = 1; break; case 'z': sep = 0; break; }
    /* the delimiter list: \n \t \\ and \0 (nothing at all) are escapes */
    {
        const char *p = delims;
        nd = 0;
        for (; *p && nd < (int)sizeof dl; p++) {
            if (*p == '\\' && p[1]) {
                p++;
                dl[nd++] = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p == '0' ? 0 : *p == '\\' ? '\\' : *p;
            } else if (*p == '\\') die(1, "delimiter list ends with an unescaped backslash: %s", delims);
            else dl[nd++] = *p;
        }
        if (!nd) { dl[0] = 0; nd = 1; }
    }
    if (!nops) ops[nops++] = "-";
    rd = xmalloc(nops * sizeof *rd);
    for (i = 0; i < nops; i++) {
        if (!strcmp(ops[i], "-")) {
            if (!stdin_loaded) { if (read_all(0, &stdin_rd.d, &stdin_rd.len) < 0) { perr("standard input"); return 1; } stdin_rd.pos = 0; stdin_loaded = 1; }
            rd[i] = stdin_rd;
            rd[i].ok = 2;                                     /* shares stdin's position, see below */
        } else {
            int fd = open(ops[i], O_RDONLY);
            rd[i].ok = 1; rd[i].d = ""; rd[i].len = rd[i].pos = 0;
            if (fd < 0 || read_all(fd, &rd[i].d, &rd[i].len) < 0) { perr("%s", ops[i]); return 1; }
            close(fd);
        }
    }
#define RD(i) (rd[i].ok == 2 ? &stdin_rd : &rd[i])
    if (serial) {
        for (i = 0; i < nops; i++) {
            char *line; size_t ll; int first = 1; unsigned k = 0;
            if (!rd[i].ok) continue;
            while (next_line(RD(i), &line, &ll, sep)) {
                if (!first) { char ch = dl[k++ % nd]; if (ch) putchar(ch); }
                first = 0;
                fwrite(line, 1, ll, stdout);
            }
            putchar(sep);
        }
    } else {
        for (;;) {
            int any = 0;
            for (i = 0; i < nops; i++) if (RD(i)->pos < RD(i)->len) any = 1;
            if (!any) break;
            for (i = 0; i < nops; i++) {
                char *line; size_t ll;
                if (next_line(RD(i), &line, &ll, sep)) fwrite(line, 1, ll, stdout);
                if (i + 1 < nops && dl[i % nd]) putchar(dl[i % nd]);
            }
            putchar(sep);
        }
    }
    return finish_stdout() ? 1 : st;
}

/* ---- uniq comm ---- */

/* what uniq compares: past -f fields and -s characters, at most -w of them */
static const char *uniq_key(const char *s, size_t len, unsigned long skipf, unsigned long skips, unsigned long maxw, int has_w, size_t *kl)
{
    const char *p = s, *end = s + len;
    while (skipf-- && p < end) {
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        while (p < end && *p != ' ' && *p != '\t') p++;
    }
    while (skips-- && p < end) p++;
    *kl = end - p;
    if (has_w && maxw < *kl) *kl = maxw;
    return p;
}

static int t_uniq(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "count", 0, 'c' }, { "repeated", 0, 'd' }, { "all-repeated", 2, 'D' }, { "skip-fields", 1, 'f' }, { "ignore-case", 0, 'i' },
        { "skip-chars", 1, 's' }, { "unique", 0, 'u' }, { "check-chars", 1, 'w' }, { "zero-terminated", 0, 'z' }, { NULL, 0, 0 } };
    unsigned long skipf = 0, skips = 0, maxw = 0;
    int c, count = 0, dup = 0, alldup = 0, uniq_only = 0, icase = 0, has_w = 0, sep = '\n';
    char *d, **v;
    size_t len, n, i, g;
    int fd;
    opts_reset();
    while ((c = getopts(argc, argv, "cdDf:is:uw:z", lo)))
        switch (c) {
        case 'c': count = 1; break;
        case 'd': dup = 1; break;
        case 'D': alldup = 1; break;
        case 'f': if (!parse_ulong(optarg, &skipf)) die(1, "%s: invalid number of fields to skip", optarg); break;
        case 'i': icase = 1; break;
        case 's': if (!parse_ulong(optarg, &skips)) die(1, "%s: invalid number of bytes to skip", optarg); break;
        case 'u': uniq_only = 1; break;
        case 'w': if (!parse_ulong(optarg, &maxw)) die(1, "%s: invalid number of bytes to compare", optarg); has_w = 1; break;
        case 'z': sep = 0; break;
        }
    if (nops > 2) extra_operand(ops[2]);
    fd = nops ? open_input(ops[0]) : 0;
    if (fd < 0) { perr("%s", ops[0]); return 1; }
    if (read_all(fd, &d, &len) < 0) { perr("%s", nops ? ops[0] : "standard input"); return 1; }
    if (nops > 1 && strcmp(ops[1], "-") && !freopen(ops[1], "w", stdout)) { perr("%s", ops[1]); return 1; }
    n = split_lines(d, len, sep, &v);
    for (i = 0; i < n; i = g) {
        size_t kl, kl2;
        const char *k = uniq_key(v[i], strlen(v[i]), skipf, skips, maxw, has_w, &kl);
        for (g = i + 1; g < n; g++) {
            const char *k2 = uniq_key(v[g], strlen(v[g]), skipf, skips, maxw, has_w, &kl2);
            if (kl != kl2 || (icase ? strncasecmp(k, k2, kl) : memcmp(k, k2, kl))) break;
        }
        {
            size_t times = g - i, j;
            if ((dup || alldup) && times < 2) continue;
            if (uniq_only && times != 1) continue;
            if (alldup) { for (j = i; j < g; j++) { fputs(v[j], stdout); putchar(sep); } continue; }
            if (count) printf("%7lu ", (unsigned long)times);
            fputs(v[i], stdout);
            putchar(sep);
        }
    }
    return finish_stdout();
}

static int t_comm(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "output-delimiter", 1, 'O' }, { "check-order", 0, 'k' }, { "nocheck-order", 0, 'n' }, { "total", 0, 't' },
        { "zero-terminated", 0, 'z' }, { NULL, 0, 0 } };
    const char *odel = "\t";
    int c, h1 = 0, h2 = 0, h3 = 0, sep = '\n', total = 0, fd;
    char *d1, *d2, **v1, **v2;
    size_t l1, l2, n1, n2, i = 0, j = 0, t1 = 0, t2 = 0, t3 = 0;
    opts_reset();
    while ((c = getopts(argc, argv, "123z", lo)))
        switch (c) {
        case '1': h1 = 1; break;
        case '2': h2 = 1; break;
        case '3': h3 = 1; break;
        case 'O': odel = optarg; break;
        case 't': total = 1; break;
        case 'z': sep = 0; break;
        }
    if (nops < 2) { if (!nops) missing_operand(); say("missing operand after '%s'", ops[0]); usage_error(); }
    if (nops > 2) extra_operand(ops[2]);
    fd = open_input(ops[0]);
    if (fd < 0 || read_all(fd, &d1, &l1) < 0) die(1, "%s: %s", ops[0], strerror(errno));
    if (fd) close(fd);
    fd = open_input(ops[1]);
    if (fd < 0 || read_all(fd, &d2, &l2) < 0) die(1, "%s: %s", ops[1], strerror(errno));
    if (fd) close(fd);
    n1 = split_lines(d1, l1, sep, &v1);
    n2 = split_lines(d2, l2, sep, &v2);
    while (i < n1 || j < n2) {
        int cmp = i >= n1 ? 1 : j >= n2 ? -1 : strcmp(v1[i], v2[j]);
        if (cmp < 0) { t1++; if (!h1) { fputs(v1[i], stdout); putchar(sep); } i++; }
        else if (cmp > 0) {
            t2++;
            if (!h2) { if (!h1) fputs(odel, stdout); fputs(v2[j], stdout); putchar(sep); }
            j++;
        } else {
            t3++;
            if (!h3) { if (!h1) fputs(odel, stdout); if (!h2) fputs(odel, stdout); fputs(v1[i], stdout); putchar(sep); }
            i++; j++;
        }
    }
    if (total) printf("%lu%s%lu%s%lu%stotal\n", (unsigned long)t1, odel, (unsigned long)t2, odel, (unsigned long)t3, odel);
    return finish_stdout();
}

/* ---- sort ---- */

struct sline { char *p; size_t len; };

#define K_NUM 1
#define K_GEN 2
#define K_HUM 4
#define K_MON 8
#define K_VER 16
#define K_DICT 32
#define K_FOLD 64
#define K_IGN 128
#define K_REV 256
#define K_BLANK 512
#define K_RAND 1024

struct skey {
    unsigned long sword, schar, eword, echar;    /* sword ~0 is the start of the line, eword ~0 the end */
    int flags, eblank;
};

static struct skey keys[32];
static int nkeys, gflags, stable, unique, tabch = -1;

static int is_blank(int c) { return c == ' ' || c == '\t'; }

static const char *begfield(const struct sline *l, const struct skey *k)
{
    const char *p = l->p, *lim = l->p + l->len;
    unsigned long w = k->sword;
    if (w == ~0UL) return p;
    if (tabch >= 0) {
        while (p < lim && w--) { while (p < lim && *p != tabch) p++; if (p < lim) p++; }
    } else {
        while (p < lim && w--) { while (p < lim && is_blank(*p)) p++; while (p < lim && !is_blank(*p)) p++; }
    }
    if (k->flags & K_BLANK) while (p < lim && is_blank(*p)) p++;
    return k->schar < (unsigned long)(lim - p) ? p + k->schar : lim;
}

static const char *limfield(const struct sline *l, const struct skey *k)
{
    const char *p = l->p, *lim = l->p + l->len;
    unsigned long w = k->eword, ec = k->echar;
    if (w == ~0UL) return lim;
    if (!ec) w++;
    if (tabch >= 0) {
        while (p < lim && w--) { while (p < lim && *p != tabch) p++; if (p < lim && (w | ec)) p++; }
    } else {
        while (p < lim && w--) { while (p < lim && is_blank(*p)) p++; while (p < lim && !is_blank(*p)) p++; }
    }
    if (ec) {
        if (k->eblank) while (p < lim && is_blank(*p)) p++;
        p = ec < (unsigned long)(lim - p) ? p + ec : lim;
    }
    return p;
}

struct num { int neg; const char *ip, *fp; size_t il, fl; };

static struct num parse_num(const char *s, size_t n)
{
    struct num r = { 0, NULL, NULL, 0, 0 };
    size_t k;
    while (n && is_blank(*s)) s++, n--;
    if (n && *s == '-') { r.neg = 1; s++; n--; }
    while (n && *s == '0') s++, n--;
    r.ip = s;
    while (r.il < n && isdigit((unsigned char)s[r.il])) r.il++;
    if (r.il < n && s[r.il] == '.') {
        r.fp = s + r.il + 1;
        while (r.il + 1 + r.fl < n && isdigit((unsigned char)r.fp[r.fl])) r.fl++;
    }
    for (k = 0; k < r.fl && r.fp[k] == '0'; k++) ;
    if (!r.il && k == r.fl) r.neg = 0;                      /* -0 is 0 */
    return r;
}

static int numcmp(const char *a, size_t al, const char *b, size_t bl)
{
    struct num x = parse_num(a, al), y = parse_num(b, bl);
    size_t i, n;
    int r = 0;
    if (x.neg != y.neg) return x.neg ? -1 : 1;
    if (x.il != y.il) r = x.il < y.il ? -1 : 1;
    else if (x.il && (r = memcmp(x.ip, y.ip, x.il))) r = r < 0 ? -1 : 1;
    else {
        n = x.fl > y.fl ? x.fl : y.fl;
        for (i = 0; i < n && !r; i++) {
            int da = i < x.fl ? x.fp[i] : '0', db = i < y.fl ? y.fp[i] : '0';
            r = da < db ? -1 : da > db;
        }
    }
    return x.neg ? -r : r;
}

static int gencmp(const char *a, size_t al, const char *b, size_t bl)
{
    char ta[128], tb[128], *ea, *eb;
    double x, y;
    int va, vb;
    snprintf(ta, sizeof ta, "%.*s", (int)(al < 127 ? al : 127), a);
    snprintf(tb, sizeof tb, "%.*s", (int)(bl < 127 ? bl : 127), b);
    x = strtod(ta, &ea); y = strtod(tb, &eb);
    va = ea != ta; vb = eb != tb;
    if (!va || !vb) return va == vb ? 0 : va ? 1 : -1;
    if (x != x || y != y) return (x != x) == (y != y) ? 0 : x != x ? -1 : 1;
    return x < y ? -1 : x > y;
}

static int humcmp(const char *a, size_t al, const char *b, size_t bl)
{
    const char *sfx = "KMGTPEZY";
    size_t ia = 0, ib = 0;
    int ua = 0, ub = 0;
    while (al && is_blank(*a)) a++, al--;
    while (bl && is_blank(*b)) b++, bl--;
    while (ia < al && (isdigit((unsigned char)a[ia]) || a[ia] == '.' || (ia == 0 && a[ia] == '-'))) ia++;
    while (ib < bl && (isdigit((unsigned char)b[ib]) || b[ib] == '.' || (ib == 0 && b[ib] == '-'))) ib++;
    if (ia < al && strchr(sfx, toupper((unsigned char)a[ia]))) ua = strchr(sfx, toupper((unsigned char)a[ia])) - sfx + 1;
    if (ib < bl && strchr(sfx, toupper((unsigned char)b[ib]))) ub = strchr(sfx, toupper((unsigned char)b[ib])) - sfx + 1;
    {
        int na = ia && a[0] == '-', nb = ib && b[0] == '-';
        if (na != nb) return na ? -1 : 1;
        if (ua != ub) return (ua < ub ? -1 : 1) * (na ? -1 : 1);
    }
    return numcmp(a, ia, b, ib);
}

static int month_of(const char *s, size_t n)
{
    static const char *m[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    int i;
    while (n && is_blank(*s)) s++, n--;
    if (n < 3) return 0;
    for (i = 0; i < 12; i++) if (!strncasecmp(s, m[i], 3)) return i + 1;
    return 0;
}

static int vercmp(const char *a, size_t al, const char *b, size_t bl)
{
    size_t i = 0, j = 0;
    while (i < al || j < bl) {
        int ca = i < al ? (unsigned char)a[i] : 0, cb = j < bl ? (unsigned char)b[j] : 0;
        if (isdigit(ca) && isdigit(cb)) {
            size_t si = i, sj = j, li, lj;
            while (si < al && a[si] == '0') si++;
            while (sj < bl && b[sj] == '0') sj++;
            for (li = 0; si + li < al && isdigit((unsigned char)a[si + li]); li++) ;
            for (lj = 0; sj + lj < bl && isdigit((unsigned char)b[sj + lj]); lj++) ;
            if (li != lj) return li < lj ? -1 : 1;
            if (memcmp(a + si, b + sj, li)) return memcmp(a + si, b + sj, li) < 0 ? -1 : 1;
            i = si + li; j = sj + lj;
            continue;
        }
        if (ca != cb) {
            int oa = ca == '~' ? -1 : isalpha(ca) ? ca : ca ? ca + 256 : 0, ob = cb == '~' ? -1 : isalpha(cb) ? cb : cb ? cb + 256 : 0;
            return oa < ob ? -1 : 1;
        }
        i++; j++;
    }
    return 0;
}

/* plain text, skipping or folding as -d -f -i ask */
static int textcmp(const char *a, size_t al, const char *b, size_t bl, int flags)
{
    size_t i = 0, j = 0;
    for (;;) {
        int ca = -1, cb = -1;
        while (i < al) {
            int c = (unsigned char)a[i];
            if (((flags & K_DICT) && !isalnum(c) && !is_blank(c)) || ((flags & K_IGN) && !isprint(c))) { i++; continue; }
            ca = (flags & K_FOLD) ? toupper(c) : c;
            break;
        }
        while (j < bl) {
            int c = (unsigned char)b[j];
            if (((flags & K_DICT) && !isalnum(c) && !is_blank(c)) || ((flags & K_IGN) && !isprint(c))) { j++; continue; }
            cb = (flags & K_FOLD) ? toupper(c) : c;
            break;
        }
        if (ca < 0 || cb < 0) return ca == cb ? 0 : ca < 0 ? -1 : 1;
        if (ca != cb) return ca < cb ? -1 : 1;
        i++; j++;
    }
}

static int compare_keys(const struct sline *a, const struct sline *b)
{
    int i, n = nkeys ? nkeys : 1;
    for (i = 0; i < n; i++) {
        struct skey whole = { ~0UL, 0, ~0UL, 0, gflags, 0 };
        const struct skey *k = nkeys ? &keys[i] : &whole;
        const char *pa = a->p, *pb = b->p, *ea = a->p + a->len, *eb = b->p + b->len;
        int r, f = k->flags;
        if (nkeys) { pa = begfield(a, k); ea = limfield(a, k); pb = begfield(b, k); eb = limfield(b, k); if (ea < pa) ea = pa; if (eb < pb) eb = pb; }
        else if (f & K_BLANK) { while (pa < ea && is_blank(*pa)) pa++; while (pb < eb && is_blank(*pb)) pb++; }
        if (f & K_NUM) r = numcmp(pa, ea - pa, pb, eb - pb);
        else if (f & K_GEN) r = gencmp(pa, ea - pa, pb, eb - pb);
        else if (f & K_HUM) r = humcmp(pa, ea - pa, pb, eb - pb);
        else if (f & K_MON) { int x = month_of(pa, ea - pa), y = month_of(pb, eb - pb); r = x < y ? -1 : x > y; }
        else if (f & K_VER) r = vercmp(pa, ea - pa, pb, eb - pb);
        else if (f & (K_DICT | K_FOLD | K_IGN)) r = textcmp(pa, ea - pa, pb, eb - pb, f);
        else {
            size_t la = ea - pa, lb = eb - pb;
            r = memcmp(pa, pb, la < lb ? la : lb);
            if (!r) r = la < lb ? -1 : la > lb;
            r = r < 0 ? -1 : r > 0;
        }
        if (r) return f & K_REV ? -r : r;
    }
    return 0;
}

static int compare_lines(const struct sline *a, const struct sline *b)
{
    int r = compare_keys(a, b);
    if (r || stable || unique) return r;
    {
        size_t m = a->len < b->len ? a->len : b->len;
        r = memcmp(a->p, b->p, m);
        if (!r) r = a->len < b->len ? -1 : a->len > b->len;
        r = r < 0 ? -1 : r > 0;
    }
    return gflags & K_REV ? -r : r;
}

static void msort(struct sline *v, struct sline *tmp, size_t n)
{
    size_t m = n / 2, i = 0, j = 0, k = 0;
    if (n < 2) return;
    msort(v, tmp, m);
    msort(v + m, tmp, n - m);
    while (i < m && j < n - m) tmp[k++] = compare_lines(&v[i], &v[m + j]) <= 0 ? v[i++] : v[m + j++];
    while (i < m) tmp[k++] = v[i++];
    while (j < n - m) tmp[k++] = v[m + j++];
    memcpy(v, tmp, n * sizeof *v);
}

/* F[.C][bdfghiMnRrV]: the field, the character in it (counted from 0 for a start), and the flags after them */
static unsigned long parse_pos(const char *s, const char **end, unsigned long *chr, int *blank, int *flags, int start)
{
    char *e;
    unsigned long w = strtoul(s, &e, 10);
    *chr = 0;
    if (e == s) die(2, "invalid number at field start: invalid count at start of '%s'", s);
    if (*e == '.') {
        s = e + 1;
        *chr = strtoul(s, &e, 10);
        if (e == s) die(2, "invalid number after '.'");
        if (start) { if (!*chr) die(2, "character offset is zero: invalid field specification"); (*chr)--; }
    }
    for (; *e && *e != ','; e++) {
        switch (*e) {
        case 'b': *blank = 1; break;
        case 'd': *flags |= K_DICT; break;
        case 'f': *flags |= K_FOLD; break;
        case 'g': *flags |= K_GEN; break;
        case 'h': *flags |= K_HUM; break;
        case 'i': *flags |= K_IGN; break;
        case 'M': *flags |= K_MON; break;
        case 'n': *flags |= K_NUM; break;
        case 'R': *flags |= K_RAND; break;
        case 'r': *flags |= K_REV; break;
        case 'V': *flags |= K_VER; break;
        default: die(2, "stray character in field spec: invalid field specification '%s'", s);
        }
    }
    *end = e;
    return w;
}

static int t_sort(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "ignore-leading-blanks", 0, 'b' }, { "dictionary-order", 0, 'd' }, { "ignore-case", 0, 'f' }, { "general-numeric-sort", 0, 'g' },
        { "human-numeric-sort", 0, 'h' }, { "ignore-nonprinting", 0, 'i' }, { "key", 1, 'k' }, { "month-sort", 0, 'M' },
        { "numeric-sort", 0, 'n' }, { "random-sort", 0, 'R' }, { "reverse", 0, 'r' }, { "version-sort", 0, 'V' },
        { "check", 2, 'c' }, { "merge", 0, 'm' }, { "output", 1, 'o' }, { "stable", 0, 's' }, { "unique", 0, 'u' },
        { "zero-terminated", 0, 'z' }, { "field-separator", 1, 't' }, { "buffer-size", 1, 'S' }, { "temporary-directory", 1, 'T' },
        { "parallel", 1, 'P' }, { "batch-size", 1, 'B' }, { "compress-program", 1, 'X' }, { "sort", 1, 'W' }, { NULL, 0, 0 } };
    const char *outfile = NULL;
    int c, check = 0, merge = 0, sep = '\n', i, st = 0;
    char *data = NULL;
    size_t total = 0, n = 0, cap = 0, k, *foff, *fline;
    struct sline *v = NULL, *tmp;
    usage_status = 2;
    opts_reset();
    while ((c = getopts(argc, argv, "bdfghik:MnRrVcCmo:suzt:S:T:y", lo)))
        switch (c) {
        case 'b': gflags |= K_BLANK; break;
        case 'd': gflags |= K_DICT; break;
        case 'f': gflags |= K_FOLD; break;
        case 'g': gflags |= K_GEN; break;
        case 'h': gflags |= K_HUM; break;
        case 'i': gflags |= K_IGN; break;
        case 'M': gflags |= K_MON; break;
        case 'n': gflags |= K_NUM; break;
        case 'R': gflags |= K_RAND; break;
        case 'r': gflags |= K_REV; break;
        case 'V': gflags |= K_VER; break;
        case 'c': case 'C': check = c == 'C' ? 2 : 1; break;
        case 'o': outfile = optarg; break;
        case 'm': merge = 1; break;
        case 's': stable = 1; break;
        case 'u': unique = 1; break;
        case 'z': sep = 0; break;
        case 't':
            if (!strcmp(optarg, "\\0")) tabch = 0;
            else if (strlen(optarg) != 1) die(2, "multi-character tab '%s'", optarg);
            else tabch = (unsigned char)optarg[0];
            break;
        case 'W':
            if (!strcmp(optarg, "numeric")) gflags |= K_NUM; else if (!strcmp(optarg, "general-numeric")) gflags |= K_GEN;
            else if (!strcmp(optarg, "human-numeric")) gflags |= K_HUM; else if (!strcmp(optarg, "month")) gflags |= K_MON;
            else if (!strcmp(optarg, "version")) gflags |= K_VER; else if (!strcmp(optarg, "random")) gflags |= K_RAND;
            else die(2, "invalid argument '%s' for '--sort'", optarg);
            break;
        case 'k': {
            struct skey *key;
            const char *e;
            unsigned long w, ew;
            int sb = 0, eb = 0;
            if (nkeys == 32) die(2, "too many keys");
            key = &keys[nkeys++];
            memset(key, 0, sizeof *key);
            w = parse_pos(optarg, &e, &key->schar, &sb, &key->flags, 1);
            if (!w) die(2, "field number is zero: invalid field specification '%s'", optarg);
            key->sword = w - 1;
            if (sb) key->flags |= K_BLANK;
            if (!key->sword && !key->schar) key->sword = ~0UL;
            if (*e == ',') {
                ew = parse_pos(e + 1, &e, &key->echar, &eb, &key->flags, 0);
                if (!ew) die(2, "field number is zero: invalid field specification '%s'", optarg);
                key->eword = ew - 1;
                key->eblank = eb;
            } else key->eword = ~0UL;
            break;
        }
        default: break;
        }
    for (i = 0; i < nkeys; i++)                                   /* a key with no flags of its own gets the global ones */
        if (!keys[i].flags && !keys[i].eblank) keys[i].flags = gflags;
    if (!nops) ops[nops++] = "-";
    foff = xmalloc((nops + 1) * sizeof *foff);
    fline = xmalloc((nops + 2) * sizeof *fline);
    for (i = 0; i < nops; i++) {
        char *d;
        size_t len;
        int fd = open_input(ops[i]);
        if (fd < 0) { say("cannot read: %s: %s", ops[i], strerror(errno)); return 2; }
        if (read_all(fd, &d, &len) < 0) { say("read failed: %s: %s", ops[i], strerror(errno)); return 2; }
        if (fd) close(fd);
        foff[i] = total;
        data = xrealloc(data, total + len + 2);
        memcpy(data + total, d, len);
        total += len;
        if (len && d[len - 1] != sep) data[total++] = sep;
        free(d);
    }
    /* lines point into data, which can move while it grows: so cut it up only now */
    for (k = 0, i = 0; k < total; ) {
        char *e = memchr(data + k, sep, total - k);
        while (i < nops && k >= foff[i]) fline[i++] = n;
        if (n == cap) { cap = cap ? cap * 2 : 1024; v = xrealloc(v, cap * sizeof *v); }
        v[n].p = data + k;
        v[n].len = e ? (size_t)(e - (data + k)) : total - k;
        n++;
        k += v[n - 1].len + 1;
    }
    while (i <= nops) fline[i++] = n;
    if (check) {
        for (k = 1; k < n; k++) {
            int r = compare_keys(&v[k - 1], &v[k]);
            if (r > 0 || (unique && !r)) {
                if (check == 1) say("%s:%lu: disorder: %.*s", ops[0], (unsigned long)k + 1, (int)v[k].len, v[k].p);
                return 1;
            }
        }
        return 0;
    }
    tmp = xmalloc((n + 1) * sizeof *tmp);
    if (gflags & K_RAND) {                                       /* a shuffle, not grouped by key like GNU's */
        for (k = n; k > 1; k--) {
            unsigned long long r;
            struct sline t;
            size_t j;
            getrandom(&r, sizeof r, 0);
            j = r % k;
            t = v[k - 1]; v[k - 1] = v[j]; v[j] = t;
        }
    } else if (merge) {
        struct sline *m = xmalloc((n + 1) * sizeof *m);
        size_t *head = xmalloc(nops * sizeof *head), cnt = 0;
        for (i = 0; i < nops; i++) head[i] = fline[i];
        for (;;) {
            int best = -1;
            for (i = 0; i < nops; i++)
                if (head[i] < fline[i + 1] && (best < 0 || compare_lines(&v[head[i]], &v[head[best]]) < 0)) best = i;
            if (best < 0) break;
            m[cnt++] = v[head[best]++];
        }
        v = m;
    } else msort(v, tmp, n);
    if (outfile && !freopen(outfile, "w", stdout)) { perr("open failed: %s", outfile); return 2; }
    for (k = 0; k < n; k++) {
        if (unique && k && !compare_keys(&v[k - 1], &v[k])) continue;
        fwrite(v[k].p, 1, v[k].len, stdout);
        putchar(sep);
    }
    return finish_stdout() ? 2 : st;
}

/* ---- helpers for the later tools ---- */

static int glob_match(const char *p, const char *s)
{
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (p[1] == '*') p++;
            if (!p[1]) return 1;
            for (; *s; s++) if (glob_match(p + 1, s)) return 1;
            return glob_match(p + 1, s);
        }
        if (!*s) return 0;
        if (*p == '?') continue;
        if (*p == '[') {
            int neg = p[1] == '!' || p[1] == '^', ok = 0;
            const char *q = p + 1 + neg;
            if (*q == ']') { if (*s == ']') ok = 1; q++; }
            for (; *q && *q != ']'; q++) {
                if (q[1] == '-' && q[2] && q[2] != ']') { if ((unsigned char)*s >= (unsigned char)q[0] && (unsigned char)*s <= (unsigned char)q[2]) ok = 1; q += 2; }
                else if (*q == *s) ok = 1;
            }
            if (!*q || ok == neg) return 0;
            p = q;
            continue;
        }
        if (*p == '\\' && p[1]) p++;
        if (*p != *s) return 0;
    }
    return !*s;
}

/* ---- test [ ---- */

static char **targv;
static int targc, tpos;

static int test_or(void);

static int t_binop(const char *s)
{
    static const char *const names[] = { "=", "==", "!=", "<", ">", "-eq", "-ne", "-lt", "-le", "-gt", "-ge", "-nt", "-ot", "-ef", NULL };
    int i;
    for (i = 0; names[i]; i++) if (!strcmp(s, names[i])) return 1;
    return 0;
}

static long long t_int(const char *s)
{
    char *e;
    long long v;
    while (isspace((unsigned char)*s)) s++;
    errno = 0;
    v = strtoll(s, &e, 10);
    while (isspace((unsigned char)*e)) e++;
    if (e == s || *e) die(2, "%s: integer expression expected", s);
    return v;
}

static int t_unary(const char *op, const char *a)
{
    struct stat st;
    int r;
    if (!strcmp(op, "-n")) return *a != 0;
    if (!strcmp(op, "-z")) return *a == 0;
    if (!strcmp(op, "-t")) return isatty((int)t_int(a));
    if (!strcmp(op, "-h") || !strcmp(op, "-L")) return lstat(a, &st) == 0 && S_ISLNK(st.st_mode);
    r = stat(a, &st);
    if (op[1] == 'r') return access(a, R_OK) == 0;
    if (op[1] == 'w') return access(a, W_OK) == 0;
    if (op[1] == 'x') return access(a, X_OK) == 0;
    if (r < 0) return 0;
    switch (op[1]) {
    case 'e': return 1;
    case 'f': return S_ISREG(st.st_mode);
    case 'd': return S_ISDIR(st.st_mode);
    case 'b': return S_ISBLK(st.st_mode);
    case 'c': return S_ISCHR(st.st_mode);
    case 'p': return S_ISFIFO(st.st_mode);
    case 'S': return S_ISSOCK(st.st_mode);
    case 's': return st.st_size > 0;
    case 'u': return !!(st.st_mode & S_ISUID);
    case 'g': return !!(st.st_mode & S_ISGID);
    case 'k': return !!(st.st_mode & S_ISVTX);
    case 'O': return st.st_uid == geteuid();
    case 'G': return st.st_gid == getegid();
    case 'N': return st.st_mtime > st.st_atime || (st.st_mtime == st.st_atime && st.st_mtime_nsec > st.st_atime_nsec);
    }
    return 0;
}

static int t_isunary(const char *s)
{
    return s[0] == '-' && s[1] && !s[2] && strchr("nzthLebcdfgkprsuwxOGNS", s[1]);
}

static int t_binary(const char *a, const char *op, const char *b)
{
    struct stat x, y;
    if (!strcmp(op, "=") || !strcmp(op, "==")) return !strcmp(a, b);
    if (!strcmp(op, "!=")) return strcmp(a, b) != 0;
    if (!strcmp(op, "<")) return strcmp(a, b) < 0;
    if (!strcmp(op, ">")) return strcmp(a, b) > 0;
    if (!strcmp(op, "-nt") || !strcmp(op, "-ot")) {
        int ha = stat(a, &x) == 0, hb = stat(b, &y) == 0, nt = op[1] == 'n';
        if (!ha || !hb) return nt ? ha && !hb : hb && !ha;
        if (x.st_mtime != y.st_mtime) return nt ? x.st_mtime > y.st_mtime : x.st_mtime < y.st_mtime;
        return nt ? x.st_mtime_nsec > y.st_mtime_nsec : x.st_mtime_nsec < y.st_mtime_nsec;
    }
    if (!strcmp(op, "-ef")) return stat(a, &x) == 0 && stat(b, &y) == 0 && x.st_dev == y.st_dev && x.st_ino == y.st_ino;
    {
        long long l = t_int(a), r = t_int(b);
        if (!strcmp(op, "-eq")) return l == r;
        if (!strcmp(op, "-ne")) return l != r;
        if (!strcmp(op, "-lt")) return l < r;
        if (!strcmp(op, "-le")) return l <= r;
        if (!strcmp(op, "-gt")) return l > r;
        return l >= r;
    }
}

static int test_primary(void)
{
    char *a;
    if (tpos >= targc) die(2, "missing argument");
    a = targv[tpos];
    if (!strcmp(a, "(") && !(tpos + 2 < targc && t_binop(targv[tpos + 1]) && tpos + 3 >= targc)) {
        int v;
        tpos++;
        v = test_or();
        if (tpos >= targc || strcmp(targv[tpos], ")")) die(2, "expected ')'");
        tpos++;
        return v;
    }
    if (tpos + 2 < targc + 0 && t_binop(targv[tpos + 1])) {
        int v = t_binary(a, targv[tpos + 1], targv[tpos + 2]);
        tpos += 3;
        return v;
    }
    if (t_isunary(a) && tpos + 1 < targc) {
        int v = t_unary(a, targv[tpos + 1]);
        tpos += 2;
        return v;
    }
    tpos++;
    return *a != 0;
}

static int test_not(void)
{
    if (tpos < targc && !strcmp(targv[tpos], "!") && tpos + 1 < targc) {
        tpos++;
        return !test_not();
    }
    return test_primary();
}

static int test_and(void)
{
    int v = test_not();
    while (tpos < targc && !strcmp(targv[tpos], "-a")) { tpos++; v = test_not() && v; }
    return v;
}

static int test_or(void)
{
    int v = test_and();
    while (tpos < targc && !strcmp(targv[tpos], "-o")) { tpos++; v = test_and() || v; }
    return v;
}

static int t_test(int argc, char **argv)
{
    int v;
    targv = argv + 1; targc = argc - 1; tpos = 0;
    if (!strcmp(prog, "[")) {
        if (argc == 2 && !strcmp(argv[1], "--help")) { print_usage(); return 0; }
        if (argc == 2 && !strcmp(argv[1], "--version")) { printf("[ (rifle) " VERSION "\n"); return 0; }
        if (targc < 1 || strcmp(targv[targc - 1], "]")) die(2, "missing ']'");
        targc--;
    } else if (argc == 2 && !strcmp(argv[1], "--help")) { print_usage(); return 0; }
    else if (argc == 2 && !strcmp(argv[1], "--version")) { printf("test (rifle) " VERSION "\n"); return 0; }
    if (!targc) return 1;
    v = test_or();
    if (tpos < targc) die(2, "extra argument '%s'", targv[tpos]);
    return !v;
}

/* ---- printf ---- */

static int pf_status;

static unsigned pf_hex(const char *p, int max, int *n)
{
    unsigned v = 0;
    *n = 0;
    while (*n < max && isxdigit((unsigned char)p[*n])) {
        v = v * 16 + (isdigit((unsigned char)p[*n]) ? p[*n] - '0' : (p[*n] | 32) - 'a' + 10);
        (*n)++;
    }
    return v;
}

static void pf_utf8(unsigned c, char *out)
{
    if (c < 0x80) { *out++ = c; }
    else if (c < 0x800) { *out++ = 0xc0 | c >> 6; *out++ = 0x80 | (c & 63); }
    else if (c < 0x10000) { *out++ = 0xe0 | c >> 12; *out++ = 0x80 | (c >> 6 & 63); *out++ = 0x80 | (c & 63); }
    else { *out++ = 0xf0 | c >> 18; *out++ = 0x80 | (c >> 12 & 63); *out++ = 0x80 | (c >> 6 & 63); *out++ = 0x80 | (c & 63); }
    *out = 0;
}

/* p is just past the backslash. the bytes go to out, the return is how far we got, *stop is set by \c */
static const char *pf_esc(const char *p, int in_b, char *out, size_t *olen, int *stop)
{
    int n, k;
    unsigned v;
    *olen = 1;
    switch (*p) {
    case 'a': out[0] = 7; return p + 1;
    case 'b': out[0] = 8; return p + 1;
    case 'f': out[0] = 12; return p + 1;
    case 'n': out[0] = 10; return p + 1;
    case 'r': out[0] = 13; return p + 1;
    case 't': out[0] = 9; return p + 1;
    case 'v': out[0] = 11; return p + 1;
    case 'e': out[0] = 27; return p + 1;
    case '\\': out[0] = '\\'; return p + 1;
    case '"': if (!in_b) { out[0] = '"'; return p + 1; } break;
    case 'c': *stop = 1; *olen = 0; return p + 1;
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
        v = 0; n = 0;
        if (in_b && *p == '0') p++;
        for (k = 0; k < 3 && p[n] >= '0' && p[n] <= '7'; k++, n++) v = v * 8 + p[n] - '0';
        out[0] = v;
        return p + n;
    case 'x':
        v = pf_hex(p + 1, 2, &n);
        if (!n) break;
        out[0] = v;
        return p + 1 + n;
    case 'u': case 'U':
        v = pf_hex(p + 1, *p == 'u' ? 4 : 8, &n);
        if (!n) break;
        pf_utf8(v, out);
        *olen = strlen(out);
        return p + 1 + n;
    }
    out[0] = '\\';
    *olen = 1;
    return p;
}

static long long pf_num(const char *s, int is_unsigned)
{
    char *e;
    long long v;
    if (!s) return 0;
    if (*s == '\'' || *s == '"') return (unsigned char)s[1];
    errno = 0;
    v = is_unsigned ? (long long)strtoull(s, &e, 0) : strtoll(s, &e, 0);
    if (e == s) { say("'%s': expected a numeric value", s); pf_status = 1; return 0; }
    if (*e) { say("'%s': value not completely converted", s); pf_status = 1; }
    return v;
}

static void pf_quote(const char *s)
{
    int plain = 1;
    const char *p;
    if (!*s) { fputs("''", stdout); return; }
    for (p = s; *p; p++) if (!isprint((unsigned char)*p)) plain = 0;
    if (!plain) {
        fputs("$'", stdout);
        for (p = s; *p; p++) {
            unsigned char c = *p;
            if (c == '\n') fputs("\\n", stdout); else if (c == '\t') fputs("\\t", stdout);
            else if (c == '\'' || c == '\\') printf("\\%c", c);
            else if (c < 32 || c == 127) printf("\\%03o", c);
            else putchar(c);
        }
        putchar('\'');
        return;
    }
    for (p = s; *p; p++) if (!isalnum((unsigned char)*p) && !strchr("_-./:,%+=@~^", *p)) break;
    if (!*p) { fputs(s, stdout); return; }
    putchar('\'');
    for (p = s; *p; p++) { if (*p == '\'') fputs("'\\''", stdout); else putchar(*p); }
    putchar('\'');
}

static int t_printf(int argc, char **argv)
{
    const char *fmt;
    char **args;
    int nargs, ai = 0, a1 = 1;
    if (argc == 2 && !strcmp(argv[1], "--help")) { print_usage(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--version")) { printf("printf (rifle) " VERSION "\n"); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--")) a1 = 2;
    if (a1 >= argc) { say("missing operand"); usage_error(); }
    fmt = argv[a1];
    args = argv + a1 + 1; nargs = argc - a1 - 1;
    for (;;) {
        const char *p = fmt;
        int used = 0, stop = 0;
        while (*p && !stop) {
            if (*p == '\\') {
                char b[8];
                size_t n;
                p = pf_esc(p + 1, 0, b, &n, &stop);
                fwrite(b, 1, n, stdout);
            } else if (*p != '%') putchar(*p++);
            else {
                char spec[64], conv;
                int k = 0, wstar = 0, pstar = 0;
                const char *arg;
                long long w = 0, pr = -1;
                p++;
                if (*p == '%') { putchar('%'); p++; continue; }
                spec[k++] = '%';
                while (*p && strchr("-+ #0'", *p) && k < 20) { if (*p != '\'') spec[k++] = *p; p++; }
                if (*p == '*') { wstar = 1; p++; if (ai < nargs) w = pf_num(args[ai++], 0); used++; }
                else while (isdigit((unsigned char)*p) && k < 40) spec[k++] = *p++;
                if (*p == '.') {
                    p++;
                    if (*p == '*') { pstar = 1; p++; if (ai < nargs) pr = pf_num(args[ai++], 0); used++; }
                    else { pr = 0; while (isdigit((unsigned char)*p)) pr = pr * 10 + *p++ - '0'; }
                }
                while (*p == 'l' || *p == 'h' || *p == 'L' || *p == 'j' || *p == 'z' || *p == 't') p++;
                conv = *p;
                if (!conv) { say("%.*s: invalid conversion specification", (int)(p - fmt), fmt); return 1; }
                p++;
                if (!strchr("diouxXeEfFgGaAcsbq", conv)) { say("%%%c: invalid conversion specification", conv); return 1; }
                used++;
                arg = ai < nargs ? args[ai++] : NULL;
                if (wstar) k += sprintf(spec + k, "%lld", w);
                if (pr >= 0 || pstar) { if (pr < 0) pr = 0; k += sprintf(spec + k, ".%lld", pr); }
                spec[k] = 0;
                if (strchr("di", conv)) { strcat(spec, "lld"); printf(spec, pf_num(arg, 0)); }
                else if (strchr("ouxX", conv)) { strcat(spec, "ll"); k = strlen(spec); spec[k] = conv; spec[k + 1] = 0; printf(spec, (unsigned long long)pf_num(arg, 1)); }
                else if (strchr("eEfFgGaA", conv)) {
                    double d = 0;
                    if (arg) {
                        char *e;
                        d = strtod(arg, &e);
                        if (e == arg) { say("'%s': expected a numeric value", arg); pf_status = 1; }
                        else if (*e) { say("'%s': value not completely converted", arg); pf_status = 1; }
                    }
                    k = strlen(spec); spec[k] = conv; spec[k + 1] = 0;
                    printf(spec, d);
                } else if (conv == 'c') { strcat(spec, "c"); printf(spec, arg ? (unsigned char)arg[0] : 0); }
                else if (conv == 's') { strcat(spec, "s"); printf(spec, arg ? arg : ""); }
                else if (conv == 'q') pf_quote(arg ? arg : "");
                else {
                    char *buf = xmalloc((arg ? strlen(arg) : 0) + 1), *o = buf;
                    const char *s = arg ? arg : "";
                    while (*s && !stop) {
                        if (*s == '\\') { char b[8]; size_t n; s = pf_esc(s + 1, 1, b, &n, &stop); memcpy(o, b, n); o += n; }
                        else *o++ = *s++;
                    }
                    *o = 0;
                    strcat(spec, "s");
                    printf(spec, buf);
                    free(buf);
                }
            }
        }
        if (stop || ai >= nargs || !used) break;
    }
    return finish_stdout() ? 1 : pf_status;
}

/* ---- expr ---- */

struct ev { int num; long long n; char *s; };

static char **eargv;
static int eargc, epos;

static int ev_true(const struct ev *v) { return v->num ? v->n != 0 : v->s[0] && !(strspn(v->s, "0") == strlen(v->s)); }
static void ev_str(struct ev *v) { if (v->num) { v->s = xmalloc(24); sprintf(v->s, "%lld", v->n); v->num = 0; } }
static int ev_isnum(struct ev *v)
{
    char *e;
    if (v->num) return 1;
    if (!*v->s) return 0;
    v->n = strtoll(v->s, &e, 10);
    if (*e || (!isdigit((unsigned char)v->s[0]) && v->s[0] != '-' && v->s[0] != '+')) return 0;
    return 1;
}
static long long ev_int(struct ev *v) { if (!ev_isnum(v)) die(2, "non-integer argument"); return v->n; }

/* a small BRE: . * [] \( \) \{ \} \+ \? \| ^ $ and backtracking through groups */
struct re { const char *p; const char *start[10], *stop[10]; int ng; };
struct cont { const char *after, *body; struct cont *next; int g, count, min, max; const char *at; };

static const char *re_close(const char *p)
{
    int d = 0;
    while (*p) {
        if (p[0] == '\\' && p[1]) {
            if (p[1] == '(') d++;
            else if (p[1] == ')') { if (!d) return p; d--; }
            p += 2;
        } else p++;
    }
    return p;
}

static const char *re_atom_end(const char *p)
{
    if (*p == '\\' && p[1] == '(') return re_close(p + 2) + 2;
    if (*p == '\\' && p[1]) return p + 2;
    if (*p == '[') {
        p++;
        if (*p == '^') p++;
        if (*p == ']') p++;
        while (*p && *p != ']') {
            if (*p == '[' && (p[1] == ':' || p[1] == '.' || p[1] == '=')) { const char *q = strstr(p + 2, p[1] == ':' ? ":]" : p[1] == '.' ? ".]" : "=]"); if (q) p = q + 1; }
            p++;
        }
        return *p ? p + 1 : p;
    }
    return p + 1;
}

static int re_class(const char *p, const char *end, int c)
{
    int neg = 0, ok = 0;
    p++;
    if (*p == '^') { neg = 1; p++; }
    for (; p < end - 1; p++) {
        if (*p == '[' && p[1] == ':') {
            const char *q = strstr(p + 2, ":]");
            char name[16];
            size_t l = q ? q - p - 2 : 0;
            if (q && l < sizeof name) {
                memcpy(name, p + 2, l); name[l] = 0;
                if ((!strcmp(name, "alpha") && isalpha(c)) || (!strcmp(name, "digit") && isdigit(c)) || (!strcmp(name, "alnum") && isalnum(c)) ||
                    (!strcmp(name, "upper") && isupper(c)) || (!strcmp(name, "lower") && islower(c)) || (!strcmp(name, "space") && isspace(c)) ||
                    (!strcmp(name, "punct") && ispunct(c)) || (!strcmp(name, "xdigit") && isxdigit(c)) || (!strcmp(name, "print") && isprint(c)) ||
                    (!strcmp(name, "blank") && (c == ' ' || c == '\t'))) ok = 1;
                p = q + 1;
                continue;
            }
        }
        if (p[1] == '-' && p + 2 < end - 1) { if (c >= (unsigned char)p[0] && c <= (unsigned char)p[2]) ok = 1; p += 2; }
        else if ((unsigned char)*p == c) ok = 1;
    }
    return ok != neg;
}

static int re_one(const char *atom, const char *aend, int c)
{
    if (!c) return 0;
    if (*atom == '.') return 1;
    if (*atom == '[') return re_class(atom, aend, c);
    if (*atom == '\\') return (unsigned char)atom[1] == c;
    return (unsigned char)*atom == c;
}

static const char *re_m(struct re *r, const char *p, const char *s, struct cont *k);

/* the next alternative of a group body, or NULL */
static const char *re_next_alt(const char *p)
{
    int d = 0;
    while (*p) {
        if (p[0] == '\\' && p[1]) {
            if (p[1] == '(') d++;
            else if (p[1] == ')') { if (!d) return NULL; d--; }
            else if (p[1] == '|' && !d) return p + 2;
            p += 2;
        } else p++;
    }
    return NULL;
}

static const char *re_group(struct re *r, const char *body, const char *s, struct cont *k)
{
    const char *alt;
    for (alt = body; alt; alt = re_next_alt(alt)) {
        const char *res = re_m(r, alt, s, k);
        if (res) return res;
    }
    return NULL;
}

static const char *re_m(struct re *r, const char *p, const char *s, struct cont *k)
{
    const char *aend, *rest;
    int min = 1, max = 1;
    if (!*p || (p[0] == '\\' && p[1] == '|')) {
        if (!k) return s;
        p = re_close(p);
    }
    if (p[0] == '\\' && p[1] == ')') {
        int cnt;
        if (!k) return s;
        cnt = k->count + 1;
        r->stop[k->g] = s;
        if (cnt < k->max && s != k->at) {
            struct cont nk = *k;
            const char *res, *os = r->start[k->g];
            nk.count = cnt; nk.at = s;
            r->start[k->g] = s;
            res = re_group(r, k->body, s, &nk);
            if (res) return res;
            r->start[k->g] = os;
        }
        if (cnt >= k->min) return re_m(r, k->after, s, k->next);
        return NULL;
    }
    if (*p == '$' && (!p[1] || (p[1] == '\\' && (p[2] == '|' || p[2] == ')')))) return *s ? NULL : re_m(r, p + 1, s, k);
    if (*p == '^' && p == r->p) return re_m(r, p + 1, s, k);
    aend = re_atom_end(p);
    rest = aend;
    if (*aend == '*') { min = 0; max = 1 << 30; rest = aend + 1; }
    else if (aend[0] == '\\' && aend[1] == '+') { min = 1; max = 1 << 30; rest = aend + 2; }
    else if (aend[0] == '\\' && aend[1] == '?') { min = 0; max = 1; rest = aend + 2; }
    else if (aend[0] == '\\' && aend[1] == '{') {
        char *e;
        min = strtol(aend + 2, &e, 10); max = min;
        if (*e == ',') { e++; max = isdigit((unsigned char)*e) ? (int)strtol(e, &e, 10) : 1 << 30; }
        rest = e + 2;
    }
    if (p[0] == '\\' && p[1] == '(') {
        struct cont nk;
        const char *res;
        nk.after = rest; nk.body = p + 2; nk.next = k; nk.g = ++r->ng; nk.count = 0; nk.min = min; nk.max = max; nk.at = s;
        if (max > 0) {
            r->start[nk.g] = s;
            res = re_group(r, nk.body, s, &nk);
            if (res) return res;
        }
        if (min == 0) return re_m(r, rest, s, k);
        return NULL;
    }
    {
        int n = 0;
        while (n < max && re_one(p, aend, (unsigned char)s[n])) n++;
        for (; n >= min; n--) {
            const char *res = re_m(r, rest, s + n, k);
            if (res) return res;
        }
    }
    return NULL;
}

static int re_match(const char *re, const char *s, struct ev *out)
{
    struct re r;
    const char *p = re, *e = NULL, *q;
    int has = 0;
    memset(&r, 0, sizeof r);
    r.p = re;
    for (;;) {
        r.ng = 0;
        e = re_m(&r, p, s, NULL);
        if (e) break;
        p = re_next_alt(p);
        if (!p) break;
    }
    for (q = re; *q; q++) { if (q[0] == '\\' && q[1] == '(') { has = 1; break; } if (q[0] == '\\' && q[1]) q++; }
    if (has) {
        out->num = 0;
        if (!e || !r.stop[1] || !r.start[1]) out->s = xstrdup("");
        else { out->s = xmalloc(r.stop[1] - r.start[1] + 1); memcpy(out->s, r.start[1], r.stop[1] - r.start[1]); out->s[r.stop[1] - r.start[1]] = 0; }
    } else { out->num = 1; out->n = e ? e - s : 0; }
    return e != NULL;
}

static struct ev ex_or(void);

static int ex_tok(const char *t) { return epos < eargc && !strcmp(eargv[epos], t); }

static struct ev ex_prim(void)
{
    struct ev v;
    if (epos >= eargc) die(2, "syntax error: missing argument");
    if (ex_tok("(")) {
        epos++;
        v = ex_or();
        if (!ex_tok(")")) die(2, epos >= eargc ? "syntax error: expecting ')' after '%s'" : "syntax error: expecting ')' instead of '%s'", eargv[epos < eargc ? epos : epos - 1]);
        epos++;
        return v;
    }
    if (ex_tok("match") && epos + 2 < eargc + 0) {
        struct ev r; v.num = 0;
        r.num = 0; r.s = eargv[epos + 1];
        v.s = eargv[epos + 2]; epos += 3;
        {
            struct ev out;
            re_match(v.s, r.s, &out);
            return out;
        }
    }
    if (ex_tok("substr") && epos + 3 < eargc + 0) {
        long long pos = atoll(eargv[epos + 2]), len = atoll(eargv[epos + 3]);
        const char *s = eargv[epos + 1];
        long long sl = strlen(s);
        v.num = 0;
        if (pos < 1 || len < 1 || pos > sl) v.s = xstrdup("");
        else { if (len > sl - pos + 1) len = sl - pos + 1; v.s = xmalloc(len + 1); memcpy(v.s, s + pos - 1, len); v.s[len] = 0; }
        epos += 4;
        return v;
    }
    if (ex_tok("index") && epos + 2 < eargc + 0) {
        const char *s = eargv[epos + 1], *c = eargv[epos + 2], *f = s + strcspn(s, c);
        v.num = 1; v.n = *f ? f - s + 1 : 0;
        epos += 3;
        return v;
    }
    if (ex_tok("length") && epos + 1 < eargc + 0) {
        v.num = 1; v.n = strlen(eargv[epos + 1]);
        epos += 2;
        return v;
    }
    if (ex_tok("+") && epos + 1 < eargc) epos++;
    v.num = 0; v.s = eargv[epos++];
    return v;
}

static struct ev ex_match(void)
{
    struct ev l = ex_prim();
    while (ex_tok(":")) {
        struct ev r, out;
        epos++;
        r = ex_prim();
        ev_str(&l); ev_str(&r);
        re_match(r.s, l.s, &out);
        l = out;
    }
    return l;
}

static struct ev ex_mul(void)
{
    struct ev l = ex_match();
    while (ex_tok("*") || ex_tok("/") || ex_tok("%")) {
        char op = eargv[epos++][0];
        struct ev r = ex_match();
        long long a = ev_int(&l), b = ev_int(&r);
        if (op != '*' && !b) die(2, "division by zero");
        l.num = 1;
        l.n = op == '*' ? a * b : op == '/' ? a / b : a % b;
    }
    return l;
}

static struct ev ex_add(void)
{
    struct ev l = ex_mul();
    while (ex_tok("+") || ex_tok("-")) {
        char op = eargv[epos++][0];
        struct ev r = ex_mul();
        long long a = ev_int(&l), b = ev_int(&r);
        l.num = 1;
        l.n = op == '+' ? a + b : a - b;
    }
    return l;
}

static struct ev ex_cmp(void)
{
    struct ev l = ex_add();
    while (ex_tok("=") || ex_tok("==") || ex_tok("!=") || ex_tok("<") || ex_tok("<=") || ex_tok(">") || ex_tok(">=")) {
        const char *op = eargv[epos++];
        struct ev r = ex_add();
        int c;
        if (ev_isnum(&l) && ev_isnum(&r)) c = l.n < r.n ? -1 : l.n > r.n;
        else { ev_str(&l); ev_str(&r); c = strcoll(l.s, r.s); }
        l.num = 1;
        if (!strcmp(op, "=") || !strcmp(op, "==")) l.n = c == 0;
        else if (!strcmp(op, "!=")) l.n = c != 0;
        else if (!strcmp(op, "<")) l.n = c < 0;
        else if (!strcmp(op, "<=")) l.n = c <= 0;
        else if (!strcmp(op, ">")) l.n = c > 0;
        else l.n = c >= 0;
    }
    return l;
}

static struct ev ex_and(void)
{
    struct ev l = ex_cmp();
    while (ex_tok("&")) {
        struct ev r;
        epos++;
        r = ex_cmp();
        if (!ev_true(&l) || !ev_true(&r)) { l.num = 1; l.n = 0; }
    }
    return l;
}

static struct ev ex_or(void)
{
    struct ev l = ex_and();
    while (ex_tok("|")) {
        struct ev r;
        epos++;
        r = ex_and();
        if (!ev_true(&l)) l = ev_true(&r) ? r : (struct ev){ 1, 0, NULL };
    }
    return l;
}

static int t_expr(int argc, char **argv)
{
    struct ev v;
    if (argc == 2 && !strcmp(argv[1], "--help")) { print_usage(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--version")) { printf("expr (rifle) " VERSION "\n"); return 0; }
    if (argc < 2) { say("missing operand"); usage_error(); }
    usage_status = 2;
    eargv = argv + 1; eargc = argc - 1; epos = 0;
    v = ex_or();
    if (epos < eargc) die(2, "syntax error: unexpected argument '%s'", eargv[epos]);
    if (v.num) printf("%lld\n", v.n); else printf("%s\n", v.s);
    return finish_stdout() ? 1 : !ev_true(&v);
}

/* ---- id groups ---- */

static int t_id(int argc, char **argv)
{
    static const struct opt lo[] = { { "user", 0, 'u' }, { "group", 0, 'g' }, { "groups", 0, 'G' }, { "name", 0, 'n' },
        { "real", 0, 'r' }, { "zero", 0, 'z' }, { NULL, 0, 0 } };
    int c, u = 0, g = 0, G = 0, n = 0, r = 0, z = 0, bad = 0, ngr, i, isgroups = !strcmp(prog, "groups");
    uid_t ruid, euid, uid;
    gid_t rgid, egid, gid, list[256];
    struct passwd *pw = NULL;
    opts_reset();
    while ((c = getopts(argc, argv, isgroups ? "" : "ugGnrzaZ", lo)))
        switch (c) {
        case 'u': u = 1; break; case 'g': g = 1; break; case 'G': G = 1; break; case 'n': n = 1; break;
        case 'r': r = 1; break; case 'z': z = 1; break; default: break;
        }
    if (isgroups) G = n = 1;
    if (u + g + G > 1) die(1, "cannot print \"only\" of more than one choice");
    if ((n || r) && !(u || g || G)) die(1, "cannot print only names or real IDs in default format");
    if (z && !(u || g || G)) die(1, "option --zero not permitted in default format");
    if (nops > 1 && !isgroups) extra_operand(ops[1]);
    do {
        const char *who = nops ? ops[isgroups ? bad : 0] : NULL;
        if (who) {
            pw = getpwnam(who);
            if (!pw) {
                unsigned long v;
                if (parse_ulong(who, &v)) pw = getpwuid(v);
            }
            if (!pw) { say("'%s': no such user", who); return 1; }
            ruid = euid = uid = pw->pw_uid; rgid = egid = gid = pw->pw_gid;
            ngr = 0;
            {
                FILE *gf = fopen("/etc/group", "r");
                char *gl = NULL;
                size_t gc = 0;
                list[ngr++] = gid;
                while (gf && getline(&gl, &gc, gf) > 0) {
                    char *f[4], *q = gl;
                    int k;
                    for (k = 0; k < 3; k++) { f[k] = q; q = strchr(q, ':'); if (!q) break; *q++ = 0; }
                    if (k < 3) continue;
                    f[3] = q;
                    q[strcspn(q, "\n")] = 0;
                    for (; f[3]; f[3] = q) {
                        q = strchr(f[3], ',');
                        if (q) *q++ = 0;
                        if (!strcmp(f[3], pw->pw_name) && ngr < 256 && (gid_t)atoi(f[2]) != gid) list[ngr++] = atoi(f[2]);
                    }
                }
                if (gf) fclose(gf);
            }
        } else {
            ruid = getuid(); euid = geteuid(); rgid = getgid(); egid = getegid();
            uid = r ? ruid : euid; gid = r ? rgid : egid;
            ngr = getgroups(256, list);
            if (ngr < 0) ngr = 0;
        }
        if (isgroups && nops) printf("%s : ", who);
        if (u) printf("%s", n ? user_name(uid) : (snprintf(NULL, 0, "") , ""));
        if (u && !n) printf("%u", uid);
        else if (g) { if (n) printf("%s", group_name(gid)); else printf("%u", gid); }
        else if (G) {
            int first = 1;
            gid_t main_g = who ? gid : egid;
            for (i = -1; i < ngr; i++) {
                gid_t x = i < 0 ? main_g : list[i];
                int j, dup = 0;
                if (i >= 0 && x == main_g) continue;
                for (j = 0; j < i; j++) if (list[j] == x) dup = 1;
                if (dup) continue;
                if (!first) putchar(isgroups || n ? ' ' : z ? 0 : ' ');
                first = 0;
                if (n) printf("%s", group_name(x)); else printf("%u", x);
            }
        } else if (!u) {
            printf("uid=%u(%s) gid=%u(%s)", ruid, user_name(ruid), rgid, group_name(rgid));
            if (euid != ruid) printf(" euid=%u(%s)", euid, user_name(euid));
            if (egid != rgid) printf(" egid=%u(%s)", egid, group_name(egid));
            printf(" groups=");
            {
                int first = 1;
                gid_t main_g = who ? gid : egid;
                for (i = -1; i < ngr; i++) {
                    gid_t x = i < 0 ? main_g : list[i];
                    int j, dup = 0;
                    if (i >= 0 && x == main_g) continue;
                    for (j = 0; j < i; j++) if (list[j] == x) dup = 1;
                    if (dup) continue;
                    printf("%s%u(%s)", first ? "" : ",", x, group_name(x));
                    first = 0;
                }
            }
        }
        putchar(z ? 0 : '\n');
        bad++;
    } while (isgroups && bad < nops);
    return finish_stdout() ? 1 : 0;
}
/* ---- md5sum sha*sum cksum sum ---- */

enum { A_MD5, A_SHA1, A_SHA224, A_SHA256, A_SHA384, A_SHA512 };

struct hash {
    int alg;
    uint32_t s[8];
    uint64_t q[8];
    uint64_t len;
    unsigned char buf[128];
    size_t n;
};

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static const uint32_t k256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

static const uint64_t k512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL };

static void md5_block(struct hash *h, const unsigned char *p)
{
    static const int sh[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
    static uint32_t k[64];
    uint32_t m[16], a = h->s[0], b = h->s[1], c = h->s[2], d = h->s[3];
    int i;
    if (!k[0]) {
        static const uint32_t t[64] = { 0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
            0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
            0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9,
            0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0,
            0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
        memcpy(k, t, sizeof k);
    }
    for (i = 0; i < 16; i++) m[i] = p[i * 4] | p[i * 4 + 1] << 8 | p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
    for (i = 0; i < 64; i++) {
        uint32_t f, t;
        int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
        else { f = c ^ (b | ~d); g = (7 * i) & 15; }
        t = d; d = c; c = b;
        b = b + ROL(a + f + k[i] + m[g], sh[i]);
        a = t;
    }
    h->s[0] += a; h->s[1] += b; h->s[2] += c; h->s[3] += d;
}

static void sha1_block(struct hash *h, const unsigned char *p)
{
    uint32_t w[80], a = h->s[0], b = h->s[1], c = h->s[2], d = h->s[3], e = h->s[4];
    int i;
    for (i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (i = 0; i < 80; i++) {
        uint32_t f, k, t;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; k = 0xca62c1d6; }
        t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    h->s[0] += a; h->s[1] += b; h->s[2] += c; h->s[3] += d; h->s[4] += e;
}

static void sha256_block(struct hash *h, const unsigned char *p)
{
    uint32_t w[64], v[8];
    int i;
    for (i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, h->s, sizeof v);
    for (i = 0; i < 64; i++) {
        uint32_t t1 = v[7] + (ROR(v[4], 6) ^ ROR(v[4], 11) ^ ROR(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + k256[i] + w[i];
        uint32_t t2 = (ROR(v[0], 2) ^ ROR(v[0], 13) ^ ROR(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1; v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++) h->s[i] += v[i];
}

static void sha512_block(struct hash *h, const unsigned char *p)
{
    uint64_t w[80], v[8];
    int i, j;
    for (i = 0; i < 16; i++) { w[i] = 0; for (j = 0; j < 8; j++) w[i] = w[i] << 8 | p[i * 8 + j]; }
    for (i = 16; i < 80; i++) {
        uint64_t s0 = ROR64(w[i - 15], 1) ^ ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7), s1 = ROR64(w[i - 2], 19) ^ ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, h->q, sizeof v);
    for (i = 0; i < 80; i++) {
        uint64_t t1 = v[7] + (ROR64(v[4], 14) ^ ROR64(v[4], 18) ^ ROR64(v[4], 41)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + k512[i] + w[i];
        uint64_t t2 = (ROR64(v[0], 28) ^ ROR64(v[0], 34) ^ ROR64(v[0], 39)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1; v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++) h->q[i] += v[i];
}

static size_t hash_bsize(int alg) { return alg >= A_SHA384 ? 128 : 64; }
static size_t hash_size(int alg) { static const size_t n[] = { 16, 20, 28, 32, 48, 64 }; return n[alg]; }

static void hash_init(struct hash *h, int alg)
{
    static const uint32_t i224[8] = { 0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4 };
    static const uint32_t i256[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    static const uint64_t i384[8] = { 0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL };
    static const uint64_t i512[8] = { 0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL };
    memset(h, 0, sizeof *h);
    h->alg = alg;
    if (alg == A_MD5) { h->s[0] = 0x67452301; h->s[1] = 0xefcdab89; h->s[2] = 0x98badcfe; h->s[3] = 0x10325476; }
    else if (alg == A_SHA1) { h->s[0] = 0x67452301; h->s[1] = 0xefcdab89; h->s[2] = 0x98badcfe; h->s[3] = 0x10325476; h->s[4] = 0xc3d2e1f0; }
    else if (alg == A_SHA224) memcpy(h->s, i224, sizeof i224);
    else if (alg == A_SHA256) memcpy(h->s, i256, sizeof i256);
    else if (alg == A_SHA384) memcpy(h->q, i384, sizeof i384);
    else memcpy(h->q, i512, sizeof i512);
}

static void hash_block(struct hash *h, const unsigned char *p)
{
    switch (h->alg) {
    case A_MD5: md5_block(h, p); break;
    case A_SHA1: sha1_block(h, p); break;
    case A_SHA224: case A_SHA256: sha256_block(h, p); break;
    default: sha512_block(h, p); break;
    }
}

static void hash_add(struct hash *h, const void *data, size_t n)
{
    const unsigned char *p = data;
    size_t bs = hash_bsize(h->alg);
    h->len += n;
    if (h->n) {
        size_t take = bs - h->n < n ? bs - h->n : n;
        memcpy(h->buf + h->n, p, take);
        h->n += take; p += take; n -= take;
        if (h->n == bs) { hash_block(h, h->buf); h->n = 0; }
    }
    while (n >= bs) { hash_block(h, p); p += bs; n -= bs; }
    if (n) { memcpy(h->buf, p, n); h->n = n; }
}

static void hash_end(struct hash *h, unsigned char *out)
{
    size_t bs = hash_bsize(h->alg), lenbytes = bs == 128 ? 16 : 8;
    uint64_t bits = h->len * 8;
    unsigned char pad[144];
    size_t padn = (h->n < bs - lenbytes ? bs - lenbytes - h->n : 2 * bs - lenbytes - h->n), i;
    memset(pad, 0, sizeof pad);
    pad[0] = 0x80;
    for (i = 0; i < 8; i++) pad[padn + i + (lenbytes - 8)] = h->alg == A_MD5 ? bits >> (8 * i) : bits >> (56 - 8 * i);
    hash_add(h, pad, padn + lenbytes);
    {
        size_t n = hash_size(h->alg);
        for (i = 0; i < n; i++) {
            if (h->alg == A_MD5) out[i] = h->s[i / 4] >> (8 * (i % 4));
            else if (h->alg >= A_SHA384) out[i] = h->q[i / 8] >> (56 - 8 * (i % 8));
            else out[i] = h->s[i / 4] >> (24 - 8 * (i % 4));
        }
    }
}

static void hex_of(const unsigned char *d, size_t n, char *out)
{
    size_t i;
    for (i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", d[i]);
}

static int hash_fd(int fd, int alg, char *hex)
{
    struct hash h;
    unsigned char buf[65536], d[64];
    ssize_t n;
    hash_init(&h, alg);
    while ((n = read(fd, buf, sizeof buf)) > 0) hash_add(&h, buf, n);
    if (n < 0) return -1;
    hash_end(&h, d);
    hex_of(d, hash_size(alg), hex);
    return 0;
}

static const char *const alg_names[] = { "MD5", "SHA1", "SHA224", "SHA256", "SHA384", "SHA512" };

static int t_digest(int argc, char **argv)
{
    static const struct opt lo[] = { { "binary", 0, 'b' }, { "text", 0, 't' }, { "check", 0, 'c' }, { "tag", 0, 'T' }, { "quiet", 0, 'q' },
        { "status", 0, 's' }, { "warn", 0, 'w' }, { "strict", 0, 'S' }, { "ignore-missing", 0, 'I' }, { "zero", 0, 'z' }, { "untagged", 0, 'U' }, { NULL, 0, 0 } };
    int alg = !strcmp(prog, "md5sum") ? A_MD5 : !strcmp(prog, "sha1sum") ? A_SHA1 : !strcmp(prog, "sha224sum") ? A_SHA224 :
        !strcmp(prog, "sha256sum") ? A_SHA256 : !strcmp(prog, "sha384sum") ? A_SHA384 : A_SHA512;
    int c, bin = 0, check = 0, tag = 0, quiet = 0, stat_only = 0, strict = 0, ignmiss = 0, zero = 0, bad = 0, i;
    char hex[130];
    opts_reset();
    while ((c = getopts(argc, argv, "btczw", lo)))
        switch (c) {
        case 'b': bin = 1; break; case 't': bin = 0; break; case 'c': check = 1; break; case 'T': tag = 1; break;
        case 'q': quiet = 1; break; case 's': stat_only = 1; break; case 'S': strict = 1; break; case 'I': ignmiss = 1; break;
        case 'z': zero = 1; break; default: break;
        }
    if (!nops) ops[nops++] = "-";
    if (!check) {
        for (i = 0; i < nops; i++) {
            int fd = open_input(ops[i]);
            const char *name = ops[i];
            int esc = 0;
            if (fd < 0 || hash_fd(fd, alg, hex) < 0) { perr("%s", ops[i]); bad = 1; if (fd > 0) close(fd); continue; }
            if (fd) close(fd);
            if (!zero) {
                const char *s;
                for (s = name; *s; s++) if (*s == '\\' || *s == '\n') esc = 1;
                if (esc) putchar('\\');
            }
            if (tag) {
                printf("%s (", alg_names[alg]);
                if (esc) { const char *s; for (s = name; *s; s++) fputs(*s == '\\' ? "\\\\" : *s == '\n' ? "\\n" : (char[]){ *s, 0 }, stdout); }
                else fputs(name, stdout);
                printf(") = %s", hex);
            } else {
                printf("%s %c", hex, bin ? '*' : ' ');
                if (esc) { const char *s; for (s = name; *s; s++) fputs(*s == '\\' ? "\\\\" : *s == '\n' ? "\\n" : (char[]){ *s, 0 }, stdout); }
                else fputs(name, stdout);
            }
            putchar(zero ? 0 : '\n');
        }
        return finish_stdout() ? 1 : bad;
    }
    {
        long fail = 0, unread = 0, improper = 0;
        for (i = 0; i < nops; i++) {
            FILE *f = !strcmp(ops[i], "-") ? stdin : fopen(ops[i], "r");
            char *line = NULL;
            size_t cap = 0;
            ssize_t n;
            long found = 0;
            if (!f) { perr("%s", ops[i]); bad = 1; continue; }
            while ((n = getline(&line, &cap, f)) >= 0) {
                char *p = line, *name, *want;
                char got[130];
                int fd, esc = 0, t = alg;
                if (n && line[n - 1] == '\n') line[--n] = 0;
                if (*p == '\\') { esc = 1; p++; }
                if (!*p || *p == '#') continue;
                {
                    int a;
                    for (a = 0; a < 6; a++) {
                        size_t l = strlen(alg_names[a]);
                        if (!strncmp(p, alg_names[a], l) && p[l] == ' ' && p[l + 1] == '(') { t = a; break; }
                    }
                    if (a < 6) {
                        char *close_p = strstr(p, ") = ");
                        if (!close_p) { improper++; continue; }
                        name = p + strlen(alg_names[a]) + 2; *close_p = 0; want = close_p + 4;
                        goto have;
                    }
                }
                {
                    size_t hl = strspn(p, "0123456789abcdefABCDEF");
                    if (hl != hash_size(alg) * 2 || p[hl] != ' ' || (p[hl + 1] != ' ' && p[hl + 1] != '*')) { improper++; continue; }
                    p[hl] = 0; want = p; name = p + hl + 2;
                }
have:
                found++;
                if (esc) {
                    char *r = name, *w = name;
                    while (*r) { if (*r == '\\' && r[1] == 'n') { *w++ = '\n'; r += 2; } else if (*r == '\\' && r[1] == '\\') { *w++ = '\\'; r += 2; } else *w++ = *r++; }
                    *w = 0;
                }
                fd = open_input(name);
                if (fd < 0) {
                    if (ignmiss && errno == ENOENT) continue;
                    fflush(stdout);
                    fprintf(stderr, "%s: %s: %s\n", prog, name, strerror(errno));
                    if (!stat_only) printf("%s: FAILED open or read\n", name);
                    unread++; continue;
                }
                if (hash_fd(fd, t, got) < 0) { perr("%s", name); unread++; if (fd) close(fd); continue; }
                if (fd) close(fd);
                {
                    size_t k;
                    int same = strlen(want) == strlen(got);
                    for (k = 0; same && got[k]; k++) if (tolower((unsigned char)want[k]) != got[k]) same = 0;
                    if (same) { if (!quiet && !stat_only) printf("%s: OK\n", name); }
                    else { fail++; if (!stat_only) printf("%s: FAILED\n", name); }
                }
            }
            free(line);
            if (f != stdin) fclose(f);
            if (!found) { if (!stat_only) say("%s: no properly formatted checksum lines found", ops[i]); bad = 1; }
        }
        fflush(stdout);
        if (!stat_only) {
            if (improper) say("WARNING: %ld line%s improperly formatted", improper, improper == 1 ? " is" : "s are");
            if (unread) say("WARNING: %ld listed file%s could not be read", unread, unread == 1 ? "" : "s");
            if (fail) say("WARNING: %ld computed checksum%s did NOT match", fail, fail == 1 ? "" : "s");
        }
        return finish_stdout() ? 1 : (bad || fail || unread || (strict && improper)) ? 1 : 0;
    }
}

static uint32_t crc_table[256];

static void crc_init(void)
{
    int i, j;
    for (i = 0; i < 256; i++) {
        uint32_t c = (uint32_t)i << 24;
        for (j = 0; j < 8; j++) c = c & 0x80000000 ? c << 1 ^ 0x04c11db7 : c << 1;
        crc_table[i] = c;
    }
}

static int t_cksum(int argc, char **argv)
{
    static const struct opt lo[] = { { "algorithm", 1, 'a' }, { "tag", 0, 'T' }, { "untagged", 0, 'U' }, { "binary", 0, 'b' }, { "text", 0, 't' }, { NULL, 0, 0 } };
    int c, bad = 0, i, mode = 0;
    const char *al = "crc";
    int issum = !strcmp(prog, "sum"), sysv = 0, implicit;
    opts_reset();
    while ((c = getopts(argc, argv, issum ? "rs" : "a:bt", lo)))
        switch (c) {
        case 'a': al = optarg; break; case 's': sysv = 1; break; default: break;
        }
    if (issum) al = sysv ? "sysv" : "bsd";
    if (!strcmp(al, "crc")) mode = 0; else if (!strcmp(al, "sysv")) mode = 1; else if (!strcmp(al, "bsd")) mode = 2;
    else {
        int a;
        for (a = 0; a < 6; a++) {
            char low[16];
            size_t k;
            for (k = 0; alg_names[a][k]; k++) low[k] = tolower((unsigned char)alg_names[a][k]);
            low[k] = 0;
            if (!strcmp(al, low)) {
                char **nv = xmalloc((argc + 2) * sizeof *nv);
                int n = 0, j;
                static const char *const progs[] = { "md5sum", "sha1sum", "sha224sum", "sha256sum", "sha384sum", "sha512sum" };
                nv[n++] = (char *)progs[a]; nv[n++] = "--tag";
                for (j = 0; j < nops; j++) nv[n++] = ops[j];
                nv[n] = NULL;
                prog = progs[a];
                opts_reset();
                return t_digest(n, nv);
            }
        }
        die(1, "invalid argument '%s' for '--algorithm'", al);
    }
    crc_init();
    implicit = !nops;
    if (!nops) ops[nops++] = "-";
    for (i = 0; i < nops; i++) {
        int fd = open_input(ops[i]);
        unsigned char buf[65536];
        ssize_t n, k;
        uint32_t crc = 0;
        unsigned long long len = 0, sum = 0;
        unsigned bsd = 0;
        if (fd < 0) { perr("%s", ops[i]); bad = 1; continue; }
        while ((n = read(fd, buf, sizeof buf)) > 0) {
            len += n;
            for (k = 0; k < n; k++) {
                if (mode == 0) crc = crc << 8 ^ crc_table[(crc >> 24 ^ buf[k]) & 255];
                else if (mode == 1) sum += buf[k];
                else { bsd = (bsd >> 1) + ((bsd & 1) << 15); bsd = (bsd + buf[k]) & 0xffff; }
            }
        }
        if (n < 0) { perr("%s", ops[i]); bad = 1; if (fd) close(fd); continue; }
        if (fd) close(fd);
        if (mode == 0) {
            unsigned long long l = len;
            for (; l; l >>= 8) crc = crc << 8 ^ crc_table[(crc >> 24 ^ (unsigned)(l & 255)) & 255];
            crc = ~crc;
            printf("%u %llu", crc, len);
        } else if (mode == 1) {
            unsigned r = (sum & 0xffff) + ((sum & 0xffffffff) >> 16);
            r = (r & 0xffff) + (r >> 16);
            printf("%u %llu", r, (len + (issum ? 511 : 511)) / 512);
        } else printf("%05u %5llu", bsd, (len + 1023) / 1024);
        if (!implicit && (!issum || nops > 1)) printf(" %s", ops[i]);
        putchar('\n');
    }
    return finish_stdout() ? 1 : bad;
}

/* ---- base64 base32 ---- */

static int t_base(int argc, char **argv)
{
    static const struct opt lo[] = { { "decode", 0, 'd' }, { "wrap", 1, 'w' }, { "ignore-garbage", 0, 'i' }, { NULL, 0, 0 } };
    int b32 = !strcmp(prog, "base32"), c, dec = 0, ign = 0, bad = 0, col = 0;
    unsigned long wrap = 76;
    const char *alpha = b32 ? "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567" : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int bits = b32 ? 5 : 6, grp = b32 ? 5 : 3, outc = b32 ? 8 : 4;
    unsigned char *d;
    size_t len, i;
    int fd;
    opts_reset();
    while ((c = getopts(argc, argv, "diw:", lo)))
        switch (c) {
        case 'd': dec = 1; break; case 'i': ign = 1; break;
        case 'w': if (!parse_ulong(optarg, &wrap)) die(1, "invalid wrap size: '%s'", optarg); break;
        default: break;
        }
    if (nops > 1) extra_operand(ops[1]);
    fd = open_input(nops ? ops[0] : "-");
    if (fd < 0) { perr("%s", ops[0]); return 1; }
    if (read_all(fd, (char **)&d, &len) < 0) { perr("read error"); return 1; }
    if (!dec) {
        for (i = 0; i < len; i += grp) {
            unsigned long long v = 0;
            size_t have = len - i < (size_t)grp ? len - i : (size_t)grp;
            int k, nchars = (int)((have * 8 + bits - 1) / bits);
            for (k = 0; k < grp; k++) v = v << 8 | (k < (int)have ? d[i + k] : 0);
            for (k = 0; k < outc; k++) {
                char ch = k < nchars ? alpha[(v >> ((outc - 1 - k) * bits)) & ((1 << bits) - 1)] : '=';
                if (wrap && col == (int)wrap) { putchar('\n'); col = 0; }
                putchar(ch); col++;
            }
        }
        if (col && wrap) putchar('\n');
    } else {
        unsigned long long acc = 0;
        int nb = 0, pad = 0;
        for (i = 0; i < len; i++) {
            const char *p;
            if (d[i] == '\n') continue;
            if (d[i] == '=') { pad++; continue; }
            p = d[i] ? strchr(alpha, d[i]) : NULL;
            if (!p || pad) { if (ign) continue; bad = 1; break; }
            acc = acc << bits | (p - alpha);
            if (++nb == outc) {
                int k;
                for (k = grp - 1; k >= 0; k--) putchar((acc >> (8 * k)) & 255);
                acc = 0; nb = 0;
            }
        }
        if (!bad && nb) {
            int bytes = nb * bits / 8, k, full = outc - nb;
            if (nb * bits % 8 >= bits || (!b32 && nb == 1)) bad = 1;
            else {
                acc <<= full * bits;
                for (k = grp - 1; k >= grp - bytes; k--) putchar((acc >> (8 * k)) & 255);
            }
        }
        if (bad) { fflush(stdout); say("invalid input"); return 1; }
    }
    return finish_stdout() ? 1 : 0;
}

/* ---- dd ---- */

static unsigned long long dd_num(const char *s)
{
    unsigned long long v = 0, m;
    char *e;
    const char *x;
    if (!isdigit((unsigned char)*s)) die(1, "invalid number: '%s'", s);
    v = strtoull(s, &e, 10);
    for (;;) {
        m = 1;
        switch (*e) {
        case 'c': m = 1; e++; break; case 'w': m = 2; e++; break; case 'b': m = 512; e++; break;
        case 'k': case 'K': m = 1024; e++; if (*e == 'B') { m = 1000; e++; } break;
        case 'M': m = 1 << 20; e++; if (*e == 'B') { m = 1000000; e++; } break;
        case 'G': m = 1ULL << 30; e++; if (*e == 'B') { m = 1000000000; e++; } break;
        case 'T': m = 1ULL << 40; e++; break;
        default: break;
        }
        v *= m;
        if (*e == 'x') { x = e + 1; if (!isdigit((unsigned char)*x)) die(1, "invalid number: '%s'", s); v *= strtoull(x, &e, 10); continue; }
        break;
    }
    if (*e) die(1, "invalid number: '%s'", s);
    return v;
}

static int t_dd(int argc, char **argv)
{
    const char *in = NULL, *out = NULL;
    unsigned long long ibs = 512, obs = 512, count = ~0ULL, skip = 0, seek = 0, rin = 0, pin = 0, rout = 0, pout = 0, bytes = 0;
    int notrunc = 0, sync_ = 0, noerror = 0, ucase = 0, lcase = 0, swab = 0, excl = 0, nocreat = 0, status = 1, i, fi = 0, fo = 1;
    unsigned char *buf, *ob;
    size_t on = 0;
    int rc = 0;
    if (argc == 2 && !strcmp(argv[1], "--help")) { print_usage(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--version")) { printf("dd (rifle) " VERSION "\n"); return 0; }
    for (i = 1; i < argc; i++) {
        char *a = argv[i], *v = strchr(a, '=');
        if (!v) { say("unrecognized operand '%s'", a); usage_error(); }
        *v++ = 0;
        if (!strcmp(a, "if")) in = v; else if (!strcmp(a, "of")) out = v;
        else if (!strcmp(a, "bs")) { ibs = obs = dd_num(v); }
        else if (!strcmp(a, "ibs")) ibs = dd_num(v); else if (!strcmp(a, "obs")) obs = dd_num(v);
        else if (!strcmp(a, "count")) count = dd_num(v); else if (!strcmp(a, "skip") || !strcmp(a, "iseek")) skip = dd_num(v);
        else if (!strcmp(a, "seek") || !strcmp(a, "oseek")) seek = dd_num(v);
        else if (!strcmp(a, "status")) status = !strcmp(v, "none") ? 0 : !strcmp(v, "noxfer") ? 2 : 1;
        else if (!strcmp(a, "conv")) {
            char *t = v, *e;
            while (t) {
                e = strchr(t, ','); if (e) *e++ = 0;
                if (!strcmp(t, "notrunc")) notrunc = 1; else if (!strcmp(t, "sync")) sync_ = 1; else if (!strcmp(t, "noerror")) noerror = 1;
                else if (!strcmp(t, "ucase")) ucase = 1; else if (!strcmp(t, "lcase")) lcase = 1; else if (!strcmp(t, "swab")) swab = 1;
                else if (!strcmp(t, "excl")) excl = 1; else if (!strcmp(t, "nocreat")) nocreat = 1; else if (strcmp(t, "fsync") && strcmp(t, "fdatasync")) die(1, "invalid conversion: '%s'", t);
                t = e;
            }
        } else if (strcmp(a, "iflag") && strcmp(a, "oflag")) { say("unrecognized operand '%s=%s'", a, v); usage_error(); }
    }
    if (!ibs || !obs) die(1, "invalid number: '0'");
    if (in) { fi = open(in, O_RDONLY); if (fi < 0) { perr("failed to open '%s'", in); return 1; } }
    if (out) {
        int fl = O_WRONLY | (nocreat ? 0 : O_CREAT) | (excl ? O_EXCL : 0) | (notrunc || seek ? 0 : O_TRUNC);
        fo = open(out, fl, 0666);
        if (fo < 0) { perr("failed to open '%s'", out); return 1; }
    }
    buf = xmalloc(ibs); ob = xmalloc(obs + ibs);
    if (skip) {
        if (lseek(fi, skip * ibs, SEEK_CUR) < 0) {
            unsigned long long k;
            for (k = 0; k < skip; k++) { ssize_t r = read(fi, buf, ibs); if (r <= 0) break; }
        }
    }
    if (seek) {
        if (lseek(fo, seek * obs, SEEK_SET) < 0) perr("cannot seek '%s'", out ? out : "standard output");
    }
    while (count == ~0ULL || rin + pin < count) {
        ssize_t n = read(fi, buf, ibs);
        size_t k;
        if (n < 0) {
            perr("error reading '%s'", in ? in : "standard input");
            rc = 1;
            if (!noerror) break;
            if (status) { fprintf(stderr, "%llu+%llu records in\n%llu+%llu records out\n", rin, pin, rout, pout); }
            n = 0;
            if (sync_) n = 0;
            if (!sync_) continue;
        }
        if (n == 0 && !(sync_ && rc)) break;
        if ((unsigned long long)n < ibs) {
            pin++;
            if (sync_) { memset(buf + n, ucase || lcase ? ' ' : 0, ibs - n); n = ibs; }
        } else rin++;
        if (swab) for (k = 0; k + 1 < (size_t)n; k += 2) { unsigned char t = buf[k]; buf[k] = buf[k + 1]; buf[k + 1] = t; }
        if (ucase) for (k = 0; k < (size_t)n; k++) buf[k] = toupper(buf[k]);
        if (lcase) for (k = 0; k < (size_t)n; k++) buf[k] = tolower(buf[k]);
        memcpy(ob + on, buf, n); on += n;
        while (on >= obs) {
            if (write_all(fo, (char *)ob, obs) < 0) { perr("error writing '%s'", out ? out : "standard output"); return 1; }
            bytes += obs; rout++;
            memmove(ob, ob + obs, on - obs); on -= obs;
        }
    }
    if (on) {
        if (write_all(fo, (char *)ob, on) < 0) { perr("error writing '%s'", out ? out : "standard output"); return 1; }
        bytes += on; pout++;
    }
    if (out && !notrunc && seek) ftruncate(fo, seek * obs + bytes);
    if (status) {
        fprintf(stderr, "%llu+%llu records in\n%llu+%llu records out\n", rin, pin, rout, pout);
        if (status == 1) fprintf(stderr, "%llu bytes copied\n", bytes);
    }
    if (out && close(fo) < 0) { perr("closing output file"); return 1; }
    return rc;
}

/* ---- od ---- */

struct odfmt { char kind; int size; int width; };

static const char *const od_names[] = { "nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel", "bs", "ht", "nl", "vt", "ff", "cr", "so", "si",
    "dle", "dc1", "dc2", "dc3", "dc4", "nak", "syn", "etb", "can", "em", "sub", "esc", "fs", "gs", "rs", "us", "sp" };

static int od_spec(const char *s, struct odfmt *f, int max)
{
    int n = 0;
    while (*s) {
        char k = *s++;
        int size = 0;
        if (n == max) die(1, "too many -t formats");
        if (k == 'a' || k == 'c') { f[n].kind = k; f[n].size = 1; f[n].width = 3; n++; continue; }
        if (!strchr("doux", k) && k != 'f') die(1, "invalid type string '%c'", k);
        if (isdigit((unsigned char)*s)) { size = strtol(s, (char **)&s, 10); }
        else if (*s == 'C') { size = 1; s++; } else if (*s == 'S') { size = 2; s++; } else if (*s == 'I') { size = 4; s++; } else if (*s == 'L') { size = 8; s++; }
        else if (*s == 'F' || *s == 'D') { size = *s == 'F' ? 4 : 8; s++; }
        else size = k == 'f' ? 8 : 4;
        if (k == 'f') { if (size != 4 && size != 8) die(1, "invalid type string 'f%d'", size); }
        else if (size != 1 && size != 2 && size != 4 && size != 8) die(1, "invalid type string '%c%d'", k, size);
        f[n].kind = k; f[n].size = size;
        if (k == 'f') f[n].width = size == 4 ? 15 : 24;
        else if (k == 'o') f[n].width = size == 1 ? 3 : size == 2 ? 6 : size == 4 ? 11 : 22;
        else if (k == 'x') f[n].width = size * 2;
        else if (k == 'u') f[n].width = size == 1 ? 3 : size == 2 ? 5 : size == 4 ? 10 : 20;
        else f[n].width = size == 1 ? 4 : size == 2 ? 6 : size == 4 ? 11 : 20;
        n++;
    }
    return n;
}

static void od_field(const struct odfmt *f, const unsigned char *p, size_t avail)
{
    unsigned long long v = 0;
    int i;
    unsigned char tmp[8] = { 0 };
    memcpy(tmp, p, avail < (size_t)f->size ? avail : (size_t)f->size);
    for (i = f->size - 1; i >= 0; i--) v = v << 8 | tmp[i];
    switch (f->kind) {
    case 'a': { int c = tmp[0] & 127; printf(" %*s", 3, c < 33 ? od_names[c] : c == 127 ? "del" : (char[]){ c, 0 }); break; }
    case 'c': {
        int c = tmp[0];
        char b[8];
        switch (c) {
        case 0: strcpy(b, "\\0"); break; case 7: strcpy(b, "\\a"); break; case 8: strcpy(b, "\\b"); break; case 12: strcpy(b, "\\f"); break;
        case 10: strcpy(b, "\\n"); break; case 13: strcpy(b, "\\r"); break; case 9: strcpy(b, "\\t"); break; case 11: strcpy(b, "\\v"); break;
        default: if (c >= 32 && c < 127) { b[0] = c; b[1] = 0; } else sprintf(b, "%03o", c);
        }
        printf(" %*s", 3, b);
        break;
    }
    case 'o': printf(" %0*llo", f->width, v); break;
    case 'x': printf(" %0*llx", f->width, v); break;
    case 'u': printf(" %*llu", f->width, v); break;
    case 'd': {
        long long sv = f->size == 1 ? (signed char)v : f->size == 2 ? (short)v : f->size == 4 ? (int)v : (long long)v;
        printf(" %*lld", f->width, sv);
        break;
    }
    case 'f': {
        if (f->size == 4) { float x; memcpy(&x, tmp, 4); printf(" %*.8g", f->width, (double)x); }
        else { double x; memcpy(&x, tmp, 8); printf(" %*.17g", f->width, x); }
        break;
    }
    }
}

static int t_od(int argc, char **argv)
{
    static const struct opt lo[] = { { "address-radix", 1, 'A' }, { "format", 1, 't' }, { "read-bytes", 1, 'N' }, { "skip-bytes", 1, 'j' },
        { "output-duplicates", 0, 'v' }, { "width", 2, 'w' }, { NULL, 0, 0 } };
    struct odfmt fm[16];
    int nf = 0, c, radix = 'o', dups = 0, i, fd, bad = 0;
    unsigned long width = 16, limit = ~0UL, skip = 0;
    unsigned char *data, *prev = NULL;
    size_t len = 0, pos;
    int starred = 0, aw;
    opts_reset();
    while ((c = getopts(argc, argv, "A:t:N:j:vw::abcdfhilosxCDFIL", lo)))
        switch (c) {
        case 'A': if (!strchr("doxn", optarg[0]) || optarg[1]) die(1, "invalid output address radix '%s'; it must be one character from [doxn]", optarg); radix = optarg[0]; break;
        case 't': nf += od_spec(optarg, fm + nf, 16 - nf); break;
        case 'N': if (!parse_ulong(optarg, &limit)) die(1, "invalid -N argument '%s'", optarg); break;
        case 'j': if (!parse_ulong(optarg, &skip)) die(1, "invalid -j argument '%s'", optarg); break;
        case 'v': dups = 1; break;
        case 'w': width = optarg ? strtoul(optarg, NULL, 10) : 32; if (!width) width = 16; break;
        case 'a': nf += od_spec("a", fm + nf, 16 - nf); break;
        case 'b': nf += od_spec("o1", fm + nf, 16 - nf); break;
        case 'c': nf += od_spec("c", fm + nf, 16 - nf); break;
        case 'd': nf += od_spec("u2", fm + nf, 16 - nf); break;
        case 'f': nf += od_spec("f4", fm + nf, 16 - nf); break;
        case 'h': case 'x': nf += od_spec("x2", fm + nf, 16 - nf); break;
        case 'i': nf += od_spec("d2", fm + nf, 16 - nf); break;
        case 'l': nf += od_spec("d8", fm + nf, 16 - nf); break;
        case 'o': nf += od_spec("o2", fm + nf, 16 - nf); break;
        case 's': nf += od_spec("d2", fm + nf, 16 - nf); break;
        default: break;
        }
    if (!nf) nf = od_spec("o2", fm, 16);
    {
        size_t cap = 0, got = 0;
        char *all = NULL;
        int k;
        if (!nops) ops[nops++] = "-";
        for (k = 0; k < nops; k++) {
            char *d;
            size_t l;
            fd = open_input(ops[k]);
            if (fd < 0) { perr("%s", ops[k]); bad = 1; continue; }
            if (read_all(fd, &d, &l) < 0) { perr("%s", ops[k]); bad = 1; continue; }
            if (fd) close(fd);
            if (got + l > cap) { cap = got + l; all = xrealloc(all, cap + 1); }
            memcpy(all + got, d, l); got += l; free(d);
        }
        data = (unsigned char *)all; len = got;
    }
    if (skip) { if (skip > len) skip = len; data += skip; len -= skip; }
    if (limit < len) len = limit;
    aw = radix == 'd' ? 7 : radix == 'x' ? 6 : 7;
    {
        int maxs = 1;
        for (i = 0; i < nf; i++) if (fm[i].size > maxs) maxs = fm[i].size;
        if (width % maxs) width = (width / maxs + 1) * maxs;
    }
    for (pos = 0; pos < len; pos += width) {
        size_t n = len - pos < width ? len - pos : width;
        int first = 1;
        if (!dups && prev && n == width && !memcmp(prev, data + pos, width)) {
            if (!starred) { puts("*"); starred = 1; }
            continue;
        }
        starred = 0;
        prev = data + pos;
        for (i = 0; i < nf; i++) {
            size_t k;
            if (first && radix != 'n') {
                if (radix == 'd') printf("%0*lu", aw, (unsigned long)(pos + skip));
                else if (radix == 'x') printf("%0*lx", aw, (unsigned long)(pos + skip));
                else printf("%0*lo", aw, (unsigned long)(pos + skip));
            } else if (radix != 'n') printf("%*s", aw, "");
            first = 0;
            for (k = 0; k < n; k += fm[i].size) od_field(&fm[i], data + pos + k, n - k);
            putchar('\n');
        }
    }
    if (radix != 'n') {
        unsigned long end = len + skip;
        if (radix == 'd') printf("%0*lu\n", aw, end); else if (radix == 'x') printf("%0*lx\n", aw, end); else printf("%0*lo\n", aw, end);
    }
    return finish_stdout() ? 1 : bad;
}
/* ---- ls ---- */

struct ent { char *name, *path, *link; struct stat st; int ok, linkok; struct stat lst; };

static struct {
    int all, almost, list, one, cols, across, comma, classify, slash, dir, inode, blocks, human, si, sortsz, sortt, sortu, sortc, rev, rec;
    int deref, derefcmd, numeric, noown, nogrp, unsorted, ver, ext, quote, escape, dirfirst, kib, full, nolist;
    int timestyle_iso, timestyle_long;
    unsigned width;
    const char *ignore[16]; int nignore;
    const char *hide[16]; int nhide;
} L;

static int ls_cmp(const void *a, const void *b)
{
    const struct ent *x = a, *y = b;
    int r = 0;
    if (L.dirfirst) {
        int dx = x->ok && S_ISDIR(x->st.st_mode), dy = y->ok && S_ISDIR(y->st.st_mode);
        if (dx != dy) return dx ? -1 : 1;
    }
    if (L.sortsz) r = y->st.st_size < x->st.st_size ? -1 : y->st.st_size > x->st.st_size;
    else if (L.sortt || L.sortu || L.sortc) {
        long sx, nx, sy, ny;
        if (L.sortu) { sx = x->st.st_atime; nx = x->st.st_atime_nsec; sy = y->st.st_atime; ny = y->st.st_atime_nsec; }
        else if (L.sortc) { sx = x->st.st_ctime; nx = x->st.st_ctime_nsec; sy = y->st.st_ctime; ny = y->st.st_ctime_nsec; }
        else { sx = x->st.st_mtime; nx = x->st.st_mtime_nsec; sy = y->st.st_mtime; ny = y->st.st_mtime_nsec; }
        r = sy < sx ? -1 : sy > sx ? 1 : ny < nx ? -1 : ny > nx;
    } else if (L.ver) r = vercmp(x->name, strlen(x->name), y->name, strlen(y->name));
    else if (L.ext) {
        const char *ex = strrchr(x->name, '.'), *ey = strrchr(y->name, '.');
        r = strcmp(ex ? ex : "", ey ? ey : "");
    }
    if (!r) r = strcmp(x->name, y->name);
    return L.rev ? -r : r;
}

static const char *ls_human(unsigned long long n, char *buf)
{
    static const char suf[] = " KMGTPE", sufsi[] = " kMGTPE";
    unsigned long long base = L.si ? 1000 : 1024, v = n;
    int i = 0;
    if (n < base) { sprintf(buf, "%llu", n); return buf; }
    while (v >= base && i < 6) { v /= base; i++; if (v < base) break; }
    {
        double d = (double)n;
        int k;
        for (k = 0; k < i; k++) d /= base;
        if (d < 10) { unsigned t = (unsigned)(d * 10 + 0.9999999); sprintf(buf, "%u.%u%c", t / 10, t % 10, (L.si ? sufsi : suf)[i]); if (t >= 100) sprintf(buf, "%u%c", t / 10, (L.si ? sufsi : suf)[i]); }
        else sprintf(buf, "%llu%c", (unsigned long long)(d + 0.9999999), (L.si ? sufsi : suf)[i]);
    }
    return buf;
}

static char ls_type(mode_t m)
{
    if (S_ISDIR(m)) return 'd'; if (S_ISLNK(m)) return 'l'; if (S_ISCHR(m)) return 'c'; if (S_ISBLK(m)) return 'b';
    if (S_ISFIFO(m)) return 'p'; if (S_ISSOCK(m)) return 's';
    return '-';
}

static char ls_mark(const struct ent *e)
{
    mode_t m = e->st.st_mode;
    if (S_ISDIR(m)) return L.classify || L.slash ? '/' : 0;
    if (!L.classify) return 0;
    if (S_ISLNK(m)) return '@'; if (S_ISFIFO(m)) return '|'; if (S_ISSOCK(m)) return '='; if (m & 0111) return '*';
    return 0;
}

static void ls_putname(const char *n)
{
    const char *p;
    if (L.quote) {
        putchar('"');
        for (p = n; *p; p++) { if (*p == '"' || *p == '\\') putchar('\\'); putchar(*p); }
        putchar('"');
    } else if (L.escape) {
        for (p = n; *p; p++) {
            unsigned char c = *p;
            if (c == ' ') fputs("\\ ", stdout); else if (c == '\\') fputs("\\\\", stdout); else if (c == '\n') fputs("\\n", stdout);
            else if (c == '\t') fputs("\\t", stdout); else if (c < 32 || c == 127) printf("\\%03o", c); else putchar(c);
        }
    } else fputs(n, stdout);
}

static size_t ls_namelen(const struct ent *e)
{
    size_t n = strlen(e->name);
    if (L.quote) n += 2;
    if (ls_mark(e)) n++;
    return n;
}

static void ls_time(const struct stat *st, char *out)
{
    time_t t = L.sortu ? st->st_atime : L.sortc ? st->st_ctime : st->st_mtime;
    long ns = L.sortu ? st->st_atime_nsec : L.sortc ? st->st_ctime_nsec : st->st_mtime_nsec;
    struct tm tm;
    time_t now = time(NULL);
    struct tm *g = gmtime(&t);
    tm = *g;
    if (L.full) strftime(out, 64, "%Y-%m-%d %H:%M:%S", &tm), sprintf(out + strlen(out), ".%09ld +0000", ns);
    else if (L.timestyle_long) strftime(out, 64, "%Y-%m-%d %H:%M", &tm);
    else if (L.timestyle_iso) strftime(out, 64, (t > now - 15778476 && t < now + 3600) ? "%m-%d %H:%M" : "%Y-%m-%d ", &tm);
    else if (t > now - 15778476 && t < now + 3600) strftime(out, 64, "%b %e %H:%M", &tm);
    else strftime(out, 64, "%b %e  %Y", &tm);
}

static void ls_mode(mode_t m, char *s)
{
    mode_string(m, s + 1);
    s[0] = ls_type(m);
}

static long long ls_blocks(const struct ent *e) { return e->st.st_blocks; }

static void ls_show(struct ent *v, int n, int dirtotal)
{
    int i;
    if (L.list) {
        size_t wl = 1, wu = 1, wg = 1, ws = 1, wi = 1, wb = 1;
        char **us = xmalloc((n + 1) * sizeof *us), **gs = xmalloc((n + 1) * sizeof *gs), **sz = xmalloc((n + 1) * sizeof *sz);
        long long total = 0;
        char buf[64];
        for (i = 0; i < n; i++) {
            struct ent *e = &v[i];
            size_t l;
            us[i] = gs[i] = sz[i] = NULL;
            if (!e->ok) continue;
            total += ls_blocks(e);
            l = snprintf(buf, sizeof buf, "%lu", (unsigned long)e->st.st_nlink); if (l > wl) wl = l;
            us[i] = xstrdup(L.numeric ? (snprintf(buf, sizeof buf, "%u", e->st.st_uid), buf) : user_name(e->st.st_uid));
            gs[i] = xstrdup(L.numeric ? (snprintf(buf, sizeof buf, "%u", e->st.st_gid), buf) : group_name(e->st.st_gid));
            if (strlen(us[i]) > wu) wu = strlen(us[i]);
            if (strlen(gs[i]) > wg) wg = strlen(gs[i]);
            if (S_ISCHR(e->st.st_mode) || S_ISBLK(e->st.st_mode)) {
                unsigned long d = e->st.st_rdev;
                snprintf(buf, sizeof buf, "%lu, %lu", (d >> 8) & 0xfff, (d & 0xff) | ((d >> 12) & 0xfff00));
            } else if (L.human) ls_human(e->st.st_size, buf);
            else snprintf(buf, sizeof buf, "%lld", (long long)e->st.st_size);
            sz[i] = xstrdup(buf);
            if (strlen(buf) > ws) ws = strlen(buf);
            l = snprintf(buf, sizeof buf, "%lu", (unsigned long)e->st.st_ino); if (l > wi) wi = l;
            if (L.human) ls_human(ls_blocks(e) * 512, buf); else snprintf(buf, sizeof buf, "%lld", (ls_blocks(e) * 512 + (L.kib || 1 ? 1023 : 0)) / 1024);
            l = strlen(buf); if (l > wb) wb = l;
        }
        if (dirtotal) {
            if (L.human) { ls_human(total * 512, buf); printf("total %s\n", buf); }
            else printf("total %lld\n", (total * 512 + 1023) / 1024);
        }
        for (i = 0; i < n; i++) {
            struct ent *e = &v[i];
            char mode[16], tm[64];
            char mk;
            if (!e->ok) { printf("? ?????????? ? ? ? ? ? %s\n", e->name); continue; }
            if (L.inode) printf("%*lu ", (int)wi, (unsigned long)e->st.st_ino);
            if (L.blocks) {
                if (L.human) ls_human(ls_blocks(e) * 512, buf); else snprintf(buf, sizeof buf, "%lld", (ls_blocks(e) * 512 + 1023) / 1024);
                printf("%*s ", (int)wb, buf);
            }
            ls_mode(e->st.st_mode, mode);
            ls_time(&e->st, tm);
            printf("%s %*lu", mode, (int)wl, (unsigned long)e->st.st_nlink);
            if (!L.noown) printf(" %-*s", (int)wu, us[i]);
            if (!L.nogrp) printf(" %-*s", (int)wg, gs[i]);
            printf(" %*s %s ", (int)ws, sz[i], tm);
            ls_putname(e->name);
            mk = ls_mark(e);
            if (mk && !S_ISLNK(e->st.st_mode)) putchar(mk);
            if (S_ISLNK(e->st.st_mode) && e->link) { fputs(" -> ", stdout); fputs(e->link, stdout); if (L.classify && e->linkok) { struct ent t = *e; t.st = e->lst; if (ls_mark(&t)) putchar(ls_mark(&t)); } }
            putchar('\n');
            free(us[i]); free(gs[i]); free(sz[i]);
        }
        free(us); free(gs); free(sz);
        return;
    }
    if (dirtotal && L.blocks) {
        long long total = 0;
        for (i = 0; i < n; i++) if (v[i].ok) total += ls_blocks(&v[i]);
        printf("total %lld\n", (total * 512 + 1023) / 1024);
    }
    if (L.comma) {
        size_t col = 0;
        for (i = 0; i < n; i++) {
            size_t l = ls_namelen(&v[i]) + 2;
            if (i && col + l + 0 > L.width) { putchar('\n'); col = 0; }
            if (i && col) putchar(' ');
            ls_putname(v[i].name);
            if (ls_mark(&v[i])) putchar(ls_mark(&v[i]));
            col += l;
            if (i + 1 < n) putchar(',');
        }
        if (n) putchar('\n');
        return;
    }
    if (L.cols || L.across) {
        size_t maxw = 0, ncols, nrows, r, c;
        for (i = 0; i < n; i++) {
            size_t l = ls_namelen(&v[i]) + (L.inode ? 8 : 0) + (L.blocks ? 5 : 0);
            if (l > maxw) maxw = l;
        }
        ncols = L.width / (maxw + 2);
        if (!ncols) ncols = 1;
        {
            size_t best = 1;
            size_t cand;
            for (cand = ncols < (size_t)n ? ncols : (size_t)n; cand >= 1; cand--) {
                size_t rows = (n + cand - 1) / cand, tot = 0, k, j;
                size_t *w = xmalloc(cand * sizeof *w);
                for (j = 0; j < cand; j++) w[j] = 0;
                for (k = 0; k < (size_t)n; k++) {
                    size_t cc = L.across ? k % cand : k / rows;
                    size_t l = ls_namelen(&v[k]) + (L.inode ? 8 : 0) + (L.blocks ? 5 : 0);
                    if (l > w[cc]) w[cc] = l;
                }
                for (j = 0; j < cand; j++) tot += w[j] + (j + 1 < cand ? 2 : 0);
                free(w);
                if (tot <= L.width || cand == 1) { best = cand; break; }
            }
            ncols = best;
        }
        nrows = (n + ncols - 1) / ncols;
        {
            size_t *w = xmalloc(ncols * sizeof *w);
            for (c = 0; c < ncols; c++) w[c] = 0;
            for (i = 0; i < n; i++) {
                size_t cc = L.across ? (size_t)i % ncols : (size_t)i / nrows, l = ls_namelen(&v[i]) + (L.inode ? 8 : 0) + (L.blocks ? 5 : 0);
                if (l > w[cc]) w[cc] = l;
            }
            for (r = 0; r < nrows; r++) {
                for (c = 0; c < ncols; c++) {
                    size_t idx = L.across ? r * ncols + c : c * nrows + r, used;
                    char mk;
                    if (idx >= (size_t)n) break;
                    if (L.inode) printf("%7lu ", (unsigned long)v[idx].st.st_ino);
                    if (L.blocks) printf("%4lld ", (ls_blocks(&v[idx]) * 512 + 1023) / 1024);
                    ls_putname(v[idx].name);
                    mk = ls_mark(&v[idx]);
                    if (mk) putchar(mk);
                    used = ls_namelen(&v[idx]) + (L.inode ? 8 : 0) + (L.blocks ? 5 : 0);
                    if (c + 1 < ncols && (L.across ? idx + 1 : idx + nrows) < (size_t)n) printf("%*s", (int)(w[c] - used + 2), "");
                }
                putchar('\n');
            }
            free(w);
        }
        return;
    }
    for (i = 0; i < n; i++) {
        char mk;
        if (L.inode) printf("%lu ", (unsigned long)v[i].st.st_ino);
        if (L.blocks) {
            char buf[32];
            if (L.human) ls_human(ls_blocks(&v[i]) * 512, buf); else snprintf(buf, sizeof buf, "%lld", (ls_blocks(&v[i]) * 512 + 1023) / 1024);
            printf("%s ", buf);
        }
        ls_putname(v[i].name);
        mk = ls_mark(&v[i]);
        if (mk) putchar(mk);
        putchar('\n');
    }
}

static int ls_ignored(const char *name)
{
    int i;
    for (i = 0; i < L.nignore; i++) if (glob_match(L.ignore[i], name)) return 1;
    return 0;
}

static void ls_stat(struct ent *e, int top)
{
    int follow = L.deref || (top && L.derefcmd);
    e->ok = (follow ? stat(e->path, &e->st) : lstat(e->path, &e->st)) == 0;
    if (!e->ok && follow) e->ok = lstat(e->path, &e->st) == 0;
    e->link = NULL; e->linkok = 0;
    if (e->ok && S_ISLNK(e->st.st_mode)) {
        char buf[PATH_MAX];
        ssize_t n = readlink(e->path, buf, sizeof buf - 1);
        if (n >= 0) { buf[n] = 0; e->link = xstrdup(buf); }
        if (L.list && L.classify) e->linkok = stat(e->path, &e->lst) == 0;
    }
}

static int ls_dir(const char *path, int header, int *first);

static int ls_dir(const char *path, int header, int *first)
{
    DIR *d = opendir(path);
    struct dirent *de;
    struct ent *v = NULL;
    int n = 0, cap = 0, i, bad = 0;
    if (!d) { perr("cannot open directory '%s'", path); return 1; }
    while ((de = readdir(d))) {
        struct ent *e;
        const char *nm = de->d_name;
        if (!L.all && !L.almost && nm[0] == '.') continue;
        if (L.almost && (!strcmp(nm, ".") || !strcmp(nm, ".."))) continue;
        if (!L.all && ls_ignored(nm)) continue;
        if (n == cap) { cap = cap ? cap * 2 : 64; v = xrealloc(v, cap * sizeof *v); }
        e = &v[n++];
        e->name = xstrdup(nm);
        e->path = xmalloc(strlen(path) + strlen(nm) + 2);
        sprintf(e->path, "%s%s%s", path, path[strlen(path) - 1] == '/' ? "" : "/", nm);
        ls_stat(e, 0);
        if (!e->ok) { perr("cannot access '%s'", e->path); bad = 1; }
    }
    closedir(d);
    if (!L.unsorted) qsort(v, n, sizeof *v, ls_cmp);
    if (header) { if (!*first) putchar('\n'); printf("%s:\n", path); }
    *first = 0;
    if (L.list || (L.blocks && 1)) ls_show(v, n, 1); else ls_show(v, n, 0);
    if (L.rec)
        for (i = 0; i < n; i++)
            if (v[i].ok && S_ISDIR(v[i].st.st_mode) && !(S_ISLNK(v[i].st.st_mode)) && strcmp(v[i].name, ".") && strcmp(v[i].name, ".."))
                bad |= ls_dir(v[i].path, 1, first);
    for (i = 0; i < n; i++) { free(v[i].name); free(v[i].path); free(v[i].link); }
    free(v);
    return bad;
}

static int t_ls(int argc, char **argv)
{
    static const struct opt lo[] = { { "all", 0, 'a' }, { "almost-all", 0, 'A' }, { "classify", 2, 'F' }, { "directory", 0, 'd' }, { "inode", 0, 'i' },
        { "size", 0, 's' }, { "human-readable", 0, 'h' }, { "si", 0, 'H' + 256 }, { "reverse", 0, 'r' }, { "recursive", 0, 'R' }, { "dereference", 0, 'L' },
        { "dereference-command-line", 0, 'H' }, { "numeric-uid-gid", 0, 'n' }, { "no-group", 0, 'G' }, { "color", 2, 'C' + 256 }, { "full-time", 0, 'T' + 256 },
        { "group-directories-first", 0, 'g' + 256 }, { "sort", 1, 'o' + 256 }, { "time", 1, 't' + 256 }, { "ignore", 1, 'I' }, { "hide", 1, 'h' + 256 },
        { "quote-name", 0, 'Q' }, { "escape", 0, 'b' }, { "hide-control-chars", 0, 'q' }, { "width", 1, 'w' }, { "format", 1, 'f' + 256 },
        { "indicator-style", 1, 'i' + 256 }, { "time-style", 1, 'y' + 256 }, { "kibibytes", 0, 'k' }, { "file-type", 0, 'F' + 512 }, { NULL, 0, 0 } };
    int c, i, first = 1, bad = 0, nfiles = 0, ndirs = 0, tty = isatty(1);
    struct ent *files = NULL, *dirs = NULL;
    memset(&L, 0, sizeof L);
    L.width = 80;
    {
        const char *w = getenv("COLUMNS");
        if (w && atoi(w) > 0) L.width = atoi(w);
    }
    L.one = !tty;
    L.cols = tty;
    opts_reset();
    while ((c = getopts(argc, argv, "aAlCx1FpdisShrRLHnogGtuUcvXkmqQbfw:I:T:", lo)))
        switch (c) {
        case 'a': L.all = 1; break; case 'A': L.almost = 1; break;
        case 'l': L.list = 1; L.cols = L.across = L.comma = 0; break;
        case 'o': L.list = 1; L.nogrp = 1; L.cols = 0; break;
        case 'g': L.list = 1; L.noown = 1; L.cols = 0; break;
        case 'n': L.list = 1; L.numeric = 1; L.cols = 0; break;
        case '1': L.one = 1; L.cols = L.across = L.comma = L.list = 0; break;
        case 'C': L.cols = 1; L.across = L.comma = L.list = 0; break;
        case 'x': L.across = 1; L.cols = 0; L.comma = 0; L.list = 0; break;
        case 'm': L.comma = 1; L.cols = L.across = L.list = 0; break;
        case 'F': L.classify = 1; break; case 'p': L.slash = 1; break; case 'd': L.dir = 1; break;
        case 'i': L.inode = 1; break; case 's': L.blocks = 1; break; case 'S': L.sortsz = 1; break;
        case 'h': L.human = 1; break; case 'H': L.derefcmd = 1; break; case 'H' + 256: L.human = L.si = 1; break;
        case 'r': L.rev = 1; break; case 'R': L.rec = 1; break; case 'L': L.deref = 1; break;
        case 'G': L.nogrp = 1; break; case 't': L.sortt = 1; break; case 'u': L.sortu = 1; break; case 'c': L.sortc = 1; break;
        case 'U': L.unsorted = 1; break; case 'v': L.ver = 1; break; case 'X': L.ext = 1; break;
        case 'k': L.kib = 1; break; case 'q': break; case 'Q': L.quote = 1; break; case 'b': L.escape = 1; break;
        case 'f': L.all = 1; L.unsorted = 1; L.list = 0; break;
        case 'w': L.width = strtoul(optarg, NULL, 10); break;
        case 'I': if (L.nignore < 16) L.ignore[L.nignore++] = optarg; break;
        case 'C' + 256: break;
        case 'T' + 256: L.full = 1; L.list = 1; L.cols = 0; break;
        case 'g' + 256: L.dirfirst = 1; break;
        case 'h' + 256: if (L.nignore < 16) L.ignore[L.nignore++] = optarg; break;
        case 'o' + 256:
            if (!strcmp(optarg, "size")) L.sortsz = 1; else if (!strcmp(optarg, "time")) L.sortt = 1; else if (!strcmp(optarg, "none")) L.unsorted = 1;
            else if (!strcmp(optarg, "version")) L.ver = 1; else if (!strcmp(optarg, "extension")) L.ext = 1; else if (strcmp(optarg, "name")) die(2, "invalid argument '%s' for '--sort'", optarg);
            break;
        case 't' + 256: if (!strcmp(optarg, "atime") || !strcmp(optarg, "access")) L.sortu = 1; else if (!strcmp(optarg, "ctime") || !strcmp(optarg, "status")) L.sortc = 1; break;
        case 'y' + 256: L.timestyle_iso = !strcmp(optarg, "iso"); L.timestyle_long = !strcmp(optarg, "long-iso"); if (!strcmp(optarg, "full-iso")) L.full = 1; break;
        case 'f' + 256:
            if (!strcmp(optarg, "long") || !strcmp(optarg, "verbose")) { L.list = 1; L.cols = 0; }
            else if (!strcmp(optarg, "commas")) { L.comma = 1; L.cols = 0; } else if (!strcmp(optarg, "across") || !strcmp(optarg, "horizontal")) { L.across = 1; L.cols = 0; }
            else if (!strcmp(optarg, "single-column")) { L.one = 1; L.cols = 0; } else if (!strcmp(optarg, "vertical")) L.cols = 1;
            break;
        case 'i' + 256: if (!strcmp(optarg, "slash")) L.slash = 1; else if (!strcmp(optarg, "classify")) L.classify = 1; break;
        case 'F' + 512: L.classify = 1; break;
        default: break;
        }
    if (L.list) L.one = 0;
    if (!nops) ops[nops++] = ".";
    for (i = 0; i < nops; i++) {
        struct ent e;
        e.name = ops[i]; e.path = ops[i];
        ls_stat(&e, 1);
        if (!e.ok) { perr("cannot access '%s'", ops[i]); bad = 2; continue; }
        if (S_ISDIR(e.st.st_mode) && !L.dir) { dirs = xrealloc(dirs, (ndirs + 1) * sizeof *dirs); dirs[ndirs++] = e; }
        else { files = xrealloc(files, (nfiles + 1) * sizeof *files); files[nfiles++] = e; }
    }
    if (!L.unsorted) { qsort(files, nfiles, sizeof *files, ls_cmp); qsort(dirs, ndirs, sizeof *dirs, ls_cmp); }
    if (nfiles) { ls_show(files, nfiles, 0); first = 0; }
    for (i = 0; i < ndirs; i++) {
        int r = ls_dir(dirs[i].path, nops > 1 || L.rec, &first);
        if (r && !bad) bad = 1;
        if (r && bad < 2) bad = r;
    }
    return finish_stdout() ? 2 : bad;
}

/* ---- stat ---- */

static const char *st_kind(mode_t m)
{
    if (S_ISREG(m)) return "regular file"; if (S_ISDIR(m)) return "directory"; if (S_ISLNK(m)) return "symbolic link";
    if (S_ISCHR(m)) return "character special file"; if (S_ISBLK(m)) return "block special file"; if (S_ISFIFO(m)) return "fifo";
    if (S_ISSOCK(m)) return "socket";
    return "weird file";
}

static void st_time(char *out, time_t t, long ns)
{
    struct tm *g = gmtime(&t);
    strftime(out, 48, "%Y-%m-%d %H:%M:%S", g);
    sprintf(out + strlen(out), ".%09ld +0000", ns);
}

static void st_perm(mode_t m, char *s) { mode_string(m, s + 1); s[0] = ls_type(m); }

static void st_quote(const char *s)
{
    const char *p;
    for (p = s; *p; p++) if (!isalnum((unsigned char)*p) && !strchr("_-./:,%+=@~^", *p)) break;
    if (!*p && *s) { fputs(s, stdout); return; }
    putchar('\'');
    for (p = s; *p; p++) { if (*p == '\'') fputs("'\\''", stdout); else putchar(*p); }
    putchar('\'');
}

static void st_format(const char *fmt, const char *name, const struct stat *st, int deref, int esc)
{
    const char *p;
    char b[64];
    for (p = fmt; *p; p++) {
        char flag = 0;
        int width = 0;
        if (esc && *p == '\\') {
            char eb[8];
            size_t en;
            int stop = 0;
            p = pf_esc(p + 1, 0, eb, &en, &stop) - 1;
            fwrite(eb, 1, en, stdout);
            continue;
        }
        if (*p != '%') { putchar(*p); continue; }
        p++;
        if (*p == '-' || *p == '0' || *p == '+' || *p == ' ' || *p == '#') flag = *p++;
        while (isdigit((unsigned char)*p)) width = width * 10 + *p++ - '0';
        switch (*p) {
        case '%': putchar('%'); break;
        case 'a': snprintf(b, sizeof b, "%o", st->st_mode & 07777); break;
        case 'A': st_perm(st->st_mode, b); break;
        case 'b': snprintf(b, sizeof b, "%lld", (long long)st->st_blocks); break;
        case 'B': strcpy(b, "512"); break;
        case 'd': snprintf(b, sizeof b, "%lu", (unsigned long)st->st_dev); break;
        case 'D': snprintf(b, sizeof b, "%lx", (unsigned long)st->st_dev); break;
        case 'f': snprintf(b, sizeof b, "%x", st->st_mode); break;
        case 'F': snprintf(b, sizeof b, "%s", st_kind(st->st_mode)); break;
        case 'g': snprintf(b, sizeof b, "%u", st->st_gid); break;
        case 'G': snprintf(b, sizeof b, "%s", group_name(st->st_gid)); break;
        case 'h': snprintf(b, sizeof b, "%lu", (unsigned long)st->st_nlink); break;
        case 'i': snprintf(b, sizeof b, "%lu", (unsigned long)st->st_ino); break;
        case 'm': strcpy(b, "/"); break;
        case 'n': snprintf(b, sizeof b, "%s", name); break;
        case 'N': {
            if (S_ISLNK(st->st_mode) && !deref) {
                char t[PATH_MAX];
                ssize_t n = readlink(name, t, sizeof t - 1);
                if (n >= 0) { t[n] = 0; st_quote(name); fputs(" -> ", stdout); st_quote(t); b[0] = 0; width = 0; break; }
            }
            st_quote(name); b[0] = 0; width = 0;
            break;
        }
        case 'o': strcpy(b, "4096"); break;
        case 's': snprintf(b, sizeof b, "%lld", (long long)st->st_size); break;
        case 't': snprintf(b, sizeof b, "%lx", (unsigned long)((st->st_rdev >> 8) & 0xfff)); break;
        case 'T': snprintf(b, sizeof b, "%lx", (unsigned long)((st->st_rdev & 0xff) | ((st->st_rdev >> 12) & 0xfff00))); break;
        case 'u': snprintf(b, sizeof b, "%u", st->st_uid); break;
        case 'U': snprintf(b, sizeof b, "%s", user_name(st->st_uid)); break;
        case 'w': strcpy(b, "-"); break;
        case 'W': strcpy(b, "0"); break;
        case 'x': st_time(b, st->st_atime, st->st_atime_nsec); break;
        case 'y': st_time(b, st->st_mtime, st->st_mtime_nsec); break;
        case 'z': st_time(b, st->st_ctime, st->st_ctime_nsec); break;
        case 'X': snprintf(b, sizeof b, "%ld", (long)st->st_atime); break;
        case 'Y': snprintf(b, sizeof b, "%ld", (long)st->st_mtime); break;
        case 'Z': snprintf(b, sizeof b, "%ld", (long)st->st_ctime); break;
        default: putchar('?'); b[0] = 0; width = 0; break;
        }
        if (b[0] || width) {
            if (flag == '-') printf("%-*s", width, b);
            else if (flag == '0' && strchr("abdDfghiosTtuWXYZ", *p)) printf("%0*s", width, b);
            else printf("%*s", width, b);
        }
        b[0] = 0;
    }
}

static int t_stat(int argc, char **argv)
{
    static const struct opt lo[] = { { "dereference", 0, 'L' }, { "format", 1, 'c' }, { "printf", 1, 'p' }, { "terse", 0, 't' }, { "file-system", 0, 'f' }, { NULL, 0, 0 } };
    int c, deref = 0, terse = 0, fsys = 0, i, bad = 0, nl = 1, esc = 0;
    const char *fmt = NULL;
    opts_reset();
    while ((c = getopts(argc, argv, "Lc:tf", lo)))
        switch (c) {
        case 'L': deref = 1; break; case 'c': fmt = optarg; break; case 'p': fmt = optarg; nl = 0; esc = 1; break;
        case 't': terse = 1; break; case 'f': fsys = 1; break; default: break;
        }
    if (!nops) missing_operand();
    for (i = 0; i < nops; i++) {
        struct stat st;
        if ((deref ? stat(ops[i], &st) : lstat(ops[i], &st)) < 0) { perr("cannot statx '%s'", ops[i]); bad = 1; continue; }
        if (fsys) {
            struct statfs f;
            if (statfs(ops[i], &f) < 0) { perr("cannot read file system information for '%s'", ops[i]); bad = 1; continue; }
            printf("  File: \"%s\"\n    ID: %lx Namelen: %ld     Type: ext2/ext3\nBlock size: %ld      Fundamental block size: %ld\nBlocks: Total: %lu   Free: %lu   Available: %lu\nInodes: Total: %lu   Free: %lu\n",
                   ops[i], 0UL, (long)f.f_namelen, (long)f.f_bsize, (long)f.f_bsize, (unsigned long)f.f_blocks, (unsigned long)f.f_bfree, (unsigned long)f.f_bavail,
                   (unsigned long)f.f_files, (unsigned long)f.f_ffree);
            continue;
        }
        if (fmt) { st_format(fmt, ops[i], &st, deref, esc); if (nl) putchar('\n'); continue; }
        if (terse) {
            printf("%s %lld %lld %x %u %u %lx %lu %lu %lx %lx %ld %ld %ld %d 4096\n", ops[i], (long long)st.st_size, (long long)st.st_blocks, st.st_mode, st.st_uid, st.st_gid,
                   (unsigned long)st.st_dev, (unsigned long)st.st_ino, (unsigned long)st.st_nlink, (unsigned long)((st.st_rdev >> 8) & 0xfff),
                   (unsigned long)((st.st_rdev & 0xff) | ((st.st_rdev >> 12) & 0xfff00)), (long)st.st_atime, (long)st.st_mtime, (long)st.st_ctime, 0);
            continue;
        }
        {
            char perm[16], a[48], m[48], ch[48];
            st_perm(st.st_mode, perm);
            st_time(a, st.st_atime, st.st_atime_nsec); st_time(m, st.st_mtime, st.st_mtime_nsec); st_time(ch, st.st_ctime, st.st_ctime_nsec);
            printf("  File: ");
            st_format("%N", ops[i], &st, deref, 0);
            printf("\n  Size: %-10lld\tBlocks: %-10lld IO Block: %-6d %s\n", (long long)st.st_size, (long long)st.st_blocks, 4096, st_kind(st.st_mode));
            printf("Device: %lu,%lu\tInode: %-10lu  Links: %lu", (unsigned long)(st.st_dev >> 8), (unsigned long)(st.st_dev & 255), (unsigned long)st.st_ino, (unsigned long)st.st_nlink);
            if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) printf("     Device type: %lx,%lx", (unsigned long)((st.st_rdev >> 8) & 0xfff), (unsigned long)(st.st_rdev & 0xff));
            printf("\nAccess: (%04o/%s)  Uid: (%5u/%8s)   Gid: (%5u/%8s)\n", st.st_mode & 07777, perm, st.st_uid, user_name(st.st_uid), st.st_gid, group_name(st.st_gid));
            printf("Access: %s\nModify: %s\nChange: %s\n Birth: -\n", a, m, ch);
        }
    }
    return finish_stdout() ? 1 : bad;
}

/* ---- du ---- */

struct du { int all, human, si, sum, total, apparent, xdev, deref, zero, null; long depth; unsigned long long blk; dev_t dev; };
struct seen { dev_t d; ino_t i; };
static struct seen *du_seen;
static size_t du_nseen, du_cap;

static int du_hard(const struct stat *st)
{
    size_t i;
    if (st->st_nlink < 2) return 0;
    for (i = 0; i < du_nseen; i++) if (du_seen[i].d == st->st_dev && du_seen[i].i == st->st_ino) return 1;
    if (du_nseen == du_cap) { du_cap = du_cap ? du_cap * 2 : 256; du_seen = xrealloc(du_seen, du_cap * sizeof *du_seen); }
    du_seen[du_nseen].d = st->st_dev; du_seen[du_nseen].i = st->st_ino; du_nseen++;
    return 0;
}

static void du_print(const struct du *o, unsigned long long bytes, const char *name)
{
    char buf[32];
    if (o->human) { L.si = o->si; ls_human(bytes, buf); }
    else sprintf(buf, "%llu", o->blk == 1 ? bytes : (bytes + o->blk - 1) / o->blk);
    printf("%s\t%s%c", buf, name, o->null ? 0 : '\n');
}

static int du_walk(const struct du *o, const char *path, int level, int top, unsigned long long *out, int *bad)
{
    struct stat st;
    unsigned long long mine, sub = 0;
    if ((o->deref ? stat(path, &st) : lstat(path, &st)) < 0) { perr("cannot access '%s'", path); *bad = 1; return 0; }
    if (o->xdev && !top && st.st_dev != o->dev) return 0;
    if (du_hard(&st)) return 0;
    mine = o->apparent ? (S_ISDIR(st.st_mode) ? 0 : (unsigned long long)st.st_size) : (unsigned long long)st.st_blocks * 512;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *e;
        if (!d) { perr("cannot read directory '%s'", path); *bad = 1; }
        else {
            char **names = NULL;
            int n = 0, cap = 0, i;
            while ((e = readdir(d))) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                if (n == cap) { cap = cap ? cap * 2 : 32; names = xrealloc(names, cap * sizeof *names); }
                names[n++] = xstrdup(e->d_name);
            }
            closedir(d);
            for (i = 0; i < n; i++) {
                char *child = xmalloc(strlen(path) + strlen(names[i]) + 2);
                unsigned long long got = 0;
                sprintf(child, "%s%s%s", path, path[strlen(path) - 1] == '/' ? "" : "/", names[i]);
                du_walk(o, child, level + 1, 0, &got, bad);
                sub += got;
                free(child); free(names[i]);
            }
            free(names);
        }
    }
    *out = mine + sub;
    if ((S_ISDIR(st.st_mode) ? 1 : o->all || top) && !o->sum && (o->depth < 0 || level <= o->depth)) du_print(o, *out, path);
    else if (o->sum && top) du_print(o, *out, path);
    return 0;
}

static int t_du(int argc, char **argv)
{
    static const struct opt lo[] = { { "all", 0, 'a' }, { "human-readable", 0, 'h' }, { "si", 0, 'H' }, { "summarize", 0, 's' }, { "total", 0, 'c' },
        { "apparent-size", 0, 'A' }, { "bytes", 0, 'b' }, { "max-depth", 1, 'd' }, { "one-file-system", 0, 'x' }, { "dereference", 0, 'L' },
        { "block-size", 1, 'B' }, { "null", 0, '0' }, { "separate-dirs", 0, 'S' }, { NULL, 0, 0 } };
    struct du o;
    int c, i, bad = 0;
    unsigned long long grand = 0;
    memset(&o, 0, sizeof o);
    o.depth = -1; o.blk = 1024;
    opts_reset();
    while ((c = getopts(argc, argv, "ahsckmbd:xLPB:0SH", lo)))
        switch (c) {
        case 'a': o.all = 1; break; case 'h': o.human = 1; break; case 'H': o.human = o.si = 1; break; case 's': o.sum = 1; break;
        case 'c': o.total = 1; break; case 'A': o.apparent = 1; break; case 'b': o.apparent = 1; o.blk = 1; break;
        case 'k': o.blk = 1024; break; case 'm': o.blk = 1 << 20; break; case 'x': o.xdev = 1; break; case 'L': o.deref = 1; break;
        case '0': o.null = 1; break;
        case 'd': o.depth = atol(optarg); break;
        case 'B': { unsigned long long v; if (parse_size(optarg, &v) < 0) die(1, "invalid -B argument '%s'", optarg); o.blk = v; break; }
        default: break;
        }
    if (!nops) ops[nops++] = ".";
    for (i = 0; i < nops; i++) {
        struct stat st;
        unsigned long long got = 0;
        if (o.xdev && stat(ops[i], &st) == 0) o.dev = st.st_dev;
        du_walk(&o, ops[i], 0, 1, &got, &bad);
        grand += got;
    }
    if (o.total) du_print(&o, grand, "total");
    return finish_stdout() ? 1 : bad;
}

/* ---- df ---- */

static int t_df(int argc, char **argv)
{
    static const struct opt lo[] = { { "human-readable", 0, 'h' }, { "si", 0, 'H' }, { "print-type", 0, 'T' }, { "all", 0, 'a' }, { "portability", 0, 'P' },
        { "block-size", 1, 'B' }, { "local", 0, 'l' }, { "inodes", 0, 'i' }, { "total", 0, 't' + 256 }, { NULL, 0, 0 } };
    int c, human = 0, kind = 0, all = 0, inodes = 0, bad = 0, any = 0, i;
    unsigned long long blk = 1024;
    FILE *f;
    char *line = NULL;
    size_t cap = 0;
    char **dev = NULL, **mnt = NULL, **typ = NULL;
    int n = 0, m = 0;
    opts_reset();
    while ((c = getopts(argc, argv, "hHTaPkB:lit", lo)))
        switch (c) {
        case 'h': human = 1; break; case 'H': human = 2; break; case 'T': kind = 1; break; case 'a': all = 1; break; case 'P': break;
        case 'k': blk = 1024; break; case 'l': break; case 'i': inodes = 1; break;
        case 'B': { unsigned long long v; if (parse_size(optarg, &v) < 0) die(1, "invalid --block-size argument '%s'", optarg); blk = v; break; }
        default: break;
        }
    f = fopen("/proc/self/mounts", "r");
    if (!f) { perr("cannot read table of mounted file systems"); return 1; }
    while (getline(&line, &cap, f) > 0) {
        char d[256], mp[256], t[64];
        {
            char *a = line, *b, *e;
            b = strchr(a, ' '); if (!b) continue; *b++ = 0;
            e = strchr(b, ' '); if (!e) continue; *e++ = 0;
            strncpy(d, a, 255); d[255] = 0; strncpy(mp, b, 255); mp[255] = 0;
            a = e; b = strchr(a, ' '); if (b) *b = 0;
            strncpy(t, a, 63); t[63] = 0;
        }
        if (m == n) { m = m ? m * 2 : 32; dev = xrealloc(dev, m * sizeof *dev); mnt = xrealloc(mnt, m * sizeof *mnt); typ = xrealloc(typ, m * sizeof *typ); }
        {
            char *o = mp, *w = mp;
            while (*o) { if (o[0] == '\\' && isdigit((unsigned char)o[1]) && isdigit((unsigned char)o[2]) && isdigit((unsigned char)o[3])) { *w++ = ((o[1] - '0') << 6) | ((o[2] - '0') << 3) | (o[3] - '0'); o += 4; } else *w++ = *o++; }
            *w = 0;
        }
        dev[n] = xstrdup(d); mnt[n] = xstrdup(mp); typ[n] = xstrdup(t); n++;
    }
    fclose(f);
    {
        int j, count = nops ? nops : n;
        for (j = 0; j < count; j++) {
            int idx = -1;
            struct statfs s;
            struct stat st;
            char sz[3][32], pct[8];
            if (nops) {
                struct stat ms;
                if (stat(ops[j], &st) < 0) { perr("%s", ops[j]); bad = 1; continue; }
                for (i = 0; i < n; i++) if (stat(mnt[i], &ms) == 0 && ms.st_dev == st.st_dev) idx = i;
                if (idx < 0) { say("%s: can't find mount point", ops[j]); bad = 1; continue; }
            } else idx = j;
            if (statfs(mnt[idx], &s) < 0) continue;
            if (!nops && !all && (s.f_blocks == 0 || !strcmp(typ[idx], "autofs"))) continue;
            if (!nops && !all) {
                int dup = 0, k;
                for (k = 0; k < idx; k++) if (!strcmp(dev[k], dev[idx]) && dev[idx][0] == '/') dup = 1;
                if (dup) continue;
            }
            if (!any) {
                if (inodes) printf("%-14s%s %9s %9s %9s %4s %s\n", "Filesystem", kind ? "Type          " : "", "Inodes", "IUsed", "IFree", "IUse%", "Mounted on");
                else if (human) printf("%-14s%s %5s %5s %5s %4s %s\n", "Filesystem", kind ? "Type          " : "", "Size", "Used", "Avail", "Use%", "Mounted on");
                else printf("%-14s%s %9s %9s %9s %4s %s\n", "Filesystem", kind ? "Type          " : "", blk == 1024 ? "1K-blocks" : "Blocks", "Used", "Available", "Use%", "Mounted on");
            }
            any = 1;
            if (inodes) {
                unsigned long long tot = s.f_files, fr = s.f_ffree, us = tot - fr;
                sprintf(pct, tot ? "%llu%%" : "-", tot ? (us * 100 + tot - 1) / tot : 0);
                printf("%-14s%s%s %9llu %9llu %9llu %4s %s\n", dev[idx], kind ? " " : "", kind ? typ[idx] : "", tot, us, fr, pct, mnt[idx]);
                continue;
            }
            {
                unsigned long long bs = s.f_bsize, tot = s.f_blocks * bs, fr = s.f_bfree * bs, av = s.f_bavail * bs, us = tot - fr;
                unsigned long long usedp = us + av ? (us * 100 + (us + av) - 1) / (us + av) : 0;
                if (human) {
                    L.si = human == 2;
                    ls_human(tot, sz[0]); ls_human(us, sz[1]); ls_human(av, sz[2]);
                } else {
                    sprintf(sz[0], "%llu", (tot + blk - 1) / blk); sprintf(sz[1], "%llu", (us + blk - 1) / blk); sprintf(sz[2], "%llu", av / blk);
                }
                if (tot) sprintf(pct, "%llu%%", usedp); else strcpy(pct, "-");
                if (kind) printf("%-14s %-13s", dev[idx], typ[idx]); else printf("%-14s", dev[idx]);
                if (human) printf(" %5s %5s %5s %4s %s\n", sz[0], sz[1], sz[2], pct, mnt[idx]);
                else printf(" %9s %9s %9s %4s %s\n", sz[0], sz[1], sz[2], pct, mnt[idx]);
            }
        }
    }
    return finish_stdout() ? 1 : bad;
}

/* ---- date ---- */

static int t_date(int argc, char **argv)
{
    static const struct opt lo[] = { { "date", 1, 'd' }, { "utc", 0, 'u' }, { "universal", 0, 'u' }, { "reference", 1, 'r' }, { "iso-8601", 2, 'I' },
        { "rfc-email", 0, 'R' }, { "rfc-2822", 0, 'R' }, { "set", 1, 's' }, { NULL, 0, 0 } };
    int c;
    const char *ds = NULL, *ref = NULL, *iso = NULL, *fmt = "%a %b %e %H:%M:%S %Z %Y";
    int rfc = 0, haveiso = 0;
    struct timespec ts;
    struct tm tm;
    char out[512], f2[512], *o;
    const char *p;
    opts_reset();
    while ((c = getopts(argc, argv, "d:ur:I::Rs:", lo)))
        switch (c) {
        case 'd': ds = optarg; break; case 'r': ref = optarg; break; case 'u': break;
        case 'I': haveiso = 1; iso = optarg; break; case 'R': rfc = 1; break;
        case 's': die(1, "cannot set date: Operation not permitted"); break;
        default: break;
        }
    if (nops > 1) extra_operand(ops[1]);
    if (nops && ops[0][0] == '+') fmt = ops[0] + 1;
    else if (nops) die(1, "invalid date '%s'", ops[0]);
    if (ref) {
        struct stat st;
        if (stat(ref, &st) < 0) { perr("%s", ref); return 1; }
        ts.tv_sec = st.st_mtime; ts.tv_nsec = st.st_mtime_nsec;
    } else if (ds) {
        if (parse_date(ds, &ts) < 0) die(1, "invalid date '%s'", ds);
    } else clock_gettime(0, &ts);
    if (rfc) fmt = "%a, %d %b %Y %H:%M:%S %z";
    if (haveiso) {
        if (!iso || !strcmp(iso, "date")) fmt = "%Y-%m-%d";
        else if (!strcmp(iso, "hours")) fmt = "%Y-%m-%dT%H%z";
        else if (!strcmp(iso, "minutes")) fmt = "%Y-%m-%dT%H:%M%z";
        else if (!strcmp(iso, "seconds")) fmt = "%Y-%m-%dT%H:%M:%S%z";
        else if (!strcmp(iso, "ns")) fmt = "%Y-%m-%dT%H:%M:%S,%N%z";
        else die(1, "invalid argument '%s' for '--iso-8601'", iso);
    }
    tm = *gmtime(&ts.tv_sec);
    o = f2;
    for (p = fmt; *p && o < f2 + sizeof f2 - 24; p++) {
        if (p[0] == '%' && p[1] == 'N') { o += sprintf(o, "%09ld", ts.tv_nsec); p++; }
        else if (p[0] == '%' && p[1] == '%') { *o++ = '%'; *o++ = '%'; p++; }
        else if (p[0] == '%' && p[1] == ':' && p[2] == 'z') { o += sprintf(o, "+00:00"); p += 2; }
        else if (p[0] == '%' && p[1] == 'z' && haveiso) { o += sprintf(o, "+00:00"); p++; }
        else if (p[0] == '%' && p[1] == 'U') { o += sprintf(o, "%02d", (tm.tm_yday + 7 - tm.tm_wday) / 7); p++; }
        else if (p[0] == '%' && p[1] == 'W') { o += sprintf(o, "%02d", (tm.tm_yday + 7 - (tm.tm_wday + 6) % 7) / 7); p++; }
        else *o++ = *p;
    }
    *o = 0;
    if (!strftime(out, sizeof out, f2, &tm)) out[0] = 0;
    puts(out);
    return finish_stdout() ? 1 : 0;
}
/* ---- install ---- */

static int make_dirs(const char *path, mode_t mode, int verbose, int leaf_only_mode)
{
    char *p = xstrdup(path), *s;
    int r = 0;
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/') p[--n] = 0;
    for (s = p + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        if (mkdir(p, 0777 & ~current_umask()) == 0 && verbose) printf("%s: creating directory '%s'\n", prog, p);
        *s = '/';
    }
    if (mkdir(p, mode) == 0) { if (verbose) printf("%s: creating directory '%s'\n", prog, p); if (leaf_only_mode) chmod(p, mode); }
    else if (errno != EEXIST) { perr("cannot create directory '%s'", p); r = 1; }
    free(p);
    return r;
}

static int t_install(int argc, char **argv)
{
    static const struct opt lo[] = { { "directory", 0, 'd' }, { "mode", 1, 'm' }, { "owner", 1, 'o' }, { "group", 1, 'g' }, { "verbose", 0, 'v' },
        { "preserve-timestamps", 0, 'p' }, { "target-directory", 1, 't' }, { "no-target-directory", 0, 'T' }, { "compare", 0, 'C' }, { "strip", 0, 's' }, { NULL, 0, 0 } };
    int c, dirmode = 0, mkparents = 0, verbose = 0, preserve = 0, notdir = 0, bad = 0, i, nsrc;
    mode_t mode = 0755;
    uid_t uid = -1; gid_t gid = -1;
    const char *tdir = NULL;
    char **srcs;
    opts_reset();
    while ((c = getopts(argc, argv, "dDm:o:g:cvpt:TCsb", lo)))
        switch (c) {
        case 'd': dirmode = 1; break; case 'D': mkparents = 1; break;
        case 'm': {
            struct change *ch = mode_compile(optarg);
            if (!ch) die(1, "invalid mode '%s'", optarg);
            mode = mode_apply(0, 0, 0, ch);
            break;
        }
        case 'o': if (lookup_user(optarg, &uid) < 0) die(1, "invalid user '%s'", optarg); break;
        case 'g': if (lookup_group(optarg, &gid) < 0) die(1, "invalid group '%s'", optarg); break;
        case 'v': verbose = 1; break; case 'p': preserve = 1; break; case 't': tdir = optarg; break; case 'T': notdir = 1; break;
        default: break;
        }
    if (dirmode) {
        if (!nops) { say("missing file operand"); usage_error(); }
        for (i = 0; i < nops; i++) {
            bad |= make_dirs(ops[i], mode, verbose, 1);
            if (uid != (uid_t)-1 || gid != (gid_t)-1) chown(ops[i], uid, gid);
        }
        return finish_stdout() ? 1 : bad;
    }
    if (!nops) { say("missing file operand"); usage_error(); }
    if (!tdir && nops == 1) { say("missing destination file operand after '%s'", ops[0]); usage_error(); }
    plan_dest(tdir, notdir, 1, &tdir, &srcs, &nsrc);
    for (i = 0; i < nsrc; i++) {
        struct cp o;
        char *dest = tdir ? dest_in(tdir, srcs[i]) : xstrdup(ops[nops - 1]), *dir;
        struct stat st;
        memset(&o, 0, sizeof o);
        o.follow_all = o.follow_top = 1; o.verbose = verbose; o.preserve = preserve; o.um = 0;
        if (stat(srcs[i], &st) < 0) { perr("cannot stat '%s'", srcs[i]); bad = 1; free(dest); continue; }
        if (S_ISDIR(st.st_mode)) { say("omitting directory '%s'", srcs[i]); bad = 1; free(dest); continue; }
        if (mkparents) {
            dir = xstrdup(dest);
            { char *sl = strrchr(dir, '/'); if (sl && sl != dir) { *sl = 0; bad |= make_dirs(dir, 0755, verbose, 0); } }
            free(dir);
        }
        unlink(dest);
        if (cp_one(&o, srcs[i], dest, 1)) { bad = 1; free(dest); continue; }
        if (chmod(dest, mode) < 0) { perr("cannot set permissions of '%s'", dest); bad = 1; }
        if ((uid != (uid_t)-1 || gid != (gid_t)-1) && chown(dest, uid, gid) < 0) { perr("cannot change ownership of '%s'", dest); bad = 1; }
        free(dest);
    }
    return finish_stdout() ? 1 : bad;
}

/* ---- timeout nice nohup chroot ---- */

static int sig_number(const char *s)
{
    static const struct { const char *n; int v; } t[] = { { "HUP", 1 }, { "INT", 2 }, { "QUIT", 3 }, { "ILL", 4 }, { "TRAP", 5 }, { "ABRT", 6 }, { "BUS", 7 },
        { "FPE", 8 }, { "KILL", 9 }, { "USR1", 10 }, { "SEGV", 11 }, { "USR2", 12 }, { "PIPE", 13 }, { "ALRM", 14 }, { "TERM", 15 }, { "CHLD", 17 },
        { "CONT", 18 }, { "STOP", 19 }, { "TSTP", 20 }, { "TTIN", 21 }, { "TTOU", 22 }, { "EXIT", 0 }, { NULL, 0 } };
    unsigned long v;
    int i;
    if (parse_ulong(s, &v)) return (int)v;
    if (!strncmp(s, "SIG", 3)) s += 3;
    for (i = 0; t[i].n; i++) if (!strcmp(t[i].n, s)) return t[i].v;
    return -1;
}

static int parse_secs(const char *s, double *out)
{
    char *e;
    double v;
    if (!(isdigit((unsigned char)*s) || *s == '.')) return -1;
    v = strtod(s, &e);
    if (e == s) return -1;
    if (*e == 's') e++; else if (*e == 'm') { v *= 60; e++; } else if (*e == 'h') { v *= 3600; e++; } else if (*e == 'd') { v *= 86400; e++; }
    if (*e) return -1;
    *out = v;
    return 0;
}

static double mono(void)
{
    struct timespec t;
    clock_gettime(1, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static int t_timeout(int argc, char **argv)
{
    static const struct opt lo[] = { { "signal", 1, 's' }, { "kill-after", 1, 'k' }, { "preserve-status", 0, 'p' }, { "foreground", 0, 'f' }, { "verbose", 0, 'v' }, { NULL, 0, 0 } };
    int c, sig = 15, keep = 0, verbose = 0, st = 0, killed_by_us = 0;
    double dur, kill_after = 0;
    pid_t pid;
    double start, limit;
    usage_status = 125;
    opts_reset();
    while ((c = getopts(argc, argv, "+s:k:pfv", lo)))
        switch (c) {
        case 's': sig = sig_number(optarg); if (sig < 0) die(125, "%s: invalid signal", optarg); break;
        case 'k': if (parse_secs(optarg, &kill_after) < 0) die(125, "invalid time interval '%s'", optarg); break;
        case 'p': keep = 1; break; case 'v': verbose = 1; break; default: break;
        }
    if (nops < 1) { say("missing operand"); usage_error(); }
    if (nops < 2) { say("missing operand after '%s'", ops[0]); usage_error(); }
    if (parse_secs(ops[0], &dur) < 0) die(125, "invalid time interval '%s'", ops[0]);
    fflush(stdout);
    pid = fork();
    if (pid < 0) die(125, "fork system call failed: %s", strerror(errno));
    if (!pid) {
        execvp(ops[1], ops + 1);
        fprintf(stderr, "%s: failed to run command '%s': %s\n", prog, ops[1], strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }
    start = mono();
    limit = dur ? start + dur : 0;
    for (;;) {
        pid_t r = waitpid(pid, &st, 1);
        if (r == pid) break;
        if (r < 0 && errno != EINTR) die(125, "waitpid: %s", strerror(errno));
        if (limit && mono() >= limit) {
            if (!killed_by_us) {
                if (verbose) fprintf(stderr, "%s: sending signal %d to command '%s'\n", prog, sig, ops[1]);
                kill(pid, sig);
                if (sig != 9 && sig != 19) kill(pid, 18);
                killed_by_us = 1;
                if (kill_after > 0) limit = mono() + kill_after; else limit = 0;
            } else {
                kill(pid, 9);
                killed_by_us = 2;
                limit = 0;
            }
        }
        {
            struct timespec ts = { 0, 2000000 };
            nanosleep(&ts, NULL);
        }
    }
    if ((st & 0x7f) == 0) {
        int code = (st >> 8) & 255;
        if (killed_by_us && !keep) return 124;
        return code;
    }
    {
        int s = st & 0x7f;
        if (killed_by_us == 2) return keep ? 128 + s : 137;
        if (killed_by_us && !keep && s != 9) return 124;
        return 128 + s;
    }
}

static int t_nice(int argc, char **argv)
{
    int adj = 10, i = 1, cur;
    usage_status = 125;
    if (argc > 1 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "--version"))) {
        if (argv[1][2] == 'h') print_usage(); else printf("nice (rifle) " VERSION "\n");
        return 0;
    }
    if (i < argc && (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--adjustment"))) {
        if (i + 1 >= argc) die(125, "option requires an argument -- 'n'");
        adj = atoi(argv[i + 1]); i += 2;
    } else if (i < argc && !strncmp(argv[i], "--adjustment=", 13)) { adj = atoi(argv[i] + 13); i++; }
    else if (i < argc && !strncmp(argv[i], "-n", 2) && argv[i][2]) { adj = atoi(argv[i] + 2); i++; }
    else if (i < argc && argv[i][0] == '-' && (isdigit((unsigned char)argv[i][1]) || (argv[i][1] == '-' && isdigit((unsigned char)argv[i][2])))) {
        adj = argv[i][1] == '-' ? -atoi(argv[i] + 2) : atoi(argv[i] + 1);
        if (argv[i][1] != '-') adj = atoi(argv[i] + 1);
        i++;
    } else if (i < argc && argv[i][0] == '-' && argv[i][1] == '-' && !argv[i][2]) i++;
    cur = getpriority(0, 0);
    if (i >= argc) {
        if (adj != 10 || argc > 1) { say("a command must be given with an adjustment"); usage_error(); }
        printf("%d\n", 20 - cur);
        return finish_stdout() ? 125 : 0;
    }
    {
        int want = 20 - cur + adj;
        if (want > 19) want = 19;
        if (want < -20) want = -20;
        if (setpriority(0, 0, want) < 0) fprintf(stderr, "%s: cannot set niceness: %s\n", prog, strerror(errno));
    }
    execvp(argv[i], argv + i);
    fprintf(stderr, "%s: '%s': %s\n", prog, argv[i], strerror(errno));
    return errno == ENOENT ? 127 : 126;
}

static int t_nohup(int argc, char **argv)
{
    int ign_in = isatty(0), ign_out = isatty(1), ign_err = isatty(2), saved = 0;
    usage_status = 125;
    if (argc == 2 && !strcmp(argv[1], "--help")) { print_usage(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--version")) { printf("nohup (rifle) " VERSION "\n"); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--")) { argv++; argc--; }
    if (argc < 2) { say("missing operand"); usage_error(); }
    if (ign_in) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd >= 0) { dup2(fd, 0); if (fd) close(fd); }
    }
    if (ign_out) {
        int fd = open("nohup.out", O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (fd < 0) {
            char *home = getenv("HOME");
            if (home) {
                char p[PATH_MAX];
                snprintf(p, sizeof p, "%s/nohup.out", home);
                fd = open(p, O_WRONLY | O_CREAT | O_APPEND, 0600);
            }
        }
        if (fd < 0) { fprintf(stderr, "%s: failed to open 'nohup.out': %s\n", prog, strerror(errno)); return 125; }
        dup2(fd, 1); if (fd != 1) close(fd);
        saved = 1;
    }
    if (ign_err) dup2(1, 2);
    if (ign_in || ign_out || ign_err) {
        fprintf(stderr, "%s: %s\n", prog, ign_in && saved ? "ignoring input and appending output to 'nohup.out'" :
            ign_in && ign_err && !ign_out ? "ignoring input and redirecting stderr to stdout" : ign_in ? "ignoring input" :
            saved ? "appending output to 'nohup.out'" : "redirecting stderr to stdout");
    }
    signal(SIGHUP, SIG_IGN);
    execvp(argv[1], argv + 1);
    fprintf(stderr, "%s: failed to run command '%s': %s\n", prog, argv[1], strerror(errno));
    return errno == ENOENT ? 127 : 126;
}

static int t_chroot(int argc, char **argv)
{
    static const struct opt lo[] = { { "skip-chdir", 0, 's' }, { "userspec", 1, 'u' }, { "groups", 1, 'g' }, { NULL, 0, 0 } };
    int c, skip = 0;
    usage_status = 125;
    opts_reset();
    while ((c = getopts(argc, argv, "+", lo))) if (c == 's') skip = 1;
    if (!nops) { say("missing operand"); usage_error(); }
    if (chroot(ops[0]) < 0) { fprintf(stderr, "%s: cannot change root directory to '%s': %s\n", prog, ops[0], strerror(errno)); return 125; }
    if (!skip && chdir("/") < 0) { fprintf(stderr, "%s: cannot chdir to root directory: %s\n", prog, strerror(errno)); return 125; }
    if (nops > 1) execvp(ops[1], ops + 1);
    else {
        char *sh = getenv("SHELL");
        char *av[] = { sh ? sh : "/bin/sh", "-i", NULL };
        execvp(av[0], av);
    }
    fprintf(stderr, "%s: failed to run command '%s': %s\n", prog, nops > 1 ? ops[1] : "/bin/sh", strerror(errno));
    return errno == ENOENT ? 127 : 126;
}

/* ---- truncate ---- */

static int t_truncate(int argc, char **argv)
{
    static const struct opt lo[] = { { "size", 1, 's' }, { "no-create", 0, 'c' }, { "reference", 1, 'r' }, { "io-blocks", 0, 'o' }, { NULL, 0, 0 } };
    int c, nocreate = 0, bad = 0, i;
    const char *sz = NULL, *ref = NULL;
    opts_reset();
    while ((c = getopts(argc, argv, "s:cr:o", lo)))
        switch (c) { case 's': sz = optarg; break; case 'c': nocreate = 1; break; case 'r': ref = optarg; break; case 'o': break; default: break; }
    if (!sz && !ref) { say("you must specify either '--size' or '--reference'"); usage_error(); }
    if (!nops) { say("missing file operand"); usage_error(); }
    for (i = 0; i < nops; i++) {
        struct stat st;
        long long cur = 0, want = 0;
        int fd = open(ops[i], O_WRONLY | (nocreate ? 0 : O_CREAT), 0666);
        char op = 0;
        const char *s = sz;
        if (fd < 0) { if (nocreate && errno == ENOENT) continue; perr("cannot open '%s' for writing", ops[i]); bad = 1; continue; }
        if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) cur = st.st_size;
        if (ref) {
            struct stat rs;
            if (stat(ref, &rs) < 0) { perr("cannot stat '%s'", ref); return 1; }
            want = rs.st_size;
        }
        if (s) {
            unsigned long long n;
            if (*s == '+' || *s == '-' || *s == '<' || *s == '>' || *s == '/' || *s == '%') op = *s++;
            if (parse_size(s, &n) < 0) die(1, "Invalid number: '%s'", sz);
            want = ref ? want : (long long)n;
            if (ref) {
                if (op == '+') want = cur + n; else if (op == '-') want = cur - n;
            } else switch (op) {
            case '+': want = cur + n; break; case '-': want = cur - n; break; case '<': want = cur < (long long)n ? cur : (long long)n; break;
            case '>': want = cur > (long long)n ? cur : (long long)n; break;
            case '/': want = n ? cur / n * n : cur; break; case '%': want = n ? (cur + n - 1) / n * n : cur; break;
            }
        }
        if (want < 0) want = 0;
        if (ftruncate(fd, want) < 0) { perr("failed to truncate '%s' at %lld bytes", ops[i], want); bad = 1; }
        close(fd);
    }
    return bad;
}

/* ---- split ---- */

static void split_name(char *out, const char *prefix, const char *add, unsigned long k, int numeric, unsigned long fixed)
{
    unsigned long base = numeric ? 10 : 26, w, i, cap;
    size_t n = strlen(prefix);
    int zs = 0;
    strcpy(out, prefix);
    if (fixed) {
        w = fixed;
        for (cap = 1, i = 0; i < w; i++) cap *= base;
        if (k >= cap && w < 20) die(1, "output file suffixes exhausted");
    } else {
        w = 2;
        for (;;) {
            for (cap = base - 1, i = 1; i < w; i++) cap *= base;
            if (k < cap) break;
            k -= cap; zs++; w++;
        }
    }
    for (i = 0; i < (unsigned long)zs; i++) out[n++] = numeric ? '9' : 'z';
    for (i = w; i-- > 0; ) { out[n + i] = (numeric ? '0' : 'a') + k % base; k /= base; }
    out[n + w] = 0;
    strcat(out, add);
}

static int t_split(int argc, char **argv)
{
    static const struct opt lo[] = { { "lines", 1, 'l' }, { "bytes", 1, 'b' }, { "number", 1, 'n' }, { "suffix-length", 1, 'a' }, { "numeric-suffixes", 2, 'd' },
        { "additional-suffix", 1, 'x' + 256 }, { "verbose", 0, 'v' }, { "line-bytes", 1, 'C' }, { "elide-empty-files", 0, 'e' }, { NULL, 0, 0 } };
    int c, numeric = 0, verbose = 0, fd, fdo = -1, elide = 0, autolen = 1;
    unsigned long suflen = 2, lines = 1000;
    unsigned long long bytes = 0, chunks = 0, lb = 0;
    const char *prefix = "x", *add = "";
    unsigned long idx = 0;
    char *buf, *name = NULL;
    size_t len, i;
    int mode = 0;
    opts_reset();
    while ((c = getopts(argc, argv, "l:b:n:a:d::C:ev", lo)))
        switch (c) {
        case 'l': if (!parse_ulong(optarg, &lines) || !lines) die(1, "invalid number of lines: '%s'", optarg); mode = 0; break;
        case 'b': if (parse_size(optarg, &bytes) < 0 || !bytes) die(1, "invalid number of bytes: '%s'", optarg); mode = 1; break;
        case 'C': if (parse_size(optarg, &lb) < 0 || !lb) die(1, "invalid number of bytes: '%s'", optarg); mode = 2; break;
        case 'n': { unsigned long v; if (!parse_ulong(optarg, &v) || !v) die(1, "invalid number of chunks: '%s'", optarg); chunks = v; mode = 3; break; }
        case 'a': if (!parse_ulong(optarg, &suflen) || !suflen) die(1, "invalid suffix length: '%s'", optarg); autolen = 0; break;
        case 'd': numeric = 1; break; case 'v': verbose = 1; break; case 'e': elide = 1; break;
        case 'x' + 256: add = optarg; break;
        default: break;
        }
    if (nops > 2) extra_operand(ops[2]);
    if (nops == 2) prefix = ops[1];
    fd = open_input(nops ? ops[0] : "-");
    if (fd < 0) { perr("cannot open '%s' for reading", ops[0]); return 1; }
    if (read_all(fd, &buf, &len) < 0) { perr("read error"); return 1; }
    name = xmalloc(strlen(prefix) + strlen(add) + 128 + suflen);
#define OPEN_NEXT() do { \
        if (fdo >= 0) close(fdo); \
        split_name(name, prefix, add, idx++, numeric, autolen ? 0 : suflen); \
        if (verbose) printf("creating file '%s'\n", name); \
        fdo = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0666); \
        if (fdo < 0) { perr("%s", name); exit(1); } } while (0)
    if (mode == 3) {
        unsigned long long k, base = len / chunks, extra = len % chunks, pos = 0;
        for (k = 0; k < chunks; k++) {
            unsigned long long n = base + (k < extra);
            if (elide && !n) continue;
            OPEN_NEXT();
            write_all(fdo, buf + pos, n);
            pos += n;
        }
    } else if (mode == 1) {
        for (i = 0; i < len; i += bytes) {
            size_t n = len - i < bytes ? len - i : bytes;
            OPEN_NEXT();
            write_all(fdo, buf + i, n);
        }
    } else if (mode == 2) {
        size_t start = 0;
        while (start < len) {
            size_t end = start, lastnl = 0;
            int found = 0;
            while (end < len && end - start < lb) { if (buf[end] == '\n') { lastnl = end + 1; found = 1; } end++; }
            if (end < len && found) end = lastnl;
            OPEN_NEXT();
            write_all(fdo, buf + start, end - start);
            start = end;
        }
    } else {
        unsigned long long n = 0;
        for (i = 0; i < len; i++) {
            if (fdo < 0 || n == 0) { OPEN_NEXT(); }
            {
                size_t e = i;
                while (e < len && buf[e] != '\n') e++;
                if (e < len) e++;
                write_all(fdo, buf + i, e - i);
                i = e - 1;
            }
            if (++n == lines) { n = 0; if (i + 1 < len) { close(fdo); fdo = -1; } }
        }
    }
    if (fdo >= 0) close(fdo);
    return finish_stdout() ? 1 : 0;
}

/* ---- expand unexpand ---- */

static int t_expand(int argc, char **argv)
{
    static const struct opt lo[] = { { "tabs", 1, 't' }, { "initial", 0, 'i' }, { "all", 0, 'a' }, { "first-only", 0, 'i' + 256 }, { NULL, 0, 0 } };
    int un = !strcmp(prog, "unexpand"), c, init = 0, all = 0, i, bad = 0;
    int tabs[128], ntabs = 0, step = 8;
    opts_reset();
    while ((c = getopts(argc, argv, un ? "at:" : "it:", lo)))
        switch (c) {
        case 'i': init = 1; break; case 'a': all = 1; break;
        case 't': {
            char *s = optarg, *e;
            ntabs = 0;
            while (*s) {
                long v = strtol(s, &e, 10);
                if (e == s || v < 0 || ((*e) && *e != ',' && *e != ' ')) die(1, "tab size contains invalid character(s): '%s'", s);
                if (ntabs && v <= tabs[ntabs - 1]) die(1, "tab sizes must be ascending");
                if (ntabs < 128) tabs[ntabs++] = v;
                s = *e ? e + 1 : e;
            }
            if (ntabs == 1) { step = tabs[0]; if (!step) die(1, "tab size cannot be 0"); ntabs = 0; }
            if (un) all = 1;
            break;
        }
        default: break;
        }
    if (!nops) ops[nops++] = "-";
    for (i = 0; i < nops; i++) {
        FILE *f = !strcmp(ops[i], "-") ? stdin : fopen(ops[i], "r");
        int ch, col = 0, blank = 1;
        if (!f) { perr("%s", ops[i]); bad = 1; continue; }
        {
#define NEXT_STOP(col) (ntabs ? ({ int t_ = 0, k_; for (k_ = 0; k_ < ntabs; k_++) if (tabs[k_] > (col)) { t_ = tabs[k_]; break; } t_ ? t_ : (col) + 1; }) : ((col) / step + 1) * step)
            if (!un) {
                while ((ch = getc(f)) != EOF) {
                    if (ch == '\t' && !(init && !blank)) { int nx = NEXT_STOP(col); while (col < nx) { putchar(' '); col++; } }
                    else {
                        putchar(ch);
                        if (ch == '\n') { col = 0; blank = 1; } else if (ch == '\b') { if (col) col--; } else { col++; if (ch != ' ' && ch != '\t') blank = 0; }
                        if (ch == '\t') col = NEXT_STOP(col - 1 > 0 ? col - 1 : 0);
                    }
                }
            } else {
                char *line = NULL;
                size_t cap = 0;
                ssize_t n;
                while ((n = getline(&line, &cap, f)) >= 0) {
                    ssize_t k = 0;
                    int c2 = 0, pend = 0, pstart = 0;
                    int leading = 1;
                    for (k = 0; k < n; k++) {
                        char x = line[k];
                        if ((x == ' ' || x == '\t') && (all || leading)) {
                            if (!pend) { pstart = c2; pend = 1; }
                            if (x == '\t') c2 = NEXT_STOP(c2); else c2++;
                            continue;
                        }
                        if (pend) {
                            int cc = pstart;
                            while (1) {
                                int nx = NEXT_STOP(cc);
                                if (nx <= c2 && nx - cc > 1) { putchar('\t'); cc = nx; }
                                else break;
                            }
                            while (cc < c2) { putchar(' '); cc++; }
                            pend = 0;
                        }
                        putchar(x);
                        if (x == '\b') { if (c2) c2--; } else c2++;
                        leading = 0;
                    }
                    if (pend) {
                        int cc = pstart;
                        while (1) { int nx = NEXT_STOP(cc); if (nx <= c2 && nx - cc > 1) { putchar('\t'); cc = nx; } else break; }
                        while (cc < c2) { putchar(' '); cc++; }
                    }
                }
                free(line);
            }
        }
        if (f != stdin) fclose(f);
    }
    return finish_stdout() ? 1 : bad;
}

/* ---- shuf ---- */

static unsigned long long rnd64(void)
{
    unsigned long long v;
    if (getrandom(&v, sizeof v, 0) != (ssize_t)sizeof v) v = (unsigned long long)time(NULL) * 6364136223846793005ULL;
    return v;
}

static unsigned long long rnd_below(unsigned long long n)
{
    unsigned long long lim = ~0ULL - (~0ULL % n), v;
    do v = rnd64(); while (v >= lim);
    return v % n;
}

static int t_shuf(int argc, char **argv)
{
    static const struct opt lo[] = { { "echo", 0, 'e' }, { "input-range", 1, 'i' }, { "head-count", 1, 'n' }, { "repeat", 0, 'r' }, { "zero-terminated", 0, 'z' },
        { "output", 1, 'o' }, { "random-source", 1, 'R' }, { NULL, 0, 0 } };
    int c, echo = 0, rep = 0, zero = 0;
    unsigned long long lo_ = 0, hi_ = 0, count = ~0ULL;
    int haverange = 0;
    char **items = NULL, *data = NULL;
    size_t n = 0, i;
    const char *outf = NULL;
    FILE *out = stdout;
    int sep;
    opts_reset();
    while ((c = getopts(argc, argv, "ei:n:rzo:", lo)))
        switch (c) {
        case 'e': echo = 1; break;
        case 'i': {
            char *dash = strchr(optarg, '-');
            if (!dash) die(1, "invalid input range: '%s'", optarg);
            lo_ = strtoull(optarg, NULL, 10); hi_ = strtoull(dash + 1, NULL, 10);
            if (hi_ + 1 < lo_) die(1, "invalid input range: '%s'", optarg);
            haverange = 1;
            break;
        }
        case 'n': { unsigned long v; if (!parse_ulong(optarg, &v)) die(1, "invalid line count: '%s'", optarg); if (v < count) count = v; break; }
        case 'r': rep = 1; break; case 'z': zero = 1; break; case 'o': outf = optarg; break;
        default: break;
        }
    sep = zero ? 0 : '\n';
    if (outf) { out = fopen(outf, "w"); if (!out) { perr("%s", outf); return 1; } }
    if (haverange) {
        unsigned long long k, total = hi_ - lo_ + 1;
        unsigned long long *v;
        if (rep) {
            if (!total) die(1, "no lines to repeat");
            for (k = 0; k < count; k++) fprintf(out, "%llu%c", lo_ + rnd_below(total), sep);
        } else {
            v = xmalloc((total ? total : 1) * sizeof *v);
            for (k = 0; k < total; k++) v[k] = lo_ + k;
            for (k = 0; k < total && k < count; k++) {
                unsigned long long j = k + rnd_below(total - k), t = v[k];
                v[k] = v[j]; v[j] = t;
                fprintf(out, "%llu%c", v[k], sep);
            }
        }
        return finish_stdout() ? 1 : 0;
    }
    if (echo) { items = ops; n = nops; }
    else {
        size_t len, cap = 0, start = 0;
        int fd;
        if (nops > 1) extra_operand(ops[1]);
        fd = open_input(nops ? ops[0] : "-");
        if (fd < 0) { perr("%s", ops[0]); return 1; }
        if (read_all(fd, &data, &len) < 0) { perr("read error"); return 1; }
        for (i = 0; i < len; i++) {
            if ((unsigned char)data[i] == (unsigned)sep || (i + 1 == len)) {
                if (n == cap) { cap = cap ? cap * 2 : 64; items = xrealloc(items, cap * sizeof *items); }
                items[n++] = data + start;
                if ((unsigned char)data[i] == (unsigned)sep) data[i] = 0; else { data = xrealloc(data, len + 1); data[len] = 0; items[n - 1] = data + start; }
                start = i + 1;
            }
        }
    }
    if (rep) {
        unsigned long long k;
        if (!n) die(1, "no lines to repeat");
        for (k = 0; k < count; k++) fprintf(out, "%s%c", items[rnd_below(n)], sep);
    } else {
        size_t k;
        for (k = 0; k < n && k < count; k++) {
            size_t j = k + rnd_below(n - k);
            char *t = items[k];
            items[k] = items[j]; items[j] = t;
            fprintf(out, "%s%c", items[k], sep);
        }
    }
    if (outf) fclose(out);
    return finish_stdout() ? 1 : 0;
}

/* ---- join ---- */

struct jl { char *line; char **f; int nf; };

static int jtab = -1, jicase;
static const char *jempty = "";

static void j_split(struct jl *l)
{
    char *p = l->line, **f = NULL;
    int n = 0, cap = 0;
    for (;;) {
        char *s;
        if (jtab < 0) while (*p == ' ' || *p == '\t') p++;
        s = p;
        if (jtab < 0) while (*p && *p != ' ' && *p != '\t') p++; else while (*p && *p != jtab) p++;
        if (n == cap) { cap = cap ? cap * 2 : 8; f = xrealloc(f, cap * sizeof *f); }
        f[n++] = xmalloc(p - s + 1);
        memcpy(f[n - 1], s, p - s); f[n - 1][p - s] = 0;
        if (!*p) break;
        if (jtab >= 0) p++;
        else { while (*p == ' ' || *p == '\t') p++; if (!*p) break; }
    }
    l->f = f; l->nf = n;
}

static int j_cmp(const char *a, const char *b) { return jicase ? strcasecmp(a, b) : strcmp(a, b); }

static int j_read(const char *name, struct jl **out)
{
    int fd = open_input(name);
    char *d;
    size_t len, i, start = 0;
    int n = 0, cap = 0;
    struct jl *v = NULL;
    if (fd < 0) { perr("%s", name); exit(1); }
    if (read_all(fd, &d, &len) < 0) { perr("%s", name); exit(1); }
    for (i = 0; i < len; i++) {
        if (d[i] == '\n' || i + 1 == len) {
            size_t e = d[i] == '\n' ? i : i + 1;
            if (n == cap) { cap = cap ? cap * 2 : 64; v = xrealloc(v, cap * sizeof *v); }
            v[n].line = xmalloc(e - start + 1);
            memcpy(v[n].line, d + start, e - start); v[n].line[e - start] = 0;
            j_split(&v[n]);
            n++;
            start = i + 1;
        }
    }
    *out = v;
    return n;
}

static int t_join(int argc, char **argv)
{
    static const struct opt lo[] = { { "ignore-case", 0, 'i' }, { NULL, 0, 0 } };
    int c, k1 = 1, k2 = 1, a1 = 0, a2 = 0, v1 = 0, v2 = 0, nf1, nf2, i1 = 0, i2 = 0, nout = 0;
    struct jl *l1, *l2;
    struct { int file, field; } outf[64];
    int useout = 0;
    opts_reset();
    while ((c = getopts(argc, argv, "1:2:j:t:a:v:e:o:i", lo)))
        switch (c) {
        case '1': k1 = atoi(optarg); break; case '2': k2 = atoi(optarg); break; case 'j': k1 = k2 = atoi(optarg); break;
        case 't': jtab = optarg[0]; break; case 'i': jicase = 1; break; case 'e': jempty = optarg; break;
        case 'a': if (atoi(optarg) == 1) a1 = 1; else if (atoi(optarg) == 2) a2 = 1; else die(1, "invalid file number: '%s'", optarg); break;
        case 'v': if (atoi(optarg) == 1) { v1 = 1; a1 = 1; } else if (atoi(optarg) == 2) { v2 = 1; a2 = 1; } else die(1, "invalid file number: '%s'", optarg); break;
        case 'o': {
            char *s = optarg;
            useout = 1;
            while (*s) {
                if (!strncmp(s, "0", 1) && (s[1] == ',' || s[1] == ' ' || !s[1])) { outf[nout].file = 0; outf[nout].field = 0; nout++; s++; }
                else {
                    outf[nout].file = atoi(s); s += strspn(s, "0123456789");
                    if (*s != '.') die(1, "invalid field specifier: '%s'", optarg);
                    outf[nout].field = atoi(s + 1); s += 1 + strspn(s + 1, "0123456789"); nout++;
                }
                while (*s == ',' || *s == ' ') s++;
            }
            break;
        }
        default: break;
        }
    if (nops != 2) { if (nops < 2) { say("missing operand after '%s'", nops ? ops[nops - 1] : argv[0]); } else extra_operand(ops[2]); usage_error(); }
    nf1 = j_read(ops[0], &l1);
    nf2 = j_read(ops[1], &l2);
#define EMIT_KEY(l, k) (((k) <= (l)->nf && (l)->f[(k) - 1][0]) ? (l)->f[(k) - 1] : jempty)
    {
        char sep = jtab >= 0 ? jtab : ' ';
        while (i1 < nf1 || i2 < nf2) {
            int r;
            const char *ka = i1 < nf1 ? EMIT_KEY(&l1[i1], k1) : NULL, *kb = i2 < nf2 ? EMIT_KEY(&l2[i2], k2) : NULL;
            if (!ka) r = 1; else if (!kb) r = -1; else r = j_cmp(ka, kb);
            if (r < 0) {
                if (a1) {
                    struct jl *l = &l1[i1];
                    int f;
                    if (useout) { for (f = 0; f < nout; f++) { if (f) putchar(sep); if (outf[f].file == 0) fputs(ka, stdout); else if (outf[f].file == 1) fputs(outf[f].field <= l->nf ? l->f[outf[f].field - 1] : jempty, stdout); else fputs(jempty, stdout); } }
                    else { fputs(ka, stdout); for (f = 0; f < l->nf; f++) if (f != k1 - 1) { putchar(sep); fputs(l->f[f], stdout); } }
                    putchar('\n');
                }
                i1++;
            } else if (r > 0) {
                if (a2) {
                    struct jl *l = &l2[i2];
                    int f;
                    if (useout) { for (f = 0; f < nout; f++) { if (f) putchar(sep); if (outf[f].file == 0) fputs(kb, stdout); else if (outf[f].file == 2) fputs(outf[f].field <= l->nf ? l->f[outf[f].field - 1] : jempty, stdout); else fputs(jempty, stdout); } }
                    else { fputs(kb, stdout); for (f = 0; f < l->nf; f++) if (f != k2 - 1) { putchar(sep); fputs(l->f[f], stdout); } }
                    putchar('\n');
                }
                i2++;
            } else {
                int e1 = i1, e2 = i2, x, y;
                while (e1 < nf1 && !j_cmp(EMIT_KEY(&l1[e1], k1), ka)) e1++;
                while (e2 < nf2 && !j_cmp(EMIT_KEY(&l2[e2], k2), kb)) e2++;
                if (!v1 && !v2)
                    for (x = i1; x < e1; x++)
                        for (y = i2; y < e2; y++) {
                            struct jl *a = &l1[x], *b = &l2[y];
                            int f;
                            if (useout) {
                                for (f = 0; f < nout; f++) {
                                    if (f) putchar(sep);
                                    if (outf[f].file == 0) fputs(ka, stdout);
                                    else { struct jl *l = outf[f].file == 1 ? a : b; fputs(outf[f].field <= l->nf && outf[f].field > 0 ? l->f[outf[f].field - 1] : jempty, stdout); }
                                }
                            } else {
                                fputs(ka, stdout);
                                for (f = 0; f < a->nf; f++) if (f != k1 - 1) { putchar(sep); fputs(a->f[f], stdout); }
                                for (f = 0; f < b->nf; f++) if (f != k2 - 1) { putchar(sep); fputs(b->f[f], stdout); }
                            }
                            putchar('\n');
                        }
                i1 = e1; i2 = e2;
            }
        }
    }
    return finish_stdout() ? 1 : 0;
}

/* ---- tsort ---- */

static int t_tsort(int argc, char **argv)
{
    char *d, **names = NULL, *tok;
    size_t len;
    int fd, n = 0, cap = 0, ne = 0, ecap = 0, *ef = NULL, *et = NULL, *indeg, *done, *queue, qh = 0, qt = 0, bad = 0, emitted = 0, k;
    opts_reset();
    while (getopts(argc, argv, "", NULL)) {}
    if (nops > 1) extra_operand(ops[1]);
    fd = open_input(nops ? ops[0] : "-");
    if (fd < 0) { perr("%s", ops[0]); return 1; }
    if (read_all(fd, &d, &len) < 0) { perr("read error"); return 1; }
    {
        int pending = -1;
        size_t p = 0;
        d = xrealloc(d, len + 1); d[len] = 0;
        while (p < len) {
            int id = -1;
            while (p < len && isspace((unsigned char)d[p])) p++;
            if (p >= len) break;
            tok = d + p;
            while (p < len && !isspace((unsigned char)d[p])) p++;
            d[p] = 0; p++;
            for (k = 0; k < n; k++) if (!strcmp(names[k], tok)) { id = k; break; }
            if (id < 0) { if (n == cap) { cap = cap ? cap * 2 : 64; names = xrealloc(names, cap * sizeof *names); } names[n] = tok; id = n++; }
            if (pending < 0) pending = id;
            else {
                if (ne == ecap) { ecap = ecap ? ecap * 2 : 64; ef = xrealloc(ef, ecap * sizeof *ef); et = xrealloc(et, ecap * sizeof *et); }
                ef[ne] = pending; et[ne] = id; ne++;
                pending = -1;
            }
        }
        if (pending >= 0) die(1, "%s: input contains an odd number of tokens", nops ? ops[0] : "-");
    }
    indeg = xmalloc((n + 1) * sizeof *indeg); done = xmalloc((n + 1) * sizeof *done); queue = xmalloc((n + 1) * sizeof *queue);
    for (k = 0; k < n; k++) { indeg[k] = 0; done[k] = 0; }
    for (k = 0; k < ne; k++) if (ef[k] != et[k]) indeg[et[k]]++;
    for (k = 0; k < n; k++) if (!indeg[k]) queue[qt++] = k;
    while (emitted < n) {
        while (qh < qt) {
            int u = queue[qh++];
            done[u] = 1; emitted++;
            puts(names[u]);
            for (k = 0; k < ne; k++) if (ef[k] == u && et[k] != u && !--indeg[et[k]]) queue[qt++] = et[k];
        }
        if (emitted < n) {
            int first = -1;
            for (k = 0; k < n; k++) if (!done[k]) { if (first < 0) first = k; }
            fflush(stdout);
            say("%s: input contains a loop:", nops ? ops[0] : "-");
            for (k = 0; k < n; k++) if (!done[k] && indeg[k]) fprintf(stderr, "%s: %s\n", prog, names[k]);
            bad = 1;
            indeg[first] = 0;
            queue[qt++] = first;
        }
    }
    return finish_stdout() ? 1 : bad;
}

/* ---- factor ---- */

typedef unsigned long long u64;

static u64 mulmod(u64 a, u64 b, u64 m)
{
    u64 r;
    __asm__("mulq %2\n divq %3" : "=&d"(r), "+a"(a) : "r"(b), "r"(m) : "cc");
    return r;
}

static u64 powmod(u64 b, u64 e, u64 m)
{
    u64 r = 1;
    b %= m;
    while (e) { if (e & 1) r = mulmod(r, b, m); b = mulmod(b, b, m); e >>= 1; }
    return r;
}

static int is_prime(u64 n)
{
    static const u64 bases[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };
    u64 d = n - 1;
    int s = 0, i;
    if (n < 2) return 0;
    for (i = 0; i < 12; i++) { if (n == bases[i]) return 1; if (n % bases[i] == 0) return 0; }
    while (!(d & 1)) { d >>= 1; s++; }
    for (i = 0; i < 12; i++) {
        u64 x = powmod(bases[i], d, n);
        int r;
        if (x == 1 || x == n - 1) continue;
        for (r = 1; r < s; r++) { x = mulmod(x, x, n); if (x == n - 1) break; }
        if (r == s) return 0;
    }
    return 1;
}

static u64 gcd64(u64 a, u64 b) { while (b) { u64 t = a % b; a = b; b = t; } return a; }

static u64 rho(u64 n)
{
    u64 c;
    for (c = 1;; c++) {
        u64 x = 2, y = 2, d = 1;
        while (d == 1) {
            x = (mulmod(x, x, n) + c) % n;
            y = (mulmod(y, y, n) + c) % n;
            y = (mulmod(y, y, n) + c) % n;
            d = gcd64(x > y ? x - y : y - x, n);
        }
        if (d != n) return d;
    }
}

static void factor_into(u64 n, u64 *out, int *cnt)
{
    if (n == 1) return;
    if (is_prime(n)) { out[(*cnt)++] = n; return; }
    {
        u64 d = rho(n);
        factor_into(d, out, cnt);
        factor_into(n / d, out, cnt);
    }
}

static int factor_one(const char *s)
{
    char *e;
    u64 n, f[64];
    int cnt = 0, i, j;
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '+') p++;
    if (!isdigit((unsigned char)*p)) { say("'%s' is not a valid positive integer", s); return 1; }
    for (n = 0, e = (char *)p; isdigit((unsigned char)*e); e++) {
        if (n > (~0ULL - (*e - '0')) / 10) { say("'%s' is too large for rifle's factor", s); return 1; }
        n = n * 10 + (*e - '0');
    }
    if (*e) { say("'%s' is not a valid positive integer", s); return 1; }
    printf("%llu:", n);
    if (n > 1) {
        u64 q;
        for (q = 2; q < 1000 && q * q <= n; q++) while (n % q == 0) { f[cnt++] = q; n /= q; }
        factor_into(n, f, &cnt);
    }
    for (i = 0; i < cnt; i++) for (j = i + 1; j < cnt; j++) if (f[j] < f[i]) { u64 t = f[i]; f[i] = f[j]; f[j] = t; }
    for (i = 0; i < cnt; i++) printf(" %llu", f[i]);
    putchar('\n');
    return 0;
}

static int t_factor(int argc, char **argv)
{
    int i, bad = 0;
    opts_reset();
    while (getopts(argc, argv, "", NULL)) {}
    if (nops) { for (i = 0; i < nops; i++) bad |= factor_one(ops[i]); }
    else {
        char tok[64];
        int c, k = 0;
        while ((c = getchar()) != EOF) {
            if (isspace(c)) { if (k) { tok[k] = 0; bad |= factor_one(tok); k = 0; } }
            else if (k < 63) tok[k++] = c;
        }
        if (k) { tok[k] = 0; bad |= factor_one(tok); }
    }
    return finish_stdout() ? 1 : bad;
}
/* ---- regex ----
 * BRE and ERE read the way GNU reads them (\w \< \b \+ \| work in a BRE too), on bytes. a pattern becomes a node tree,
 * the tree a short program, and a backtracking machine runs that, keeping the longest match from the leftmost start.
 * grep and sed both use it */

enum { R_CHAR, R_ANY, R_SET, R_SPLIT, R_JMP, R_SAVE, R_BOL, R_EOL, R_WB, R_NWB, R_WBEG, R_WEND, R_BEFORE, R_AFTER,
       R_BREF, R_MARK, R_PROG, R_MATCH };
enum { N_EMPTY, N_CHAR, N_ANY, N_SET, N_CAT, N_ALT, N_REP, N_GRP, N_ASSERT, N_BREF };

struct rinst { int op, x, y, id; };
struct rnode { int t, c, min, max; struct rnode *a, *b; };
struct regex {
    struct rinst *prog; int n, cap, ngrp, nslot, nsplit, nmark, backref, icase, first;
    unsigned char (*sets)[32]; int nsets;
};
struct rparse { const char *p, *e; int ere, sed, depth, local; struct regex *re; };

static int re_exit = 2;
#define RE_DUP_MAX 32767

static __attribute__((noreturn)) void re_die(const char *m) { die(re_exit, "%s", m); }

static struct rnode *rn(int t, struct rnode *a, struct rnode *b)
{
    struct rnode *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->t = t; n->a = a; n->b = b;
    return n;
}
static struct rnode *rn_cat(struct rnode *a, struct rnode *b) { return a ? rn(N_CAT, a, b) : b; }

static struct regex *re_new(int icase)
{
    struct regex *re = xmalloc(sizeof *re);
    memset(re, 0, sizeof *re);
    re->icase = icase; re->first = -1;
    return re;
}

static int re_newset(struct regex *re)
{
    re->sets = xrealloc(re->sets, (re->nsets + 1) * sizeof *re->sets);
    memset(re->sets[re->nsets], 0, 32);
    return re->nsets++;
}
#define SET_ADD(s, c) ((s)[(unsigned char)(c) >> 3] |= 1 << ((c) & 7))
#define SET_HAS(s, c) ((s)[(unsigned char)(c) >> 3] >> ((c) & 7) & 1)

static void set_fold(unsigned char *s)
{
    int c;
    for (c = 'a'; c <= 'z'; c++) {
        if (SET_HAS(s, c) || SET_HAS(s, c - 32)) { SET_ADD(s, c); SET_ADD(s, c - 32); }
    }
}

static struct rnode *re_ch(struct rparse *ps, int c)
{
    struct rnode *n;
    if (ps->re->icase && isalpha(c)) {
        n = rn(N_SET, NULL, NULL);
        n->c = re_newset(ps->re);
        SET_ADD(ps->re->sets[n->c], c); SET_ADD(ps->re->sets[n->c], c ^ 32);
        return n;
    }
    n = rn(N_CHAR, NULL, NULL);
    n->c = c;
    return n;
}

static int class_has(const char *name, size_t l, int c)
{
    static const char *names[] = { "alpha", "digit", "alnum", "upper", "lower", "space", "blank", "punct", "print", "graph", "cntrl", "xdigit" };
    int k;
    for (k = 0; k < 12; k++) if (strlen(names[k]) == l && !strncmp(names[k], name, l)) break;
    switch (k) {
    case 0: return isalpha(c);
    case 1: return isdigit(c);
    case 2: return isalnum(c);
    case 3: return isupper(c);
    case 4: return islower(c);
    case 5: return isspace(c);
    case 6: return c == ' ' || c == '\t';
    case 7: return ispunct(c);
    case 8: return c >= 32 && c < 127;
    case 9: return c > 32 && c < 127;
    case 10: return c < 32 || c == 127;
    case 11: return isxdigit(c);
    }
    return -1;
}

static int class_name_ok(const char *name, size_t l) { return class_has(name, l, 'a') >= 0; }

static void class_add(unsigned char *set, const char *name, size_t l)
{
    int c;
    for (c = 0; c < 256; c++) if (class_has(name, l, c) > 0) SET_ADD(set, c);
}

static int re_bchar(struct rparse *ps, const char **pp)
{
    const char *p = *pp;
    int c = (unsigned char)*p++;
    if (c == '[' && p < ps->e && *p == '.') {
        if (p + 3 >= ps->e || p[2] != '.' || p[3] != ']') re_die("Invalid collation character");
        c = (unsigned char)p[1]; p += 4;
    } else if (ps->sed && c == '\\' && p < ps->e && (*p == 'n' || *p == 't')) c = *p++ == 'n' ? '\n' : '\t';
    *pp = p;
    return c;
}

/* after the [ */
static struct rnode *re_bracket(struct rparse *ps)
{
    struct regex *re = ps->re;
    int idx = re_newset(re), neg = 0, first = 1, c, k;
    const char *p = ps->p, *start;
    if (p < ps->e && *p == '^') { neg = 1; p++; }
    start = p;
    for (;; first = 0) {
        int lo;
        if (p >= ps->e) re_die("Unmatched [, [^, [:, [., or [=");
        if (*p == ']' && !first) { p++; break; }
        if (*p == '[' && p + 1 < ps->e && (p[1] == ':' || p[1] == '=')) {
            const char *q = p + 2;
            while (q + 1 < ps->e && !(*q == p[1] && q[1] == ']')) q++;
            if (q + 1 >= ps->e) re_die("Unmatched [, [^, [:, [., or [=");
            if (p[1] == ':') {
                if (!class_name_ok(p + 2, q - p - 2)) re_die("Invalid character class name");
                class_add(re->sets[idx], p + 2, q - p - 2);
            } else {
                if (q - p != 3) re_die("Invalid collation character");
                SET_ADD(re->sets[idx], p[2]);
            }
            p = q + 2;
            continue;
        }
        lo = re_bchar(ps, &p);
        if (p + 1 < ps->e && *p == '-' && p[1] != ']') {
            int hi;
            p++;
            hi = re_bchar(ps, &p);
            if (hi < lo) re_die("Invalid range end");
            for (c = lo; c <= hi; c++) SET_ADD(re->sets[idx], c);
        } else SET_ADD(re->sets[idx], lo);
    }
    if (p - start >= 3 && *start == ':' && p[-2] == ':') re_die("character class syntax is [[:space:]], not [:space:]");
    ps->p = p;
    if (re->icase) set_fold(re->sets[idx]);
    if (neg) for (k = 0; k < 32; k++) re->sets[idx][k] = ~re->sets[idx][k];
    {
        struct rnode *n = rn(N_SET, NULL, NULL);
        n->c = idx;
        return n;
    }
}

static struct rnode *re_alt(struct rparse *ps);

static struct rnode *re_assert(int op)
{
    struct rnode *n = rn(N_ASSERT, NULL, NULL);
    n->c = op;
    return n;
}

static struct rnode *re_shorthand(struct rparse *ps, int c)
{
    struct rnode *n = rn(N_SET, NULL, NULL);
    unsigned char *s;
    int k;
    n->c = re_newset(ps->re);
    s = ps->re->sets[n->c];
    for (k = 0; k < 256; k++) {
        int w = isalnum(k) || k == '_', sp = isspace(k);
        if (c == 'w' ? w : c == 'W' ? !w : c == 's' ? sp : !sp) SET_ADD(s, k);
    }
    return n;
}

/* the {m,n} after a brace. in an ERE a brace that doesn't make an interval is only a brace, in a BRE it is an error */
static int re_interval(struct rparse *ps, int *mn, int *mx)
{
    const char *p = ps->p;
    long a = -1, b = -1;
    int comma = 0;
    while (p < ps->e && isdigit((unsigned char)*p)) { a = (a < 0 ? 0 : a) * 10 + *p++ - '0'; if (a > RE_DUP_MAX + 1) a = RE_DUP_MAX + 1; }
    if (p < ps->e && *p == ',') {
        comma = 1; p++;
        while (p < ps->e && isdigit((unsigned char)*p)) { b = (b < 0 ? 0 : b) * 10 + *p++ - '0'; if (b > RE_DUP_MAX + 1) b = RE_DUP_MAX + 1; }
    }
    if (ps->ere ? !(p < ps->e && *p == '}') : !(p + 1 < ps->e && p[0] == '\\' && p[1] == '}')) {
        if (ps->ere) return 0;
        re_die(p >= ps->e ? "Unmatched \\{" : "Invalid content of \\{\\}");
    }
    if (a < 0 && !comma) { if (ps->ere) return 0; re_die("Invalid content of \\{\\}"); }
    p += ps->ere ? 1 : 2;
    if (a < 0) a = 0;
    if (!comma) b = a;
    if (b >= 0 && a > b) re_die("Invalid content of \\{\\}");
    if (a > RE_DUP_MAX || b > RE_DUP_MAX) re_die("Regular expression too big");
    *mn = a; *mx = b;
    ps->p = p;
    return 1;
}

/* one atom, no repetition. *anchor says it was the ^ of a BRE, after which a * is still an ordinary character */
static struct rnode *re_atom(struct rparse *ps, int start, int *anchor)
{
    const char *p = ps->p;
    int c = (unsigned char)*p++, ere = ps->ere;
    *anchor = 0;
    if (c == '\\') {
        if (p >= ps->e) re_die("Trailing backslash");
        c = (unsigned char)*p++;
        if (!ere && c == '(') goto group;
        switch (c) {
        case 'w': case 'W': case 's': case 'S': ps->p = p; return re_shorthand(ps, c);
        case 'b': ps->p = p; return re_assert(R_WB);
        case 'B': ps->p = p; return re_assert(R_NWB);
        case '<': ps->p = p; return re_assert(R_WBEG);
        case '>': ps->p = p; return re_assert(R_WEND);
        case '`': ps->p = p; return re_assert(R_BOL);
        case '\'': ps->p = p; return re_assert(R_EOL);
        }
        if (c >= '1' && c <= '9') {
            struct rnode *n;
            if (c - '0' > ps->local) re_die("Invalid back reference");
            n = rn(N_BREF, NULL, NULL);
            n->c = ps->re->ngrp - ps->local + c - '0';
            ps->re->backref = 1;
            ps->p = p;
            return n;
        }
        if (ps->sed) {
            int v = c == 'n' ? '\n' : c == 't' ? '\t' : c == 'f' ? '\f' : c == 'v' ? '\v' : c == 'a' ? 7 : c == 'r' ? '\r' : -1;
            if (c == 'x' && p + 1 < ps->e && isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1])) {
                v = (isdigit((unsigned char)p[0]) ? p[0] - '0' : (p[0] | 32) - 'a' + 10) * 16 + (isdigit((unsigned char)p[1]) ? p[1] - '0' : (p[1] | 32) - 'a' + 10);
                p += 2;
            }
            if (v >= 0) c = v;
        }
        ps->p = p;
        return re_ch(ps, c);
    }
    if (ere && c == '(') {
    group:
        {
            struct rnode *g = rn(N_GRP, NULL, NULL);
            g->c = ++ps->re->ngrp; ps->local++;
            ps->p = p; ps->depth++;
            g->a = re_alt(ps);
            ps->depth--;
            if (ps->ere ? !(ps->p < ps->e && *ps->p == ')') : !(ps->p + 1 < ps->e && ps->p[0] == '\\' && ps->p[1] == ')'))
                re_die("Unmatched ( or \\(");
            ps->p += ps->ere ? 1 : 2;
            return g;
        }
    }
    ps->p = p;
    switch (c) {
    case '.': return rn(N_ANY, NULL, NULL);
    case '[': return re_bracket(ps);
    case '^':
        if (ere) return re_assert(R_BOL);
        if (start) { *anchor = 1; return re_assert(R_BOL); }
        break;
    case '$':
        if (ere || p >= ps->e || (p + 1 < ps->e && p[0] == '\\' && (p[1] == ')' || p[1] == '|'))) return re_assert(R_EOL);
        break;
    }
    return re_ch(ps, c);
}

/* is there a repetition operator next? */
static int re_op(struct rparse *ps, int *mn, int *mx)
{
    const char *p = ps->p;
    int bs = 0;
    if (p >= ps->e) return 0;
    if (!ps->ere && *p == '\\') { if (p + 1 >= ps->e) return 0; bs = 1; p++; }
    if (!bs && *p == '*') { *mn = 0; *mx = -1; ps->p = p + 1; return 1; }
    if (bs == ps->ere) return 0;
    if (*p == '+') { *mn = 1; *mx = -1; ps->p = p + 1; return 1; }
    if (*p == '?') { *mn = 0; *mx = 1; ps->p = p + 1; return 1; }
    if (*p == '{') { const char *save = ps->p; ps->p = p + 1; if (re_interval(ps, mn, mx)) return 1; ps->p = save; }
    return 0;
}

static struct rnode *re_cat(struct rparse *ps)
{
    struct rnode *head = NULL;
    int start = 1;
    while (ps->p < ps->e) {
        struct rnode *n;
        int anchor, mn, mx;
        if (ps->ere) {
            if (*ps->p == '|' || (*ps->p == ')' && ps->depth)) break;
        } else if (*ps->p == '\\' && ps->p + 1 < ps->e && (ps->p[1] == '|' || ps->p[1] == ')')) {
            if (ps->p[1] == ')' && !ps->depth) re_die("Unmatched ) or \\)");
            break;
        }
        if (start && ps->ere && re_op(ps, &mn, &mx)) continue;
        n = re_atom(ps, start, &anchor);
        start = anchor;
        if (!anchor)
            while (re_op(ps, &mn, &mx)) {
                struct rnode *r = rn(N_REP, n, NULL);
                r->min = mn; r->max = mx;
                n = r;
            }
        head = head ? rn(N_CAT, head, n) : n;
    }
    return head ? head : rn(N_EMPTY, NULL, NULL);
}

static struct rnode *re_alt(struct rparse *ps)
{
    struct rnode *n = re_cat(ps);
    while (ps->p < ps->e && (ps->ere ? *ps->p == '|' : (*ps->p == '\\' && ps->p + 1 < ps->e && ps->p[1] == '|'))) {
        ps->p += ps->ere ? 1 : 2;
        n = rn(N_ALT, n, re_cat(ps));
    }
    return n;
}

static struct rnode *re_parse(struct regex *re, const char *pat, size_t len, int ere, int sed)
{
    struct rparse ps = { pat, pat + len, ere, sed, 0, 0, re };
    return re_alt(&ps);
}

static struct rnode *re_literal(struct regex *re, const char *pat, size_t len)
{
    struct rparse ps = { pat, pat + len, 0, 0, 0, 0, re };
    struct rnode *n = NULL;
    size_t i;
    for (i = 0; i < len; i++) n = rn_cat(n, re_ch(&ps, (unsigned char)pat[i]));
    return n ? n : rn(N_EMPTY, NULL, NULL);
}

static int re_put(struct regex *re, int op, int x, int y)
{
    if (re->n == re->cap) {
        if (re->cap > (1 << 22)) re_die("Regular expression too big");
        re->cap = re->cap ? re->cap * 2 : 64;
        re->prog = xrealloc(re->prog, re->cap * sizeof *re->prog);
    }
    re->prog[re->n].op = op; re->prog[re->n].x = x; re->prog[re->n].y = y; re->prog[re->n].id = 0;
    return re->n++;
}
static int re_split(struct regex *re)
{
    int j = re_put(re, R_SPLIT, 0, 0);
    re->prog[j].x = re->n; re->prog[j].id = re->nsplit++;
    return j;
}

static int re_nullable(const struct rnode *n)
{
    switch (n->t) {
    case N_CHAR: case N_ANY: case N_SET: return 0;
    case N_CAT: return re_nullable(n->a) && re_nullable(n->b);
    case N_ALT: return re_nullable(n->a) || re_nullable(n->b);
    case N_REP: return !n->min || re_nullable(n->a);
    case N_GRP: return re_nullable(n->a);
    }
    return 1;
}

static void re_emit(struct regex *re, const struct rnode *n)
{
    int i, j, k;
    switch (n->t) {
    case N_CHAR: re_put(re, R_CHAR, n->c, 0); break;
    case N_ANY: re_put(re, R_ANY, 0, 0); break;
    case N_SET: re_put(re, R_SET, n->c, 0); break;
    case N_ASSERT: re_put(re, n->c, 0, 0); break;
    case N_BREF: re_put(re, R_BREF, n->c, 0); break;
    case N_CAT: re_emit(re, n->a); re_emit(re, n->b); break;
    case N_GRP:
        re_put(re, R_SAVE, 2 * n->c, 0);
        re_emit(re, n->a);
        re_put(re, R_SAVE, 2 * n->c + 1, 0);
        break;
    case N_ALT:
        j = re_split(re);
        re_emit(re, n->a);
        k = re_put(re, R_JMP, 0, 0);
        re->prog[j].y = re->n;
        re_emit(re, n->b);
        re->prog[k].x = re->n;
        break;
    case N_REP:
        for (i = 0; i < n->min; i++) re_emit(re, n->a);
        if (n->max < 0) {
            int mk = re_nullable(n->a) ? re->nmark++ : -1;
            j = re_split(re);
            if (mk >= 0) re_put(re, R_MARK, mk, 0);
            re_emit(re, n->a);
            if (mk >= 0) re_put(re, R_PROG, mk, 0);
            re_put(re, R_JMP, j, 0);
            re->prog[j].y = re->n;
        } else if (n->max > n->min) {
            int *js = xmalloc((n->max - n->min) * sizeof *js);
            for (i = 0; i < n->max - n->min; i++) { js[i] = re_split(re); re_emit(re, n->a); }
            for (i = 0; i < n->max - n->min; i++) re->prog[js[i]].y = re->n;
            free(js);
        }
        break;
    }
}

/* the program for a tree. the group slots come first, then one slot for each loop that could match nothing */
static void re_finish(struct regex *re, struct rnode *n)
{
    int i;
    re_emit(re, n);
    re_put(re, R_MATCH, 0, 0);
    for (i = 0; i < re->n; i++) if (re->prog[i].op == R_MARK || re->prog[i].op == R_PROG) re->prog[i].x += 2 * (re->ngrp + 1);
    re->nslot = 2 * (re->ngrp + 1) + re->nmark;
    if (re->prog[0].op == R_CHAR) re->first = re->prog[0].x;
}

static int word_at(const char *s, size_t len, long i) { return i >= 0 && (size_t)i < len && (isalnum((unsigned char)s[i]) || s[i] == '_'); }

struct rframe { int pc, slot; long v; };

/* the leftmost match at or after start. caps gets 2 * (ngrp + 1) offsets, -1 for a group that didn't take part.
 * longest 0 stops at the first way to match, which is all a yes-or-no question needs */
static int re_exec(const struct regex *re, const char *s, size_t len, size_t start, int longest, long *caps)
{
    static struct rframe *stk;
    static size_t stkcap;
    static long *work;
    static size_t workcap;
    static uint32_t *seen, gen;
    static size_t seencap;
    size_t st, stride = len + 1, need = (size_t)re->nsplit * stride;
    int memo = !re->backref && need && need <= (1u << 26), ncap = 2 * (re->ngrp + 1);
    const struct rinst *prog = re->prog;
    if (workcap < (size_t)re->nslot) { workcap = re->nslot; work = xrealloc(work, workcap * sizeof *work); }
    if (memo) {
        if (seencap < need) { seencap = need; seen = xrealloc(seen, seencap * sizeof *seen); memset(seen, 0, seencap * sizeof *seen); gen = 0; }
        if (++gen == 0) { memset(seen, 0, seencap * sizeof *seen); gen = 1; }
    }
    for (st = start; st <= len; st++) {
        long best = -1;
        size_t sp = 0;
        int pc = 0, i;
        long pos = st;
        if (re->first >= 0) {
            const char *q = memchr(s + st, re->first, len - st);
            if (!q) return 0;
            st = pos = q - s;
        }
        for (i = 0; i < re->nslot; i++) work[i] = -1;
        for (;;) {
            const struct rinst *in = &prog[pc];
            switch (in->op) {
            case R_CHAR: if ((size_t)pos < len && (unsigned char)s[pos] == in->x) { pos++; pc++; continue; } goto fail;
            case R_ANY: if ((size_t)pos < len) { pos++; pc++; continue; } goto fail;
            case R_SET: if ((size_t)pos < len && SET_HAS(re->sets[in->x], s[pos])) { pos++; pc++; continue; } goto fail;
            case R_SPLIT:
                if (memo) {
                    uint32_t *m = &seen[(size_t)in->id * stride + pos];
                    if (*m == gen) goto fail;
                    *m = gen;
                }
                if (sp == stkcap) { stkcap = stkcap ? stkcap * 2 : 256; stk = xrealloc(stk, stkcap * sizeof *stk); }
                stk[sp].pc = in->y; stk[sp].v = pos; sp++;
                pc = in->x;
                continue;
            case R_JMP: pc = in->x; continue;
            case R_SAVE: case R_MARK:
                if (sp == stkcap) { stkcap = stkcap ? stkcap * 2 : 256; stk = xrealloc(stk, stkcap * sizeof *stk); }
                stk[sp].slot = in->x; stk[sp].pc = -1; stk[sp].v = work[in->x]; sp++;
                work[in->x] = pos; pc++;
                continue;
            case R_PROG: if (work[in->x] == pos) goto fail; pc++; continue;
            case R_BOL: if (pos == 0) { pc++; continue; } goto fail;
            case R_EOL: if ((size_t)pos == len) { pc++; continue; } goto fail;
            case R_WB: if (word_at(s, len, pos - 1) != word_at(s, len, pos)) { pc++; continue; } goto fail;
            case R_NWB: if (word_at(s, len, pos - 1) == word_at(s, len, pos)) { pc++; continue; } goto fail;
            case R_WBEG: if (!word_at(s, len, pos - 1) && word_at(s, len, pos)) { pc++; continue; } goto fail;
            case R_WEND: if (word_at(s, len, pos - 1) && !word_at(s, len, pos)) { pc++; continue; } goto fail;
            case R_BEFORE: if (!word_at(s, len, pos - 1)) { pc++; continue; } goto fail;
            case R_AFTER: if (!word_at(s, len, pos)) { pc++; continue; } goto fail;
            case R_BREF: {
                long a = work[2 * in->x], b = work[2 * in->x + 1], n;
                if (a < 0 || b < 0) goto fail;
                n = b - a;
                if ((size_t)(pos + n) > len || (re->icase ? strncasecmp(s + a, s + pos, n) : memcmp(s + a, s + pos, n))) goto fail;
                pos += n; pc++;
                continue;
            }
            case R_MATCH:
                if (pos > best) {
                    best = pos;
                    if (caps) { memcpy(caps, work, ncap * sizeof *caps); caps[0] = st; caps[1] = pos; }
                }
                if (!longest || (size_t)pos == len) return 1;
                goto fail;
            }
        fail:
            for (;;) {
                if (!sp) goto done;
                sp--;
                if (stk[sp].pc < 0) work[stk[sp].slot] = stk[sp].v;
                else { pc = stk[sp].pc; pos = stk[sp].v; break; }
            }
        }
    done:
        if (best >= 0) return 1;
    }
    return 0;
}

/* ---- reading lines ----
 * a chunk at a time, so grep and sed can start answering while a pipe is still filling. a line is good until the next call */

struct lr { int fd, eof, err, binary; char *buf; size_t cap, pos, end, scan; };

static void lr_init(struct lr *r, int fd) { memset(r, 0, sizeof *r); r->fd = fd; }

static int lr_fill(struct lr *r)
{
    ssize_t n;
    if (r->pos) {
        memmove(r->buf, r->buf + r->pos, r->end - r->pos);
        r->scan -= r->pos; r->end -= r->pos; r->pos = 0;
    }
    if (r->end == r->cap) { r->cap = r->cap ? r->cap * 2 : 65536; r->buf = xrealloc(r->buf, r->cap); }
    do n = read(r->fd, r->buf + r->end, r->cap - r->end); while (n < 0 && errno == EINTR);
    if (n <= 0) { if (n < 0) r->err = errno; r->eof = 1; return 0; }
    if (memchr(r->buf + r->end, 0, n)) r->binary = 1;
    r->end += n;
    return 1;
}

static int lr_line(struct lr *r, char **s, size_t *len, int *nl)
{
    for (;;) {
        char *e = r->end > r->scan ? memchr(r->buf + r->scan, '\n', r->end - r->scan) : NULL;
        if (e) {
            *s = r->buf + r->pos; *len = e - *s; *nl = 1;
            r->pos = r->scan = e - r->buf + 1;
            return 1;
        }
        r->scan = r->end;
        if (r->eof) {
            if (r->pos == r->end) return 0;
            *s = r->buf + r->pos; *len = r->end - r->pos; *nl = 0;
            r->pos = r->end;
            return 1;
        }
        lr_fill(r);
    }
}

/* is there more after the line just read? sed needs it for $. a line from before is gone once this reads */
static int lr_more(struct lr *r) { return r->pos < r->end || (!r->eof && lr_fill(r)); }

/* ---- grep ---- */

static struct regex *g_re;
static int g_invert, g_count, g_list, g_nolist, g_num, g_name, g_only, g_quiet, g_nomsg, g_byte, g_null, g_text, g_recurse,
    g_dirs, g_ctx, g_lbuf, g_any, g_err, g_printed;
static long g_after, g_before, g_max = -1, *g_caps;
static char **g_inc, **g_exc, **g_excd;
static int g_ninc, g_nexc, g_nexcd;
static const char *g_label = "(standard input)";
static const char *gf_name;
static int gf_show;
static long gf_last;

static void g_prefix(long no, long off, int sep)
{
    if (gf_show) { fputs(gf_name, stdout); putchar(g_null ? 0 : sep); }
    if (g_num) printf("%ld%c", no, sep);
    if (g_byte) printf("%ld%c", off, sep);
}

static void g_line(long no, long off, const char *s, size_t len, int sep)
{
    if (g_ctx && g_printed && !(gf_last >= 0 && no == gf_last + 1)) puts("--");
    g_printed = 1; gf_last = no;
    if (g_only) {
        size_t st = 0;
        if (sep != ':' || g_invert) return;
        while (st <= len && re_exec(g_re, s, len, st, 1, g_caps)) {
            if (g_caps[1] == g_caps[0]) { st = g_caps[1] + 1; continue; }
            g_prefix(no, off + g_caps[0], sep);
            fwrite(s + g_caps[0], 1, g_caps[1] - g_caps[0], stdout);
            putchar('\n');
            st = g_caps[1];
        }
    } else {
        g_prefix(no, off, sep);
        fwrite(s, 1, len, stdout);
        putchar('\n');
    }
    if (g_lbuf) fflush(stdout);
}

struct held { char *s; size_t len, cap; long no, off; };

static long grep_fd(int fd)
{
    struct lr r;
    struct held *ring = NULL;
    char *s;
    size_t len;
    int nl, done = 0;
    long no = 0, off = 0, count = 0, left = 0, j;
    lr_init(&r, fd);
    gf_last = -1;
    if (g_before) { ring = xmalloc(g_before * sizeof *ring); memset(ring, 0, g_before * sizeof *ring); }
    while (lr_line(&r, &s, &len, &nl)) {
        int sel;
        no++;
        if (r.binary && g_text == 2) { count = 0; break; }
        if (done) {
            if (left-- <= 0) break;
            g_line(no, off, s, len, '-');
        } else if ((sel = re_exec(g_re, s, len, 0, 0, NULL) != g_invert)) {
            count++; g_any = 1;
            if (g_quiet) exit(0);
            if (g_list || g_nolist) break;
            if (!g_count) {
                if (r.binary && !g_text) { say("%s: binary file matches", gf_name); break; }
                for (j = no - g_before; j < no; j++)
                    if (j >= 1 && j > gf_last && ring[j % g_before].no == j) g_line(j, ring[j % g_before].off, ring[j % g_before].s, ring[j % g_before].len, '-');
                g_line(no, off, s, len, ':');
                left = g_after;
            }
            if (g_max >= 0 && count >= g_max) { done = 1; if (g_count || !left) break; }
        } else if (!g_count && !g_list && !g_nolist) {
            if (left > 0) { left--; g_line(no, off, s, len, '-'); }
            else if (g_before) {
                struct held *h = &ring[no % g_before];
                if (h->cap < len) { h->cap = len + 64; h->s = xrealloc(h->s, h->cap); }
                memcpy(h->s, s, len); h->len = len; h->no = no; h->off = off;
            }
        }
        off += len + nl;
    }
    if (r.err) { if (!g_nomsg) { errno = r.err; perr("%s", gf_name); } g_err = 1; }
    free(r.buf);
    if (ring) { for (j = 0; j < g_before; j++) free(ring[j].s); free(ring); }
    return count;
}

static int name_listed(char **v, int n, const char *path)
{
    const char *b = base_of(path);
    int i;
    for (i = 0; i < n; i++) if (glob_match(v[i], b) || glob_match(v[i], path)) return 1;
    return 0;
}

static int cmp_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static const char g_dot[] = ".";

static void grep_path(const char *path, int top)
{
    struct stat st;
    int fd;
    long count;
    const char *shown = path;
    if (top && !strcmp(path, "-")) { fd = 0; shown = g_label; goto go; }
    if (g_recurse || g_dirs) {
        if ((top || g_recurse == 2 ? stat(path, &st) : lstat(path, &st)) < 0) {
            if (!g_nomsg) perr("%s", path);
            g_err = 1;
            return;
        }
        if (S_ISLNK(st.st_mode)) return;
        if (S_ISDIR(st.st_mode)) {
            if (g_dirs == 1 || (g_nexcd && strcmp(path, ".") && name_listed(g_excd, g_nexcd, path))) return;
            if (g_recurse) {
                DIR *d = opendir(path);
                struct dirent *e;
                char **v = NULL;
                int n = 0, cap = 0, i;
                if (!d) { if (!g_nomsg) perr("%s", path); g_err = 1; return; }
                while ((e = readdir(d))) {
                    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                    if (n == cap) { cap = cap ? cap * 2 : 32; v = xrealloc(v, cap * sizeof *v); }
                    v[n++] = xstrdup(e->d_name);
                }
                closedir(d);
                qsort(v, n, sizeof *v, cmp_names);
                for (i = 0; i < n; i++) {
                    size_t pl = path == g_dot ? 0 : strlen(path);
                    char *child = xmalloc(pl + strlen(v[i]) + 2);
                    memcpy(child, path, pl);
                    if (pl && path[pl - 1] != '/') child[pl++] = '/';
                    strcpy(child + pl, v[i]);
                    grep_path(child, 0);
                    free(child); free(v[i]);
                }
                free(v);
                return;
            }
        } else if (!top && !S_ISREG(st.st_mode)) return;
    }
    if ((g_ninc && !name_listed(g_inc, g_ninc, path)) || (g_nexc && name_listed(g_exc, g_nexc, path))) return;
    fd = open(path, O_RDONLY);
    if (fd < 0) { if (!g_nomsg) perr("%s", path); g_err = 1; return; }
go:
    gf_name = shown;
    gf_show = g_name > 0 || (g_name < 0 && !top);
    count = grep_fd(fd);
    if (fd) close(fd);
    if (g_count) {
        if (gf_show) { fputs(shown, stdout); putchar(g_null ? 0 : ':'); }
        printf("%ld\n", count);
    }
    if ((g_list && count) || (g_nolist && !count)) { fputs(shown, stdout); putchar(g_null ? 0 : '\n'); }
}

static int t_grep(int argc, char **argv)
{
    static const struct opt lo[] = {
        { "extended-regexp", 0, 'E' }, { "fixed-strings", 0, 'F' }, { "basic-regexp", 0, 'G' }, { "perl-regexp", 0, 'P' },
        { "regexp", 1, 'e' }, { "file", 1, 'f' }, { "ignore-case", 0, 'i' }, { "no-ignore-case", 0, 'j' }, { "invert-match", 0, 'v' },
        { "word-regexp", 0, 'w' }, { "line-regexp", 0, 'x' }, { "count", 0, 'c' }, { "color", 2, 'k' }, { "colour", 2, 'k' },
        { "files-without-match", 0, 'L' }, { "files-with-matches", 0, 'l' }, { "max-count", 1, 'm' }, { "only-matching", 0, 'o' },
        { "quiet", 0, 'q' }, { "silent", 0, 'q' }, { "no-messages", 0, 's' }, { "byte-offset", 0, 'b' }, { "with-filename", 0, 'H' },
        { "no-filename", 0, 'h' }, { "label", 1, 300 }, { "line-number", 0, 'n' }, { "null", 0, 'Z' }, { "text", 0, 'a' },
        { "binary-files", 1, 301 }, { "directories", 1, 'd' }, { "devices", 1, 'D' }, { "recursive", 0, 'r' },
        { "dereference-recursive", 0, 'R' }, { "include", 1, 'N' }, { "exclude", 1, 'X' }, { "exclude-dir", 1, 'Y' },
        { "after-context", 1, 'A' }, { "before-context", 1, 'B' }, { "context", 1, 'C' }, { "line-buffered", 0, 'u' },
        { "binary", 0, 'U' }, { "initial-tab", 0, 'T' }, { NULL, 0, 0 } };
    int c, ere = 0, fixed = 0, icase = 0, w = 0, x = 0, havepat = 0, digits = 0, mode = 0;
    char *pbuf = NULL;
    size_t plen = 0;
    struct rnode *all = NULL;
    const char *p, *e;
    usage_status = 2;
    if (!strcmp(prog, "egrep")) ere = 1;
    if (!strcmp(prog, "fgrep")) fixed = 1;
    g_name = -2;
    opts_reset();
    while ((c = getopts(argc, argv, "EFGPe:f:iyvwxcLlm:oqsbHhnZaIUTd:D:rRA:B:C:V0123456789", lo))) {
        if (c >= '0' && c <= '9') {
            g_after = g_before = (digits ? g_after * 10 : 0) + c - '0';
            g_ctx = digits = 1;
            continue;
        }
        digits = 0;
        switch (c) {
        case 'E': case 'F': case 'G':
            if (mode && mode != c) die(2, "conflicting matchers specified");
            mode = c; ere = c == 'E'; fixed = c == 'F';
            break;
        case 'P': die(2, "Perl matching not supported in a --disable-perl-regexp build");
        case 'e': case 'f': {
            const char *add = optarg;
            size_t al = strlen(optarg);
            char *fb = NULL;
            havepat = 1;
            if (c == 'f') {
                int fd = open_input(optarg);
                if (fd < 0 || read_all(fd, &fb, &al) < 0) die(2, "%s: %s", optarg, strerror(errno));
                if (fd) close(fd);
                add = fb;
            }
            pbuf = xrealloc(pbuf, plen + al + 2);
            memcpy(pbuf + plen, add, al); plen += al;
            if (c == 'e' || (al && add[al - 1] != '\n')) pbuf[plen++] = '\n';
            free(fb);
            break;
        }
        case 'i': case 'y': icase = 1; break;
        case 'j': icase = 0; break;
        case 'v': g_invert = 1; break;
        case 'w': w = 1; break;
        case 'x': x = 1; break;
        case 'c': g_count = 1; break;
        case 'L': g_nolist = 1; g_list = 0; break;
        case 'l': g_list = 1; g_nolist = 0; break;
        case 'm': { unsigned long m; if (!parse_ulong(optarg, &m)) die(2, "invalid max count"); g_max = m; break; }
        case 'o': g_only = 1; break;
        case 'q': g_quiet = 1; break;
        case 's': g_nomsg = 1; break;
        case 'b': g_byte = 1; break;
        case 'H': g_name = 1; break;
        case 'h': g_name = 0; break;
        case 'n': g_num = 1; break;
        case 'Z': g_null = 1; break;
        case 'a': g_text = 1; break;
        case 'I': g_text = 2; break;
        case 300: g_label = optarg; break;
        case 301: g_text = !strcmp(optarg, "text") ? 1 : !strcmp(optarg, "without-match") ? 2 : !strcmp(optarg, "binary") ? 0 : (die(2, "unknown binary-files type"), 0); break;
        case 'd':
            if (!strcmp(optarg, "recurse")) g_recurse = 1;
            else if (strcmp(optarg, "read") && !(g_dirs = !strcmp(optarg, "skip"))) die(2, "invalid argument '%s' for '--directories'", optarg);
            break;
        case 'r': g_recurse = 1; break;
        case 'R': g_recurse = 2; break;
        case 'N': g_inc = xrealloc(g_inc, (g_ninc + 1) * sizeof *g_inc); g_inc[g_ninc++] = optarg; break;
        case 'X': g_exc = xrealloc(g_exc, (g_nexc + 1) * sizeof *g_exc); g_exc[g_nexc++] = optarg; break;
        case 'Y': g_excd = xrealloc(g_excd, (g_nexcd + 1) * sizeof *g_excd); g_excd[g_nexcd++] = optarg; break;
        case 'A': case 'B': case 'C': {
            unsigned long n;
            if (!parse_ulong(optarg, &n)) die(2, "%s: invalid context length argument", optarg);
            if (c != 'B') g_after = n;
            if (c != 'A') g_before = n;
            g_ctx = 1;
            break;
        }
        case 'u': g_lbuf = 1; break;
        case 'V': printf("grep (rifle) " VERSION "\n"); return 0;
        }
    }
    if (!havepat) {
        if (!nops) { say("no pattern given"); usage_error(); }
        p = ops[0];
        pbuf = xmalloc(strlen(p) + 2);
        plen = strlen(p);
        memcpy(pbuf, p, plen); pbuf[plen++] = '\n';
        ops++; nops--;
    }
    g_re = re_new(icase);
    for (p = pbuf; p < pbuf + plen; p = e + 1) {
        struct rnode *n;
        e = memchr(p, '\n', pbuf + plen - p);
        n = fixed ? re_literal(g_re, p, e - p) : re_parse(g_re, p, e - p, ere, 0);
        all = all ? rn(N_ALT, all, n) : n;
    }
    if (!all) { all = rn(N_SET, NULL, NULL); all->c = re_newset(g_re); }
    if (x) all = rn(N_CAT, re_assert(R_BOL), rn(N_CAT, all, re_assert(R_EOL)));
    else if (w) all = rn(N_CAT, re_assert(R_BEFORE), rn(N_CAT, all, re_assert(R_AFTER)));
    re_finish(g_re, all);
    g_caps = xmalloc(2 * (g_re->ngrp + 1) * sizeof *g_caps);
    if (g_name == -2) g_name = nops > 1 ? 1 : g_recurse ? -1 : 0;
    if (g_max == 0) return 1;
    if (!nops) {
        grep_path(g_recurse ? g_dot : "-", 1);
    } else {
        int i;
        for (i = 0; i < nops; i++) grep_path(ops[i], 1);
    }
    if (finish_stdout()) return 2;
    return g_err ? 2 : g_any ? 0 : 1;
}

/* ---- the table and main ---- */

struct applet { const char *name; int (*fn)(int, char **); const char *usage; const char *about; };

static const struct applet applets[] = {
    { "[", t_test, "EXPRESSION ]", "Check file types and compare values." },
    { "base32", t_base, "[OPTION]... [FILE]", "Base32 encode or decode FILE." },
    { "base64", t_base, "[OPTION]... [FILE]", "Base64 encode or decode FILE." },
    { "chroot", t_chroot, "[OPTION] NEWROOT [COMMAND [ARG]...]", "Run COMMAND with root directory set to NEWROOT." },
    { "cksum", t_cksum, "[OPTION]... [FILE]...", "Print CRC checksum and byte counts of each FILE." },
    { "date", t_date, "[OPTION]... [+FORMAT]", "Print the system date and time." },
    { "dd", t_dd, "[OPERAND]...", "Copy a file, converting and formatting according to the operands." },
    { "df", t_df, "[OPTION]... [FILE]...", "Show information about the file system on which each FILE resides." },
    { "du", t_du, "[OPTION]... [FILE]...", "Summarize device usage of the set of FILEs, recursively for directories." },
    { "expand", t_expand, "[OPTION]... [FILE]...", "Convert tabs in each FILE to spaces." },
    { "expr", t_expr, "EXPRESSION", "Print the value of EXPRESSION to standard output." },
    { "factor", t_factor, "[NUMBER]...", "Print the prime factors of each NUMBER." },
    { "egrep", t_grep, "[OPTION]... PATTERNS [FILE]...", "Search for PATTERNS in each FILE." },
    { "fgrep", t_grep, "[OPTION]... PATTERNS [FILE]...", "Search for PATTERNS in each FILE." },
    { "grep", t_grep, "[OPTION]... PATTERNS [FILE]...", "Search for PATTERNS in each FILE." },
    { "groups", t_id, "[OPTION]... [USERNAME]...", "Print the groups a user is in." },
    { "id", t_id, "[OPTION]... [USER]", "Print user and group information for the specified USER, or for the current user." },
    { "install", t_install, "[OPTION]... SOURCE... DIRECTORY", "Copy files and set attributes." },
    { "join", t_join, "[OPTION]... FILE1 FILE2", "For each pair of input lines with identical join fields, write a line to standard output." },
    { "ls", t_ls, "[OPTION]... [FILE]...", "List information about the FILEs (the current directory by default)." },
    { "md5sum", t_digest, "[OPTION]... [FILE]...", "Print or check MD5 (128-bit) checksums." },
    { "nice", t_nice, "[OPTION] [COMMAND [ARG]...]", "Run COMMAND with an adjusted niceness." },
    { "nohup", t_nohup, "COMMAND [ARG]...", "Run COMMAND, ignoring hangup signals." },
    { "od", t_od, "[OPTION]... [FILE]...", "Write an unambiguous representation, octal bytes by default, of FILE to standard output." },
    { "sha1sum", t_digest, "[OPTION]... [FILE]...", "Print or check SHA1 (160-bit) checksums." },
    { "sha224sum", t_digest, "[OPTION]... [FILE]...", "Print or check SHA224 (224-bit) checksums." },
    { "sha256sum", t_digest, "[OPTION]... [FILE]...", "Print or check SHA256 (256-bit) checksums." },
    { "sha384sum", t_digest, "[OPTION]... [FILE]...", "Print or check SHA384 (384-bit) checksums." },
    { "sha512sum", t_digest, "[OPTION]... [FILE]...", "Print or check SHA512 (512-bit) checksums." },
    { "shuf", t_shuf, "[OPTION]... [FILE]", "Write a random permutation of the input lines to standard output." },
    { "split", t_split, "[OPTION]... [FILE [PREFIX]]", "Output pieces of FILE to PREFIXaa, PREFIXab, ..." },
    { "stat", t_stat, "[OPTION]... FILE...", "Display file or file system status." },
    { "sum", t_cksum, "[OPTION]... [FILE]...", "Print checksum and block counts for each FILE." },
    { "test", t_test, "EXPRESSION", "Check file types and compare values." },
    { "timeout", t_timeout, "[OPTION] DURATION COMMAND [ARG]...", "Start COMMAND, and kill it if still running after DURATION." },
    { "truncate", t_truncate, "OPTION... FILE...", "Shrink or extend the size of each FILE to the specified size." },
    { "tsort", t_tsort, "[OPTION] [FILE]", "Write totally ordered list consistent with the partial ordering in FILE." },
    { "unexpand", t_expand, "[OPTION]... [FILE]...", "Convert spaces in each FILE to tabs." },
    { "arch", t_uname, "[OPTION]...", "Print the machine hardware name." },
    { "basename", t_basename, "NAME [SUFFIX] | OPTION... NAME...", "Print NAME without any leading directory components, and SUFFIX if it ends the name." },
    { "cat", t_cat, "[OPTION]... [FILE]...", "Concatenate FILE(s) to standard output." },
    { "chgrp", t_chown, "[OPTION]... GROUP FILE...", "Change the group of each FILE to GROUP." },
    { "chmod", t_chmod, "[OPTION]... MODE[,MODE]... FILE...", "Change the mode of each FILE to MODE." },
    { "chown", t_chown, "[OPTION]... [OWNER][:[GROUP]] FILE...", "Change the owner and/or group of each FILE." },
    { "comm", t_comm, "[OPTION]... FILE1 FILE2", "Compare sorted files FILE1 and FILE2 line by line." },
    { "cp", t_cp, "[OPTION]... SOURCE... DEST", "Copy SOURCE to DEST, or several SOURCEs to a DIRECTORY." },
    { "cut", t_cut, "OPTION... [FILE]...", "Print selected parts of lines from each FILE." },
    { "dirname", t_dirname, "[OPTION] NAME...", "Print NAME with its last component removed." },
    { "echo", t_echo, "[-neE] [STRING]...", "Print the STRINGs, separated by spaces." },
    { "env", t_env, "[OPTION]... [-] [NAME=VALUE]... [COMMAND [ARG]...]", "Run COMMAND in a changed environment, or print the environment." },
    { "false", t_true, "", "Do nothing, unsuccessfully." },
    { "fold", t_fold, "[OPTION]... [FILE]...", "Wrap each input line to fit in a given width." },
    { "head", t_head, "[OPTION]... [FILE]...", "Print the first 10 lines of each FILE." },
    { "hostid", t_hostid, "", "Print the numeric identifier of this machine." },
    { "hostname", t_hostname, "[NAME]", "Print the name of this machine, or set it to NAME." },
    { "link", t_link, "FILE1 FILE2", "Make a hard link named FILE2 to FILE1." },
    { "ln", t_ln, "[OPTION]... TARGET [LINK_NAME]", "Make links between files." },
    { "logname", t_logname, "", "Print the name of the user who logged in." },
    { "mkdir", t_mkdir, "[OPTION]... DIRECTORY...", "Create the DIRECTORY(ies), if they do not already exist." },
    { "mkfifo", t_mkfifo, "[OPTION]... NAME...", "Create named pipes (FIFOs) with the given NAMEs." },
    { "mknod", t_mknod, "[OPTION]... NAME TYPE [MAJOR MINOR]", "Create the special file NAME of the given TYPE." },
    { "mktemp", t_mktemp, "[OPTION]... [TEMPLATE]", "Create a temporary file or directory and print its name." },
    { "mv", t_mv, "[OPTION]... SOURCE... DEST", "Rename SOURCE to DEST, or move SOURCEs to a DIRECTORY." },
    { "nl", t_nl, "[OPTION]... [FILE]", "Number the lines of FILE." },
    { "nproc", t_nproc, "[OPTION]...", "Print the number of processing units available." },
    { "paste", t_paste, "[OPTION]... [FILE]...", "Merge lines of files side by side." },
    { "printf", t_printf, "FORMAT [ARGUMENT]...", "Print ARGUMENT(s) according to FORMAT." },
    { "printenv", t_printenv, "[OPTION]... [VARIABLE]...", "Print the values of the given environment VARIABLEs, or all of them." },
    { "pwd", t_pwd, "[OPTION]...", "Print the name of the current working directory." },
    { "readlink", t_readlink, "[OPTION]... FILE...", "Print the value of a symbolic link, or a file's canonical name." },
    { "realpath", t_realpath, "[OPTION]... FILE...", "Print the resolved absolute file name." },
    { "rm", t_rm, "[OPTION]... [FILE]...", "Remove (unlink) the FILE(s)." },
    { "rmdir", t_rmdir, "[OPTION]... DIRECTORY...", "Remove the DIRECTORY(ies), if they are empty." },
    { "seq", t_seq, "[OPTION]... [FIRST [INCREMENT]] LAST", "Print numbers from FIRST to LAST in steps of INCREMENT." },
    { "sleep", t_sleep, "NUMBER[SUFFIX]...", "Pause for NUMBER seconds. SUFFIX may be s, m, h or d." },
    { "sort", t_sort, "[OPTION]... [FILE]...", "Write the sorted concatenation of all FILEs to standard output." },
    { "sync", t_sync, "[OPTION] [FILE]...", "Flush file system buffers." },
    { "tac", t_tac, "[OPTION]... [FILE]...", "Print each FILE last line first." },
    { "tail", t_tail, "[OPTION]... [FILE]...", "Print the last 10 lines of each FILE." },
    { "tee", t_tee, "[OPTION]... [FILE]...", "Copy standard input to each FILE, and also to standard output." },
    { "touch", t_touch, "[OPTION]... FILE...", "Update the access and modification times of each FILE." },
    { "tr", t_tr, "[OPTION]... SET1 [SET2]", "Translate, squeeze, and/or delete characters from standard input." },
    { "true", t_true, "", "Do nothing, successfully." },
    { "tty", t_tty, "[OPTION]...", "Print the file name of the terminal connected to standard input." },
    { "uname", t_uname, "[OPTION]...", "Print system information." },
    { "uniq", t_uniq, "[OPTION]... [INPUT [OUTPUT]]", "Filter adjacent matching lines." },
    { "unlink", t_unlink, "FILE", "Remove FILE with the unlink call." },
    { "wc", t_wc, "[OPTION]... [FILE]...", "Print newline, word, and byte counts for each FILE." },
    { "whoami", t_whoami, "", "Print the user name for the current effective user ID." },
    { "yes", t_yes, "[STRING]...", "Repeatedly output a line with all specified STRING(s), or 'y'." },
};
#define NAPPLETS ((int)(sizeof applets / sizeof applets[0]))

static const struct applet *find_applet(const char *name)
{
    int i;
    for (i = 0; i < NAPPLETS; i++) if (!strcmp(applets[i].name, name)) return &applets[i];
    return NULL;
}

/* rifle --install DIR: a link to this program for every tool, relative if it lives in DIR too */
static int install_links(const char *dir)
{
    char exe[PATH_MAX], target[PATH_MAX], link_path[PATH_MAX], *slash;
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    int i, st = 0;
    if (n < 0) { perr("cannot find myself"); return 1; }
    exe[n] = 0;
    slash = strrchr(exe, '/');
    {
        char want[PATH_MAX];
        realpath(dir, want);
        if (slash && (size_t)(slash - exe) == strlen(want) && !strncmp(exe, want, slash - exe)) snprintf(target, sizeof target, "%s", slash + 1);
        else snprintf(target, sizeof target, "%s", exe);
    }
    for (i = 0; i < NAPPLETS; i++) {
        snprintf(link_path, sizeof link_path, "%s/%s", dir, applets[i].name);
        unlink(link_path);
        if (symlink(target, link_path) < 0) { perr("cannot make '%s'", link_path); st = 1; }
    }
    return st;
}

int main(int argc, char **argv)
{
    const char *name = base_of(argv[0]);
    const struct applet *a;
    int rc;
    if (!strcmp(name, "rifle")) {
        /* run as itself, not through a link: a few things of its own, and "rifle tool args" for trying one out */
        if (argc >= 2 && !strcmp(argv[1], "--list")) { int i; for (i = 0; i < NAPPLETS; i++) puts(applets[i].name); return finish_stdout(); }
        if (argc >= 3 && !strcmp(argv[1], "--install")) return install_links(argv[2]);
        if (argc >= 2 && !strcmp(argv[1], "--version")) { puts("rifle " VERSION); return finish_stdout(); }
        if (argc < 2 || !strcmp(argv[1], "--help")) {
            puts("rifle " VERSION ": coreutils in one program. cat, ls and the rest are links to it.\n"
                 "  rifle --install DIR    make the links in DIR\n"
                 "  rifle --list           the tools in here");
            return finish_stdout();
        }
        argv++; argc--;
        name = base_of(argv[0]);
    }
    if (!(a = find_applet(name))) { prog = name; die(127, "not a tool in rifle"); }
    prog = a->name; tool_usage = a->usage; tool_about = a->about;
    argv[0] = (char *)a->name;
    rc = a->fn(argc, argv);
    if (finish_stdout()) rc = rc ? rc : 1;
    return rc;
}

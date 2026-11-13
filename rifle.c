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
            if (p[1] == ':') {
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

/* ---- the table and main ---- */

struct applet { const char *name; int (*fn)(int, char **); const char *usage; const char *about; };

static const struct applet applets[] = {
    { "arch", t_uname, "[OPTION]...", "Print the machine hardware name." },
    { "basename", t_basename, "NAME [SUFFIX] | OPTION... NAME...", "Print NAME without any leading directory components, and SUFFIX if it ends the name." },
    { "cat", t_cat, "[OPTION]... [FILE]...", "Concatenate FILE(s) to standard output." },
    { "chgrp", t_chown, "[OPTION]... GROUP FILE...", "Change the group of each FILE to GROUP." },
    { "chmod", t_chmod, "[OPTION]... MODE[,MODE]... FILE...", "Change the mode of each FILE to MODE." },
    { "chown", t_chown, "[OPTION]... [OWNER][:[GROUP]] FILE...", "Change the owner and/or group of each FILE." },
    { "comm", t_comm, "[OPTION]... FILE1 FILE2", "Compare sorted files FILE1 and FILE2 line by line." },
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
    { "nl", t_nl, "[OPTION]... [FILE]", "Number the lines of FILE." },
    { "nproc", t_nproc, "[OPTION]...", "Print the number of processing units available." },
    { "paste", t_paste, "[OPTION]... [FILE]...", "Merge lines of files side by side." },
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

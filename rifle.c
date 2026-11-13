/* rifle: coreutils in one C file, on c.h. nothing from GNU in it, not a line.
 *
 * it's one program with a lot of names. cat, ls, cp and the rest are links to it, and it runs whichever one it was
 * called as. you type cat, never rifle cat. build.sh makes the links in bin/, and "rifle --install /bin" makes them
 * anywhere else. "rifle --list" says what's in here.
 *
 * build:  ./build.sh        test:  tests/run.sh   (every case runs under GNU and under rifle and compares)
 *
 * how it's laid out: the shared parts first (messages, options, modes, users), then the tools in the order of the
 * table at the bottom, then main. a tool is a function t_name(argc, argv) that returns its exit status. */
#include "c.h"

#define VERSION "0.2"

/* ======================================================================= messages */

static const char *prog = "rifle";   /* the tool that's running: what the messages start with */
static const char *tool_usage;       /* what --help says after "Usage: name" */
static const char *tool_about;       /* and the line under it */
static int status;                   /* the exit status so far: a tool sets it to 1 when something failed and goes on
                                      * with the next file, like the real ones do */
static int usage_status = 1;         /* what a bad option exits with. 1 for most, env and a few others use 125 */

static void say(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s: ", prog);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
}
/* say(), with ": what went wrong" from errno at the end */
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

/* write all of it, or say why not and return -1 */
static int write_all(int fd, const char *buf, size_t n)
{
    while (n) {
        ssize_t w = write(fd, buf, n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        buf += w; n -= w;
    }
    return 0;
}
/* printf and friends write to stdout through a buffer: this is how a tool finds out at the end that it didn't work */
static int finish_stdout(void)
{
    if (fflush(stdout) == EOF || ferror(stdout)) { say("write error: %s", strerror(errno ? errno : EIO)); return 1; }
    return 0;
}

/* ======================================================================= options
 * what getopt_long does, written for rifle: options anywhere between the operands (unless the short list starts
 * with +, then the first operand ends them), -abc and -s5 and -s 5, --name=value and --name value, a unique start of
 * a long name will do, -- ends them. --help and --version work for every tool. what's not an option ends up in ops. */

struct opt { const char *name; int arg; int val; };   /* arg: 0 none, 1 needs one, 2 may have one (--name=x only) */
static char **ops;            /* the operands, after the options are done */
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
                if (!a[2]) { opt_stop = 1; opt_i++; continue; }          /* a bare -- */
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

/* ======================================================================= small helpers */

/* a whole number, nothing after it. returns 0 on trouble */
static int parse_ulong(const char *s, unsigned long *out)
{
    char *end;
    if (!isdigit((unsigned char)*s)) return 0;
    errno = 0;
    *out = strtoul(s, &end, 10);
    return !*end && errno != ERANGE;
}

/* user and group names, or the number when there's no name for it */
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
/* "root" or "0" to a uid. -1 if there's no such user */
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

/* ======================================================================= modes
 * chmod's mode language: 755, or u=rwx,g=rx,o= and u+x and a-w and g=u and +X and o+t, comma separated.
 * a mode is compiled into a list of changes, then applied to the file's old mode. */

struct change { char op; char flag; mode_t affected, value, mentioned; };
#define MODE_ORDINARY 0
#define MODE_COPY 1       /* g=u: take the bits from another class */
#define MODE_X_IF_ANY 2   /* X: x, but only on directories and files that have an x already */
#define MODE_BITS 07777

/* returns a malloc'd list ended by op 0, or NULL if it's not a mode */
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

/* "rwxr-xr-x" for a mode, with s S t T for the special bits */
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

/* the next word of a line (spaces and tabs between them), cut off with a 0. *p moves on. NULL at the end */
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

/* a directory entry, walked with the fds so a long path or a swapped-in symlink can't confuse it */
static const char *base_of(const char *p)
{
    const char *s = strrchr(p, '/');
    return s && s[1] ? s + 1 : p;
}

/* ======================================================================= true false */

static int t_true(int argc, char **argv)
{
    /* they ignore everything, except as the only argument: --help and --version still work */
    if (argc == 2 && !strcmp(argv[1], "--help")) { print_usage(); return prog[0] == 'f'; }
    if (argc == 2 && !strcmp(argv[1], "--version")) { printf("%s (rifle) " VERSION "\n", prog); return prog[0] == 'f'; }
    return prog[0] == 'f';
}

/* ======================================================================= echo */

static int t_echo(int argc, char **argv)
{
    int nl = 1, esc = 0, i = 1, first;
    if (argc == 2 && !strcmp(argv[1], "--help")) { print_usage(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--version")) { printf("echo (rifle) " VERSION "\n"); return 0; }
    for (; i < argc; i++) {                       /* -n -e -E, bundled, but only if every letter is one of them */
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
            case 'c': return finish_stdout();       /* \c: stop, and no newline either */
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

/* ======================================================================= yes */

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
    /* fill the buffer with copies of the line, so each write is a big one */
    for (i = len; i + len <= sizeof buf; i += len) memcpy(buf + i, buf, len);
    len = i;
    for (;;) if (write_all(1, buf, len) < 0) { perr("standard output"); return 1; }
}

/* ======================================================================= cat */

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

/* ======================================================================= pwd */

static int t_pwd(int argc, char **argv)
{
    static const struct opt lo[] = { { "logical", 0, 'L' }, { "physical", 0, 'P' }, { NULL, 0, 0 } };
    int c, logical = getenv("POSIXLY_CORRECT") != NULL;
    char buf[PATH_MAX], *w;
    opts_reset();
    while ((c = getopts(argc, argv, "LP", lo))) logical = c == 'L';
    if (nops) say("ignoring non-option arguments");
    if (logical && (w = getenv("PWD")) && w[0] == '/') {
        /* $PWD counts if it names this very directory and has no . or .. in it */
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

/* ======================================================================= sleep */

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

/* ======================================================================= basename dirname */

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
        while (n > 1 && s[n - 1] == '/') s[--n] = 0;       /* trailing slashes don't count, but "/" stays */
        b = n > 1 || s[0] != '/' ? (strrchr(s, '/') ? strrchr(s, '/') + 1 : s) : s;
        if (b[0] == '/' && !b[1]) { /* the root */ }
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
        if (k == 0) fputs(".", stdout);                     /* no slash at all */
        else {
            while (k > 1 && s[k - 1] == '/') k--;
            fwrite(s, 1, k, stdout);
        }
        putchar(zero ? 0 : '\n');
    }
    return finish_stdout();
}

/* ======================================================================= sync */

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

/* ======================================================================= uname arch hostname */

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
        if (all) s = n = r = v = m = o = 1;       /* -a leaves out -p and -i when there's nothing to say for them */
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

/* ======================================================================= printenv env */

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
    /* "-" is -i. everything after the options up to the first thing that isn't NAME=VALUE is the command */
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

/* ======================================================================= link unlink */

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

/* ======================================================================= tty nproc whoami logname hostid */

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

static unsigned long omp_value(const char *name)     /* the first number of $OMP_NUM_THREADS and the like, or 0 */
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
    if (all) {                                    /* every cpu the kernel knows about: the cpuN entries in sysfs */
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
        if (!all && (n = omp_value("OMP_NUM_THREADS"))) goto done;       /* OpenMP's override wins, as it does for GNU */
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
    /* the kernel remembers who logged in on this session, in /proc/self/loginuid */
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
        /* the way glibc does it without /etc/hostid: the IPv4 address of our name in /etc/hosts, halves swapped */
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

/* ======================================================================= walking trees
 * chmod -R, chown -R: children first, then the directory itself (so chmod 000 on a tree works). fn is called for
 * everything, with the stat of what it is: a symlink that's not followed shows up as a symlink. */

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

/* ======================================================================= mkdir rmdir */

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
    if (change) mode = mode_apply(0777 & ~um, 1, um, change);   /* changes start from what the umask leaves */
    for (i = 0; i < nops; i++) {
        char *path = xstrdup(ops[i]), *p;
        struct stat sb;
        if (parents) {
            /* every missing directory on the way gets made, with the usual mode and u+wx so we can go on into it */
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
            if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) goto next;   /* mkdir -p on one that's there is fine */
        }
        if (change) umask(0);
        if (mkdir(path, change ? (mode & 0777) : 0777) < 0) {
            if (change) umask(um);
            perr("cannot create directory '%s'", path); st = 1; goto next;
        }
        if (change) {
            umask(um);
            if (mode & ~0777) chmod(path, mode);       /* mkdir leaves the sticky and set-id bits to chmod */
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
            while (slash > dir && slash[-1] == '/') slash--;      /* the slashes at the end, then the name before them */
            while (slash > dir && slash[-1] != '/') slash--;
            while (slash > dir && slash[-1] == '/') slash--;
            if (slash == dir) break;
            *slash = 0;
        }
        free(dir);
    }
    return finish_stdout() ? 1 : st;
}

/* ======================================================================= rm */

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
            if (bad) return 1;                      /* something inside stayed, so the directory can't go */
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
        /* the root first of all, before anything is touched: "rm -rf /" is the one mistake that can't be undone */
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

/* ======================================================================= chmod chown chgrp */

struct chmod_ctx { const struct change *change; int verbose, changes_only, quiet; mode_t um; };

static int chmod_visit(const char *path, const struct stat *st, void *v)
{
    struct chmod_ctx *x = v;
    mode_t old = st->st_mode & MODE_BITS, new;
    char a[10], b[10];
    if (S_ISLNK(st->st_mode)) return 0;                    /* links met while walking are left alone */
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
    /* chmod's options are R c f v and the long ones. anything else starting with - is a mode, like -w or -rw */
    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (ends || a[0] != '-' || !a[1]) {                 /* an operand: the mode first, then the files */
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
        if (modestr) files[nfiles++] = modestr;                 /* with --reference the first operand was a file */
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
    else if (!colon && strchr(s, '.') && lookup_user(s, &x->u) < 0) colon = strchr(s, '.');   /* the old user.group */
    if (colon) { *colon = 0; g = colon + 1; }
    if (u && *u) {
        if (lookup_user(u, &x->u) < 0) { say("invalid user: '%s'", spec); return -1; }
        x->have_u = 1;
    }
    if (g && *g) {
        if (lookup_group(g, &x->g) < 0) { say("invalid group: '%s'", spec); return -1; }
        x->have_g = 1;
    } else if (colon && x->have_u) {                  /* "user:" means the user's own group */
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

/* ======================================================================= mkfifo mknod */

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

/* ======================================================================= the table and main */

struct applet { const char *name; int (*fn)(int, char **); const char *usage; const char *about; };

static const struct applet applets[] = {
    { "arch", t_uname, "[OPTION]...", "Print the machine hardware name." },
    { "basename", t_basename, "NAME [SUFFIX] | OPTION... NAME...", "Print NAME without any leading directory components, and SUFFIX if it ends the name." },
    { "cat", t_cat, "[OPTION]... [FILE]...", "Concatenate FILE(s) to standard output." },
    { "chgrp", t_chown, "[OPTION]... GROUP FILE...", "Change the group of each FILE to GROUP." },
    { "chmod", t_chmod, "[OPTION]... MODE[,MODE]... FILE...", "Change the mode of each FILE to MODE." },
    { "chown", t_chown, "[OPTION]... [OWNER][:[GROUP]] FILE...", "Change the owner and/or group of each FILE." },
    { "dirname", t_dirname, "[OPTION] NAME...", "Print NAME with its last component removed." },
    { "echo", t_echo, "[-neE] [STRING]...", "Print the STRINGs, separated by spaces." },
    { "env", t_env, "[OPTION]... [-] [NAME=VALUE]... [COMMAND [ARG]...]", "Run COMMAND in a changed environment, or print the environment." },
    { "false", t_true, "", "Do nothing, unsuccessfully." },
    { "hostid", t_hostid, "", "Print the numeric identifier of this machine." },
    { "hostname", t_hostname, "[NAME]", "Print the name of this machine, or set it to NAME." },
    { "link", t_link, "FILE1 FILE2", "Make a hard link named FILE2 to FILE1." },
    { "logname", t_logname, "", "Print the name of the user who logged in." },
    { "mkdir", t_mkdir, "[OPTION]... DIRECTORY...", "Create the DIRECTORY(ies), if they do not already exist." },
    { "mkfifo", t_mkfifo, "[OPTION]... NAME...", "Create named pipes (FIFOs) with the given NAMEs." },
    { "mknod", t_mknod, "[OPTION]... NAME TYPE [MAJOR MINOR]", "Create the special file NAME of the given TYPE." },
    { "nproc", t_nproc, "[OPTION]...", "Print the number of processing units available." },
    { "printenv", t_printenv, "[OPTION]... [VARIABLE]...", "Print the values of the given environment VARIABLEs, or all of them." },
    { "pwd", t_pwd, "[OPTION]...", "Print the name of the current working directory." },
    { "rm", t_rm, "[OPTION]... [FILE]...", "Remove (unlink) the FILE(s)." },
    { "rmdir", t_rmdir, "[OPTION]... DIRECTORY...", "Remove the DIRECTORY(ies), if they are empty." },
    { "sleep", t_sleep, "NUMBER[SUFFIX]...", "Pause for NUMBER seconds. SUFFIX may be s, m, h or d." },
    { "sync", t_sync, "[OPTION] [FILE]...", "Flush file system buffers." },
    { "true", t_true, "", "Do nothing, successfully." },
    { "tty", t_tty, "[OPTION]...", "Print the file name of the terminal connected to standard input." },
    { "uname", t_uname, "[OPTION]...", "Print system information." },
    { "unlink", t_unlink, "FILE", "Remove FILE with the unlink call." },
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

/* rifle --install DIR: a link for every tool, pointing at this program. relative if the program is in DIR too */
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

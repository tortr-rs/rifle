/* c.h: the whole libc of Chamesle Source Linux, in one file. x86-64 Linux only.
 * this copy lives in rifle and has grown: directories, links, users and groups, the errno messages, a real
 * malloc, strftime, setenv. what rifle added is marked [rifle]. nothing the original had was taken out.
 * include it once (in one .c file) and you get everything: the start-up code, system calls, strings, memory,
 * files and printf. it builds with tcc (your programs) and gcc (vi and tcc themselves). */
#ifndef C_H
#define C_H

/* ---- types ---- */
typedef unsigned long size_t;
typedef long ssize_t;
typedef long ptrdiff_t;
typedef long intptr_t;
typedef unsigned long uintptr_t;
typedef long off_t;
typedef long time_t;
typedef int pid_t;
typedef int mode_t;
typedef unsigned int uid_t;    /* [rifle] */
typedef unsigned int gid_t;
typedef unsigned long dev_t;
typedef unsigned long ino_t;
typedef unsigned long nlink_t;
typedef long clock_t;
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef long long intmax_t;
typedef unsigned long long uintmax_t;
typedef int wchar_t_c; /* not wchar_t: tcc and gcc both predefine their own */
#define NULL ((void *)0)
#define offsetof(t, m) ((size_t)&((t *)0)->m)
#define CHAR_BIT 8
#define INT_MAX 2147483647
#define INT_MIN (-INT_MAX - 1)
#define UINT_MAX 4294967295U
#define LONG_MAX 9223372036854775807L
#define LONG_MIN (-LONG_MAX - 1)
#define ULONG_MAX 18446744073709551615UL
#define LLONG_MAX LONG_MAX
#define LLONG_MIN LONG_MIN
#define ULLONG_MAX ULONG_MAX
#define PATH_MAX 4096
#define NAME_MAX 255
#define SIZE_MAX ULONG_MAX
#define SSIZE_MAX LONG_MAX
#define SHRT_MAX 32767
#define UINT32_MAX 4294967295U
#define INT64_MAX LONG_MAX
#define INT64_MIN LONG_MIN
#define UINT64_MAX ULONG_MAX
#ifndef __cplusplus
typedef _Bool bool;
#define true 1
#define false 0
#endif
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, t) __builtin_va_arg(ap, t)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(d, s) __builtin_va_copy(d, s)

/* ---- system calls ----
 * sys6(n, a..f): the number goes in rax, the arguments in rdi rsi rdx r10 r8 r9. written in plain asm so it
 * builds the same under gcc and tcc. returns -errno on failure, like the kernel */
long sys6(long n, long a, long b, long c, long d, long e, long f);
__asm__(".text\n.global sys6\nsys6:\n"
        " mov %rdi,%rax\n mov %rsi,%rdi\n mov %rdx,%rsi\n mov %rcx,%rdx\n mov %r8,%r10\n mov %r9,%r8\n"
        " mov 8(%rsp),%r9\n syscall\n ret\n");
#define sys0(n) sys6(n, 0, 0, 0, 0, 0, 0)
#define sys1(n, a) sys6(n, (long)(a), 0, 0, 0, 0, 0)
#define sys2(n, a, b) sys6(n, (long)(a), (long)(b), 0, 0, 0, 0)
#define sys3(n, a, b, c) sys6(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)

#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_lseek 8
#define SYS_mmap 9
#define SYS_mprotect 10
#define SYS_munmap 11
#define SYS_ioctl 16
#define SYS_fork 57
#define SYS_execve 59
#define SYS_exit 60
#define SYS_wait4 61
#define SYS_getcwd 79
#define SYS_mkdir 83
#define SYS_unlink 87
#define SYS_mount 165
#define SYS_time 201
#define SYS_gettimeofday 96
#define SYS_stat 4
#define SYS_fstat 5
#define SYS_brk 12
#define SYS_rt_sigaction 13
#define SYS_rt_sigreturn 15
#define SYS_getpid 39
#define SYS_chdir 80
#define SYS_creat 85
#define SYS_chmod 90
#define SYS_pipe 22
#define SYS_dup 32
#define SYS_getuid 102

/* errno: the libc keeps the last error here. one program, one thread: a plain variable does it */
int errno;
#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define ETXTBSY 26
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define EDOM 33
#define ERANGE 34
#define EDEADLK 35
#define ENAMETOOLONG 36
#define ENOSYS 38
#define ENOTEMPTY 39
#define ELOOP 40
#define ENODATA 61
#define EOVERFLOW 75
#define EILSEQ 84
#define EOPNOTSUPP 95
#define ETIMEDOUT 110
#define ESTALE 116
#define EDQUOT 122
static long fail(long r) { if (r < 0 && r > -4096) { errno = -r; return -1; } return r; }

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_EXCL 0200          /* [rifle] */
#define O_NOCTTY 0400
#define O_NONBLOCK 04000
#define O_SYNC 04010000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW 0400000
#define O_CLOEXEC 02000000
#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define _SC_PAGESIZE 30

ssize_t read(int fd, void *buf, size_t n) { return fail(sys3(SYS_read, fd, buf, n)); }
ssize_t write(int fd, const void *buf, size_t n) { return fail(sys3(SYS_write, fd, buf, n)); }
int open(const char *path, int flags, ...)
{
    va_list ap; int mode;
    va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
    return fail(sys3(SYS_open, path, flags, flags & O_CREAT ? mode : 0));
}
int close(int fd) { return fail(sys1(SYS_close, fd)); }
off_t lseek(int fd, off_t off, int whence) { return fail(sys3(SYS_lseek, fd, off, whence)); }
int ioctl(int fd, unsigned long req, void *arg) { return fail(sys3(SYS_ioctl, fd, req, arg)); }
int mprotect(void *p, size_t n, int prot) { return fail(sys3(SYS_mprotect, p, n, prot)); }
int unlink(const char *path) { return fail(sys1(SYS_unlink, path)); }
int mkdir(const char *path, mode_t mode) { return fail(sys2(SYS_mkdir, path, mode)); }
pid_t fork(void) { return fail(sys0(SYS_fork)); }
int execve(const char *path, char *const argv[], char *const envp[]) { return fail(sys3(SYS_execve, path, argv, envp)); }
pid_t waitpid(pid_t pid, int *status, int options) { return fail(sys6(SYS_wait4, pid, (long)status, options, 0, 0, 0)); }
int mount(const char *src, const char *dst, const char *type, unsigned long flags, const void *data)
{
    return fail(sys6(SYS_mount, (long)src, (long)dst, (long)type, flags, (long)data, 0));
}
char *getcwd(char *buf, size_t n) { return fail(sys2(SYS_getcwd, buf, n)) < 0 ? NULL : buf; }
time_t time(time_t *t) { time_t now = sys1(SYS_time, 0); if (t) *t = now; return now; }
long sysconf(int name) { return name == _SC_PAGESIZE ? 4096 : -1; }
int creat(const char *path, mode_t mode) { return fail(sys2(SYS_creat, path, mode)); }
int chdir(const char *path) { return fail(sys1(SYS_chdir, path)); }
int chmod(const char *path, mode_t mode) { return fail(sys2(SYS_chmod, path, mode)); }
int dup(int fd) { return fail(sys1(SYS_dup, fd)); }
int pipe(int fd[2]) { return fail(sys1(SYS_pipe, fd)); }
pid_t getpid(void) { return sys0(SYS_getpid); }
int getuid(void) { return sys0(SYS_getuid); }
pid_t wait(int *status) { return waitpid(-1, status, 0); }

/* the end of the data segment, the "break". brk(p) moves it to p, sbrk(n) moves it n bytes and returns where it was */
static char *cur_brk;
int brk(void *p)
{
    char *r = (char *)sys1(SYS_brk, p);
    cur_brk = r;
    if (r != p) { errno = ENOMEM; return -1; }
    return 0;
}
void *sbrk(long n)
{
    char *old;
    if (!cur_brk) cur_brk = (char *)sys1(SYS_brk, 0);
    old = cur_brk;
    if (n && brk(old + n) < 0) return (void *)-1;
    return old;
}

/* ---- files' details ---- */
struct stat {
    unsigned long st_dev, st_ino, st_nlink;
    unsigned int st_mode, st_uid, st_gid, pad0;
    unsigned long st_rdev;
    long st_size, st_blksize, st_blocks;
    unsigned long st_atime, st_atime_nsec, st_mtime, st_mtime_nsec, st_ctime, st_ctime_nsec;
    long unused[3];
};
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFREG 0100000
#define S_IFBLK 0060000     /* [rifle] */
#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 070
#define S_IRGRP 040
#define S_IWGRP 020
#define S_IXGRP 010
#define S_IRWXO 07
#define S_IROTH 04
#define S_IWOTH 02
#define S_IXOTH 01
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
int stat(const char *path, struct stat *st) { return fail(sys2(SYS_stat, path, st)); }
int fstat(int fd, struct stat *st) { return fail(sys2(SYS_fstat, fd, st)); }

/* ---- the terminal ---- */
struct termios { unsigned int c_iflag, c_oflag, c_cflag, c_lflag; unsigned char c_line, c_cc[19]; };
#define TCGETS 0x5401
#define TCSETSW 0x5403
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define ICRNL 0400
#define IXON 02000
#define OPOST 01
#define ONLCR 04
#define TAB3 014000
#define ISIG 01
#define ICANON 02
#define ECHO 010
#define IEXTEN 0100000
int tcgetattr(int fd, struct termios *t) { return ioctl(fd, TCGETS, t); }
int tcsetattr(int fd, int when, struct termios *t) { (void)when; return ioctl(fd, TCSETSW, t); }
int isatty(int fd) { struct termios t; return tcgetattr(fd, &t) == 0; }

/* ---- signals ----
 * signal(n, f): f runs when signal n comes. SIG_DFL (0) puts back what the kernel does, SIG_IGN (1) ignores it.
 * the kernel needs a way back from f: restore_rt, which asks it to carry on where the signal hit */
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGKILL 9
#define SIGTERM 15
#define SIGCHLD 17
void c_restore_rt(void);
__asm__(".text\n.global c_restore_rt\nc_restore_rt:\n mov $15,%rax\n syscall\n");
struct c_sigaction { sighandler_t handler; unsigned long flags; void (*restorer)(void); unsigned long mask; };
sighandler_t signal(int n, sighandler_t f)
{
    struct c_sigaction new = { f, 0x04000000 | 0x10000000 /* restorer, restart */, c_restore_rt, 0 }, old;
    if (fail(sys6(SYS_rt_sigaction, n, (long)&new, (long)&old, 8, 0, 0)) < 0) return SIG_ERR;
    return old.handler;
}
__attribute__((noreturn)) void _exit(int code) { for (;;) sys1(SYS_exit, code); }

/* ---- more system calls [rifle] ---- */
#define SYS_lstat 6
#define SYS_access 21
#define SYS_mremap 25
#define SYS_dup2 33
#define SYS_nanosleep 35
#define SYS_kill 62
#define SYS_uname 63
#define SYS_fcntl 72
#define SYS_fsync 74
#define SYS_fdatasync 75
#define SYS_truncate 76
#define SYS_ftruncate 77
#define SYS_rename 82
#define SYS_rmdir 84
#define SYS_link 86
#define SYS_symlink 88
#define SYS_readlink 89
#define SYS_fchmod 91
#define SYS_chown 92
#define SYS_fchown 93
#define SYS_lchown 94
#define SYS_umask 95
#define SYS_getgid 104
#define SYS_geteuid 107
#define SYS_getegid 108
#define SYS_getppid 110
#define SYS_getgroups 115
#define SYS_mknod 133
#define SYS_sync 162
#define SYS_sethostname 170
#define SYS_sched_getaffinity 204
#define SYS_getdents64 217
#define SYS_clock_gettime 228
#define SYS_utimensat 280
#define SYS_openat 257
#define SYS_mkdirat 258
#define SYS_newfstatat 262
#define SYS_unlinkat 263
#define SYS_syncfs 306
#define SYS_statfs 137
#define SYS_fstatfs 138
#define SYS_getrandom 318
#define SYS_fchdir 81
#define SYS_pread64 17
#define SYS_pwrite64 18
#define SYS_sysinfo 99
#define SYS_chroot 161
#define SYS_setuid 105
#define SYS_setgid 106
#define SYS_times 100
#define SYS_getpgid 121
#define SYS_setsid 112
#define SYS_sendfile 40
#define SYS_copy_file_range 326
#define F_GETFL 3
#define F_SETFL 4
#define F_GETFD 1
#define F_SETFD 2
#define FD_CLOEXEC 1
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
#define UTIME_NOW ((1L << 30) - 1L)
#define UTIME_OMIT ((1L << 30) - 2L)
struct timespec { long tv_sec, tv_nsec; };
struct utsname { char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65]; };
struct statfs {
    long f_type, f_bsize;
    unsigned long f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int f_fsid[2];
    long f_namelen, f_frsize, f_flags, f_spare[4];
};
int lstat(const char *path, struct stat *st) { return fail(sys2(SYS_lstat, path, st)); }
int fstatat(int dirfd, const char *path, struct stat *st, int flags) { return fail(sys6(SYS_newfstatat, dirfd, (long)path, (long)st, flags, 0, 0)); }
int openat(int dirfd, const char *path, int flags, ...)
{
    va_list ap; int mode;
    va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
    return fail(sys6(SYS_openat, dirfd, (long)path, flags, flags & O_CREAT ? mode : 0, 0, 0));
}
int unlinkat(int dirfd, const char *path, int flags) { return fail(sys3(SYS_unlinkat, dirfd, path, flags)); }
int mkdirat(int dirfd, const char *path, mode_t mode) { return fail(sys3(SYS_mkdirat, dirfd, path, mode)); }
int rmdir(const char *path) { return fail(sys1(SYS_rmdir, path)); }
int rename(const char *a, const char *b) { return fail(sys2(SYS_rename, a, b)); }
int link(const char *a, const char *b) { return fail(sys2(SYS_link, a, b)); }
int symlink(const char *a, const char *b) { return fail(sys2(SYS_symlink, a, b)); }
ssize_t readlink(const char *path, char *buf, size_t n) { return fail(sys3(SYS_readlink, path, buf, n)); }
int fchmod(int fd, mode_t mode) { return fail(sys2(SYS_fchmod, fd, mode)); }
int chown(const char *path, uid_t u, gid_t g) { return fail(sys3(SYS_chown, path, u, g)); }
int lchown(const char *path, uid_t u, gid_t g) { return fail(sys3(SYS_lchown, path, u, g)); }
int fchown(int fd, uid_t u, gid_t g) { return fail(sys3(SYS_fchown, fd, u, g)); }
mode_t umask(mode_t m) { return sys1(SYS_umask, m); }
int access(const char *path, int mode) { return fail(sys2(SYS_access, path, mode)); }
int truncate(const char *path, off_t n) { return fail(sys2(SYS_truncate, path, n)); }
int ftruncate(int fd, off_t n) { return fail(sys2(SYS_ftruncate, fd, n)); }
int fsync(int fd) { return fail(sys1(SYS_fsync, fd)); }
int fdatasync(int fd) { return fail(sys1(SYS_fdatasync, fd)); }
void sync(void) { sys0(SYS_sync); }
int syncfs(int fd) { return fail(sys1(SYS_syncfs, fd)); }
int mknod(const char *path, mode_t mode, dev_t dev) { return fail(sys3(SYS_mknod, path, mode, dev)); }
int mkfifo(const char *path, mode_t mode) { return mknod(path, (mode & 07777) | S_IFIFO, 0); }
int dup2(int a, int b) { return fail(sys2(SYS_dup2, a, b)); }
int fcntl(int fd, int cmd, long arg) { return fail(sys3(SYS_fcntl, fd, cmd, arg)); }
int fchdir(int fd) { return fail(sys1(SYS_fchdir, fd)); }
int kill(pid_t pid, int sig) { return fail(sys2(SYS_kill, pid, sig)); }
int uname(struct utsname *u) { return fail(sys1(SYS_uname, u)); }
int sethostname(const char *name, size_t n) { return fail(sys2(SYS_sethostname, name, n)); }
int nanosleep(const struct timespec *req, struct timespec *rem) { return fail(sys2(SYS_nanosleep, req, rem)); }
int clock_gettime(int clk, struct timespec *t) { return fail(sys2(SYS_clock_gettime, clk, t)); }
int utimensat(int dirfd, const char *path, const struct timespec t[2], int flags) { return fail(sys6(SYS_utimensat, dirfd, (long)path, (long)t, flags, 0, 0)); }
int statfs(const char *path, struct statfs *s) { return fail(sys2(SYS_statfs, path, s)); }
pid_t getppid(void) { return sys0(SYS_getppid); }
uid_t geteuid(void) { return sys0(SYS_geteuid); }
gid_t getgid(void) { return sys0(SYS_getgid); }
gid_t getegid(void) { return sys0(SYS_getegid); }
int getgroups(int n, gid_t *list) { return fail(sys2(SYS_getgroups, n, list)); }
int sched_getaffinity(pid_t pid, size_t n, void *mask) { return fail(sys3(SYS_sched_getaffinity, pid, n, mask)); }
ssize_t getrandom(void *buf, size_t n, unsigned flags) { return fail(sys3(SYS_getrandom, buf, n, flags)); }
ssize_t pread(int fd, void *buf, size_t n, off_t off) { return fail(sys6(SYS_pread64, fd, (long)buf, n, off, 0, 0)); }
ssize_t pwrite(int fd, const void *buf, size_t n, off_t off) { return fail(sys6(SYS_pwrite64, fd, (long)buf, n, off, 0, 0)); }
int setuid(uid_t u) { return fail(sys1(SYS_setuid, u)); }
int setgid(gid_t g) { return fail(sys1(SYS_setgid, g)); }
int chroot(const char *path) { return fail(sys1(SYS_chroot, path)); }
#define major(d) ((unsigned)((((d) >> 32) & 0xfffff000) | (((d) >> 8) & 0xfff)))
#define minor(d) ((unsigned)((((d) >> 12) & 0xffffff00) | ((d) & 0xff)))
#define makedev(ma, mi) ((dev_t)((((unsigned long)(ma) & 0xfffff000UL) << 32) | (((unsigned long)(ma) & 0xfffUL) << 8) | \
                                 (((unsigned long)(mi) & 0xffffff00UL) << 12) | ((unsigned long)(mi) & 0xffUL)))

/* ---- characters ---- */
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isxdigit(int c) { return isdigit(c) || ((c | 32) >= 'a' && (c | 32) <= 'f'); }
int isprint(int c) { return c >= 32 && c < 127; }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }

/* ---- strings ---- */
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (unsigned char)*a - (unsigned char)*b; }
int strncmp(const char *a, const char *b, size_t n)
{
    for (; n && *a && *a == *b; n--) a++, b++;
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)); return r; }
char *strncpy(char *d, const char *s, size_t n) { size_t i = 0; for (; i < n && s[i]; i++) d[i] = s[i]; for (; i < n; i++) d[i] = 0; return d; }
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strchr(const char *s, int c) { for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return NULL; } }
char *strrchr(const char *s, int c) { const char *r = NULL; for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; } }
char *strstr(const char *h, const char *n)
{
    size_t l = strlen(n);
    for (; *h; h++) if (!strncmp(h, n, l)) return (char *)h;
    return l ? NULL : (char *)h;
}
char *strpbrk(const char *s, const char *set) { for (; *s; s++) if (strchr(set, *s)) return (char *)s; return NULL; }
void *memcpy(void *d, const void *s, size_t n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, size_t n)
{
    char *a = d; const char *b = s;
    if (a < b) while (n--) *a++ = *b++;
    else while (n--) a[n] = b[n];
    return d;
}
void *memset(void *d, int c, size_t n) { char *a = d; while (n--) *a++ = c; return d; }
int memcmp(const void *x, const void *y, size_t n)
{
    const unsigned char *a = x, *b = y;
    for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
    return 0;
}
void *memchr(const void *s, int c, size_t n) { const unsigned char *p = s; for (; n; n--, p++) if (*p == (unsigned char)c) return (void *)p; return NULL; }
/* the messages are glibc's, so what rifle says matches what everyone's scripts expect [rifle] */
static const struct { int e; const char *m; } c_errlist[] = {
    { EPERM, "Operation not permitted" }, { ENOENT, "No such file or directory" }, { ESRCH, "No such process" },
    { EINTR, "Interrupted system call" }, { EIO, "Input/output error" }, { ENXIO, "No such device or address" },
    { E2BIG, "Argument list too long" }, { ENOEXEC, "Exec format error" }, { EBADF, "Bad file descriptor" },
    { ECHILD, "No child processes" }, { EAGAIN, "Resource temporarily unavailable" }, { ENOMEM, "Cannot allocate memory" },
    { EACCES, "Permission denied" }, { EFAULT, "Bad address" }, { EBUSY, "Device or resource busy" },
    { EEXIST, "File exists" }, { EXDEV, "Invalid cross-device link" }, { ENODEV, "No such device" },
    { ENOTDIR, "Not a directory" }, { EISDIR, "Is a directory" }, { EINVAL, "Invalid argument" },
    { ENFILE, "Too many open files in system" }, { EMFILE, "Too many open files" },
    { ENOTTY, "Inappropriate ioctl for device" }, { ETXTBSY, "Text file busy" }, { EFBIG, "File too large" },
    { ENOSPC, "No space left on device" }, { ESPIPE, "Illegal seek" }, { EROFS, "Read-only file system" },
    { EMLINK, "Too many links" }, { EPIPE, "Broken pipe" }, { EDOM, "Numerical argument out of domain" },
    { ERANGE, "Numerical result out of range" }, { EDEADLK, "Resource deadlock avoided" },
    { ENAMETOOLONG, "File name too long" }, { ENOSYS, "Function not implemented" },
    { ENOTEMPTY, "Directory not empty" }, { ELOOP, "Too many levels of symbolic links" },
    { ENODATA, "No data available" }, { EOVERFLOW, "Value too large for defined data type" },
    { EILSEQ, "Invalid or incomplete multibyte or wide character" }, { EOPNOTSUPP, "Operation not supported" },
    { ETIMEDOUT, "Connection timed out" }, { ESTALE, "Stale file handle" }, { EDQUOT, "Disk quota exceeded" },
};
int snprintf(char *buf, size_t cap, const char *fmt, ...);
char *strerror(int e)
{
    static char unknown[32];
    unsigned i;
    for (i = 0; i < sizeof c_errlist / sizeof c_errlist[0]; i++) if (c_errlist[i].e == e) return (char *)c_errlist[i].m;
    snprintf(unknown, sizeof unknown, "Unknown error %d", e);
    return unknown;
}

/* ---- memory ----
 * [rifle] small blocks (up to 2032 bytes) come from 256K chunks and go on a list for their size when freed, big
 * blocks are their own mmap and grow with mremap. every block has 16 bytes in front of it holding its size. */
#define MALLOC_BIG 2048
static void *malloc_lists[12];
static char *malloc_cur, *malloc_end;
static void *c_mmap(size_t len)
{
    long p = sys6(SYS_mmap, 0, len, PROT_READ | PROT_WRITE, 0x22 /* private, anonymous */, -1, 0);
    return (p < 0 && p > -4096) ? NULL : (void *)p;
}
void *malloc(size_t n)
{
    size_t need = n + 16;
    if (need < n) { errno = ENOMEM; return NULL; }
    if (need <= MALLOC_BIG) {
        size_t sz = 32; int cl = 5;
        while (sz < need) { sz <<= 1; cl++; }
        char *b = malloc_lists[cl];
        if (b) { malloc_lists[cl] = *(void **)(b + 16); return b + 16; }
        if (malloc_cur + sz > malloc_end) {
            malloc_cur = c_mmap(256 * 1024);
            if (!malloc_cur) { malloc_end = NULL; errno = ENOMEM; return NULL; }
            malloc_end = malloc_cur + 256 * 1024;
        }
        b = malloc_cur; malloc_cur += sz;
        *(size_t *)b = sz;
        return b + 16;
    }
    size_t len = (need + 4095) & ~4095UL;
    if (len < need) { errno = ENOMEM; return NULL; }
    char *p = c_mmap(len);
    if (!p) { errno = ENOMEM; return NULL; }
    *(size_t *)p = len;
    return p + 16; /* 16: what a long double or an SSE value wants */
}
void free(void *p)
{
    if (!p) return;
    char *h = (char *)p - 16;
    size_t sz = *(size_t *)h;
    if (sz <= MALLOC_BIG) {
        int cl = 5; size_t c = 32;
        while (c < sz) { c <<= 1; cl++; }
        *(void **)p = malloc_lists[cl]; malloc_lists[cl] = h;
    } else sys2(SYS_munmap, h, sz);
}
void *calloc(size_t n, size_t m)
{
    size_t total = n * m;
    if (m && total / m != n) { errno = ENOMEM; return NULL; }
    void *p = malloc(total);
    if (p && total + 16 <= MALLOC_BIG) memset(p, 0, total); /* big blocks come from mmap, already zero */
    return p;
}
void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    char *h = (char *)p - 16;
    size_t sz = *(size_t *)h, old = sz - 16;
    if (n <= old) return p;
    if (sz > MALLOC_BIG && n + 16 > n) {
        size_t len = (n + 16 + 4095) & ~4095UL;
        long q = sys6(SYS_mremap, (long)h, sz, len, 1 /* may move */, 0, 0);
        if (q < 0 && q > -4096) { errno = ENOMEM; return NULL; }
        *(size_t *)q = len;
        return (char *)q + 16;
    }
    void *q = malloc(n);
    if (q) { memcpy(q, p, old); free(p); }
    return q;
}
size_t strspn(const char *s, const char *set) { size_t n = 0; while (s[n] && strchr(set, s[n])) n++; return n; }  /* [rifle] */
size_t strcspn(const char *s, const char *set) { size_t n = 0; while (s[n] && !strchr(set, s[n])) n++; return n; }
int strcasecmp(const char *a, const char *b) { while (*a && tolower(*a) == tolower(*b)) a++, b++; return tolower((unsigned char)*a) - tolower((unsigned char)*b); }
int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n && *a && tolower(*a) == tolower(*b); n--) a++, b++;
    return n ? tolower((unsigned char)*a) - tolower((unsigned char)*b) : 0;
}
void *memrchr(const void *s, int c, size_t n) { const unsigned char *p = (const unsigned char *)s + n; while (n--) if (*--p == (unsigned char)c) return (void *)p; return NULL; }
char *strndup(const char *s, size_t n) { size_t l = 0; char *d; while (l < n && s[l]) l++; d = malloc(l + 1); if (!d) return NULL; memcpy(d, s, l); d[l] = 0; return d; }
char *stpcpy(char *d, const char *s) { while ((*d = *s++)) d++; return d; }
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
char *strdup(const char *s) { char *d = malloc(strlen(s) + 1); return d ? strcpy(d, s) : NULL; }

/* ---- numbers from text ---- */
static int digit_of(int c) { return isdigit(c) ? c - '0' : isalpha(c) ? (c | 32) - 'a' + 10 : 99; }
unsigned long long strtoull(const char *s, char **end, int base)
{
    const char *p = s; unsigned long long v = 0; int neg = 0, any = 0;
    while (isspace(*p)) p++;
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] | 32) == 'x' && isxdigit(p[2])) { p += 2; base = 16; }
    else if (base == 0) base = *p == '0' ? 8 : 10;
    for (; digit_of(*p) < base; p++, any = 1) v = v * base + digit_of(*p);
    if (end) *end = (char *)(any ? p : s);
    return neg ? -v : v;
}
long long strtoll(const char *s, char **end, int base) { return (long long)strtoull(s, end, base); }
unsigned long strtoul(const char *s, char **end, int base) { return strtoull(s, end, base); }
long strtol(const char *s, char **end, int base) { return strtoull(s, end, base); }
int atoi(const char *s) { return strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

long double ldexpl(long double x, int e)
{
    while (e > 0) { x *= 2; e--; }
    while (e < 0) { x /= 2; e++; }
    return x;
}
double ldexp(double x, int e) { return ldexpl(x, e); }
/* decimal and hex floating point. good to the last digit or so, not correctly rounded in every case */
long double strtold(const char *s, char **end)
{
    const char *p = s; long double v = 0; int neg = 0, any = 0, ex = 0;
    while (isspace(*p)) p++;
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    if (p[0] == '0' && (p[1] | 32) == 'x') {
        p += 2;
        for (; isxdigit(*p); p++, any = 1) v = v * 16 + digit_of(*p);
        if (*p == '.') for (p++; isxdigit(*p); p++, any = 1) { v = v * 16 + digit_of(*p); ex -= 4; }
        if (any && (*p | 32) == 'p') { char *e2; long x = strtol(p + 1, &e2, 10); if (e2 != p + 1) { ex += x; p = e2; } }
        v = ldexpl(v, ex);
    } else {
        for (; isdigit(*p); p++, any = 1) v = v * 10 + (*p - '0');
        if (*p == '.') for (p++; isdigit(*p); p++, any = 1) { v = v * 10 + (*p - '0'); ex--; }
        if (any && (*p | 32) == 'e') { char *e2; long x = strtol(p + 1, &e2, 10); if (e2 != p + 1) { ex += x; p = e2; } }
        for (; ex > 0; ex--) v *= 10;
        for (; ex < 0; ex++) v /= 10;
    }
    if (end) *end = (char *)(any ? p : s);
    return neg ? -v : v;
}
double strtod(const char *s, char **end) { return strtold(s, end); }
float strtof(const char *s, char **end) { return strtold(s, end); }
double atof(const char *s) { return strtod(s, NULL); }

/* ---- sorting ---- */
static void swap_bytes(char *a, char *b, size_t n) { while (n--) { char t = *a; *a++ = *b; *b++ = t; } }
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    /* shell sort: short, no recursion, fine for the sizes a compiler sorts */
    char *b = base; size_t gap, i, j;
    for (gap = n / 2; gap; gap /= 2)
        for (i = gap; i < n; i++)
            for (j = i; j >= gap && cmp(b + (j - gap) * size, b + j * size) > 0; j -= gap)
                swap_bytes(b + (j - gap) * size, b + j * size, size);
}

/* ---- jumps ---- */
typedef long jmp_buf[8];
int setjmp(jmp_buf b);
__attribute__((noreturn)) void longjmp(jmp_buf b, int v);
__asm__(".text\n.global setjmp\n.global _setjmp\nsetjmp:\n_setjmp:\n"
        " mov %rbx,(%rdi)\n mov %rbp,8(%rdi)\n mov %r12,16(%rdi)\n mov %r13,24(%rdi)\n mov %r14,32(%rdi)\n"
        " mov %r15,40(%rdi)\n lea 8(%rsp),%rdx\n mov %rdx,48(%rdi)\n mov (%rsp),%rdx\n mov %rdx,56(%rdi)\n"
        " xor %eax,%eax\n ret\n"
        ".global longjmp\nlongjmp:\n"
        " mov %esi,%eax\n test %eax,%eax\n jnz 1f\n inc %eax\n1:\n"
        " mov (%rdi),%rbx\n mov 8(%rdi),%rbp\n mov 16(%rdi),%r12\n mov 24(%rdi),%r13\n mov 32(%rdi),%r14\n"
        " mov 40(%rdi),%r15\n mov 48(%rdi),%rsp\n jmp *56(%rdi)\n");

/* ---- time ---- */
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
/* there are no time zones here: local time is UTC */
struct tm *localtime(const time_t *t)
{
    static struct tm r;
    long s = *t, days = s / 86400, rem = s % 86400, y = 1970;
    static const int md[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    r.tm_hour = rem / 3600; r.tm_min = rem / 60 % 60; r.tm_sec = rem % 60;
    r.tm_wday = (days + 4) % 7;
    for (;;) {
        int len = (y % 4 == 0 && (y % 100 || y % 400 == 0)) ? 366 : 365;
        if (days < len) break;
        days -= len; y++;
    }
    r.tm_year = y - 1900; r.tm_yday = days;
    for (r.tm_mon = 0; ; r.tm_mon++) {
        int len = md[r.tm_mon] + (r.tm_mon == 1 && (y % 4 == 0 && (y % 100 || y % 400 == 0)));
        if (days < len) break;
        days -= len;
    }
    r.tm_mday = days + 1; r.tm_isdst = 0;
    return &r;
}
struct tm *gmtime(const time_t *t) { return localtime(t); }
struct timeval { long tv_sec, tv_usec; };
int gettimeofday(struct timeval *tv, void *tz) { (void)tz; return fail(sys2(SYS_gettimeofday, tv, 0)); }

/* ---- files with buffers ---- */
typedef struct FILE { int fd, write, err, eof, n, pos; char buf[4096]; int line; } FILE;   /* [rifle] line: 0 not asked yet, 1 yes, 2 no */
static FILE files_std[3] = { { 0 }, { 1, 1 }, { 2, 1 } }; /* the rest of each is zero */
FILE *stdin = &files_std[0], *stdout = &files_std[1], *stderr = &files_std[2];
#define EOF (-1)
#define BUFSIZ 4096

int fflush(FILE *f)
{
    if (f && f->write && f->n) {
        int off = 0;
        while (off < f->n) { ssize_t w = write(f->fd, f->buf + off, f->n - off); if (w <= 0) { f->err = 1; break; } off += w; }
        f->n = 0;
    }
    return f && f->err ? EOF : 0;
}
static int open_flags(const char *mode)
{
    int f = mode[0] == 'r' ? O_RDONLY : mode[0] == 'w' ? O_WRONLY | O_CREAT | O_TRUNC : O_WRONLY | O_CREAT | O_APPEND;
    if (strchr(mode, '+')) f = (f & ~(O_RDONLY | O_WRONLY)) | O_RDWR;
    return f;
}
FILE *fdopen(int fd, const char *mode)
{
    FILE *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    f->fd = fd; f->write = mode[0] != 'r' || strchr(mode, '+');
    return f;
}
FILE *fopen(const char *path, const char *mode)
{
    int fd = open(path, open_flags(mode), 0666);
    return fd < 0 ? NULL : fdopen(fd, mode);
}
FILE *freopen(const char *path, const char *mode, FILE *f)
{
    int fd;
    fflush(f);
    if (!path) return f;
    if ((fd = open(path, open_flags(mode), 0666)) < 0) return NULL;
    close(f->fd);
    f->fd = fd; f->write = mode[0] != 'r' || strchr(mode, '+'); f->n = f->pos = f->err = f->eof = 0;
    return f;
}
int fclose(FILE *f)
{
    int r = fflush(f);
    close(f->fd);
    if (f < files_std || f > files_std + 2) free(f);
    return r;
}
/* stdout is flushed at every newline when it's a terminal, and when it's a pipe or a file only when full, like
 * everything else does. stderr always goes out at once [rifle] */
size_t fwrite(const void *p, size_t size, size_t n, FILE *f)
{
    const char *s = p; size_t len = size * n, left = len;
    if (size && len / size != n) return 0;
    while (left) {
        size_t room = BUFSIZ - f->n, c = left < room ? left : room;
        memcpy(f->buf + f->n, s, c);
        f->n += c; s += c; left -= c;
        if (f->n == BUFSIZ) fflush(f);
    }
    if (f == stderr) fflush(f);
    else if (f == stdout && len) {
        if (!f->line) f->line = isatty(1) ? 1 : 2;
        if (f->line == 1 && memchr(p, '\n', len)) fflush(f);
    }
    return f->err ? 0 : n;
}
int fputc(int c, FILE *f) { unsigned char ch = c; return fwrite(&ch, 1, 1, f) ? ch : EOF; }
int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }
int fputs(const char *s, FILE *f) { return fwrite(s, 1, strlen(s), f) ? 0 : EOF; }
int puts(const char *s) { fputs(s, stdout); return putchar('\n'); }
int fgetc(FILE *f)
{
    if (f->pos == f->n) {
        if (f == stdin) fflush(stdout);
        ssize_t r = read(f->fd, f->buf, BUFSIZ);
        if (r <= 0) { if (r < 0) f->err = 1; else f->eof = 1; f->n = f->pos = 0; return EOF; }
        f->n = r; f->pos = 0;
    }
    return (unsigned char)f->buf[f->pos++];
}
int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }
size_t fread(void *p, size_t size, size_t n, FILE *f)
{
    char *d = p; size_t len = size * n, i = 0; int c;
    if (size && len / size != n) return 0;
    while (i < len) {
        if (f->pos < f->n) {                      /* [rifle] what's buffered goes over in one piece */
            size_t c2 = f->n - f->pos; if (c2 > len - i) c2 = len - i;
            memcpy(d + i, f->buf + f->pos, c2); f->pos += c2; i += c2;
        } else if ((c = fgetc(f)) == EOF) break;
        else d[i++] = c;
    }
    return size ? i / size : 0;
}
char *fgets(char *s, int n, FILE *f)
{
    int i = 0, c;
    while (i < n - 1 && (c = fgetc(f)) != EOF) { s[i++] = c; if (c == '\n') break; }
    s[i] = 0;
    return i ? s : NULL;
}
int feof(FILE *f) { return f->eof; }
int ferror(FILE *f) { return f->err; }
int fileno(FILE *f) { return f->fd; }
int fseek(FILE *f, long off, int whence)
{
    fflush(f);
    if (whence == SEEK_CUR && !f->write) off -= f->n - f->pos;
    f->n = f->pos = 0; f->eof = 0;
    return lseek(f->fd, off, whence) < 0 ? -1 : 0;
}
long ftell(FILE *f)
{
    long p = lseek(f->fd, 0, SEEK_CUR);
    return p < 0 ? -1 : f->write ? p + f->n : p - (f->n - f->pos);
}
int remove(const char *path) { return unlink(path); }

/* ---- printf ----
 * flags - + space # 0, width and precision (numbers or *), sizes hh h l ll z j t L,
 * conversions d i u o x X c s p f F e E g G and %% */
struct out { char *buf; size_t cap, len; FILE *f; };
static void out_c(struct out *o, char c)
{
    if (o->f) fputc(c, o->f);
    else if (o->len + 1 < o->cap) o->buf[o->len] = c;
    o->len++;
}
static void out_pad(struct out *o, char c, int n) { while (n-- > 0) out_c(o, c); }
static void out_field(struct out *o, const char *pre, const char *s, int len, int width, int left, int zero)
{
    int plen = strlen(pre), pad = width - plen - len;
    if (!left && !zero) out_pad(o, ' ', pad);
    while (*pre) out_c(o, *pre++);
    if (!left && zero) out_pad(o, '0', pad);
    while (len-- > 0) out_c(o, *s++);
    if (left) out_pad(o, ' ', pad);
}
/* a long double as digits: fixed (f), exponent (e) or the shorter of both (g) */
static int fmt_float(char *b, long double v, int prec, char conv, int alt)
{
    int n = 0, e = 0, i, upper = conv == 'E' || conv == 'G' || conv == 'F';
    char c = conv | 32;
    if (v != v) { strcpy(b, upper ? "NAN" : "nan"); return 3; }
    if (v > 1e4900L || v < -1e4900L) { strcpy(b, upper ? "INF" : "inf"); return 3; }
    if (prec < 0) prec = 6;
    if (v != 0) { long double a = v < 0 ? -v : v; while (a >= 10) { a /= 10; e++; } while (a < 1) { a *= 10; e--; } }
    if (c == 'g') {
        if (prec == 0) prec = 1;
        c = (e < -4 || e >= prec) ? 'e' : 'f';
        prec = c == 'e' ? prec - 1 : prec - 1 - e;
        if (prec < 0) prec = 0;
        if (!alt) alt = -1; /* -1: drop the trailing zeros */
    }
    long double a = v < 0 ? -v : v, r = 0.5L;
    if (c == 'e') { if (v != 0) { for (i = 0; i < e; i++) a /= 10; for (i = 0; i > e; i--) a *= 10; } }
    for (i = 0; i < prec; i++) r /= 10;
    a += r;
    if (c == 'e' && a >= 10) { a /= 10; e++; }
    long double ip = 1; int idig = 1;
    while (ip * 10 <= a) { ip *= 10; idig++; }
    for (i = 0; i < idig; i++) { int d = (int)(a / ip); if (d > 9) d = 9; b[n++] = '0' + d; a -= d * ip; ip /= 10; }
    if (prec > 0 || alt > 0) b[n++] = '.';
    for (i = 0; i < prec; i++) { a *= 10; int d = (int)a; if (d > 9) d = 9; b[n++] = '0' + d; a -= d; }
    if (alt < 0 && prec > 0) { while (b[n - 1] == '0') n--; if (b[n - 1] == '.') n--; }
    if (c == 'e') {
        b[n++] = upper ? 'E' : 'e'; b[n++] = e < 0 ? '-' : '+';
        if (e < 0) e = -e;
        if (e >= 100) b[n++] = '0' + e / 100;
        b[n++] = '0' + e / 10 % 10; b[n++] = '0' + e % 10;
    }
    b[n] = 0;
    return n;
}
static int format(struct out *o, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') { out_c(o, *fmt); continue; }
        int left = 0, plus = 0, space = 0, alt = 0, zero = 0, width = 0, prec = -1, size = 0;
        for (;; fmt++) {
            if (fmt[1] == '-') left = 1; else if (fmt[1] == '+') plus = 1; else if (fmt[1] == ' ') space = 1;
            else if (fmt[1] == '#') alt = 1; else if (fmt[1] == '0') zero = 1; else break;
        }
        fmt++;
        if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } fmt++; }
        else while (isdigit(*fmt)) width = width * 10 + *fmt++ - '0';
        if (*fmt == '.') {
            fmt++; prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (isdigit(*fmt)) prec = prec * 10 + *fmt++ - '0';
        }
        for (;; fmt++) {
            if (*fmt == 'h') size = size == -1 ? -2 : -1;
            else if (*fmt == 'l') size++;
            else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't') size = 2;
            else if (*fmt == 'L') size = 3;
            else break;
        }
        char b[128], *s = b; int len = 0; const char *pre = "";
        switch (*fmt) {
        case 'd': case 'i': {
            long long v = size >= 1 ? va_arg(ap, long long) : va_arg(ap, int);
            if (size == -1) v = (short)v; else if (size == -2) v = (signed char)v;
            unsigned long long u = v < 0 ? -(unsigned long long)v : v;
            pre = v < 0 ? "-" : plus ? "+" : space ? " " : "";
            char *p = b + sizeof b; *--p = 0;
            do *--p = '0' + u % 10; while (u /= 10);
            if (prec == 0 && v == 0) p++;
            while (prec > (int)(b + sizeof b - 1 - p)) *--p = '0';
            if (prec >= 0) zero = 0;
            out_field(o, pre, p, strlen(p), width, left, zero);
            continue;
        }
        case 'u': case 'o': case 'x': case 'X': case 'p': {
            unsigned long long u;
            int base = *fmt == 'o' ? 8 : *fmt == 'u' ? 10 : 16;
            const char *dig = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            if (*fmt == 'p') { u = (uintptr_t)va_arg(ap, void *); alt = 1; }
            else u = size >= 1 ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
            if (size == -1) u = (unsigned short)u; else if (size == -2) u = (unsigned char)u;
            if (alt && base == 16 && u) pre = *fmt == 'X' ? "0X" : "0x";
            char *p = b + sizeof b; *--p = 0;
            do *--p = dig[u % base]; while (u /= base);
            if (prec == 0 && p[0] == '0' && !p[1] && *fmt != 'p') p++;
            while (prec > (int)(b + sizeof b - 1 - p)) *--p = '0';
            if (alt && base == 8 && *p != '0') *--p = '0';
            if (prec >= 0) zero = 0;
            out_field(o, pre, p, strlen(p), width, left, zero);
            continue;
        }
        case 'c': b[0] = va_arg(ap, int); len = 1; break;
        case 's': s = va_arg(ap, char *); if (!s) s = "(null)"; len = strlen(s); if (prec >= 0 && prec < len) len = prec; break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            long double v = size == 3 ? va_arg(ap, long double) : va_arg(ap, double);
            pre = v < 0 ? "-" : plus ? "+" : space ? " " : "";
            len = fmt_float(b, v, prec, *fmt, alt);
            out_field(o, pre, b, len, width, left, zero);
            continue;
        }
        case '%': b[0] = '%'; len = 1; break;
        default: out_c(o, '%'); out_c(o, *fmt); continue;
        }
        out_field(o, "", s, len, width, left, 0);
    }
    return o->len;
}
int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    struct out o = { buf, cap, 0, NULL };
    int n = format(&o, fmt, ap);
    if (cap) buf[o.len < cap ? o.len : cap - 1] = 0;
    return n;
}
int vsprintf(char *buf, const char *fmt, va_list ap) { return vsnprintf(buf, (size_t)-1 / 2, fmt, ap); }
int vfprintf(FILE *f, const char *fmt, va_list ap) { struct out o = { NULL, 0, 0, f }; return format(&o, fmt, ap); }
int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }
int snprintf(char *buf, size_t cap, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vsnprintf(buf, cap, fmt, ap); va_end(ap); return n; }
int sprintf(char *buf, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vsprintf(buf, fmt, ap); va_end(ap); return n; }
int fprintf(FILE *f, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vfprintf(f, fmt, ap); va_end(ap); return n; }
int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vfprintf(stdout, fmt, ap); va_end(ap); return n; }

/* ---- the end of a program ---- */
__attribute__((noreturn)) void exit(int code) { fflush(stdout); fflush(stderr); _exit(code); }
__attribute__((noreturn)) void abort(void) { fputs("abort\n", stderr); _exit(134); }
#define assert(x) ((x) ? (void)0 : (fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #x), abort()))

/* ---- paths ---- */
/* the absolute path, with . and .. taken out (links aren't followed, there are none to follow here) */
char *realpath(const char *path, char *out)
{
    char tmp[PATH_MAX], *r = out ? out : malloc(PATH_MAX), *p, *seg;
    size_t n = 0;
    if (!r) return NULL;
    if (path[0] == '/') strcpy(tmp, path);
    else { if (!getcwd(tmp, sizeof tmp)) return NULL; strcat(tmp, "/"); strcat(tmp, path); }
    r[0] = 0;
    for (p = tmp; *p; ) {
        while (*p == '/') p++;
        seg = p;
        while (*p && *p != '/') p++;
        size_t l = p - seg;
        if (l == 0 || (l == 1 && seg[0] == '.')) continue;
        if (l == 2 && seg[0] == '.' && seg[1] == '.') { while (n && r[--n] != '/'); r[n] = 0; continue; }
        r[n++] = '/'; memcpy(r + n, seg, l); n += l; r[n] = 0;
    }
    if (!n) strcpy(r, "/");
    return r;
}

/* ---- tcc's helper for va_arg ----
 * under tcc every va_arg calls __va_arg(ap, kind, size, align): kind 0 = a general register, 1 = an SSE register,
 * 2 = the stack. the layout is the x86-64 ABI's: 6 general registers (48 bytes), then 8 SSE ones of 16 */
#ifdef __TINYC__
void *__va_arg(__builtin_va_list ap, int kind, int size, int align)
{
    size = (size + 7) & ~7; align = (align + 7) & ~7;
    if (kind == 0 && ap->gp_offset + size <= 48) { ap->gp_offset += size; return ap->reg_save_area + ap->gp_offset - size; }
    if (kind == 1 && size <= 8 && ap->fp_offset < 176) { ap->fp_offset += 16; return ap->reg_save_area + ap->fp_offset - 16; }
    ap->overflow_arg_area = (char *)(((uintptr_t)ap->overflow_arg_area + align - 1) & -(uintptr_t)align);
    ap->overflow_arg_area += size;
    return ap->overflow_arg_area - size;
}
#endif

/* ---- the environment and running programs ---- */
char **environ;
char *getenv(const char *name)
{
    size_t l = strlen(name); char **e;
    for (e = environ; e && *e; e++) if (!strncmp(*e, name, l) && (*e)[l] == '=') return *e + l + 1;
    return NULL;
}
/* execl(path, arg0, arg1, ..., 0): execve with the arguments written out */
int execl(const char *path, const char *arg, ...)
{
    char *argv[64]; int n = 0; va_list ap;
    argv[n++] = (char *)arg;
    va_start(ap, arg);
    while (n < 63 && (argv[n] = va_arg(ap, char *))) n++;
    va_end(ap);
    argv[n] = NULL;
    return execve(path, argv, environ);
}
/* execve, looking through $PATH when the name has no / in it */
int execvp(const char *file, char *const argv[])
{
    char path[PATH_MAX]; const char *p = getenv("PATH"), *q;
    if (strchr(file, '/')) return execve(file, argv, environ);
    if (!p) p = "/bin";
    for (; *p; p = *q ? q + 1 : q) {
        q = strchr(p, ':'); if (!q) q = p + strlen(p);
        snprintf(path, sizeof path, "%.*s/%s", (int)(q - p), p, file);
        execve(path, argv, environ);
    }
    return -1;
}

/* ---- directories [rifle] ----
 * opendir/readdir over getdents64. readdir hands back a pointer into the buffer, good until the next call */
struct dirent { ino_t d_ino; long d_off; unsigned short d_reclen; unsigned char d_type; char d_name[256]; };
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
typedef struct DIR { int fd, pos, len; char buf[8192]; } DIR;
DIR *fdopendir(int fd)
{
    DIR *d = malloc(sizeof *d);
    if (!d) return NULL;
    d->fd = fd; d->pos = d->len = 0;
    return d;
}
DIR *opendir(const char *path)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *d;
    if (fd < 0) return NULL;
    if (!(d = fdopendir(fd))) { close(fd); return NULL; }
    return d;
}
struct dirent *readdir(DIR *d)
{
    if (d->pos >= d->len) {
        long r = sys3(SYS_getdents64, d->fd, d->buf, sizeof d->buf);
        if (r <= 0) { if (r < 0) errno = -r; return NULL; }   /* errno stays alone at the end of the list */
        d->len = r; d->pos = 0;
    }
    struct dirent *e = (struct dirent *)(d->buf + d->pos);
    d->pos += e->d_reclen;
    return e;
}
int closedir(DIR *d) { int r = close(d->fd); free(d); return r; }
int dirfd(DIR *d) { return d->fd; }

/* ---- users and groups [rifle] ----
 * read straight from /etc/passwd and /etc/group each time. no NSS, no caches: what's in the files is the answer */
struct passwd { char *pw_name, *pw_passwd; uid_t pw_uid; gid_t pw_gid; char *pw_gecos, *pw_dir, *pw_shell; };
struct group { char *gr_name, *gr_passwd; gid_t gr_gid; char **gr_mem; };
static char *c_field(char **p)
{
    char *r = *p, *q = strchr(*p, ':');
    if (q) { *q = 0; *p = q + 1; } else *p += strlen(*p);
    return r;
}
static struct passwd *c_getpw(const char *name, long id)
{
    static struct passwd pw; static char line[2048];
    FILE *f = fopen("/etc/passwd", "r");
    if (!f) return NULL;
    while (fgets(line, sizeof line, f)) {
        char *p = line, *nl = strchr(line, '\n'), *uid, *gid;
        if (nl) *nl = 0;
        if (!line[0] || line[0] == '#') continue;
        pw.pw_name = c_field(&p); pw.pw_passwd = c_field(&p); uid = c_field(&p); gid = c_field(&p);
        pw.pw_gecos = c_field(&p); pw.pw_dir = c_field(&p); pw.pw_shell = c_field(&p);
        pw.pw_uid = strtoul(uid, NULL, 10); pw.pw_gid = strtoul(gid, NULL, 10);
        if (name ? !strcmp(pw.pw_name, name) : (long)pw.pw_uid == id) { fclose(f); return &pw; }
    }
    fclose(f);
    return NULL;
}
struct passwd *getpwnam(const char *name) { return c_getpw(name, 0); }
struct passwd *getpwuid(uid_t uid) { return c_getpw(NULL, uid); }
static struct group *c_getgr(const char *name, long id)
{
    static struct group gr; static char line[8192]; static char *mem[512];
    FILE *f = fopen("/etc/group", "r");
    if (!f) return NULL;
    while (fgets(line, sizeof line, f)) {
        char *p = line, *nl = strchr(line, '\n'), *gid, *list;
        int n = 0;
        if (nl) *nl = 0;
        if (!line[0] || line[0] == '#') continue;
        gr.gr_name = c_field(&p); gr.gr_passwd = c_field(&p); gid = c_field(&p); list = p;
        gr.gr_gid = strtoul(gid, NULL, 10);
        while (*list && n < 511) { mem[n++] = list; while (*list && *list != ',') list++; if (*list) *list++ = 0; }
        mem[n] = NULL; gr.gr_mem = mem;
        if (name ? !strcmp(gr.gr_name, name) : (long)gr.gr_gid == id) { fclose(f); return &gr; }
    }
    fclose(f);
    return NULL;
}
struct group *getgrnam(const char *name) { return c_getgr(name, 0); }
struct group *getgrgid(gid_t gid) { return c_getgr(NULL, gid); }

/* ---- time, written out [rifle] ----
 * the days between 1970 and a date, counted the way everyone does it. TZ isn't read yet: the time is UTC */
static long c_days(long y, int m, int d)   /* m 1..12 */
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
time_t timegm(struct tm *t)
{
    long y = t->tm_year + 1900 + t->tm_mon / 12; int m = t->tm_mon % 12;
    if (m < 0) { m += 12; y--; }
    return c_days(y, m + 1, 1) * 86400L + (t->tm_mday - 1) * 86400L + t->tm_hour * 3600L + t->tm_min * 60L + t->tm_sec;
}
time_t mktime(struct tm *t) { return timegm(t); }
static const char *c_wday[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *c_month[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September",
                                 "October", "November", "December" };
size_t strftime(char *buf, size_t cap, const char *fmt, const struct tm *t)
{
    size_t n = 0;
    char tmp[64];
    for (; *fmt; fmt++) {
        const char *r = tmp; int pad = 0;
        if (*fmt != '%') { if (n + 1 >= cap) return 0; buf[n++] = *fmt; continue; }
        fmt++;
        if (*fmt == '-' || *fmt == '_' || *fmt == '0') { pad = *fmt++; }   /* %-d no padding, %_d spaces */
        tmp[0] = 0;
        switch (*fmt) {
        case 'a': r = c_wday[t->tm_wday % 7]; snprintf(tmp, 4, "%s", r); r = tmp; break;
        case 'A': r = c_wday[t->tm_wday % 7]; break;
        case 'b': case 'h': snprintf(tmp, 4, "%s", c_month[t->tm_mon % 12]); break;
        case 'B': r = c_month[t->tm_mon % 12]; break;
        case 'c': snprintf(tmp, sizeof tmp, "%.3s %.3s %2d %02d:%02d:%02d %d", c_wday[t->tm_wday % 7], c_month[t->tm_mon % 12],
                           t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, t->tm_year + 1900); break;
        case 'C': snprintf(tmp, sizeof tmp, "%02d", (t->tm_year + 1900) / 100); break;
        case 'd': snprintf(tmp, sizeof tmp, pad == '-' ? "%d" : pad == '_' ? "%2d" : "%02d", t->tm_mday); break;
        case 'D': snprintf(tmp, sizeof tmp, "%02d/%02d/%02d", t->tm_mon + 1, t->tm_mday, t->tm_year % 100); break;
        case 'e': snprintf(tmp, sizeof tmp, pad == '-' ? "%d" : pad == '0' ? "%02d" : "%2d", t->tm_mday); break;
        case 'F': snprintf(tmp, sizeof tmp, "%d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday); break;
        case 'H': snprintf(tmp, sizeof tmp, pad == '-' ? "%d" : pad == '_' ? "%2d" : "%02d", t->tm_hour); break;
        case 'I': snprintf(tmp, sizeof tmp, pad == '-' ? "%d" : pad == '_' ? "%2d" : "%02d", t->tm_hour % 12 ? t->tm_hour % 12 : 12); break;
        case 'j': snprintf(tmp, sizeof tmp, pad == '-' ? "%d" : "%03d", t->tm_yday + 1); break;
        case 'k': snprintf(tmp, sizeof tmp, "%2d", t->tm_hour); break;
        case 'l': snprintf(tmp, sizeof tmp, "%2d", t->tm_hour % 12 ? t->tm_hour % 12 : 12); break;
        case 'm': snprintf(tmp, sizeof tmp, pad == '-' ? "%d" : pad == '_' ? "%2d" : "%02d", t->tm_mon + 1); break;
        case 'M': snprintf(tmp, sizeof tmp, pad == '-' ? "%d" : pad == '_' ? "%2d" : "%02d", t->tm_min); break;
        case 'n': r = "\n"; break;
        case 'p': r = t->tm_hour < 12 ? "AM" : "PM"; break;
        case 'P': r = t->tm_hour < 12 ? "am" : "pm"; break;
        case 'r': snprintf(tmp, sizeof tmp, "%02d:%02d:%02d %s", t->tm_hour % 12 ? t->tm_hour % 12 : 12, t->tm_min, t->tm_sec, t->tm_hour < 12 ? "AM" : "PM"); break;
        case 'R': snprintf(tmp, sizeof tmp, "%02d:%02d", t->tm_hour, t->tm_min); break;
        case 's': { struct tm c = *t; snprintf(tmp, sizeof tmp, "%ld", (long)timegm(&c)); break; }
        case 'S': snprintf(tmp, sizeof tmp, pad == '-' ? "%d" : pad == '_' ? "%2d" : "%02d", t->tm_sec); break;
        case 't': r = "\t"; break;
        case 'T': case 'X': snprintf(tmp, sizeof tmp, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec); break;
        case 'u': snprintf(tmp, sizeof tmp, "%d", t->tm_wday ? t->tm_wday : 7); break;
        case 'w': snprintf(tmp, sizeof tmp, "%d", t->tm_wday); break;
        case 'x': snprintf(tmp, sizeof tmp, "%02d/%02d/%02d", t->tm_mon + 1, t->tm_mday, t->tm_year % 100); break;
        case 'y': snprintf(tmp, sizeof tmp, "%02d", t->tm_year % 100); break;
        case 'Y': snprintf(tmp, sizeof tmp, "%d", t->tm_year + 1900); break;
        case 'z': r = "+0000"; break;
        case 'Z': r = "UTC"; break;
        case '%': r = "%"; break;
        default: snprintf(tmp, sizeof tmp, "%%%c", *fmt); break;
        }
        size_t l = strlen(r);
        if (n + l >= cap) return 0;
        memcpy(buf + n, r, l); n += l;
    }
    if (n >= cap) return 0;
    buf[n] = 0;
    return n;
}
unsigned sleep(unsigned s) { struct timespec t = { s, 0 }; return nanosleep(&t, &t) < 0 ? t.tv_sec : 0; }

/* ---- the environment, changed [rifle] ----
 * the first change copies the list the kernel gave us onto the heap; after that it grows as needed */
static int c_env_own;
static int c_env_len(void) { int n = 0; while (environ && environ[n]) n++; return n; }
static int c_env_copy(void)
{
    if (!c_env_own) {
        int n = c_env_len(), i;
        char **e = malloc((n + 2) * sizeof *e);
        if (!e) return -1;
        for (i = 0; i < n; i++) e[i] = environ[i];
        e[n] = NULL; environ = e; c_env_own = 1;
    }
    return 0;
}
int unsetenv(const char *name)
{
    size_t l = strlen(name); int i, j;
    if (!l || strchr(name, '=')) { errno = EINVAL; return -1; }
    if (c_env_copy()) return -1;
    for (i = j = 0; environ[i]; i++)
        if (!(!strncmp(environ[i], name, l) && environ[i][l] == '=')) environ[j++] = environ[i];
    environ[j] = NULL;
    return 0;
}
int putenv(char *entry)
{
    char *eq = strchr(entry, '='); size_t l; int i, n;
    if (!eq || eq == entry) { errno = EINVAL; return -1; }
    l = eq - entry;
    if (c_env_copy()) return -1;
    for (i = 0; environ[i]; i++)
        if (!strncmp(environ[i], entry, l) && environ[i][l] == '=') { environ[i] = entry; return 0; }
    n = i;
    {
        char **e = realloc(environ, (n + 2) * sizeof *e);
        if (!e) return -1;
        environ = e; e[n] = entry; e[n + 1] = NULL;
    }
    return 0;
}
int setenv(const char *name, const char *value, int overwrite)
{
    size_t l = strlen(name), v = strlen(value); char *e;
    if (!l || strchr(name, '=')) { errno = EINVAL; return -1; }
    if (!overwrite && getenv(name)) return 0;
    if (!(e = malloc(l + v + 2))) return -1;
    memcpy(e, name, l); e[l] = '='; memcpy(e + l + 1, value, v + 1);
    return putenv(e);
}
int clearenv(void) { if (c_env_copy()) return -1; environ[0] = NULL; return 0; }

/* ---- start-up ----
 * the kernel leaves argc, argv and the environment on the stack, then jumps to _start */
int main(); /* no prototype: int main(void), main(argc, argv) and main(argc, argv, envp) all work */
void c_start(long *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    environ = argv + argc + 1;
    exit(main(argc, argv, environ));
}
#ifndef C_NO_START
__asm__(".text\n.global _start\n_start:\n xor %rbp,%rbp\n mov %rsp,%rdi\n and $-16,%rsp\n call c_start\n hlt\n");
#endif

#endif

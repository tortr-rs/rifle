; rifle - coreutils in one asm file
; nasm -f elf64 rifle.asm && ld.lld -s -N -o rifle rifle.o

BITS 64
default rel

%define VERSION "0.1"

; syscalls
%define SYS_read            0
%define SYS_write           1
%define SYS_open            2
%define SYS_close           3
%define SYS_stat            4
%define SYS_fstat           5
%define SYS_lstat           6
%define SYS_lseek           8
%define SYS_mmap            9
%define SYS_munmap          11
%define SYS_rt_sigaction    13
%define SYS_rt_sigprocmask  14
%define SYS_ioctl           16
%define SYS_access          21
%define SYS_mremap          25
%define SYS_dup2            33
%define SYS_nanosleep       35
%define SYS_getpid          39
%define SYS_fork            57
%define SYS_execve          59
%define SYS_wait4           61
%define SYS_kill            62
%define SYS_uname           63
%define SYS_fsync           74
%define SYS_truncate        76
%define SYS_ftruncate       77
%define SYS_getcwd          79
%define SYS_chdir           80
%define SYS_rename          82
%define SYS_mkdir           83
%define SYS_rmdir           84
%define SYS_link            86
%define SYS_unlink          87
%define SYS_symlink         88
%define SYS_readlink        89
%define SYS_chmod           90
%define SYS_chown           92
%define SYS_lchown          94
%define SYS_umask           95
%define SYS_getuid          102
%define SYS_getgid          104
%define SYS_geteuid         107
%define SYS_getegid         108
%define SYS_getgroups       115
%define SYS_rt_sigtimedwait 128
%define SYS_mknod           133
%define SYS_getpriority     140
%define SYS_setpriority     141
%define SYS_chroot          161
%define SYS_sync            162
%define SYS_sethostname     170
%define SYS_sched_getaffinity 204
%define SYS_getdents64      217
%define SYS_clock_gettime   228
%define SYS_exit_group      231
%define SYS_utimensat       280
%define SYS_getrandom       318

; constants
%define O_RDONLY     0
%define O_WRONLY     1
%define O_RDWR       2
%define O_CREAT      0x40
%define O_EXCL       0x80
%define O_NOCTTY     0x100
%define O_TRUNC      0x200
%define O_APPEND     0x400
%define O_NONBLOCK   0x800
%define O_DIRECTORY  0x10000
%define O_NOFOLLOW   0x20000
%define O_CLOEXEC    0x80000
%define AT_FDCWD     -100

%define S_IFMT   0xF000
%define S_IFSOCK 0xC000
%define S_IFLNK  0xA000
%define S_IFREG  0x8000
%define S_IFBLK  0x6000
%define S_IFDIR  0x4000
%define S_IFCHR  0x2000
%define S_IFIFO  0x1000

; struct stat (x86-64)
%define ST_DEV     0
%define ST_INO     8
%define ST_NLINK   16
%define ST_MODE    24
%define ST_UID     28
%define ST_GID     32
%define ST_RDEV    40
%define ST_SIZE    48
%define ST_BLKSIZE 56
%define ST_BLOCKS  64
%define ST_ATIME   72
%define ST_MTIME   88
%define ST_CTIME   104
%define STAT_SIZE  144

%define ENOENT  2
%define EINTR   4
%define EEXIST  17
%define ENOTDIR 20
%define EISDIR  21
%define EINVAL  22

%define OBUFSZ  65536
%define IBUFSZ  65536
%define PATHSZ  4096

; tool table: name, entry, usage
section .atab progbits alloc noexec nowrite align=4
atab:
section .text

%macro APPLET 3
[section .rodata]
%%n: db %1, 0
%%u: db %3, 0
[section .atab]
dd %%n, %2, %%u
__SECT__
%endmacro

; long opts: LONGOPT 'c', "name" ... LONGEND
%macro LONGOPT 2
db %1, %2, 0
%endmacro
%define LONGEND db 0

; data
section .rodata
s_rifle:    db "rifle", 0
s_banner:   db "Rifle ", VERSION, " - The Rifle shoots the herd.", 10
            db "usage: rifle TOOL [ARGS]...   (or run a link named TOOL)", 10
            db "       rifle --list           one tool name per line", 10, 10, 0
s_list:     db "--list", 0
s_unknown:  db "unknown tool: ", 0
s_colsp:    db ": ", 0
s_error:    db "error", 0
s_missing:  db "missing operand", 0
s_extra:    db "extra operand", 0
s_dash:     db "-", 0
s_dashdash: db "--", 0
s_dot:      db ".", 0
s_slash:    db "/", 0
s_y:        db "y", 0
s_usage:    db "Usage: ", 0
s_versuf:   db " (Rifle) ", VERSION, 10, 0
s_help:     db "help", 0
s_version:  db "version", 0
s_badopt:   db "invalid option -- '", 0
s_needarg:  db "option requires an argument -- '", 0
s_unrec:    db "unrecognized option '--", 0
s_ambig:    db "option '--", 0
s_ambig2:   db "' is ambiguous", 0
s_noarg2:   db "' doesn't allow an argument", 0
s_needarg2: db "' requires an argument", 0
s_quote:    db "'", 0
s_try1:     db "Try '", 0
s_try2:     db " --help' for more information.", 0
s_badnum:   db "invalid number: ", 0
s_notdir:   db "target is not a directory: ", 0
s_samefile: db "are the same file", 0
s_omitdir:  db "omitting directory", 0
s_tmpl:     db "tmp.XXXXXXXXXX", 0
s_tmp:      db "/tmp", 0
s_TMPDIR:   db "TMPDIR", 0
s_PATH:     db "PATH", 0
s_defpath:  db "/usr/local/bin:/bin:/usr/bin", 0
s_SHELL:    db "SHELL", 0
s_binsh:    db "/bin/sh", 0
s_dashi:    db "-i", 0
s_passwd:   db "/etc/passwd", 0
s_group:    db "/etc/group", 0
s_nohupout: db "nohup.out", 0
s_ignoring: db "ignoring input and appending output to 'nohup.out'", 0
s_ignoring2: db "ignoring input", 0
s_proc_fd0: db "/proc/self/fd/0", 0
s_notatty:  db "not a tty", 10, 0
s_loginuid: db "/proc/self/loginuid", 0
s_nologin:  db "no login name", 0
s_hostid:   db "/etc/hostid", 0
s_OMP:      db "OMP_NUM_THREADS", 0
s_OMPL:     db "OMP_THREAD_LIMIT", 0
s_cpupresent: db "/sys/devices/system/cpu/present", 0

errtab:
    db 1,  "Operation not permitted", 0
    db 2,  "No such file or directory", 0
    db 3,  "No such process", 0
    db 4,  "Interrupted system call", 0
    db 5,  "Input/output error", 0
    db 7,  "Argument list too long", 0
    db 8,  "Exec format error", 0
    db 9,  "Bad file descriptor", 0
    db 12, "Cannot allocate memory", 0
    db 13, "Permission denied", 0
    db 14, "Bad address", 0
    db 16, "Device or resource busy", 0
    db 17, "File exists", 0
    db 18, "Invalid cross-device link", 0
    db 19, "No such device", 0
    db 20, "Not a directory", 0
    db 21, "Is a directory", 0
    db 22, "Invalid argument", 0
    db 24, "Too many open files", 0
    db 25, "Inappropriate ioctl for device", 0
    db 26, "Text file busy", 0
    db 27, "File too large", 0
    db 28, "No space left on device", 0
    db 29, "Illegal seek", 0
    db 30, "Read-only file system", 0
    db 31, "Too many links", 0
    db 32, "Broken pipe", 0
    db 34, "Numerical result out of range", 0
    db 36, "File name too long", 0
    db 38, "Function not implemented", 0
    db 39, "Directory not empty", 0
    db 40, "Too many levels of symbolic links", 0
    db 95, "Operation not supported", 0
    db 122, "Disk quota exceeded", 0
    db 0

section .bss
alignb 16
obuf:     resb OBUFSZ
ibuf:     resb IBUFSZ
pbuf:     resb PATHSZ          ; path scratch
pbuf2:    resb PATHSZ
pbuf3:    resb PATHSZ
ebuf:     resb 1024
statbuf:  resb STAT_SIZE
statbuf2: resb STAT_SIZE
numbuf:   resb 32
utsbuf:   resb 390
olen:     resq 1
elen:     resq 1
envp:     resq 1
prog:     resq 1               ; tool name for messages
cur_ap:   resq 1               ; applet table entry
; option parser state
g_argc:   resq 1
g_argv:   resq 1
g_idx:    resq 1
g_out:    resq 1
g_sub:    resq 1
g_spec:   resq 1
g_long:   resq 1
g_stop:   resq 1
optarg:   resq 1
nop:      resq 1               ; operand count after parsing; operands at argv[1..]
; misc
status:   resq 1
tmpq:     resq 4

section .text
global _start

; entry and dispatch
_start:
    mov r12, [rsp]                  ; argc
    lea r13, [rsp+8]                ; argv
    lea rax, [r13+r12*8+8]
    mov [envp], rax
    mov rdi, [r13]
    call basename_of
    mov rbx, rax
    mov rdi, rbx
    lea rsi, [s_rifle]
    call streq
    jne .lookup
        cmp r12, 2
    jb .banner
    mov rdi, [r13+8]
    lea rsi, [s_list]
    call streq
    je .list
    dec r12
    add r13, 8
    mov rbx, [r13]
.lookup:
    lea r14, [atab]
.find:
    lea rax, [atab_end]
    cmp r14, rax
    jae .unknown
    mov esi, [r14]
    mov rdi, rbx
    call streq
    je .found
    add r14, 12
    jmp .find
.found:
    mov [prog], rbx
    mov [cur_ap], r14
    mov rdi, r12
    mov rsi, r13
    mov eax, [r14+4]
    call rax
    mov edi, eax
    jmp exit

.unknown:
    lea rax, [s_rifle]
    mov [prog], rax
    call e_prog
    lea rsi, [s_unknown]
    call e_add
    mov rsi, rbx
    call e_add
    call e_out
    mov edi, 127
    jmp sys_exit

.banner:
    lea rsi, [s_banner]
    call out_str
    xor ebx, ebx                    ; column
    lea r14, [atab]
.bl:
    lea rax, [atab_end]
    cmp r14, rax
    jae .bend
    mov edi, [r14]
    call strlen
    lea rbx, [rbx+rax+1]
    cmp rbx, 78
    jb .bsame
    mov edi, 10
    call out_ch
    mov edi, [r14]
    call strlen
    lea rbx, [rax+1]
.bsame:
    mov esi, [r14]
    call out_str
    mov edi, ' '
    call out_ch
    add r14, 12
    jmp .bl
.bend:
    mov edi, 10
    call out_ch
    xor edi, edi
    jmp exit

.list:
    lea r14, [atab]
.ll:
    lea rax, [atab_end]
    cmp r14, rax
    jae .lend
    mov esi, [r14]
    call out_str
    mov edi, 10
    call out_ch
    add r14, 12
    jmp .ll
.lend:
    xor edi, edi
    jmp exit

exit:
    push rdi
    call flush
    pop rdi
sys_exit:
    mov eax, SYS_exit_group
    syscall

; string helpers

; strlen(rdi) -> rax
strlen:
    mov rax, rdi
.l: cmp byte [rax], 0
    je .d
    inc rax
    jmp .l
.d: sub rax, rdi
    ret

; streq(rdi, rsi) -> zf
streq:
.l: mov al, [rdi]
    cmp al, [rsi]
    jne .r
    inc rdi
    inc rsi
    test al, al
    jnz .l
.r: ret

; memeq(rdi, rsi, rdx) -> zf
memeq:
    test rdx, rdx
    jz .r
.l: mov al, [rdi]
    cmp al, [rsi]
    jne .r
    inc rdi
    inc rsi
    dec rdx
    jnz .l
.r: ret

; startswith(rdi, rsi) -> zf, rdi past prefix
startswith:
.l: mov al, [rsi]
    test al, al
    jz .r                           ; ZF=1
    cmp al, [rdi]
    jne .r
    inc rdi
    inc rsi
    jmp .l
.r: ret

; strcpy(rdi dst, rsi src) -> rdi at nul
strcpy:
.l: mov al, [rsi]
    mov [rdi], al
    test al, al
    jz .d
    inc rdi
    inc rsi
    jmp .l
.d: ret

; strchr(rdi, sil) -> rax or 0
strchr:
.l: mov al, [rdi]
    cmp al, sil
    je .f
    test al, al
    jz .n
    inc rdi
    jmp .l
.f: mov rax, rdi
    ret
.n: xor eax, eax
    ret

; basename_of(rdi) -> rax
basename_of:
    mov rax, rdi
.l: mov cl, [rdi]
    test cl, cl
    jz .d
    inc rdi
    cmp cl, '/'
    jne .l
    mov rax, rdi
    jmp .l
.d: ret

; path_join(rdi, rsi) -> rax = pbuf3 "dir/name"
path_join:
    push rsi
    mov rsi, rdi
    lea rdi, [pbuf3]
    call strcpy
    lea rax, [pbuf3]
    cmp rdi, rax
    je .ns
    cmp byte [rdi-1], '/'
    je .ns
    mov byte [rdi], '/'
    inc rdi
.ns:
    pop rsi
    call strcpy
    lea rax, [pbuf3]
    ret

; getenv(rdi) -> rax or 0
getenv:
    mov rdx, rdi
    mov r8, [envp]
.l: mov rsi, [r8]
    test rsi, rsi
    jz .n
    mov rdi, rdx
.c: mov al, [rdi]
    test al, al
    jz .end
    cmp al, [rsi]
    jne .nx
    inc rdi
    inc rsi
    jmp .c
.end:
    cmp byte [rsi], '='
    jne .nx
    lea rax, [rsi+1]
    ret
.nx:
    add r8, 8
    jmp .l
.n: xor eax, eax
    ret

; output

; out_ch(dil)
out_ch:
    mov rax, [olen]
    lea rcx, [obuf]
    mov [rcx+rax], dil
    inc rax
    mov [olen], rax
    cmp rax, OBUFSZ
    jae flush
    ret

; out_str(rsi)
out_str:
    mov rdi, rsi
    call strlen
    mov rdx, rax
; out_mem(rsi, rdx)
out_mem:
.top:
    test rdx, rdx
    jz .r
    mov rax, [olen]
    mov rcx, OBUFSZ
    sub rcx, rax
    cmp rcx, rdx
    jbe .n
    mov rcx, rdx
.n: lea rdi, [obuf]
    add rdi, rax
    add rax, rcx
    mov [olen], rax
    sub rdx, rcx
    rep movsb
    cmp qword [olen], OBUFSZ
    jb .top
    push rsi
    push rdx
    call flush
    pop rdx
    pop rsi
    jmp .top
.r: ret

out_nl:
    mov edi, 10
    jmp out_ch

out_sp:
    mov edi, ' '
    jmp out_ch

; flush stdout, exit 1 on write error
flush:
    mov rdx, [olen]
    test rdx, rdx
    jz .r
    mov qword [olen], 0
    lea rsi, [obuf]
    mov edi, 1
    call write_all
    test rax, rax
    js .fail
.r: ret
.fail:
    mov edi, 1
    jmp sys_exit

; write_all(edi, rsi, rdx) -> rax 0/-errno
write_all:
.l: test rdx, rdx
    jz .ok
    mov eax, SYS_write
    syscall
    cmp rax, -EINTR
    je .l
    test rax, rax
    js .r
    add rsi, rax
    sub rdx, rax
    jmp .l
.ok:
    xor eax, eax
.r: ret

; read with EINTR retry
read_fd:
    xor eax, eax
    syscall
    cmp rax, -EINTR
    je read_fd
    ret

out_u:
    call fmt_u
    jmp out_mem
; fmt_u(rdi) -> rsi, rdx
fmt_u:
    mov rax, rdi
    lea rsi, [numbuf+32]
    mov ecx, 10
.l: xor edx, edx
    div rcx
    add dl, '0'
    dec rsi
    mov [rsi], dl
    test rax, rax
    jnz .l
    lea rdx, [numbuf+32]
    sub rdx, rsi
    ret

out_s:
    test rdi, rdi
    jns out_u
    push rdi
    mov edi, '-'
    call out_ch
    pop rdi
    neg rdi
    jmp out_u

; out_upad(rdi, esi width, dl pad)
out_upad:
    push rbx
    push rbp
    mov ebx, esi
    movzx ebp, dl
    call fmt_u
.p: cmp rdx, rbx
    jae .go
    push rsi
    push rdx
    mov edi, ebp
    call out_ch
    pop rdx
    pop rsi
    inc rdx                          ; count the pad
    jmp .p
.go:
    lea rdx, [numbuf+32]
    sub rdx, rsi
    pop rbp
    pop rbx
    jmp out_mem

; out_hex(rdi, esi digits)
out_hex:
    lea r8, [numbuf+32]
    mov rcx, rsi
.l: mov eax, edi
    and eax, 15
    cmp eax, 10
    jb .d
    add eax, 'a'-10-'0'
.d: add eax, '0'
    dec r8
    mov [r8], al
    shr rdi, 4
    dec rcx
    jnz .l
    mov rsi, r8
    lea rdx, [numbuf+32]
    sub rdx, r8
    jmp out_mem

; errors

; e_add(rsi string)
e_add:
    mov rdi, [elen]
    lea rcx, [ebuf]
.l: mov al, [rsi]
    test al, al
    jz .d
    cmp rdi, 1000
    jae .d
    mov [rcx+rdi], al
    inc rdi
    inc rsi
    jmp .l
.d: mov [elen], rdi
    ret

; e_addc(dil)
e_addc:
    mov rax, [elen]
    lea rcx, [ebuf]
    mov [rcx+rax], dil
    inc rax
    mov [elen], rax
    ret

e_prog:
    call flush
    mov qword [elen], 0
    mov rsi, [prog]
    call e_add
    lea rsi, [s_colsp]
    jmp e_add

e_out:
    mov rdi, [elen]
    lea rsi, [ebuf]
    mov byte [rsi+rdi], 10
    lea rdx, [rdi+1]
    mov qword [elen], 0
    mov edi, 2
    jmp write_all

e_errno:
    mov eax, esi
    neg eax
    lea rsi, [errtab]
.f: movzx ecx, byte [rsi]
    test ecx, ecx
    jz .u
    inc rsi
    cmp ecx, eax
    je e_add
.s: cmp byte [rsi], 0
    lea rsi, [rsi+1]
    jne .s
    jmp .f
.u: lea rsi, [s_error]
    jmp e_add

; perr(rdi name or 0, esi -errno)
perr:
    push rbx
    push r12
    mov rbx, rdi
    mov r12d, esi
    call e_prog
    test rbx, rbx
    jz .n
    mov rsi, rbx
    call e_add
    lea rsi, [s_colsp]
    call e_add
.n: mov esi, r12d
    call e_errno
    call e_out
    pop r12
    pop rbx
    ret

; same, name quoted
perrq:
    push rbx
    push r12
    mov rbx, rdi
    mov r12d, esi
    call e_prog
    lea rsi, [s_quote]
    call e_add
    mov rsi, rbx
    call e_add
    lea rsi, [s_quote]
    call e_add
    lea rsi, [s_colsp]
    call e_add
    mov esi, r12d
    call e_errno
    call e_out
    pop r12
    pop rbx
    ret

emsg:
    push rdi
    call e_prog
    pop rsi
    call e_add
    jmp e_out

emsg2:
    push rsi
    push rdi
    call e_prog
    pop rsi
    call e_add
    pop rsi
    call e_add
    jmp e_out

die:
    call emsg
    mov edi, 1
    jmp exit

missing:
    lea rdi, [s_missing]
    jmp usage_err
extra:
    lea rdi, [s_extra]
usage_err:
    call emsg
try_help:
    lea rsi, [s_try1]
    call e_add
    mov rsi, [prog]
    call e_add
    lea rsi, [s_try2]
    call e_add
    call e_out
    mov edi, 1
    jmp exit

; option parsing, gnu style
opt_init:
    mov [g_argc], rdi
    mov [g_argv], rsi
    mov qword [g_idx], 1
    mov qword [g_out], 1
    mov qword [g_sub], 0
    mov qword [g_stop], 0
    cmp byte [rdx], '+'
    jne .s
    inc rdx
    mov qword [g_stop], 1
.s: mov [g_spec], rdx
    mov [g_long], rcx
    ret

; spec_find(al) -> zf, ecx=1 if it takes an arg
spec_find:
    mov rdx, [g_spec]
.l: mov cl, [rdx]
    test cl, cl
    jz .no
    cmp cl, ':'
    je .skip
    cmp cl, al
    je .yes
.skip:
    inc rdx
    jmp .l
.yes:
    xor ecx, ecx                    ; 0 no arg, 1 arg, 2 optional arg ("::")
    cmp byte [rdx+1], ':'
    jne .y1
    inc ecx
    cmp byte [rdx+2], ':'
    jne .y1
    inc ecx
.y1:
    cmp eax, eax                    ; ZF=1
    ret
.no:
    or edx, 1                       ; ZF=0
    ret

opt_next:
    mov rsi, [g_sub]
    test rsi, rsi
    jnz .short
.arg:
    mov rax, [g_idx]
    cmp rax, [g_argc]
    jae .finish
    mov rdx, [g_argv]
    mov rsi, [rdx+rax*8]
    cmp byte [rsi], '-'
    jne .operand
    cmp byte [rsi+1], 0
    je .operand
    inc qword [g_idx]
    cmp byte [rsi+1], '-'
    je .long
    inc rsi
.short:
    movzx eax, byte [rsi]
    inc rsi
    mov [g_sub], rsi
    cmp byte [rsi], 0
    jne .s1
    mov qword [g_sub], 0
.s1:
    push rsi
    push rax
    call spec_find
    pop rax
    pop rsi
    jne .badshort
    test ecx, ecx
    jz .ret
    cmp ecx, 2
    je .optshort
    cmp byte [rsi], 0
    je .nextword
    mov [optarg], rsi
    mov qword [g_sub], 0
    ret
.nextword:
    mov rcx, [g_idx]
    cmp rcx, [g_argc]
    jae .needshort
    mov rdx, [g_argv]
    mov rdx, [rdx+rcx*8]
    mov [optarg], rdx
    inc qword [g_idx]
.ret:
    ret
.optshort:                          ; optional arg: only when attached, -xARG
    mov qword [optarg], 0
    cmp byte [rsi], 0
    je .ret
    mov [optarg], rsi
    mov qword [g_sub], 0
    ret

.operand:
    cmp qword [g_stop], 0
    jne .rest
    mov rcx, [g_out]
    mov [rdx+rcx*8], rsi
    inc qword [g_out]
    inc qword [g_idx]
    jmp .arg

.rest:
    mov rax, [g_idx]
    mov rdx, [g_argv]
.rl:
    cmp rax, [g_argc]
    jae .finish
    mov rsi, [rdx+rax*8]
    mov rcx, [g_out]
    mov [rdx+rcx*8], rsi
    inc qword [g_out]
    inc rax
    jmp .rl

.finish:
    mov rax, [g_out]
    mov rdx, [g_argv]
    mov qword [rdx+rax*8], 0
    dec rax
    mov [nop], rax
    xor eax, eax
    ret

.long:
    add rsi, 2
    cmp byte [rsi], 0
    jne .lname
    mov rax, [g_idx]                ; "--"
    mov rdx, [g_argv]
    jmp .rl
.lname:
    push rbx
    push r12
    push r13
    push r14
    mov rbx, rsi                    ; name (may contain '=')
    ; length of the name part
    mov r12, rsi
.ln: mov al, [r12]
    test al, al
    jz .lnd
    cmp al, '='
    je .lnd
    inc r12
    jmp .ln
.lnd:
    sub r12, rbx                    ; r12 = name length
    ; --help / --version
    mov rdi, rbx
    lea rsi, [s_help]
    mov rdx, r12
    call long_is
    je do_help
    mov rdi, rbx
    lea rsi, [s_version]
    mov rdx, r12
    call long_is
    je do_version
    xor r13d, r13d                  ; match entry
    xor r14d, r14d                  ; number of prefix matches
    mov r8, [g_long]
    test r8, r8
    jz .lnone
.lt:
    movzx eax, byte [r8]
    test eax, eax
    jz .ltend
    lea rdi, [r8+1]
    mov rsi, rbx
    mov rdx, r12
    push r8
    call memeq
    pop r8
    jne .ltnext
    lea rdi, [r8+1]
    cmp byte [rdi+r12], 0
    je .exact
    inc r14
    mov r13, r8
.ltnext:
    lea rdi, [r8+1]
    push r8
    call strlen
    pop r8
    lea r8, [r8+rax+2]
    jmp .lt
.exact:
    mov r13, r8
    mov r14d, 1
.ltend:
    cmp r14, 1
    je .lfound
    ja .lambig
.lnone:
    call e_prog
    lea rsi, [s_unrec]
    call e_add
    mov byte [rbx+r12], 0
    mov rsi, rbx
    call e_add
    lea rsi, [s_quote]
    call e_add
    call e_out
    jmp try_help
.lambig:
    call e_prog
    lea rsi, [s_ambig]
    call e_add
    mov byte [rbx+r12], 0
    mov rsi, rbx
    call e_add
    lea rsi, [s_ambig2]
    call e_add
    call e_out
    jmp try_help
.lfound:
    movzx eax, byte [r13]
    push rax
    call spec_find
    pop rax
    lea r9, [rbx+r12]               ; '=' or NUL
    test ecx, ecx
    jz .lnoarg
    cmp ecx, 2
    jne .lreq
    mov qword [optarg], 0           ; optional: only --name=ARG
    cmp byte [r9], '='
    jne .lret
    inc r9
    mov [optarg], r9
    jmp .lret
.lreq:
    cmp byte [r9], '='
    jne .lnext
    inc r9
    mov [optarg], r9
    jmp .lret
.lnext:
    mov rcx, [g_idx]
    cmp rcx, [g_argc]
    jae .lneed
    mov rdx, [g_argv]
    mov rdx, [rdx+rcx*8]
    mov [optarg], rdx
    inc qword [g_idx]
    jmp .lret
.lnoarg:
    cmp byte [r9], '='
    je .lextra
.lret:
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
.lneed:
    lea rsi, [s_needarg2]
    jmp .lerr
.lextra:
    lea rsi, [s_noarg2]
.lerr:
    push rsi
    call e_prog
    lea rsi, [s_ambig]              ; "option '--"
    call e_add
    mov byte [rbx+r12], 0
    lea rsi, [r13+1]
    call e_add
    pop rsi
    call e_add
    call e_out
    jmp try_help

.badshort:
    push rax
    call e_prog
    lea rsi, [s_badopt]
    jmp .shorterr
.needshort:
    push rax
    call e_prog
    lea rsi, [s_needarg]
.shorterr:
    call e_add
    pop rdi
    call e_addc
    lea rsi, [s_quote]
    call e_add
    call e_out
    jmp try_help

long_is:
    push rdx
    push rsi
    call memeq
    pop rsi
    pop rdx
    jne .r
    cmp byte [rsi+rdx], 0
    je .r                           ; exact
    cmp rdx, 3
    jb .no
    cmp eax, eax
    ret
.no: or edx, 1
.r:  ret

do_help:
    lea rsi, [s_usage]
    call out_str
    mov rsi, [prog]
    call out_str
    call out_sp
    mov rax, [cur_ap]
    mov esi, [rax+8]
    call out_str
    call out_nl
    xor edi, edi
    jmp exit

do_version:
    mov rsi, [prog]
    call out_str
    lea rsi, [s_versuf]
    call out_str
    xor edi, edi
    jmp exit

opts_none:
    lea rdx, [s_empty]
    xor ecx, ecx
    call opt_init
    jmp opt_next

; numbers

; parse_u(rdi) -> rax, cf on error
parse_u:
    xor eax, eax
    cmp byte [rdi], 0
    je .bad
.l: movzx ecx, byte [rdi]
    test ecx, ecx
    jz .ok
    sub ecx, '0'
    cmp ecx, 9
    ja .bad
    imul rax, rax, 10
    jo .bad
    add rax, rcx
    jc .bad
    inc rdi
    jmp .l
.ok: clc
    ret
.bad: stc
    ret

; parse_s: signed, blanks around ok (test wants that)
parse_s:
    push rbx
.ws: mov al, [rdi]
    cmp al, ' '
    je .wsn
    cmp al, 9
    jne .sgn
.wsn: inc rdi
    jmp .ws
.sgn:
    xor ebx, ebx
    cmp al, '-'
    jne .plus
    inc ebx
    inc rdi
    jmp .dig
.plus:
    cmp al, '+'
    jne .dig
    inc rdi
.dig:
    xor eax, eax
    movzx ecx, byte [rdi]
    sub ecx, '0'
    cmp ecx, 9
    ja .bad
.l: movzx ecx, byte [rdi]
    sub ecx, '0'
    cmp ecx, 9
    ja .end
    imul rax, rax, 10
    jo .bad
    add rax, rcx
    jo .bad
    inc rdi
    jmp .l
.end:
    mov cl, [rdi]
    cmp cl, ' '
    je .tws
    cmp cl, 9
    je .tws
    test cl, cl
    jnz .bad
    test ebx, ebx
    jz .ok
    neg rax
.ok: pop rbx
    clc
    ret
.tws: inc rdi
    jmp .end
.bad: pop rbx
    stc
    ret

parse_oct:
    xor eax, eax
    cmp byte [rdi], 0
    je .bad
.l: movzx ecx, byte [rdi]
    test ecx, ecx
    jz .ok
    sub ecx, '0'
    cmp ecx, 7
    ja .bad
    shl rax, 3
    or rax, rcx
    inc rdi
    jmp .l
.ok: clc
    ret
.bad: stc
    ret

; parse_size: 10K, 5MB, 2GiB, 4b ...
parse_size:
    push rbx
    xor eax, eax
    movzx ecx, byte [rdi]
    sub ecx, '0'
    cmp ecx, 9
    ja .bad
.l: movzx ecx, byte [rdi]
    sub ecx, '0'
    cmp ecx, 9
    ja .suf
    imul rax, rax, 10
    add rax, rcx
    inc rdi
    jmp .l
.suf:
    movzx ecx, byte [rdi]
    test ecx, ecx
    jz .ok
    cmp ecx, 'b'
    jne .s1
    shl rax, 9
    inc rdi
    jmp .tail
.s1:
    lea rsi, [s_sizesuf]
    xor ebx, ebx
.sl: mov dl, [rsi+rbx]
    test dl, dl
    jz .bad
    inc ebx
    cmp dl, cl
    je .sf
    add dl, 32                      ; lower case k is common
    cmp dl, cl
    je .sf
    jmp .sl
.sf:
    inc rdi
    mov r8d, 1024
    cmp byte [rdi], 'B'
    jne .si
    mov r8d, 1000
    inc rdi
    jmp .mul
.si:
    cmp byte [rdi], 'i'
    jne .mul
    cmp byte [rdi+1], 'B'
    jne .bad
    add rdi, 2
.mul:
    imul rax, r8
    dec ebx
    jnz .mul
.tail:
    cmp byte [rdi], 0
    jne .bad
.ok: pop rbx
    clc
    ret
.bad: pop rbx
    stc
    ret

; files

; open_ro(rdi) -> fd/-errno, "-" is stdin
open_ro:
    cmp byte [rdi], '-'
    jne .o
    cmp byte [rdi+1], 0
    jne .o
    xor eax, eax
    ret
.o: xor esi, esi
    mov eax, SYS_open
    syscall
    ret

close_fd:
    cmp edi, 2
    jbe .r
    mov eax, SYS_close
    syscall
.r: ret

; copy_fd(edi in, esi out) -> rax 0/-errno
copy_fd:
    push rbx
    push rbp
    mov ebx, edi
    mov ebp, esi
    call flush
.l: mov edi, ebx
    lea rsi, [ibuf]
    mov edx, IBUFSZ
    call read_fd
    test rax, rax
    jle .d
    mov rdx, rax
    mov edi, ebp
    lea rsi, [ibuf]
    call write_all
    test rax, rax
    jns .l
    mov edi, 1
    jmp exit
.d: pop rbp
    pop rbx
    ret

mmap_anon:
    mov rsi, rdi
    xor edi, edi
    mov edx, 3                      ; PROT_READ|PROT_WRITE
    mov r10d, 0x22                  ; MAP_PRIVATE|MAP_ANONYMOUS
    mov r8, -1
    xor r9d, r9d
    mov eax, SYS_mmap
    syscall
    cmp rax, -4096
    ja oom
    ret
oom:
    mov esi, -12
    xor edi, edi
    call perr
    mov edi, 1
    jmp exit

; slurp(edi) -> rax buf, rdx len, rcx err. one spare byte at the end
slurp:
    push rbx
    push rbp
    push r12
    push r13
    mov ebx, edi
    mov r12d, 1 << 20               ; capacity
    mov rdi, r12
    call mmap_anon
    mov rbp, rax
    xor r13d, r13d                  ; length
.l: mov rdx, r12
    sub rdx, r13
    dec rdx
    jnz .rd
    mov rdi, rbp
    mov rsi, r12
    lea rdx, [r12*2]
    mov r10d, 1
    mov eax, SYS_mremap
    syscall
    cmp rax, -4096
    ja oom
    mov rbp, rax
    shl r12, 1
    jmp .l
.rd:
    mov edi, ebx
    lea rsi, [rbp+r13]
    call read_fd
    test rax, rax
    jz .ok
    js .err
    add r13, rax
    jmp .l
.ok: xor ecx, ecx
    jmp .d
.err: mov rcx, rax
.d: mov rax, rbp
    mov rdx, r13
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

slurp_path:
    push rbx
    call open_ro
    test rax, rax
    js .e
    mov ebx, eax
    mov edi, eax
    call slurp
    push rax
    push rcx
    push rdx
    mov edi, ebx
    call close_fd
    pop rdx
    pop rcx
    pop rax
    pop rbx
    ret
.e: mov rcx, rax
    xor eax, eax
    xor edx, edx
    pop rbx
    ret

is_dir:
    lea rsi, [statbuf2]
    mov eax, SYS_stat
    syscall
    test rax, rax
    js .no
    mov eax, [statbuf2+ST_MODE]
    and eax, S_IFMT
    cmp eax, S_IFDIR
    ret
.no: or eax, 1
    ret

isatty:
    mov esi, 0x5401                 ; TCGETS
    lea rdx, [utsbuf]
    mov eax, SYS_ioctl
    syscall
    test rax, rax
    ret

; exec

; execvp(rdi, rsi) -> -errno
execvp:
    push rbx
    push rbp
    push r12
    push r13
    mov rbx, rdi
    mov rbp, rsi
    mov esi, '/'
    call strchr
    test rax, rax
    jz .search
    mov rdi, rbx
    mov rsi, rbp
    mov rdx, [envp]
    mov eax, SYS_execve
    syscall
    jmp .r
.search:
    lea rdi, [s_PATH]
    call getenv
    test rax, rax
    jnz .p
    lea rax, [s_defpath]
.p: mov r12, rax                    ; walking PATH
    mov r13, -ENOENT                ; error to report
.dir:
    lea rdi, [pbuf3]
.cp: mov al, [r12]
    test al, al
    jz .cpend
    cmp al, ':'
    je .cpend
    mov [rdi], al
    inc rdi
    inc r12
    jmp .cp
.cpend:
    lea rax, [pbuf3]
    cmp rdi, rax
    jne .ne
    mov byte [rdi], '.'             ; empty entry = current directory
    inc rdi
.ne: mov byte [rdi], '/'
    inc rdi
    mov rsi, rbx
    call strcpy
    lea rdi, [pbuf3]
    mov rsi, rbp
    mov rdx, [envp]
    mov eax, SYS_execve
    syscall
    cmp rax, -ENOENT
    je .next
    cmp rax, -ENOTDIR
    je .next
    mov r13, rax                    ; EACCES etc: remember, keep looking
.next:
    cmp byte [r12], 0
    je .fail
    inc r12
    jmp .dir
.fail:
    mov rax, r13
.r: pop r13
    pop r12
    pop rbp
    pop rbx
    ret

exec_or_die:
    push rdi
    call flush
    pop rdi
    push rdi
    call execvp
    pop rdi
    mov rbx, rax
    mov esi, eax
    call perrq
    mov edi, 127
    cmp rbx, -ENOENT
    je .x
    mov edi, 126
.x: jmp exit

; passwd/group
section .bss
pw_buf:  resq 1
pw_len:  resq 1
gr_buf:  resq 1
gr_len:  resq 1
db_fld:  resq 8                     ; field pointers of the current line
db_flen: resq 8
section .text

load_db:
    cmp qword [rsi], 0
    jne .r
    push rsi
    push rdx
    call slurp_path
    pop r8
    pop r9
    test rax, rax
    jz .r
    mov [r9], rax
    mov [r8], rdx
.r: ret

; db_scan(rdi buf, rsi len, rdx field, rcx key, r8 keylen) -> zf, db_fld/db_flen
db_scan:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov rbx, rdi                    ; cursor
    lea rbp, [rdi+rsi]              ; end
    mov r12, rdx
    mov r13, rcx
    mov r14, r8
.line:
    cmp rbx, rbp
    jae .nf
    xor r15d, r15d
    lea rax, [db_fld]
    mov [rax], rbx
.f: cmp rbx, rbp
    jae .eol
    mov cl, [rbx]
    cmp cl, 10
    je .eol
    cmp cl, ':'
    jne .fn
    ; end of field r15
    lea rax, [db_fld]
    mov rdx, rbx
    sub rdx, [rax+r15*8]
    lea rax, [db_flen]
    mov [rax+r15*8], rdx
    cmp r15, 7
    jae .fn
    inc r15
    lea rax, [db_fld]
    lea rdx, [rbx+1]
    mov [rax+r15*8], rdx
.fn: inc rbx
    jmp .f
.eol:
    lea rax, [db_fld]
    mov rdx, rbx
    sub rdx, [rax+r15*8]
    lea rax, [db_flen]
    mov [rax+r15*8], rdx
    inc rbx                         ; past the newline
    lea rax, [db_flen]
.z: inc r15
    cmp r15, 8
    jae .cmp
    mov qword [rax+r15*8], 0
    jmp .z
.cmp:
    lea rax, [db_flen]
    cmp [rax+r12*8], r14
    jne .line
    lea rax, [db_fld]
    mov rdi, [rax+r12*8]
    mov rsi, r13
    mov rdx, r14
    call memeq
    jne .line
    cmp eax, eax
    jmp .d
.nf: or eax, 1
.d: pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

; name_by_id(rdi, esi 0=user 1=group) -> zf, rsi, rdx
name_by_id:
    push rbx
    push rbp
    mov ebp, esi
    call fmt_u                      ; id as text in numbuf
    mov rbx, rsi
    push rdx
    test ebp, ebp
    jnz .g
    lea rdi, [s_passwd]
    lea rsi, [pw_buf]
    lea rdx, [pw_len]
    call load_db
    mov rdi, [pw_buf]
    mov rsi, [pw_len]
    jmp .s
.g: lea rdi, [s_group]
    lea rsi, [gr_buf]
    lea rdx, [gr_len]
    call load_db
    mov rdi, [gr_buf]
    mov rsi, [gr_len]
.s: pop r8
    test rdi, rdi
    jz .nf
    mov edx, 2
    mov rcx, rbx
    call db_scan
    jne .nf
    mov rsi, [db_fld]
    mov rdx, [db_flen]
    cmp eax, eax
    jmp .d
.nf: or eax, 1
.d: pop rbp
    pop rbx
    ret

out_name_or_id:
    push rdi
    call name_by_id
    pop rdi
    jne out_u
    jmp out_mem

; user_by_name(rdi) -> zf, rax uid, rdx gid
user_by_name:
    push rbx
    mov rbx, rdi
    lea rdi, [s_passwd]
    lea rsi, [pw_buf]
    lea rdx, [pw_len]
    call load_db
    mov rdi, rbx
    call strlen
    mov r8, rax
    mov rcx, rbx
    xor edx, edx
    mov rdi, [pw_buf]
    mov rsi, [pw_len]
    test rdi, rdi
    jz .nf
    call db_scan
    jne .nf
    mov rdi, [db_fld+16]
    mov rsi, [db_flen+16]
    call num_n
    push rax
    mov rdi, [db_fld+24]
    mov rsi, [db_flen+24]
    call num_n
    mov rdx, rax
    pop rax
    cmp ecx, ecx
    pop rbx
    ret
.nf: or ecx, 1
    pop rbx
    ret

num_n:
    xor eax, eax
.l: test rsi, rsi
    jz .d
    movzx ecx, byte [rdi]
    sub ecx, '0'
    imul rax, rax, 10
    add rax, rcx
    inc rdi
    dec rsi
    jmp .l
.d: ret

section .rodata
s_empty: db 0
s_sizesuf: db "KMGTPE", 0
section .text

; ---- true, false

APPLET "true", t_true, "[ignored]"
t_true:
    xor eax, eax
    ret

APPLET "false", t_false, "[ignored]"
t_false:
    mov eax, 1
    ret

; ---- echo

APPLET "echo", t_echo, "[-neE] [STRING]..."
t_echo:
    mov r12, rdi
    mov r13, rsi
    mov ebx, 1
    mov r14d, 1                     ; trailing newline
    xor r15d, r15d                  ; -e
.opt:
    cmp rbx, r12
    jae .go
    mov rsi, [r13+rbx*8]
    cmp byte [rsi], '-'
    jne .go
    inc rsi
    cmp byte [rsi], 0
    je .go
    mov rdi, rsi
.chk:                               ; only n, e, E make it an option
    mov al, [rdi]
    test al, al
    jz .set
    cmp al, 'n'
    je .ok
    cmp al, 'e'
    je .ok
    cmp al, 'E'
    jne .go
.ok:
    inc rdi
    jmp .chk
.set:
    mov al, [rsi]
    test al, al
    jz .next
    cmp al, 'n'
    jne .s1
    xor r14d, r14d
.s1:
    cmp al, 'e'
    jne .s2
    mov r15d, 1
.s2:
    cmp al, 'E'
    jne .s3
    xor r15d, r15d
.s3:
    inc rsi
    jmp .set
.next:
    inc rbx
    jmp .opt
.go:
    mov rbp, rbx
.w:
    cmp rbx, r12
    jae .end
    cmp rbx, rbp
    je .nosp
    call out_sp
.nosp:
    mov rsi, [r13+rbx*8]
    test r15d, r15d
    jnz .esc
    call out_str
    jmp .wn
.esc:
    call out_escaped
    jc .stop
.wn:
    inc rbx
    jmp .w
.end:
    test r14d, r14d
    jz .stop
    call out_nl
.stop:
    xor eax, eax
    ret

; out_escaped(rsi): \n \t \0nnn \xHH etc; cf=1 after \c
out_escaped:
    push rbx
    mov rbx, rsi
.l:
    movzx edi, byte [rbx]
    test edi, edi
    jz .done
    inc rbx
    cmp edi, '\'
    jne .put
    movzx eax, byte [rbx]
    test eax, eax
    jz .put
    inc rbx
    cmp eax, 'c'
    je .cut
    cmp eax, '0'
    je .oct
    cmp eax, 'x'
    je .hex
    lea rsi, [esc_from]
    xor ecx, ecx
.f:
    mov dl, [rsi+rcx]
    test dl, dl
    jz .unk
    cmp dl, al
    je .hit
    inc ecx
    jmp .f
.hit:
    lea rsi, [esc_to]
    movzx edi, byte [rsi+rcx]
    jmp .put
.unk:
    push rax
    mov edi, '\'
    call out_ch
    pop rdi
    jmp .put
.oct:
    xor edi, edi
    mov ecx, 3
.ol:
    movzx eax, byte [rbx]
    sub eax, '0'
    cmp eax, 7
    ja .put
    shl edi, 3
    add edi, eax
    inc rbx
    dec ecx
    jnz .ol
    jmp .put
.hex:
    xor edi, edi
    xor r8d, r8d
.hl:
    movzx eax, byte [rbx]
    call hexval
    jc .hd
    shl edi, 4
    or edi, eax
    inc rbx
    inc r8d
    cmp r8d, 2
    jb .hl
.hd:
    test r8d, r8d
    jnz .put
    mov edi, '\'
    call out_ch
    mov edi, 'x'
.put:
    call out_ch
    jmp .l
.done:
    pop rbx
    clc
    ret
.cut:
    pop rbx
    stc
    ret

; hexval(eax char) -> eax, cf if not a hex digit
hexval:
    cmp eax, '0'
    jb .n
    cmp eax, '9'
    jbe .d
    or eax, 0x20
    cmp eax, 'a'
    jb .n
    cmp eax, 'f'
    ja .n
    sub eax, 'a' - 10
    clc
    ret
.d:
    sub eax, '0'
    clc
    ret
.n:
    stc
    ret

section .rodata
esc_from: db "abefnrtv\", 0
esc_to:   db 7, 8, 27, 12, 10, 13, 9, 11, '\'
section .text

; ---- yes

APPLET "yes", t_yes, "[STRING]..."
t_yes:
    call opts_none
    mov r12, [nop]
    mov r13, [g_argv]
    test r12, r12
    jnz .l
    lea rax, [s_y]
    mov [r13+8], rax
    inc r12
.l:
    mov ebx, 1
.w:
    mov rsi, [r13+rbx*8]
    call out_str
    cmp rbx, r12
    je .eol
    call out_sp
    inc rbx
    jmp .w
.eol:
    call out_nl
    jmp .l

; ---- cat

APPLET "cat", t_cat, "[-AbeEnstTuv] [FILE]..."
t_cat:
    lea rdx, [cat_spec]
    lea rcx, [cat_long]
    call opt_init
    xor ebx, ebx
.o:
    call opt_next
    test eax, eax
    jz .od
    lea rsi, [cat_map]
.m:
    mov cl, [rsi]
    test cl, cl
    jz .o
    cmp cl, al
    je .mh
    add rsi, 2
    jmp .m
.mh:
    movzx ecx, byte [rsi+1]
    or ebx, ecx
    jmp .o
.od:
    test ebx, 2                     ; -b beats -n
    jz .nb
    and ebx, ~1
.nb:
    mov [cat_fl], ebx
    mov byte [cat_bol], 1
    xor ebp, ebp
    call stdin_if_none
    mov r14d, 1
.f:
    cmp r14, r12
    ja .done
    mov r15, [r13+r14*8]
    mov rdi, r15
    call open_ro
    test rax, rax
    js .err
    mov rbx, rax
    mov edi, eax
    call cat_fd
    push rax
    mov edi, ebx
    call close_fd
    pop rax
    test rax, rax
    jns .nx
.err:
    mov rdi, r15
    mov esi, eax
    call perr
    mov ebp, 1
.nx:
    inc r14
    jmp .f
.done:
    mov eax, ebp
    ret

; stdin_if_none: r12 = nop, r13 = argv; with no operands pretend "-" was given
stdin_if_none:
    mov r12, [nop]
    mov r13, [g_argv]
    test r12, r12
    jnz .r
    lea rax, [s_dash]
    mov [r13+8], rax
    mov qword [r13+16], 0
    inc r12
.r:
    ret

cat_fd:
    mov eax, [cat_fl]
    test eax, eax
    jnz .slow
    mov esi, 1
    jmp copy_fd
.slow:
    push rbx
    push rbp
    push r12
    push r13
    mov ebx, edi
.rd:
    mov edi, ebx
    lea rsi, [ibuf]
    mov edx, IBUFSZ
    call read_fd
    test rax, rax
    jle .out
    lea r12, [ibuf]
    lea r13, [ibuf+rax]
.b:
    cmp r12, r13
    jae .rd
    movzx ebp, byte [r12]
    inc r12
    mov eax, [cat_fl]
    cmp byte [cat_bol], 0
    je .mid
    mov byte [cat_bol], 0
    cmp ebp, 10
    jne .full
    test eax, 4                     ; -s
    jz .bl
    cmp byte [cat_pb], 0
    je .bl
    mov byte [cat_bol], 1
    jmp .b
.bl:
    mov byte [cat_pb], 1
    test eax, 1
    jz .mid
    call cat_num
    jmp .mid
.full:
    mov byte [cat_pb], 0
    test eax, 3
    jz .mid
    call cat_num
.mid:
    mov eax, [cat_fl]
    cmp ebp, 10
    jne .c1
    test eax, 8
    jz .nl
    mov edi, '$'
    call out_ch
.nl:
    call out_nl
    mov byte [cat_bol], 1
    jmp .b
.c1:
    cmp ebp, 9
    jne .c2
    test eax, 16
    jz .put
    mov edi, '^'
    call out_ch
    mov edi, 'I'
    jmp .put2
.c2:
    test eax, 32
    jz .put
    cmp ebp, 128
    jb .c3
    mov edi, 'M'
    call out_ch
    mov edi, '-'
    call out_ch
    sub ebp, 128
.c3:
    cmp ebp, 32
    jae .c4
    mov edi, '^'
    call out_ch
    lea edi, [rbp+64]
    jmp .put2
.c4:
    cmp ebp, 127
    jne .put
    mov edi, '^'
    call out_ch
    mov edi, '?'
    jmp .put2
.put:
    mov edi, ebp
.put2:
    call out_ch
    jmp .b
.out:
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

cat_num:
    inc qword [cat_ln]
    mov rdi, [cat_ln]
    mov esi, 6
    mov dl, ' '
    call out_upad
    mov edi, 9
    jmp out_ch

section .rodata
cat_spec: db "AbeEnstTuv", 0
cat_map:  db 'A', 56, 'b', 2, 'e', 40, 'E', 8, 'n', 1, 's', 4, 't', 48, 'T', 16, 'u', 0, 'v', 32, 0
cat_long:
    LONGOPT 'A', "show-all"
    LONGOPT 'b', "number-nonblank"
    LONGOPT 'E', "show-ends"
    LONGOPT 'n', "number"
    LONGOPT 's', "squeeze-blank"
    LONGOPT 'T', "show-tabs"
    LONGOPT 'v', "show-nonprinting"
    LONGEND
section .bss
cat_fl:  resd 1
cat_ln:  resq 1
cat_bol: resb 1
cat_pb:  resb 1
section .text

; ---- pwd

APPLET "pwd", t_pwd, "[-LP]"
t_pwd:
    lea rdx, [pwd_spec]
    lea rcx, [pwd_long]
    call opt_init
    xor ebx, ebx
.o:
    call opt_next
    test eax, eax
    jz .od
    cmp al, 'L'
    sete bl
    jmp .o
.od:
    test ebx, ebx
    jz .phys
    ; -L: $PWD if it is absolute, has no . or .. parts and is the same directory
    lea rdi, [s_PWD]
    call getenv
    test rax, rax
    jz .phys
    mov rbx, rax
    cmp byte [rbx], '/'
    jne .phys
    mov rdi, rbx
    call has_dot_part
    je .phys
    mov rdi, rbx
    lea rsi, [statbuf]
    mov eax, SYS_stat
    syscall
    test rax, rax
    js .phys
    lea rdi, [s_dot]
    lea rsi, [statbuf2]
    mov eax, SYS_stat
    syscall
    test rax, rax
    js .phys
    mov rax, [statbuf+ST_INO]
    cmp rax, [statbuf2+ST_INO]
    jne .phys
    mov rax, [statbuf+ST_DEV]
    cmp rax, [statbuf2+ST_DEV]
    jne .phys
    mov rsi, rbx
    jmp .print
.phys:
    lea rdi, [pbuf]
    mov esi, PATHSZ
    mov eax, SYS_getcwd
    syscall
    test rax, rax
    js .err
    lea rsi, [pbuf]
.print:
    call out_str
    call out_nl
    xor eax, eax
    ret
.err:
    xor edi, edi
    mov esi, eax
    call perr
    mov eax, 1
    ret

; has_dot_part(rdi path) -> zf=1 if some component is . or ..
has_dot_part:
.l:
    mov al, [rdi]
    test al, al
    jz .no
    cmp al, '/'
    jne .skip
    inc rdi
    cmp byte [rdi], '.'
    jne .l
    mov al, [rdi+1]
    test al, al
    jz .yes
    cmp al, '/'
    je .yes
    cmp al, '.'
    jne .l
    mov al, [rdi+2]
    test al, al
    jz .yes
    cmp al, '/'
    je .yes
    jmp .l
.skip:
    inc rdi
    jmp .l
.yes:
    cmp eax, eax
    ret
.no:
    or eax, 1
    ret

section .rodata
pwd_spec: db "LP", 0
pwd_long:
    LONGOPT 'L', "logical"
    LONGOPT 'P', "physical"
    LONGEND
s_PWD: db "PWD", 0
section .text

; ---- sleep

APPLET "sleep", t_sleep, "NUMBER[smhd]..."
t_sleep:
    call opts_none
    mov r12, [nop]
    test r12, r12
    jz missing
    mov r13, [g_argv]
    xor r14d, r14d                  ; total ns
    mov ebx, 1
.a:
    cmp rbx, r12
    ja .go
    mov rdi, [r13+rbx*8]
    call parse_dur
    jc .bad
    add r14, rax
    inc rbx
    jmp .a
.go:
    mov rax, r14
    xor edx, edx
    mov ecx, 1000000000
    div rcx
    mov [tmpq], rax
    mov [tmpq+8], rdx
.again:
    lea rdi, [tmpq]
    lea rsi, [tmpq]                 ; remaining time comes back here
    mov eax, SYS_nanosleep
    syscall
    cmp rax, -EINTR
    je .again
    xor eax, eax
    ret
.bad:
    lea rdi, [s_badtime]
    mov rsi, [r13+rbx*8]
    call emsg_q
    jmp try_help

; parse_dur(rdi) -> rax nanoseconds; "1.5", "2m", ".5h", "3d". cf on error
parse_dur:
    push rbx
    xor eax, eax                    ; whole seconds
    xor ebx, ebx                    ; ns part
    xor r8d, r8d                    ; digits seen
.i:
    movzx ecx, byte [rdi]
    sub ecx, '0'
    cmp ecx, 9
    ja .dot
    imul rax, rax, 10
    add rax, rcx
    inc rdi
    inc r8d
    jmp .i
.dot:
    cmp byte [rdi], '.'
    jne .suf
    inc rdi
    mov r9d, 100000000
.fr:
    movzx ecx, byte [rdi]
    sub ecx, '0'
    cmp ecx, 9
    ja .suf
    inc r8d
    imul ecx, r9d
    add ebx, ecx
    mov r10d, 10
    push rax
    mov eax, r9d
    xor edx, edx
    div r10d
    mov r9d, eax
    pop rax
    inc rdi
    jmp .fr
.suf:
    test r8d, r8d
    jz .bad
    mov ecx, 1
    movzx edx, byte [rdi]
    test edx, edx
    jz .mul
    inc rdi
    cmp byte [rdi], 0
    jne .bad
    cmp edx, 's'
    je .mul
    mov ecx, 60
    cmp edx, 'm'
    je .mul
    mov ecx, 3600
    cmp edx, 'h'
    je .mul
    mov ecx, 86400
    cmp edx, 'd'
    jne .bad
.mul:
    imul rax, rax, 1000000000
    add rax, rbx
    imul rax, rcx
    pop rbx
    clc
    ret
.bad:
    pop rbx
    stc
    ret

; emsg_q(rdi msg, rsi word): "prog: msg 'word'"
emsg_q:
    push rsi
    push rdi
    call e_prog
    pop rsi
    call e_add
    lea rsi, [s_quote]
    call e_add
    pop rsi
    call e_add
    lea rsi, [s_quote]
    call e_add
    jmp e_out

section .rodata
s_badtime: db "invalid time interval ", 0
section .text

; ---- basename, dirname

APPLET "basename", t_basename, "NAME [SUFFIX]  or  -a [-s SUFFIX] [-z] NAME..."
t_basename:
    lea rdx, [bn_spec]
    lea rcx, [bn_long]
    call opt_init
    xor ebx, ebx                    ; multiple
    xor r14d, r14d                  ; suffix
    mov r15d, 10                    ; terminator
.o:
    call opt_next
    test eax, eax
    jz .od
    cmp al, 'a'
    jne .o1
    mov ebx, 1
.o1:
    cmp al, 's'
    jne .o2
    mov r14, [optarg]
    mov ebx, 1
.o2:
    cmp al, 'z'
    jne .o
    xor r15d, r15d
    jmp .o
.od:
    mov r12, [nop]
    mov r13, [g_argv]
    test r12, r12
    jz missing
    test ebx, ebx
    jnz .many
    cmp r12, 2
    ja extra
    jb .one
    mov r14, [r13+16]
.one:
    mov r12d, 1
.many:
    mov ebx, 1
.l:
    cmp rbx, r12
    ja .d
    mov rdi, [r13+rbx*8]
    mov rsi, r14
    call base_part
    call out_mem
    mov edi, r15d
    call out_ch
    inc rbx
    jmp .l
.d:
    xor eax, eax
    ret

; base_part(rdi path, rsi suffix or 0) -> rsi, rdx
base_part:
    push rbx
    mov rbx, rsi
    call strlen
    mov rdx, rax
    mov rsi, rdi
.strip:
    cmp rdx, 1
    jbe .s
    cmp byte [rsi+rdx-1], '/'
    jne .s
    dec rdx
    jmp .strip
.s:
    cmp rdx, 1
    jne .find
    cmp byte [rsi], '/'
    je .r
.find:
    lea rcx, [rsi+rdx]
    mov rax, rcx
.b:
    cmp rax, rsi
    je .got
    cmp byte [rax-1], '/'
    je .got
    dec rax
    jmp .b
.got:
    mov rsi, rax
    mov rdx, rcx
    sub rdx, rax
    test rbx, rbx
    jz .r
    push rsi
    push rdx
    mov rdi, rbx
    call strlen
    pop rdx
    pop rsi
    cmp rax, rdx
    jae .r
    lea rdi, [rsi+rdx]
    sub rdi, rax
    push rsi
    push rdx
    push rax
    mov rsi, rbx
    mov rdx, rax
    call memeq
    pop rax
    pop rdx
    pop rsi
    jne .r
    sub rdx, rax
.r:
    pop rbx
    ret

section .rodata
bn_spec: db "as:z", 0
bn_long:
    LONGOPT 'a', "multiple"
    LONGOPT 's', "suffix"
    LONGOPT 'z', "zero"
    LONGEND
section .text

APPLET "dirname", t_dirname, "[-z] NAME..."
t_dirname:
    lea rdx, [dn_spec]
    lea rcx, [dn_long]
    call opt_init
    mov r15d, 10
.o:
    call opt_next
    test eax, eax
    jz .od
    xor r15d, r15d
    jmp .o
.od:
    mov r12, [nop]
    mov r13, [g_argv]
    test r12, r12
    jz missing
    mov ebx, 1
.l:
    cmp rbx, r12
    ja .d
    mov rdi, [r13+rbx*8]
    call dir_part
    call out_mem
    mov edi, r15d
    call out_ch
    inc rbx
    jmp .l
.d:
    xor eax, eax
    ret

; dir_part(rdi) -> rsi, rdx
dir_part:
    call strlen
    mov rsi, rdi
    mov rdx, rax
.s1:
    cmp rdx, 1
    jbe .f
    cmp byte [rsi+rdx-1], '/'
    jne .f
    dec rdx
    jmp .s1
.f:
    test rdx, rdx
    jz .dot
    cmp byte [rsi+rdx-1], '/'
    je .s2
    dec rdx
    jmp .f
.s2:
    cmp rdx, 1
    jbe .r
    cmp byte [rsi+rdx-1], '/'
    jne .r
    dec rdx
    jmp .s2
.dot:
    lea rsi, [s_dot]
    mov edx, 1
.r:
    ret

section .rodata
dn_spec: db "z", 0
dn_long:
    LONGOPT 'z', "zero"
    LONGEND
section .text

; ---- sync

APPLET "sync", t_sync, "[-df] [FILE]..."
t_sync:
    lea rdx, [sync_spec]
    lea rcx, [sync_long]
    call opt_init
    xor ebx, ebx
.o:
    call opt_next
    test eax, eax
    jz .od
    cmp al, 'd'
    jne .o1
    or ebx, 1
.o1:
    cmp al, 'f'
    jne .o
    or ebx, 2
    jmp .o
.od:
    mov r12, [nop]
    mov r13, [g_argv]
    test r12, r12
    jnz .files
    mov eax, SYS_sync
    syscall
    xor eax, eax
    ret
.files:
    xor ebp, ebp
    mov r14d, 1
.l:
    cmp r14, r12
    ja .d
    mov r15, [r13+r14*8]
    mov rdi, r15
    mov esi, O_RDONLY | O_NONBLOCK
    mov eax, SYS_open
    syscall
    test rax, rax
    js .err
    mov rdi, rax
    push rax
    mov eax, 75                     ; fdatasync
    test ebx, 1
    jnz .call
    mov eax, 306                    ; syncfs
    test ebx, 2
    jnz .call
    mov eax, SYS_fsync
.call:
    syscall
    pop rdi
    push rax
    mov eax, SYS_close
    syscall
    pop rax
    test rax, rax
    jns .nx
.err:
    mov rdi, r15
    mov esi, eax
    call perrq
    mov ebp, 1
.nx:
    inc r14
    jmp .l
.d:
    mov eax, ebp
    ret

section .rodata
sync_spec: db "df", 0
sync_long:
    LONGOPT 'd', "data"
    LONGOPT 'f', "file-system"
    LONGEND
section .text

; ---- uname, arch, hostname

APPLET "uname", t_uname, "[-asnrvmpio]"
t_uname:
    lea rdx, [un_spec]
    lea rcx, [un_long]
    call opt_init
    xor ebx, ebx
.o:
    call opt_next
    test eax, eax
    jz .od
    lea rsi, [un_spec]
    xor ecx, ecx
.f:
    cmp [rsi+rcx], al
    je .h
    inc ecx
    jmp .f
.h:
    bts ebx, ecx
    jmp .o
.od:
    cmp qword [nop], 0
    jne extra
    test ebx, 1                     ; -a: s n r v m o
    jz .na
    or ebx, 2 | 4 | 8 | 16 | 32 | 256
.na:
    test ebx, ebx
    jnz .go
    mov ebx, 2
.go:
    lea rdi, [utsbuf]
    mov eax, SYS_uname
    syscall
    xor ebp, ebp                    ; printed anything yet
    mov r12d, 1
.l:
    cmp r12d, 9
    jae .end
    bt ebx, r12d
    jnc .nx
    test ebp, ebp
    jz .first
    call out_sp
.first:
    mov ebp, 1
    lea rax, [un_field]
    movzx eax, word [rax+r12*2]
    lea rsi, [utsbuf]
    add rsi, rax
    cmp r12d, 6
    jb .p
    lea rsi, [s_unknown2]
    cmp r12d, 8
    jne .p
    lea rsi, [s_Linux]
.p:
    call out_str
.nx:
    inc r12d
    jmp .l
.end:
    call out_nl
    xor eax, eax
    ret

section .rodata
un_spec: db "asnrvmpio", 0
un_field: dw 0, 0, 65, 130, 195, 260, 0, 0, 0
un_long:
    LONGOPT 'a', "all"
    LONGOPT 's', "kernel-name"
    LONGOPT 'n', "nodename"
    LONGOPT 'r', "kernel-release"
    LONGOPT 'v', "kernel-version"
    LONGOPT 'm', "machine"
    LONGOPT 'p', "processor"
    LONGOPT 'i', "hardware-platform"
    LONGOPT 'o', "operating-system"
    LONGEND
s_unknown2: db "unknown", 0
s_Linux: db "Linux", 0
section .text

APPLET "arch", t_arch, ""
t_arch:
    call opts_none
    cmp qword [nop], 0
    jne extra
    lea rdi, [utsbuf]
    mov eax, SYS_uname
    syscall
    lea rsi, [utsbuf+260]
    call out_str
    call out_nl
    xor eax, eax
    ret

APPLET "hostname", t_hostname, "[NAME]"
t_hostname:
    call opts_none
    mov rax, [nop]
    test rax, rax
    jnz .set
    lea rdi, [utsbuf]
    mov eax, SYS_uname
    syscall
    lea rsi, [utsbuf+65]
    call out_str
    call out_nl
    xor eax, eax
    ret
.set:
    cmp rax, 1
    ja extra
    mov rbx, [g_argv]
    mov rbx, [rbx+8]
    mov rdi, rbx
    call strlen
    mov rdi, rbx
    mov rsi, rax
    mov eax, SYS_sethostname
    syscall
    test rax, rax
    jns .ok
    xor edi, edi
    mov esi, eax
    call perr
    mov eax, 1
    ret
.ok:
    xor eax, eax
    ret

; ---- printenv

APPLET "printenv", t_printenv, "[-0] [VARIABLE]..."
t_printenv:
    lea rdx, [pe_spec]
    lea rcx, [pe_long]
    call opt_init
    mov r15d, 10
.o:
    call opt_next
    test eax, eax
    jz .od
    xor r15d, r15d
    jmp .o
.od:
    mov r12, [nop]
    mov r13, [g_argv]
    test r12, r12
    jnz .some
    mov rbx, [envp]
.all:
    mov rsi, [rbx]
    test rsi, rsi
    jz .ok
    call out_str
    mov edi, r15d
    call out_ch
    add rbx, 8
    jmp .all
.some:
    xor ebp, ebp
    mov ebx, 1
.l:
    cmp rbx, r12
    ja .d
    mov rdi, [r13+rbx*8]
    mov esi, '='
    call strchr
    test rax, rax
    jnz .miss
    mov rdi, [r13+rbx*8]
    call getenv
    test rax, rax
    jz .miss
    mov rsi, rax
    call out_str
    mov edi, r15d
    call out_ch
    jmp .nx
.miss:
    mov ebp, 1
.nx:
    inc rbx
    jmp .l
.d:
    mov eax, ebp
    ret
.ok:
    xor eax, eax
    ret

section .rodata
pe_spec: db "0", 0
pe_long:
    LONGOPT '0', "null"
    LONGEND
section .text

; ---- env

APPLET "env", t_env, "[-0i] [-u NAME] [-C DIR] [NAME=VALUE]... [COMMAND [ARG]...]"
t_env:
    mov r12, rdi
    mov r13, rsi
    ; a lone "-" means -i
    cmp r12, 2
    jb .p
    mov rdi, [r13+8]
    lea rsi, [s_dash]
    call streq
    jne .p
    lea rax, [s_dashi]
    mov [r13+8], rax
.p:
    ; room for the new environment
    mov rdi, 1 << 20
    call mmap_anon
    mov r14, rax                    ; env array
    xor r15d, r15d                  ; count
    mov rdi, r12
    mov rsi, r13
    lea rdx, [env_spec]
    lea rcx, [env_long]
    call opt_init
    xor ebx, ebx                    ; bit0 -i, bit1 -0
    xor ebp, ebp                    ; -C dir
    mov qword [env_unset], 0
.o:
    call opt_next
    test eax, eax
    jz .od
    cmp al, 'i'
    jne .o1
    or ebx, 1
.o1:
    cmp al, '0'
    jne .o2
    or ebx, 2
.o2:
    cmp al, 'C'
    jne .o3
    mov rbp, [optarg]
.o3:
    cmp al, 'u'
    jne .o
    ; remember the -u names in a list after the env slots
    mov rcx, [env_unset]
    lea rdx, [r14 + (1 << 19)]
    mov rax, [optarg]
    mov [rdx+rcx*8], rax
    inc qword [env_unset]
    jmp .o
.od:
    test ebx, 1
    jnz .clean
    mov rsi, [envp]
.copy:
    mov rax, [rsi]
    test rax, rax
    jz .clean
    mov [r14+r15*8], rax
    inc r15
    add rsi, 8
    jmp .copy
.clean:
    ; drop the -u names
    xor ecx, ecx
.un:
    cmp rcx, [env_unset]
    jae .assign
    lea rdx, [r14 + (1 << 19)]
    mov rdi, [rdx+rcx*8]
    push rcx
    call env_del
    pop rcx
    inc rcx
    jmp .un
.assign:
    mov r12, [nop]
    mov r13, [g_argv]
    mov r8d, 1
.as:
    cmp r8, r12
    ja .print
    mov rdi, [r13+r8*8]
    push r8
    mov esi, '='
    call strchr
    pop r8
    test rax, rax
    jz .cmd
    mov rdi, [r13+r8*8]
    push r8
    call env_del
    pop r8
    mov rax, [r13+r8*8]
    mov [r14+r15*8], rax
    inc r15
    inc r8
    jmp .as
.cmd:
    mov qword [r14+r15*8], 0
    mov [envp], r14
    test rbp, rbp
    jz .run
    mov rdi, rbp
    mov eax, SYS_chdir
    syscall
    test rax, rax
    jns .run
    mov rdi, rbp
    mov esi, eax
    call perrq
    mov eax, 125
    ret
.run:
    lea rsi, [r13+r8*8]
    mov rdi, [rsi]
    jmp exec_or_die
.print:
    mov qword [r14+r15*8], 0
    xor ecx, ecx
.pl:
    mov rsi, [r14+rcx*8]
    test rsi, rsi
    jz .pd
    push rcx
    call out_str
    mov edi, 10
    test ebx, 2
    jz .pnl
    xor edi, edi
.pnl:
    call out_ch
    pop rcx
    inc rcx
    jmp .pl
.pd:
    xor eax, eax
    ret

; env_del(rdi "NAME" or "NAME=..."): remove NAME from r14[0..r15)
env_del:
    mov r9, rdi
    xor r10d, r10d                  ; name length
.nl:
    mov al, [r9+r10]
    test al, al
    jz .go
    cmp al, '='
    je .go
    inc r10
    jmp .nl
.go:
    xor r11d, r11d
.l:
    cmp r11, r15
    jae .r
    mov rdi, [r14+r11*8]
    mov rsi, r9
    mov rdx, r10
    call memeq
    jne .nx
    cmp byte [rdi], '='
    jne .nx
    dec r15                         ; move the last one into this slot
    mov rax, [r14+r15*8]
    mov [r14+r11*8], rax
    jmp .l
.nx:
    inc r11
    jmp .l
.r:
    ret

section .rodata
env_spec: db "+i0u:C:", 0
env_long:
    LONGOPT 'i', "ignore-environment"
    LONGOPT '0', "null"
    LONGOPT 'u', "unset"
    LONGOPT 'C', "chdir"
    LONGEND
section .bss
env_unset: resq 1
section .text

; ---- link, unlink

APPLET "link", t_link, "FILE1 FILE2"
t_link:
    call opts_none
    mov rax, [nop]
    cmp rax, 2
    jb missing
    ja extra
    mov rbx, [g_argv]
    mov rdi, [rbx+8]
    mov rsi, [rbx+16]
    mov eax, SYS_link
    syscall
    test rax, rax
    jns .ok
    mov rdi, [rbx+16]
    mov esi, eax
    call perrq
    mov eax, 1
    ret
.ok:
    xor eax, eax
    ret

APPLET "unlink", t_unlink, "FILE"
t_unlink:
    call opts_none
    mov rax, [nop]
    cmp rax, 1
    jb missing
    ja extra
    mov rbx, [g_argv]
    mov rdi, [rbx+8]
    mov eax, SYS_unlink
    syscall
    test rax, rax
    jns .ok
    mov rdi, [rbx+8]
    mov esi, eax
    call perrq
    mov eax, 1
    ret
.ok:
    xor eax, eax
    ret

; ---- tty

APPLET "tty", t_tty, "[-s]"
t_tty:
    lea rdx, [tty_spec]
    lea rcx, [tty_long]
    call opt_init
    xor ebx, ebx
.o:
    call opt_next
    test eax, eax
    jz .od
    mov ebx, 1
    jmp .o
.od:
    cmp qword [nop], 0
    jne extra
    xor edi, edi
    call isatty
    jnz .no
    test ebx, ebx
    jnz .ok
    lea rdi, [s_proc_fd0]
    lea rsi, [pbuf]
    mov edx, PATHSZ - 1
    mov eax, SYS_readlink
    syscall
    test rax, rax
    js .no
    lea rsi, [pbuf]
    mov rdx, rax
    call out_mem
    call out_nl
.ok:
    xor eax, eax
    ret
.no:
    test ebx, ebx
    jnz .q
    lea rsi, [s_notatty]
    call out_str
.q:
    mov eax, 1
    ret

section .rodata
tty_spec: db "s", 0
tty_long:
    LONGOPT 's', "silent"
    LONGOPT 's', "quiet"
    LONGEND
section .text

; ---- nproc

APPLET "nproc", t_nproc, "[--all] [--ignore=N]"
t_nproc:
    lea rdx, [np_spec]
    lea rcx, [np_long]
    call opt_init
    xor ebx, ebx                    ; --all
    xor r12d, r12d                  ; --ignore
.o:
    call opt_next
    test eax, eax
    jz .od
    cmp al, 1
    jne .o1
    mov ebx, 1
    jmp .o
.o1:
    mov rdi, [optarg]
    call parse_u
    jc .bad
    mov r12, rax
    jmp .o
.od:
    cmp qword [nop], 0
    jne extra
    test ebx, ebx
    jnz .all
    ; OMP_NUM_THREADS (first number) and OMP_THREAD_LIMIT, like GNU
    lea rdi, [s_OMP]
    call omp_num
    mov r13, rax
    test r13, r13
    jnz .lim
    xor edi, edi
    mov esi, 128
    lea rdx, [pbuf]
    mov eax, SYS_sched_getaffinity
    syscall
    test rax, rax
    jle .all
    xor r13d, r13d
    xor ecx, ecx
.cnt:
    movzx edx, byte [pbuf+rcx]
    popcnt edx, edx
    add r13, rdx
    inc rcx
    cmp rcx, rax
    jb .cnt
    jmp .lim
.all:
    lea rdi, [s_cpupresent]
    call slurp_path
    test rax, rax
    jz .one
    mov rdi, rax
    lea rsi, [rax+rdx]
    call count_ranges
    mov r13, rax
    jmp .lim
.one:
    mov r13d, 1
.lim:
    lea rdi, [s_OMPL]
    call omp_num
    test rax, rax
    jz .ign
    cmp r13, rax
    jbe .ign
    mov r13, rax
.ign:
    sub r13, r12
    jbe .min
    jmp .p
.min:
    mov r13d, 1
.p:
    mov rdi, r13
    call out_u
    call out_nl
    xor eax, eax
    ret
.bad:
    lea rdi, [s_badnum]
    mov rsi, [optarg]
    call emsg2
    mov eax, 1
    ret

; omp_num(rdi var) -> rax, 0 if unset or bad
omp_num:
    call getenv
    test rax, rax
    jz .r
    mov rdi, rax
    xor eax, eax
.l:
    movzx ecx, byte [rdi]
    sub ecx, '0'
    cmp ecx, 9
    ja .r
    imul rax, rax, 10
    add rax, rcx
    inc rdi
    jmp .l
.r:
    ret

; count_ranges(rdi text, rsi end): "0-3,8,10-11" -> 7
count_ranges:
    xor r8d, r8d
.n:
    cmp rdi, rsi
    jae .r
    xor eax, eax
    movzx ecx, byte [rdi]
    sub ecx, '0'
    cmp ecx, 9
    ja .skip
.a:
    movzx ecx, byte [rdi]
    sub ecx, '0'
    cmp ecx, 9
    ja .ae
    imul rax, rax, 10
    add rax, rcx
    inc rdi
    jmp .a
.ae:
    mov r9, rax
    cmp byte [rdi], '-'
    jne .single
    inc rdi
    xor eax, eax
.b:
    movzx ecx, byte [rdi]
    sub ecx, '0'
    cmp ecx, 9
    ja .be
    imul rax, rax, 10
    add rax, rcx
    inc rdi
    jmp .b
.be:
    sub rax, r9
    lea r8, [r8+rax+1]
    jmp .n
.single:
    inc r8
    jmp .n
.skip:
    inc rdi
    jmp .n
.r:
    mov rax, r8
    ret

section .rodata
np_spec: db 1, 2, ":", 0
np_long:
    LONGOPT 1, "all"
    LONGOPT 2, "ignore"
    LONGEND
section .text

; ---- whoami, logname

APPLET "whoami", t_whoami, ""
t_whoami:
    call opts_none
    cmp qword [nop], 0
    jne extra
    mov eax, SYS_geteuid
    syscall
    mov rbx, rax
    mov rdi, rax
    xor esi, esi
    call name_by_id
    jne .nf
    call out_mem
    call out_nl
    xor eax, eax
    ret
.nf:
    lea rdi, [s_nouser]
    mov rsi, rbx
    call emsg_u
    mov eax, 1
    ret

; emsg_u(rdi msg, rsi number)
emsg_u:
    push rsi
    push rdi
    call e_prog
    pop rsi
    call e_add
    pop rdi
    call fmt_u
    mov byte [rsi+rdx], 0
    call e_add
    jmp e_out

section .rodata
s_nouser: db "cannot find name for user ID ", 0
section .text

APPLET "logname", t_logname, ""
t_logname:
    call opts_none
    cmp qword [nop], 0
    jne extra
    lea rdi, [s_loginuid]
    call slurp_path
    test rax, rax
    jz .no
    mov rdi, rax
    call parse_u_prefix
    mov ecx, 0xffffffff
    cmp rax, rcx
    je .no
    mov rdi, rax
    xor esi, esi
    call name_by_id
    jne .no
    call out_mem
    call out_nl
    xor eax, eax
    ret
.no:
    lea rdi, [s_nologin]
    call emsg
    mov eax, 1
    ret

; parse_u_prefix(rdi) -> rax: leading digits only
parse_u_prefix:
    xor eax, eax
.l:
    movzx ecx, byte [rdi]
    sub ecx, '0'
    cmp ecx, 9
    ja .r
    imul rax, rax, 10
    add rax, rcx
    inc rdi
    jmp .l
.r:
    ret

; ---- hostid

APPLET "hostid", t_hostid, ""
t_hostid:
    call opts_none
    cmp qword [nop], 0
    jne extra
    lea rdi, [s_hostid]
    call slurp_path
    test rax, rax
    jz .hosts
    cmp rdx, 4
    jb .hosts
    mov edi, [rax]
    jmp .p
.hosts:
    ; like glibc: the IPv4 address of our hostname, halves swapped
    lea rdi, [utsbuf]
    mov eax, SYS_uname
    syscall
    lea rdi, [s_etchosts]
    call slurp_path
    xor edi, edi
    test rax, rax
    jz .p
    mov rdi, rax
    lea rsi, [rax+rdx]
    lea rdx, [utsbuf+65]
    call hosts_lookup
    mov edi, eax
    rol edi, 16
.p:
    mov esi, 8
    call out_hex
    call out_nl
    xor eax, eax
    ret

; hosts_lookup(rdi text, rsi end, rdx name) -> eax ipv4 in network order, 0 if none
hosts_lookup:
    push rbx
    push rbp
    push r12
    push r13
    mov rbx, rdi
    mov rbp, rsi
    mov r12, rdx
.line:
    cmp rbx, rbp
    jae .nf
    call .ws
    cmp rbx, rbp
    jae .nf
    cmp byte [rbx], '#'
    je .skipline
    cmp byte [rbx], 10
    je .eol
    ; address
    mov rdi, rbx
    call parse_ipv4
    mov r13d, eax
    jc .skipline
    mov rbx, rdi
.name:
    call .ws
    cmp rbx, rbp
    jae .nf
    movzx eax, byte [rbx]
    cmp al, 10
    je .eol
    cmp al, '#'
    je .skipline
    ; compare this word with the name
    mov rdi, rbx
    mov rsi, r12
.cmp:
    mov al, [rsi]
    test al, al
    jz .end
    cmp al, [rdi]
    jne .word
    inc rdi
    inc rsi
    jmp .cmp
.end:
    mov al, [rdi]
    cmp al, ' '
    je .hit
    cmp al, 9
    je .hit
    cmp al, 10
    je .hit
    cmp rdi, rbp
    jae .hit
.word:
    cmp rbx, rbp
    jae .nf
    mov al, [rbx]
    cmp al, ' '
    je .name
    cmp al, 9
    je .name
    cmp al, 10
    je .eol
    inc rbx
    jmp .word
.hit:
    mov eax, r13d
    jmp .r
.skipline:
    cmp rbx, rbp
    jae .nf
    cmp byte [rbx], 10
    je .eol
    inc rbx
    jmp .skipline
.eol:
    inc rbx
    jmp .line
.nf:
    xor eax, eax
.r:
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
.ws:
    cmp rbx, rbp
    jae .wsr
    cmp byte [rbx], ' '
    je .wsn
    cmp byte [rbx], 9
    jne .wsr
.wsn:
    inc rbx
    jmp .ws
.wsr:
    ret

; parse_ipv4(rdi) -> eax (network order), rdi past it, cf if not a.b.c.d
parse_ipv4:
    xor eax, eax
    xor ecx, ecx                    ; byte index
.oct:
    xor edx, edx
    movzx r8d, byte [rdi]
    sub r8d, '0'
    cmp r8d, 9
    ja .bad
.d:
    movzx r8d, byte [rdi]
    sub r8d, '0'
    cmp r8d, 9
    ja .od
    imul edx, edx, 10
    add edx, r8d
    inc rdi
    jmp .d
.od:
    cmp edx, 255
    ja .bad
    shl edx, cl
    or eax, edx
    add ecx, 8
    cmp ecx, 32
    je .ok
    cmp byte [rdi], '.'
    jne .bad
    inc rdi
    jmp .oct
.ok:
    clc
    ret
.bad:
    stc
    ret

section .rodata
s_etchosts: db "/etc/hosts", 0
section .text

; ---- shared bits for the file tools

; flagbit(al, rsi map) -> ecx; map is: db char, dd bits ... db 0
flagbit:
    xor ecx, ecx
.l:
    mov dl, [rsi]
    test dl, dl
    jz .r
    cmp dl, al
    je .h
    add rsi, 5
    jmp .l
.h:
    mov ecx, [rsi+1]
.r:
    ret

; opt_bits(rdi argc, rsi argv, rdx spec, rcx long, r8 map) -> eax all flags
; for tools whose options are plain switches
opt_bits:
    push rbx
    push r12
    mov r12, r8
    call opt_init
    xor ebx, ebx
.o:
    call opt_next
    test eax, eax
    jz .d
    mov rsi, r12
    call flagbit
    or ebx, ecx
    jmp .o
.d:
    mov eax, ebx
    pop r12
    pop rbx
    ret

; out_q(rsi): 'name' on stdout
out_q:
    push rsi
    mov edi, 0x27
    call out_ch
    pop rsi
    call out_str
    mov edi, 0x27
    jmp out_ch

; out_progq(rdi text, rsi name): "prog: text'name'\n" on stdout (verbose messages)
out_progq:
    push rsi
    push rdi
    mov rsi, [prog]
    call out_str
    lea rsi, [s_colsp]
    call out_str
    pop rsi
    call out_str
    pop rsi
    call out_q
    jmp out_nl

; get_umask -> eax
get_umask:
    xor edi, edi
    mov eax, SYS_umask
    syscall
    push rax
    mov edi, eax
    mov eax, SYS_umask
    syscall
    pop rax
    ret

; ask_yes(rdi text, rsi name): "prog: text 'name'? " then read a line; zf=1 if it starts with y
ask_yes:
    push rsi
    push rdi
    call flush
    call e_prog
    pop rsi
    call e_add
    lea rsi, [s_quote]
    call e_add
    pop rsi
    call e_add
    lea rsi, [s_qmark]
    call e_add
    mov rdx, [elen]
    lea rsi, [ebuf]
    mov qword [elen], 0
    mov edi, 2
    call write_all
    ; read one line from stdin
    xor ebx, ebx
    mov byte [tmpq], 0
.r:
    xor edi, edi
    lea rsi, [tmpq+8]
    mov edx, 1
    call read_fd
    test rax, rax
    jle .d
    mov al, [tmpq+8]
    cmp al, 10
    je .d
    cmp byte [tmpq], 0
    jne .r
    mov [tmpq], al
    jmp .r
.d:
    mov al, [tmpq]
    or al, 0x20
    cmp al, 'y'
    ret

section .rodata
s_qmark: db "'? ", 0
section .text

; mode_apply(rdi mode string, esi current mode, edx is a directory) -> eax, cf on error
mode_apply:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov r12, rdi
    mov r13d, esi
    mov r14d, edx
    movzx eax, byte [r12]
    sub eax, '0'
    cmp eax, 7
    ja .sym
    mov rdi, r12
    call parse_oct
    jc .bad
    cmp rax, 7777o
    ja .bad
    ; like GNU, a short number keeps set-id bits on directories
    test r14d, r14d
    jz .done
    mov rdi, r12
    push rax
    call strlen
    mov rcx, rax
    pop rax
    cmp rcx, 5
    jae .done
    mov ecx, r13d
    and ecx, 6000o
    or eax, ecx
    jmp .done
.sym:
    call get_umask
    mov r15d, eax
.clause:
    xor ebx, ebx
    xor ebp, ebp
.who:
    mov al, [r12]
    mov ecx, 4700o
    cmp al, 'u'
    je .w
    mov ecx, 2070o
    cmp al, 'g'
    je .w
    mov ecx, 1007o
    cmp al, 'o'
    je .w
    mov ecx, 7777o
    cmp al, 'a'
    jne .whod
.w:
    or ebx, ecx
    mov ebp, 1
    inc r12
    jmp .who
.whod:
    test ebp, ebp
    jnz .op
    mov ebx, 7777o
.op:
    movzx r8d, byte [r12]
    cmp r8d, '+'
    je .opok
    cmp r8d, '-'
    je .opok
    cmp r8d, '='
    jne .bad
.opok:
    inc r12
    xor r9d, r9d
.perm:
    movzx eax, byte [r12]
    mov ecx, 444o
    cmp al, 'r'
    je .p
    mov ecx, 222o
    cmp al, 'w'
    je .p
    mov ecx, 111o
    cmp al, 'x'
    je .p
    mov ecx, 6000o
    cmp al, 's'
    je .p
    mov ecx, 1000o
    cmp al, 't'
    je .p
    cmp al, 'X'
    jne .copy
    xor ecx, ecx
    test r14d, r14d
    jnz .X
    test r13d, 111o
    jz .p
.X:
    mov ecx, 111o
    jmp .p
.copy:
    mov ecx, r13d
    cmp al, 'u'
    jne .c1
    shr ecx, 6
    jmp .c3
.c1:
    cmp al, 'g'
    jne .c2
    shr ecx, 3
    jmp .c3
.c2:
    cmp al, 'o'
    jne .apply
.c3:
    and ecx, 7
    imul ecx, ecx, 111o
.p:
    or r9d, ecx
    inc r12
    jmp .perm
.apply:
    and r9d, ebx
    test ebp, ebp
    jnz .noum
    mov eax, r15d
    not eax
    and r9d, eax
.noum:
    cmp r8d, '+'
    jne .a1
    or r13d, r9d
    jmp .next
.a1:
    cmp r8d, '-'
    jne .a2
    mov eax, r9d
    not eax
    and r13d, eax
    jmp .next
.a2:
    mov eax, ebx
    test r14d, r14d                 ; = keeps set-id on directories
    jz .a3
    and eax, ~6000o
.a3:
    test ebp, ebp
    jnz .a4
    mov eax, 777o
.a4:
    not eax
    and r13d, eax
    or r13d, r9d
.next:
    mov al, [r12]
    test al, al
    jz .sdone
    cmp al, ','
    jne .op
    inc r12
    jmp .clause
.sdone:
    mov eax, r13d
.done:
    clc
    jmp .r
.bad:
    stc
.r:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

; ---- tree walk
; walk(rdi len of path in pbuf): lstat it (stat if [walk_follow] at the top),
; call [walk_fn]; for directories visit the children, then [walk_post].
; callbacks see pbuf and statbuf; return 0 ok, 1 error, 2 skip children.
; -> eax 0 or 1
section .bss
walk_fn:     resq 1
walk_post:   resq 1
walk_follow: resq 1
walk_dirfail: resq 1                ; message flag
section .text

walk:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov rbx, rdi
    lea rdi, [pbuf]
    lea rsi, [statbuf]
    mov eax, SYS_lstat
    cmp qword [walk_follow], 0
    je .st
    mov eax, SYS_stat
    mov qword [walk_follow], 0
.st:
    syscall
    test rax, rax
    jns .ok
    lea rdi, [pbuf]
    mov esi, eax
    call perr_cannot
    mov eax, 1
    jmp .ret
.ok:
    mov ebp, [statbuf+ST_MODE]
    call [walk_fn]
    mov r15d, eax
    cmp eax, 2
    jne .t
    xor r15d, r15d
    jmp .done
.t:
    and ebp, S_IFMT
    cmp ebp, S_IFDIR
    jne .done
    lea rdi, [pbuf]
    mov esi, O_RDONLY | O_DIRECTORY | O_NOFOLLOW
    mov eax, SYS_open
    syscall
    test rax, rax
    jns .opened
    lea rdi, [pbuf]
    mov esi, eax
    call perr_cannot
    mov r15d, 1
    jmp .post
.opened:
    mov r12, rax
    sub rsp, 8192
.rd:
    mov edi, r12d
    mov rsi, rsp
    mov edx, 8192
    mov eax, SYS_getdents64
    syscall
    test rax, rax
    jle .rdend
    mov r13, rsp
    lea r14, [rsp+rax]
.ent:
    cmp r13, r14
    jae .rd
    lea rsi, [r13+19]
    cmp byte [rsi], '.'
    jne .use
    cmp byte [rsi+1], 0
    je .skip
    cmp byte [rsi+1], '.'
    jne .use
    cmp byte [rsi+2], 0
    je .skip
.use:
    lea rdi, [pbuf]
    add rdi, rbx
    cmp byte [rdi-1], '/'
    je .noslash
    mov byte [rdi], '/'
    inc rdi
.noslash:
    call strcpy
    lea rax, [pbuf]
    sub rdi, rax
    call walk
    or r15d, eax
    lea rax, [pbuf]
    mov byte [rax+rbx], 0
.skip:
    movzx eax, word [r13+16]
    add r13, rax
    jmp .ent
.rdend:
    add rsp, 8192
    mov edi, r12d
    mov eax, SYS_close
    syscall
.post:
    mov rax, [walk_post]
    test rax, rax
    jz .done
    call rax
    or r15d, eax
.done:
    mov eax, r15d
    and eax, 1
.ret:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

; perr_cannot(rdi name, esi -errno): "prog: cannot access 'name': message"
perr_cannot:
    push rbx
    push r12
    mov rbx, rdi
    mov r12d, esi
    call e_prog
    lea rsi, [s_cannot]
    call e_add
    mov rsi, rbx
    call e_add
    lea rsi, [s_qcolsp]
    call e_add
    mov esi, r12d
    call e_errno
    call e_out
    pop r12
    pop rbx
    ret

; load_path(rdi str) -> rdi = length; copies into pbuf
load_path:
    mov rsi, rdi
    lea rdi, [pbuf]
    call strcpy
    lea rax, [pbuf]
    sub rdi, rax
    ret

section .rodata
s_cannot: db "cannot access '", 0
s_qcolsp: db "': ", 0
section .text

; ---- mkdir

APPLET "mkdir", t_mkdir, "[-pv] [-m MODE] DIRECTORY..."
t_mkdir:
    lea rdx, [mkd_spec]
    lea rcx, [mkd_long]
    call opt_init
    xor ebx, ebx                    ; 1 -p, 2 -v
    xor r14d, r14d                  ; -m
.o:
    call opt_next
    test eax, eax
    jz .od
    cmp al, 'p'
    jne .o1
    or ebx, 1
.o1:
    cmp al, 'v'
    jne .o2
    or ebx, 2
.o2:
    cmp al, 'm'
    jne .o
    mov r14, [optarg]
    jmp .o
.od:
    mov r12, [nop]
    mov r13, [g_argv]
    test r12, r12
    jz missing
    mov r15d, 777o
    test r14, r14
    jz .go
    call get_umask
    mov esi, eax
    not esi
    and esi, 777o
    mov rdi, r14
    mov edx, 1
    call mode_apply
    jc .badmode
    mov r15d, eax
.go:
    xor ebp, ebp
    mov qword [tmpq+16], 1
.l:
    mov rax, [tmpq+16]
    cmp rax, r12
    ja .done
    mov rdi, [r13+rax*8]
    test ebx, 1
    jz .one
    call load_path
    call mkdir_parents
.one:
    mov rax, [tmpq+16]
    mov rdi, [r13+rax*8]
    mov esi, r15d
    mov eax, SYS_mkdir
    syscall
    test rax, rax
    jns .made
    cmp rax, -EEXIST
    jne .err
    test ebx, 1
    jz .err
    mov rax, [tmpq+16]
    mov rdi, [r13+rax*8]
    call is_dir
    je .nx
    mov rax, -EEXIST
.err:
    mov rcx, [tmpq+16]
    mov rdi, [r13+rcx*8]
    mov esi, eax
    call perr_mkdir
    mov ebp, 1
    jmp .nx
.made:
    test r14, r14
    jz .v
    mov rax, [tmpq+16]
    mov rdi, [r13+rax*8]
    mov esi, r15d
    mov eax, SYS_chmod
    syscall
.v:
    test ebx, 2
    jz .nx
    lea rdi, [s_created]
    mov rax, [tmpq+16]
    mov rsi, [r13+rax*8]
    call out_progq
.nx:
    inc qword [tmpq+16]
    jmp .l
.done:
    mov eax, ebp
    ret
.badmode:
    lea rdi, [s_badmode]
    mov rsi, r14
    call emsg_q
    mov eax, 1
    ret

; perr_mkdir(rdi, esi): "mkdir: cannot create directory 'x': ..."
perr_mkdir:
    push rbx
    push r12
    mov rbx, rdi
    mov r12d, esi
    call e_prog
    lea rsi, [s_cantmk]
    call e_add
    mov rsi, rbx
    call e_add
    lea rsi, [s_qcolsp]
    call e_add
    mov esi, r12d
    call e_errno
    call e_out
    pop r12
    pop rbx
    ret

; mkdir_parents: create every leading directory of pbuf (verbose if ebx&2)
mkdir_parents:
    push r12
    lea r12, [pbuf+1]
.l:
    mov al, [r12]
    test al, al
    jz .d
    cmp al, '/'
    jne .n
    cmp byte [r12-1], '/'
    je .n
    mov byte [r12], 0
    lea rdi, [pbuf]
    mov esi, 777o
    mov eax, SYS_mkdir
    syscall
    test rax, rax
    js .back
    test ebx, 2
    jz .back
    lea rdi, [s_created]
    lea rsi, [pbuf]
    call out_progq
.back:
    mov byte [r12], '/'
.n:
    inc r12
    jmp .l
.d:
    pop r12
    ret

section .rodata
mkd_spec: db "pvm:", 0
mkd_long:
    LONGOPT 'p', "parents"
    LONGOPT 'v', "verbose"
    LONGOPT 'm', "mode"
    LONGEND
s_created: db "created directory ", 0
s_cantmk:  db "cannot create directory '", 0
s_badmode: db "invalid mode ", 0
section .text

; ---- rmdir

APPLET "rmdir", t_rmdir, "[-pv] [--ignore-fail-on-non-empty] DIRECTORY..."
t_rmdir:
    lea rdx, [rmd_spec]
    lea rcx, [rmd_long]
    lea r8, [rmd_map]
    call opt_bits
    mov ebx, eax                    ; 1 -p, 2 -v, 4 ignore
    mov r12, [nop]
    mov r13, [g_argv]
    test r12, r12
    jz missing
    xor ebp, ebp
    mov r14d, 1
.l:
    cmp r14, r12
    ja .done
    mov rdi, [r13+r14*8]
    call load_path
    mov r15, rdi                    ; length
.again:
    test ebx, 2
    jz .rm
    lea rdi, [s_removing]
    lea rsi, [pbuf]
    call out_progq
.rm:
    lea rdi, [pbuf]
    mov eax, SYS_rmdir
    syscall
    test rax, rax
    jns .removed
    test ebx, 4
    jz .err
    cmp rax, -39
    je .nx
    cmp rax, -EEXIST
    je .nx
.err:
    push rax
    call e_prog
    lea rsi, [s_failrm]
    call e_add
    lea rsi, [pbuf]
    call e_add
    lea rsi, [s_qcolsp]
    call e_add
    pop rsi
    call e_errno
    call e_out
    mov ebp, 1
    jmp .nx
.removed:
    test ebx, 1
    jz .nx
    ; -p: cut the last component and go again
.cut:
    test r15, r15
    jz .nx
    lea rax, [pbuf]
    cmp byte [rax+r15-1], '/'
    je .slash
    dec r15
    jmp .cut
.slash:
    dec r15
    test r15, r15
    jz .nx
    lea rax, [pbuf]
    cmp byte [rax+r15-1], '/'
    je .slash
    mov byte [rax+r15], 0
    jmp .again
.nx:
    inc r14
    jmp .l
.done:
    mov eax, ebp
    ret

section .rodata
rmd_spec: db "pv", 1, 0
rmd_map: db 'p'
         dd 1
         db 'v'
         dd 2
         db 1
         dd 4
         db 0
rmd_long:
    LONGOPT 'p', "parents"
    LONGOPT 'v', "verbose"
    LONGOPT 1, "ignore-fail-on-non-empty"
    LONGEND
s_removing: db "removing directory, ", 0
s_failrm:   db "failed to remove '", 0
section .text

; ---- rm

APPLET "rm", t_rm, "[-dfirRv] [--no-preserve-root] FILE..."
t_rm:
    lea rdx, [rm_spec]
    lea rcx, [rm_long]
    lea r8, [rm_map]
    call opt_bits
    mov [rm_fl], eax                ; 1 f, 2 i, 4 r, 8 d, 16 v, 32 no-preserve-root
    test eax, 1                     ; -f after -i wins and the other way round,
    jz .f                           ; close enough: -f turns -i off
    and dword [rm_fl], ~2
.f:
    mov r12, [nop]
    mov r13, [g_argv]
    test r12, r12
    jnz .go
    test dword [rm_fl], 1
    jnz .ok
    jmp missing
.go:
    xor ebp, ebp
    mov r14d, 1
    lea rax, [rm_visit]
    mov [walk_fn], rax
    lea rax, [rm_dirdone]
    mov [walk_post], rax
.l:
    cmp r14, r12
    ja .done
    mov rdi, [r13+r14*8]
    call rm_refuse
    je .bad
    mov rdi, [r13+r14*8]
    call load_path
    test dword [rm_fl], 4
    jz .flat
    mov qword [walk_follow], 0
    call walk
    or ebp, eax
    jmp .nx
.flat:
    lea rdi, [pbuf]
    lea rsi, [statbuf]
    mov eax, SYS_lstat
    syscall
    test rax, rax
    js .gone
    call rm_visit
    cmp eax, 2
    jne .acc
    xor eax, eax
.acc:
    or ebp, eax
    jmp .nx
.gone:
    test dword [rm_fl], 1
    jnz .nx
    push rax
    lea rdi, [s_cantrm]
    lea rsi, [pbuf]
    pop rdx
    call perr_what
    mov ebp, 1
    jmp .nx
.bad:
    mov ebp, 1
.nx:
    inc r14
    jmp .l
.done:
    mov eax, ebp
    ret
.ok:
    xor eax, eax
    ret

; rm_refuse(rdi arg) -> zf=1 when it must not be touched ("/", ".", "..")
rm_refuse:
    push rbx
    mov rbx, rdi
    call base_part_raw
    cmp rdx, 1
    jne .dd
    cmp byte [rsi], '.'
    je .dots
    cmp byte [rsi], '/'
    jne .no
    test dword [rm_fl], 32
    jnz .no
    lea rdi, [s_rootrm]
    call emsg
    cmp eax, eax
    pop rbx
    ret
.dd:
    cmp rdx, 2
    jne .no
    cmp word [rsi], '..'
    jne .no
.dots:
    call e_prog
    lea rsi, [s_refuse]
    call e_add
    mov rsi, rbx
    call e_add
    lea rsi, [s_quote]
    call e_add
    call e_out
    cmp eax, eax
    pop rbx
    ret
.no:
    or eax, 1
    pop rbx
    ret

; base_part_raw(rdi) -> rsi, rdx: last component, trailing slashes dropped
base_part_raw:
    xor esi, esi
    jmp base_part

; rm_visit: pbuf/statbuf. files go now, directories after their children
rm_visit:
    mov eax, [statbuf+ST_MODE]
    and eax, S_IFMT
    cmp eax, S_IFDIR
    je .dir
    test dword [rm_fl], 2
    jz .rm
    lea rdi, [s_rmask]
    lea rsi, [pbuf]
    call ask_yes
    jne .skip
.rm:
    lea rdi, [pbuf]
    mov eax, SYS_unlink
    syscall
    test rax, rax
    js .err
    test dword [rm_fl], 16
    jz .ok
    lea rdi, [s_removed]
    lea rsi, [pbuf]
    call out_plainq
.ok:
    xor eax, eax
    ret
.err:
    cmp rax, -ENOENT
    jne .e2
    test dword [rm_fl], 1
    jnz .ok
.e2:
    mov rdx, rax
    lea rdi, [s_cantrm]
    lea rsi, [pbuf]
    call perr_what
    mov eax, 1
    ret
.dir:
    test dword [rm_fl], 4
    jnz .rdir
    test dword [rm_fl], 8
    jnz .rdir
    mov rdx, -EISDIR
    lea rdi, [s_cantrm]
    lea rsi, [pbuf]
    call perr_what
    mov eax, 1
    ret
.rdir:
    test dword [rm_fl], 2
    jz .descend
    lea rdi, [s_rmdask]
    lea rsi, [pbuf]
    call ask_yes
    jne .skip
.descend:
    test dword [rm_fl], 4
    jnz .ok
    call rm_dirdone                 ; -d without -r: just rmdir
    ret
.skip:
    mov eax, 2
    ret

rm_dirdone:
    mov eax, [statbuf+ST_MODE]      ; statbuf belongs to the last child here,
    lea rdi, [pbuf]                 ; so only trust the rmdir result
    mov eax, SYS_rmdir
    syscall
    test rax, rax
    js .err
    test dword [rm_fl], 16
    jz .ok
    lea rdi, [s_removeddir]
    lea rsi, [pbuf]
    call out_plainq
.ok:
    xor eax, eax
    ret
.err:
    cmp rax, -ENOTDIR               ; it was a file all along (post on a file)
    je .ok
    mov rdx, rax
    lea rdi, [s_cantrm]
    lea rsi, [pbuf]
    call perr_what
    mov eax, 1
    ret

; out_plainq(rdi text, rsi name): "text'name'\n" on stdout
out_plainq:
    push rsi
    mov rsi, rdi
    call out_str
    pop rsi
    call out_q
    jmp out_nl

; perr_what(rdi "cannot remove '", rsi name, rdx -errno)
perr_what:
    push rbx
    push r12
    push r13
    mov rbx, rdi
    mov r12, rsi
    mov r13, rdx
    call e_prog
    mov rsi, rbx
    call e_add
    mov rsi, r12
    call e_add
    lea rsi, [s_qcolsp]
    call e_add
    mov esi, r13d
    call e_errno
    call e_out
    pop r13
    pop r12
    pop rbx
    ret

section .rodata
rm_spec: db "dfirRvI", 1, 2, 0
rm_map: db 'f'
        dd 1
        db 'i'
        dd 2
        db 'r'
        dd 4
        db 'R'
        dd 4
        db 'd'
        dd 8
        db 'v'
        dd 16
        db 1
        dd 32
        db 0
rm_long:
    LONGOPT 'f', "force"
    LONGOPT 'i', "interactive"
    LONGOPT 'r', "recursive"
    LONGOPT 'd', "dir"
    LONGOPT 'v', "verbose"
    LONGOPT 1, "no-preserve-root"
    LONGOPT 2, "preserve-root"
    LONGOPT 2, "one-file-system"
    LONGEND
s_cantrm:     db "cannot remove '", 0
s_removed:    db "removed ", 0
s_removeddir: db "removed directory ", 0
s_rmask:      db "remove ", 0
s_rmdask:     db "descend into directory ", 0
s_rootrm:     db "it is dangerous to operate recursively on '/'", 10, "rm: use --no-preserve-root to override this failsafe", 0
s_refuse:     db "refusing to remove '.' or '..' directory: skipping '", 0
section .bss
rm_fl: resd 1
section .text

; ---- chmod

APPLET "chmod", t_chmod, "[-Rcfv] MODE[,MODE]... FILE...  or  --reference=RFILE FILE..."
t_chmod:
    mov r12, rdi
    mov r13, rsi
    mov qword [chm_mode], 0
    ; "-w", "-x,g+r" etc. are modes, not options: take the first one out
    xor r14d, r14d
    mov ebx, 1
.scan:
    cmp rbx, r12
    jae .scand
    mov rsi, [r13+rbx*8]
    cmp byte [rsi], '-'
    jne .sn
    cmp byte [rsi+1], 0
    je .sn
    cmp byte [rsi+1], '-'
    je .sn
    lea rdi, [rsi+1]
.sc:
    mov al, [rdi]
    test al, al
    jz .ismode
    push rdi
    lea rdi, [chm_chars]
    mov esi, eax
    call strchr
    pop rdi
    test rax, rax
    jz .sn
    inc rdi
    jmp .sc
.ismode:
    mov r14, [r13+rbx*8]
    mov [chm_mode], r14
    ; drop it from argv
.sh:
    mov rax, [r13+rbx*8+8]
    mov [r13+rbx*8], rax
    inc rbx
    cmp rbx, r12
    jb .sh
    dec r12
    jmp .scand
.sn:
    inc rbx
    jmp .scan
.scand:
    mov rdi, r12
    mov rsi, r13
    lea rdx, [chm_spec]
    lea rcx, [chm_long]
    call opt_init
    xor ebx, ebx                    ; 1 R, 2 c, 4 f, 8 v
    xor r15d, r15d                  ; reference
.o:
    call opt_next
    test eax, eax
    jz .od
    cmp al, 1
    jne .o1
    mov r15, [optarg]
    jmp .o
.o1:
    lea rsi, [chm_map]
    call flagbit
    or ebx, ecx
    jmp .o
.od:
    mov [chm_fl], ebx
    mov r12, [nop]
    mov r13, [g_argv]
    mov r14d, 1
    test r15, r15
    jz .nref
    mov rdi, r15
    lea rsi, [statbuf]
    mov eax, SYS_stat
    syscall
    test rax, rax
    js .referr
    mov eax, [statbuf+ST_MODE]
    and eax, 7777o
    mov [chm_ref], eax
    mov qword [chm_mode], 0
    jmp .files
.nref:
    cmp qword [chm_mode], 0         ; the mode came out of argv already?
    jne .have
    test r12, r12
    jz missing
    mov r14, [r13+8]
    mov [chm_mode], r14
    mov r14d, 2
    jmp .chk
.have:
    mov r14d, 1
.chk:
    mov rdi, [chm_mode]
    xor esi, esi
    xor edx, edx
    call mode_apply
    jc .badmode
.files:
    cmp r14, r12
    ja missing
    xor ebp, ebp
    lea rax, [chm_one]
    mov [walk_fn], rax
    mov qword [walk_post], 0
.l:
    cmp r14, r12
    ja .done
    mov rdi, [r13+r14*8]
    call load_path
    test dword [chm_fl], 1
    jz .single
    mov qword [walk_follow], 1
    call walk
    or ebp, eax
    jmp .nx
.single:
    lea rdi, [pbuf]
    lea rsi, [statbuf]
    mov eax, SYS_stat
    syscall
    test rax, rax
    js .serr
    call chm_one
    or ebp, eax
    jmp .nx
.serr:
    test dword [chm_fl], 4
    jnz .se1
    lea rdi, [pbuf]
    mov esi, eax
    call perr_cannot
.se1:
    mov ebp, 1
.nx:
    inc r14
    jmp .l
.done:
    mov eax, ebp
    ret
.badmode:
    lea rdi, [s_badmode]
    mov rsi, [chm_mode]
    call emsg_q
    mov eax, 1
    ret
.referr:
    mov rdi, r15
    mov esi, eax
    call perr_cannot
    mov eax, 1
    ret

; chm_one: change pbuf (statbuf has its mode). symlinks found while
; recursing are left alone, like GNU
chm_one:
    push rbx
    push r12
    mov ebx, [statbuf+ST_MODE]
    mov eax, ebx
    and eax, S_IFMT
    cmp eax, S_IFLNK
    je .ok
    mov r12d, [chm_ref]
    mov rdi, [chm_mode]
    test rdi, rdi
    jz .set
    mov esi, ebx
    and esi, 7777o
    xor edx, edx
    cmp eax, S_IFDIR
    sete dl
    call mode_apply
    mov r12d, eax
.set:
    lea rdi, [pbuf]
    mov esi, r12d
    mov eax, SYS_chmod
    syscall
    test rax, rax
    js .err
    mov eax, ebx
    and eax, 7777o
    mov ecx, [chm_fl]
    test ecx, 8
    jnz .say
    test ecx, 2
    jz .ok
    cmp eax, r12d
    je .ok
.say:
    mov edi, eax
    mov esi, r12d
    call chm_report
.ok:
    xor eax, eax
    jmp .r
.err:
    test dword [chm_fl], 4
    jnz .e1
    mov rdx, rax
    lea rdi, [s_chmfail]
    lea rsi, [pbuf]
    call perr_what
.e1:
    mov eax, 1
.r:
    pop r12
    pop rbx
    ret

; chm_report(edi old, esi new)
chm_report:
    push rbx
    push r12
    mov ebx, edi
    mov r12d, esi
    lea rsi, [s_modeof]
    call out_str
    lea rsi, [pbuf]
    call out_q
    cmp ebx, r12d
    je .kept
    lea rsi, [s_chfrom]
    call out_str
    mov edi, ebx
    call out_modepair
    lea rsi, [s_to]
    call out_str
    jmp .n
.kept:
    lea rsi, [s_retained]
    call out_str
.n:
    mov edi, r12d
    call out_modepair
    call out_nl
    pop r12
    pop rbx
    ret

; out_modepair(edi): "0755 (rwxr-xr-x)"
out_modepair:
    push rbx
    mov ebx, edi
    mov edi, '0'
    call out_ch
    mov edi, ebx
    call out_oct4
    lea rsi, [s_sparen]
    call out_str
    mov edi, ebx
    call fmt_perms
    lea rsi, [numbuf+1]
    mov edx, 9
    call out_mem
    mov edi, ')'
    call out_ch
    pop rbx
    ret

; out_oct4(edi): four octal digits... three when the high digit is 0
out_oct4:
    mov eax, edi
    shr eax, 9
    and eax, 7
    jz .three
    add eax, '0'
    push rdi
    mov edi, eax
    call out_ch
    pop rdi
.three:
    mov ecx, 6
.l:
    mov eax, edi
    shr eax, cl
    and eax, 7
    add eax, '0'
    push rdi
    push rcx
    mov edi, eax
    call out_ch
    pop rcx
    pop rdi
    sub ecx, 3
    jns .l
    ret

; fmt_perms(edi mode) -> numbuf[0..10) "-rwxr-xr-x" style (type char first)
fmt_perms:
    lea rsi, [numbuf]
    mov eax, edi
    and eax, S_IFMT
    mov cl, '-'
    cmp eax, S_IFDIR
    jne .t1
    mov cl, 'd'
.t1:
    cmp eax, S_IFLNK
    jne .t2
    mov cl, 'l'
.t2:
    cmp eax, S_IFCHR
    jne .t3
    mov cl, 'c'
.t3:
    cmp eax, S_IFBLK
    jne .t4
    mov cl, 'b'
.t4:
    cmp eax, S_IFIFO
    jne .t5
    mov cl, 'p'
.t5:
    cmp eax, S_IFSOCK
    jne .t6
    mov cl, 's'
.t6:
    mov [rsi], cl
    lea r8, [s_rwx]
    xor ecx, ecx
.b:
    mov eax, 400o
    shr eax, cl
    mov dl, '-'
    test edi, eax
    jz .p
    mov dl, [r8+rcx]
.p:
    mov [rsi+rcx+1], dl
    inc ecx
    cmp ecx, 9
    jb .b
    ; set-id and sticky
    test edi, 4000o
    jz .g
    mov dl, 'S'
    cmp byte [rsi+3], 'x'
    jne .u
    mov dl, 's'
.u:
    mov [rsi+3], dl
.g:
    test edi, 2000o
    jz .k
    mov dl, 'S'
    cmp byte [rsi+6], 'x'
    jne .g2
    mov dl, 's'
.g2:
    mov [rsi+6], dl
.k:
    test edi, 1000o
    jz .r
    mov dl, 'T'
    cmp byte [rsi+9], 'x'
    jne .k2
    mov dl, 't'
.k2:
    mov [rsi+9], dl
.r:
    ret

section .rodata
chm_chars: db "rwxXstugoa,+-=", 0
chm_spec: db "Rcfv", 1, ":", 0
chm_map: db 'R'
         dd 1
         db 'c'
         dd 2
         db 'f'
         dd 4
         db 'v'
         dd 8
         db 0
chm_long:
    LONGOPT 'R', "recursive"
    LONGOPT 'c', "changes"
    LONGOPT 'f', "silent"
    LONGOPT 'f', "quiet"
    LONGOPT 'v', "verbose"
    LONGOPT 1, "reference"
    LONGEND
s_rwx:      db "rwxrwxrwx"
s_modeof:   db "mode of ", 0
s_chfrom:   db " changed from ", 0
s_to:       db " to ", 0
s_retained: db " retained as ", 0
s_sparen:   db " (", 0
s_chmfail:  db "changing permissions of '", 0
section .bss
chm_fl:   resd 1
chm_ref:  resd 1
chm_mode: resq 1
section .text

; ---- chown, chgrp

APPLET "chown", t_chown, "[-Rcfhv] [OWNER][:[GROUP]] FILE..."
t_chown:
    xor eax, eax
    jmp chown_main

APPLET "chgrp", t_chgrp, "[-Rcfhv] GROUP FILE..."
t_chgrp:
    mov eax, 1

chown_main:
    mov [cho_grp], eax
    lea rdx, [cho_spec]
    lea rcx, [cho_long]
    call opt_init
    xor ebx, ebx                    ; 1 R, 2 c, 4 f, 8 v, 16 h
    xor r15d, r15d
.o:
    call opt_next
    test eax, eax
    jz .od
    cmp al, 1
    jne .o1
    mov r15, [optarg]
    jmp .o
.o1:
    lea rsi, [cho_map]
    call flagbit
    or ebx, ecx
    jmp .o
.od:
    mov [cho_fl], ebx
    mov r12, [nop]
    mov r13, [g_argv]
    mov qword [cho_uid], -1
    mov qword [cho_gid], -1
    mov r14d, 1
    test r15, r15
    jz .spec
    mov rdi, r15
    lea rsi, [statbuf]
    mov eax, SYS_stat
    syscall
    test rax, rax
    js .referr
    mov eax, [statbuf+ST_UID]
    cmp dword [cho_grp], 0
    jne .rg
    mov [cho_uid], rax
.rg:
    mov eax, [statbuf+ST_GID]
    mov [cho_gid], rax
    jmp .files
.spec:
    test r12, r12
    jz missing
    mov rdi, [r13+8]
    mov r14d, 2
    cmp dword [cho_grp], 0
    jne .grp
    call parse_owner
    jc .bad
    jmp .files
.grp:
    call group_id
    jc .badg
    mov [cho_gid], rax
.files:
    cmp r14, r12
    ja missing
    xor ebp, ebp
    lea rax, [cho_one]
    mov [walk_fn], rax
    mov qword [walk_post], 0
.l:
    cmp r14, r12
    ja .done
    mov rdi, [r13+r14*8]
    call load_path
    test dword [cho_fl], 1
    jz .single
    mov qword [walk_follow], 1
    call walk
    or ebp, eax
    jmp .nx
.single:
    lea rdi, [pbuf]
    lea rsi, [statbuf]
    mov eax, SYS_stat
    test dword [cho_fl], 16
    jz .s1
    mov eax, SYS_lstat
.s1:
    syscall
    test rax, rax
    js .serr
    call cho_one
    or ebp, eax
    jmp .nx
.serr:
    lea rdi, [pbuf]
    mov esi, eax
    call perr_cannot
    mov ebp, 1
.nx:
    inc r14
    jmp .l
.done:
    mov eax, ebp
    ret
.bad:
    lea rdi, [s_baduser]
    mov rsi, [r13+8]
    call emsg_q
    mov eax, 1
    ret
.badg:
    lea rdi, [s_badgroup]
    mov rsi, [r13+8]
    call emsg_q
    mov eax, 1
    ret
.referr:
    mov rdi, r15
    mov esi, eax
    call perr_cannot
    mov eax, 1
    ret

; cho_one: pbuf, statbuf
cho_one:
    mov eax, [statbuf+ST_MODE]
    and eax, S_IFMT
    mov edx, SYS_chown
    cmp eax, S_IFLNK
    jne .call
    mov edx, SYS_lchown
    test dword [cho_fl], 16
    jnz .call
    test dword [cho_fl], 1          ; while recursing links are not followed
    jnz .call
    mov edx, SYS_chown
.call:
    lea rdi, [pbuf]
    mov rsi, [cho_uid]
    mov eax, edx
    mov rdx, [cho_gid]
    syscall
    test rax, rax
    js .err
    mov r8d, [statbuf+ST_UID]
    mov r9d, [statbuf+ST_GID]
    mov r10d, r8d
    mov r11d, r9d
    cmp qword [cho_uid], -1
    je .nu
    mov r10d, [cho_uid]
.nu:
    cmp qword [cho_gid], -1
    je .ng
    mov r11d, [cho_gid]
.ng:
    xor ecx, ecx
    cmp r8d, r10d
    setne cl
    cmp r9d, r11d
    setne al
    or cl, al                       ; changed?
    test dword [cho_fl], 8
    jnz .say
    test dword [cho_fl], 2
    jz .ok
    test cl, cl
    jz .ok
.say:
    push r8
    push r9
    push r10
    push r11
    push rcx
    test cl, cl
    jz .kept
    lea rsi, [s_changed]
    call out_str
.kept:
    lea rsi, [s_ownof]
    cmp dword [cho_grp], 0
    je .v
    lea rsi, [s_grpof]
.v:
    call out_str
    lea rsi, [pbuf]
    call out_q
    pop rcx
    pop r11
    pop r10
    pop r9
    pop r8
    test cl, cl
    jz .ret
    push r11
    push r10
    lea rsi, [s_from]
    call out_str
    mov edi, r8d
    mov esi, r9d
    call cho_ids
    lea rsi, [s_to]
    call out_str
    pop rdi
    pop rsi
    call cho_ids
    jmp .nl
.ret:
    push r11
    push r10
    lea rsi, [s_retas]
    call out_str
    pop rdi
    pop rsi
    call cho_ids
.nl:
    call out_nl
.ok:
    xor eax, eax
    ret
.err:
    test dword [cho_fl], 4
    jnz .e1
    mov rdx, rax
    lea rdi, [s_chofail]
    cmp dword [cho_grp], 0
    je .e0
    lea rdi, [s_chgfail]
.e0:
    lea rsi, [pbuf]
    call perr_what
.e1:
    mov eax, 1
    ret

; cho_ids(edi uid, esi gid): "user", "user:group" or "group" as the command wants
cho_ids:
    push rbx
    push r12
    mov ebx, edi
    mov r12d, esi
    cmp dword [cho_grp], 0
    jne .g
    mov edi, ebx
    xor esi, esi
    call out_name_or_id
    cmp qword [cho_gid], -1
    je .r
    mov edi, ':'
    call out_ch
.g:
    mov edi, r12d
    mov esi, 1
    call out_name_or_id
.r:
    pop r12
    pop rbx
    ret

; parse_owner(rdi "user[:group]") -> cho_uid/cho_gid, cf on error
parse_owner:
    push rbx
    push r12
    mov rbx, rdi
    mov esi, ':'
    call strchr
    test rax, rax
    jnz .split
    mov esi, '.'
    mov rdi, rbx
    call strchr
.split:
    mov r12, rax                    ; separator or 0
    test r12, r12
    jz .user
    mov byte [r12], 0
.user:
    cmp byte [rbx], 0
    je .group
    mov rdi, rbx
    call user_id
    jc .bad
    mov [cho_uid], rax
    test r12, r12
    jz .ok
    cmp byte [r12+1], 0
    jne .group
    mov [cho_gid], rdx              ; "user:" = login group
    jmp .ok
.group:
    test r12, r12
    jz .ok
    lea rdi, [r12+1]
    cmp byte [rdi], 0
    je .ok
    call group_id
    jc .bad
    mov [cho_gid], rax
.ok:
    test r12, r12
    jz .ok2
    mov byte [r12], ':'
.ok2:
    clc
    pop r12
    pop rbx
    ret
.bad:
    test r12, r12
    jz .bad2
    mov byte [r12], ':'
.bad2:
    stc
    pop r12
    pop rbx
    ret

; user_id(rdi name or number) -> rax uid, rdx gid (gid -1 if numeric), cf
user_id:
    push rdi
    call user_by_name
    pop rdi
    jne .num
    clc
    ret
.num:
    call parse_u
    mov rdx, -1
    ret

; group_id(rdi) -> rax gid, cf
group_id:
    push rbx
    mov rbx, rdi
    lea rdi, [s_group]
    lea rsi, [gr_buf]
    lea rdx, [gr_len]
    call load_db
    mov rdi, rbx
    call strlen
    mov r8, rax
    mov rcx, rbx
    xor edx, edx
    mov rdi, [gr_buf]
    mov rsi, [gr_len]
    test rdi, rdi
    jz .num
    call db_scan
    jne .num
    mov rdi, [db_fld+16]
    mov rsi, [db_flen+16]
    call num_n
    pop rbx
    clc
    ret
.num:
    mov rdi, rbx
    pop rbx
    jmp parse_u

section .rodata
cho_spec: db "RcfvhHLP", 1, ":", 0
cho_map: db 'R'
         dd 1
         db 'c'
         dd 2
         db 'f'
         dd 4
         db 'v'
         dd 8
         db 'h'
         dd 16
         db 0
cho_long:
    LONGOPT 'R', "recursive"
    LONGOPT 'c', "changes"
    LONGOPT 'f', "silent"
    LONGOPT 'f', "quiet"
    LONGOPT 'v', "verbose"
    LONGOPT 'h', "no-dereference"
    LONGOPT 1, "reference"
    LONGEND
s_baduser:  db "invalid user: ", 0
s_badgroup: db "invalid group: ", 0
s_ownof:    db "ownership of ", 0
s_grpof:    db "group of ", 0
s_retas:    db " retained as ", 0
s_changed:  db "changed ", 0
s_from:     db " from ", 0
s_chofail:  db "changing ownership of '", 0
s_chgfail:  db "changing group of '", 0
section .bss
cho_grp: resd 1
cho_fl:  resd 1
cho_uid: resq 1
cho_gid: resq 1
section .text

; ---- mkfifo, mknod

APPLET "mkfifo", t_mkfifo, "[-m MODE] NAME..."
t_mkfifo:
    lea rdx, [mkf_spec]
    lea rcx, [mkf_long]
    call opt_init
    mov r15d, 666o
    xor r14d, r14d
.o:
    call opt_next
    test eax, eax
    jz .od
    mov r14, [optarg]
    call get_umask
    mov esi, eax
    not esi
    and esi, 666o
    mov rdi, r14
    xor edx, edx
    call mode_apply
    jc .bad
    mov r15d, eax
    jmp .o
.od:
    mov r12, [nop]
    mov r13, [g_argv]
    test r12, r12
    jz missing
    xor ebp, ebp
    mov ebx, 1
.l:
    cmp rbx, r12
    ja .d
    mov rdi, [r13+rbx*8]
    mov esi, r15d
    or esi, S_IFIFO
    xor edx, edx
    mov eax, SYS_mknod
    syscall
    test rax, rax
    js .err
    test r14, r14
    jz .nx
    mov rdi, [r13+rbx*8]
    mov esi, r15d
    mov eax, SYS_chmod
    syscall
    jmp .nx
.err:
    mov rdx, rax
    lea rdi, [s_cantfifo]
    mov rsi, [r13+rbx*8]
    call perr_what
    mov ebp, 1
.nx:
    inc rbx
    jmp .l
.d:
    mov eax, ebp
    ret
.bad:
    lea rdi, [s_badmode]
    mov rsi, r14
    call emsg_q
    mov eax, 1
    ret

APPLET "mknod", t_mknod, "[-m MODE] NAME TYPE [MAJOR MINOR]"
t_mknod:
    lea rdx, [mkf_spec]
    lea rcx, [mkf_long]
    call opt_init
    mov r15d, 666o
    xor r14d, r14d
.o:
    call opt_next
    test eax, eax
    jz .od
    mov r14, [optarg]
    call get_umask
    mov esi, eax
    not esi
    and esi, 666o
    mov rdi, r14
    xor edx, edx
    call mode_apply
    jc .bad
    mov r15d, eax
    jmp .o
.od:
    mov r12, [nop]
    mov r13, [g_argv]
    cmp r12, 2
    jb missing
    mov rax, [r13+16]
    movzx eax, byte [rax]
    xor ebx, ebx                    ; dev
    mov ecx, S_IFIFO
    cmp al, 'p'
    je .nodev
    mov ecx, S_IFBLK
    cmp al, 'b'
    je .dev
    mov ecx, S_IFCHR
    cmp al, 'c'
    je .dev
    cmp al, 'u'
    je .dev
    lea rdi, [s_badtype]
    mov rsi, [r13+16]
    call emsg_q
    jmp try_help
.nodev:
    cmp r12, 2
    ja extra
    jmp .mk
.dev:
    cmp r12, 4
    jb missing
    ja extra
    push rcx
    mov rdi, [r13+24]
    call parse_u
    jc .badn
    mov rbx, rax
    mov rdi, [r13+32]
    call parse_u
    jc .badn
    ; makedev
    mov rcx, rax
    and ecx, 0xff
    mov rdx, rbx
    and edx, 0xfff
    shl edx, 8
    or ecx, edx
    mov rdx, rax
    and rdx, ~0xff
    shl rdx, 12
    or rcx, rdx
    mov rdx, rbx
    and rdx, ~0xfff
    shl rdx, 32
    or rcx, rdx
    mov rbx, rcx
    pop rcx
.mk:
    mov rdi, [r13+8]
    mov esi, r15d
    or esi, ecx
    mov rdx, rbx
    mov eax, SYS_mknod
    syscall
    test rax, rax
    js .err
    test r14, r14
    jz .ok
    mov rdi, [r13+8]
    mov esi, r15d
    mov eax, SYS_chmod
    syscall
.ok:
    xor eax, eax
    ret
.err:
    mov rdi, [r13+8]
    mov esi, eax
    call perr
    mov eax, 1
    ret
.badn:
    pop rcx
    lea rdi, [s_badnum]
    call emsg
    mov eax, 1
    ret
.bad:
    lea rdi, [s_badmode]
    mov rsi, r14
    call emsg_q
    mov eax, 1
    ret

section .rodata
mkf_spec: db "m:", 0
mkf_long:
    LONGOPT 'm', "mode"
    LONGEND
s_cantfifo: db "cannot create fifo '", 0
s_badtype:  db "invalid device type ", 0
section .text

; end of tools
section .atab
atab_end:

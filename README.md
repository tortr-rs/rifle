rifle
=====

Coreutils in one C file. No GNU in it, no libc under it: it's built on c.h, the one-file libc from
Chamesle Source (this copy has grown a bit, directories and users and the like).

cat, ls, cp and the rest are links to the one program, and it runs whichever name it was called by.
You type `cat`, not `rifle cat`.

    ./build.sh               builds rifle with clang and lld, makes the links in bin/
    rifle --install /bin     links in /bin (or anywhere)
    rifle --list             what's in here
    tests/run.sh             runs every case in tests/cases under GNU coreutils and under rifle, and compares
                             what they print and how they exit

It sorts and compares by bytes, always. No locales.

grep has its own regex matcher (BRE and ERE, backtracking, longest match from the leftmost start). The grep, sed, find
and xargs cases are compared against GNU grep 3.12, sed 4.10 and findutils 4.11, the rest against GNU coreutils.

What's in so far
----------------

[ arch base32 base64 basename cat chgrp chmod chown chroot cksum comm cp cut date dd df dirname du echo egrep env
expand expr factor false fgrep fold grep groups head hostid hostname id install join link ln logname ls md5sum mkdir
mkfifo mknod mktemp mv nice nl nohup nproc od paste printenv printf pwd readlink realpath rm rmdir seq
sha1sum sha224sum sha256sum sha384sum sha512sum shuf sleep sort split stat sum sync tac tail tee test
timeout touch tr true truncate tsort tty uname unexpand uniq unlink wc whoami yes

Not here yet: stty, who and friends, fmt, pr, csplit, numfmt, shred, stdbuf, ptx, pathchk, dircolors.
factor stops at 64 bits.

attic/ has the first version, 29 tools in assembly.

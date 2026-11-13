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

What's in so far
----------------

arch basename cat chgrp chmod chown comm cut dirname echo env false fold head hostid hostname link ln
logname mkdir mkfifo mknod mktemp nl nproc paste printenv pwd readlink realpath rm rmdir seq sleep sort
sync tac tail tee touch tr true tty uname uniq unlink wc whoami yes

Still to come: cp mv install ls stat id date df du dd od test expr printf the checksums, and the rest.

attic/ has the first version, 29 tools in assembly.

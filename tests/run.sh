#!/bin/sh
# run every case under GNU and under rifle, compare stdout and exit status.
# a case says $X cat f. under GNU that's /usr/bin/env -- cat f. under rifle it's cat found in rifle's bin/, the plain
# name, the way you'd use it, with everything else in the case (ls, find, sort...) still the system's
# usage: tests/run.sh [pattern]
cd "$(dirname "$0")/.."
export LC_ALL=C TZ=UTC
BIN=$PWD/bin
[ -x "$BIN/cat" ] || { echo 'run ./build.sh first' >&2; exit 2; }
T=$(mktemp -d /var/tmp/rifle-test.XXXXXX)
pass=0; fail=0
while IFS= read -r c; do
	case $c in ''|'#'*) continue;; esac
	[ -n "$1" ] && case $c in *"$1"*) ;; *) continue;; esac
	# a case that starts with @ro runs with the whole filesystem read-only (bwrap), for the ones that could do harm
	# if the tool had a bug: rm -rf /
	ro=
	case $c in '@ro '*)
		command -v bwrap >/dev/null || { echo "skipped (no bwrap): $c"; continue; }
		c=${c#@ro }; ro="bwrap --ro-bind / / --dev /dev --proc /proc";;
	esac
	rm -rf "$T/g" "$T/r"; mkdir "$T/g" "$T/r"
	(cd "$T/g" && X="/usr/bin/env --" PATH=/usr/bin:/bin $ro sh -c "$c" >"$T/g.out" 2>"$T/g.err"; echo "exit $?" >>"$T/g.out")
	(cd "$T/r" && X="/usr/bin/env PATH=$BIN" PATH=/usr/bin:/bin $ro sh -c "$c" >"$T/r.out" 2>"$T/r.err"; echo "exit $?" >>"$T/r.out")
	if cmp -s "$T/g.out" "$T/r.out"; then
		pass=$((pass+1))
	else
		fail=$((fail+1))
		printf "FAIL: %s\n" "$c"
		diff "$T/g.out" "$T/r.out" | head -8 | sed 's/^/    /'
	fi
done < tests/cases
rm -rf "$T"
echo "$pass passed, $fail failed"

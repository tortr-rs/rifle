#!/bin/sh
# run every case under GNU and under rifle, compare stdout and exit status
# usage: tests/run.sh [pattern]
cd "$(dirname "$0")/.."
R=$PWD/rifle
T=$(mktemp -d /var/tmp/rifle-test.XXXXXX)
pass=0; fail=0
while IFS= read -r c; do
	case $c in ''|'#'*) continue;; esac
	[ -n "$1" ] && case $c in *"$1"*) ;; *) continue;; esac
	rm -rf "$T/g" "$T/r"; mkdir "$T/g" "$T/r"
	(cd "$T/g" && X="/usr/bin/env --" PATH=/usr/bin:/bin sh -c "$c" >"$T/g.out" 2>"$T/g.err"; echo "exit $?" >>"$T/g.out")
	(cd "$T/r" && X="$R" PATH=/usr/bin:/bin sh -c "$c" >"$T/r.out" 2>"$T/r.err"; echo "exit $?" >>"$T/r.out")
	if cmp -s "$T/g.out" "$T/r.out"; then
		pass=$((pass+1))
	else
		fail=$((fail+1))
		echo "FAIL: $c"
		diff "$T/g.out" "$T/r.out" | head -8 | sed 's/^/    /'
	fi
done < tests/cases
rm -rf "$T"
echo "$pass passed, $fail failed"

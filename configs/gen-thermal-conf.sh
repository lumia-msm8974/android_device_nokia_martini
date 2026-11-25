#!/bin/sh
# Writes thermal-engine-8974.conf from thermal-engine-8974.src.conf without comments. The thermal-engine blob
# reads only the first 5120 bytes of its configuration: anything after that is silently dropped, so this
# fails when the result is larger. Run it after every change of the source and commit both files.
set -e
cd "$(dirname "$0")"
sed -e '/^#/d' -e 's/[[:space:]]\{1,\}/ /g' -e 's/ $//' thermal-engine-8974.src.conf | cat -s | sed '1{/^$/d}' > thermal-engine-8974.conf
n=$(wc -c < thermal-engine-8974.conf)
echo "thermal-engine-8974.conf: $n bytes, $(grep -c '^\[' thermal-engine-8974.conf) sections (limit 5120 bytes)"
[ "$n" -le 5120 ] || { echo "too large: the blob would drop the end of the file" >&2; exit 1; }

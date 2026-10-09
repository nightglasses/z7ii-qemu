#!/bin/sh
# Read-only, finite original-byte gate. This script never launches a guest.
set -eu
if [ "$#" -ne 1 ]; then
    printf '%s\n' 'usage: check-software-wait-sites.sh ORIGINAL_MAS_PAYLOAD' >&2
    exit 2
fi
payload=$1
if [ ! -f "$payload" ]; then
    printf '%s\n' 'original MAS input is not a regular file' >&2
    exit 2
fi
size=$(wc -c < "$payload")
if [ "$size" -ne 38239096 ]; then
    printf '%s\n' 'original MAS size mismatch' >&2
    exit 1
fi
check_site() {
    va=$1 offset=$2 expected=$3
    if [ "$((va - offset))" -ne "$((0x10020000))" ]; then
        printf '%s\n' 'invalid static VA/outer-offset relation' >&2
        exit 1
    fi
    actual=$(xxd -p -s "$offset" -l "$((${#expected} / 2))" "$payload" | tr -d '\n')
    if [ "$actual" != "$expected" ]; then
        printf 'original byte mismatch at %s\n' "$va" >&2
        exit 1
    fi
}
# Entire two original-byte candidates remain source_accepted=false.
check_site 0x104000b0 0x3e00b0 c0b2022810b517d1e1f6c6fbb2f723f9e1f60afec6f710fbe4f6c0f8e5f6f4f844f63dfe97f6e9f8b5f75dfbe5f6f7fc45f66af889f724ff012010bd
check_site 0x103b2306 0x392306 10b5d6f797fcd6f7a4fad7f70cfad6f78cfabde810408f48d6f753bd
# Exact saved native source instructions from RTOS 17f765d, not decoded anew.
check_site 0x10201a24 0x1e1a24 8847
check_site 0x10360ed8 0x340ed8 06b0
check_site 0x10120908 0x100908 f0412de9
check_site 0x1010fdd4 0xefdd4 300081e5
check_site 0x1010ffd8 0xeffd8 f0472de9
check_site 0x10110138 0xf0138 c64100eb
check_site 0x10120858 0x100858 402090e5
printf '%s\n' 'PASS: nine original byte guards; no guest or native launch'

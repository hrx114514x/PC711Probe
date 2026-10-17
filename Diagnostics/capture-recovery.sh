#!/bin/sh
# Read-only commands; output only under Recovery /tmp. No EFI changes.
set -u
umask 077
out=$(mktemp -d /tmp/PM9C1a-review.XXXXXX) || exit 1
capture() {
    label=$1
    shift
    "$@" > "$out/$label.txt" 2>&1
    status=$?
    printf '%s exit=%s\n' "$label" "$status" >> "$out/status.txt"
    return 0
}
capture version sw_vers
capture kernel uname -a
capture boot-args nvram boot-args
capture kexts kmutil showloaded
capture kextstat kextstat
capture physical-disks diskutil list physical
capture nvme ioreg -r -c IONVMeController -t -l -w0 -x
capture pci ioreg -r -c IOPCIDevice -t -l -w0 -x
capture logs log show --last boot --style compact --info --debug --predicate \
    '(process == "kernel") AND (eventMessage CONTAINS[c] "PM9C1a" OR eventMessage CONTAINS[c] "IONVMe" OR eventMessage CONTAINS[c] "PC711")'
printf 'Saved to %s. Copy to separate evidence media before reboot; review privacy before sharing.\n' "$out"
printf 'Missing logs or markers do not prove a branch was not executed.\n'

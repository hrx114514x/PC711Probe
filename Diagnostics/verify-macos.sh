#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sh "$root/Diagnostics/build-macos.sh"
out="$root/build/Issue9Review"
binary="$out/PM9C1aReadOnly.kext/Contents/MacOS/PM9C1aReadOnly"
plist="$out/PM9C1aReadOnly.kext/Contents/Info.plist"
file "$binary" | grep -q 'Mach-O 64-bit kext bundle x86_64'
nm -g "$binary" | grep -q ' _kmod_info$'
nm "$binary" | grep -q ' __start$'
nm "$binary" | grep -q ' __stop$'
nm "$binary" | grep -q ' _PM9C1aReadOnly_kern_start$'
nm "$binary" | grep -q ' _PM9C1aReadOnly_kern_stop$'
nm -u "$binary" | grep -q '__ZN7LiluAPI10onKextLoad'
if nm -u "$binary" | grep -E 'configWrite|configureInterrupts|mapDeviceMemory|allocateDeviceInterrupts'; then
    echo 'Unexpected hardware mutation or mapping dependency in diagnostic' >&2
    exit 1
fi
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist")" = 'com.stationk9.diagnostic.PM9C1aReadOnly'
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")" = 'PM9C1aReadOnly'
cat > "$out/REVIEW_ONLY.txt" <<'EOF'
PM9C1aReadOnly is a diagnostic experiment, not a storage compatibility fix.
Native compilation and static inspection do not establish bootability or hardware safety.
Enable only after reviewing docs/ISSUE9_REVIEW.md and the exact Apple binary ABI.
Darwin 24.6.0, PCI 144D:A80D, NVMe class 01:08:02, explicit -pm9c1adiag.
-pm9c1adiagoff takes precedence. Use a separate rollback-capable USB EFI.
Do not combine this with PC711Probe, Force, NVMeFix or the old PM9C1aDiag.
No hardware tests have been performed for this artifact.
EOF
echo 'Issue 9 diagnostic native build and static inspection passed'

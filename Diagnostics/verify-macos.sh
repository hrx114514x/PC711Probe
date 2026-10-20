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
# plugin_start.o emits the IOService subclass vtable, which references inherited
# IOService::mapDeviceMemoryWithIndex even though our diagnostic never calls it.
# Inspect the diagnostic implementation object, not inherited vtable imports.
if nm -u "$out/diag.o" | grep -E 'configWrite|configureInterrupts|mapDeviceMemory|allocateDeviceInterrupts|getDeviceMemory|getInterruptType|getPhysicalSegment|createMapping|resolveInterrupts'; then
    echo 'Unexpected hardware mutation or mapping dependency in diagnostic' >&2
    exit 1
fi
if grep -E 'configWrite|configureInterrupts|mapDeviceMemory|allocateDeviceInterrupts|getDeviceMemory|getInterruptType|getPhysicalSegment|createMapping|resolveInterrupts' "$root/Diagnostics/PM9C1aReadOnly.cpp"; then
    echo 'Unexpected hardware mutation or mapping API in diagnostic source' >&2
    exit 1
fi
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist")" = 'com.stationk9.diagnostic.PM9C1aReadOnly'
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")" = 'PM9C1aReadOnly'
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$plist")" = '0.3.0'
test "$(/usr/libexec/PlistBuddy -c 'Print :OSBundleLibraries:as.vit9696.Lilu' "$plist")" = '1.7.3'
xcrun clang++ -std=c++14 -Wall -Wextra -Werror -pthread \
    "$root/Tests/issue9_tracking.cpp" -o "$out/tracking-test"
"$out/tracking-test"
xcrun clang++ -std=c++14 -Wall -Wextra -Werror \
    "$root/Tests/issue9_resources.cpp" -o "$out/resources-test"
"$out/resources-test"
cat > "$out/REVIEW_ONLY.txt" <<'EOF'
PM9C1aReadOnly 0.3.0 observes exact InitializePCI exits and passive PCI resource metadata.
It is not a storage compatibility fix.
Native compilation and static inspection do not establish bootability or hardware safety.
Enable only after reviewing docs/ISSUE9_REVIEW.md and the exact Apple binary ABI.
Darwin 24.6.0, exact verified Apple code bytes, Lilu 1.7.3+, PCI 144D:A80D,
NVMe class 01:08:02, explicit -pm9c1adiag. Fingerprint/range mismatch installs no routes.
-pm9c1adiagoff takes precedence. Use a separate rollback-capable USB EFI.
Do not combine this with PC711Probe, Force, NVMeFix or the old PM9C1aDiag.
Version 0.2.0 identified the map exit on the reporter hardware. Version 0.3.0 has not been hardware tested.
New PM9C1aROResBefore/ResAtMapExit and LastBridge dictionaries require matching Call/ProviderID.
SnapshotReady=0, incomplete scans and missing properties are inconclusive.
Inspect PM9C1aROLastInitializePCI: Result=0 and ObservedExitMask 1=provider,
2=map unavailable, 4=virtual address zero, 8=mapper false. Zero is inconclusive.
InstalledRoutes=127 means all seven routes installed; Tracked=1 means this call was tracked.
EOF
cp "$root/docs/ISSUE9_RUNTIME.md" "$out/ISSUE9_RUNTIME.md"
tar -czf "$out/PM9C1aReadOnly-0.3.0-REVIEW_ONLY.tar.gz" -C "$out" \
    PM9C1aReadOnly.kext REVIEW_ONLY.txt ISSUE9_RUNTIME.md undefined-symbols.txt
echo 'Issue 9 diagnostic native build and static inspection passed'

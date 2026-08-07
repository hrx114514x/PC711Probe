#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="$project_dir/build/Debug/PC711Probe.kext/Contents/MacOS/PC711Probe"
plist="$project_dir/build/Debug/PC711Probe.kext/Contents/Info.plist"
source="$project_dir/Driver/PC711Probe.cpp"
license="$project_dir/LICENSE"

"$project_dir/Scripts/build.sh"

plutil -lint "$plist"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$plist")" = "1.2.0"
test "$(/usr/libexec/PlistBuddy -c 'Print :OSBundleLibraries:com.apple.iokit.IOPCIFamily' "$plist")" = "2.9"
file "$binary" | grep -q "Mach-O 64-bit kext bundle x86_64"
nm -g "$binary" | grep -q ' _kmod_info$'
nm "$binary" | grep -q ' __start$'
nm "$binary" | grep -q ' __stop$'
nm "$binary" | grep -q ' _OSKextGetCurrentIdentifier$'
nm "$binary" | grep -q ' _PC711Probe_kern_start$'
nm "$binary" | grep -q ' _PC711Probe_kern_stop$'
nm -u "$binary" | grep -q '__ZN7LiluAPI10onKextLoad'
strings -a "$binary" | grep -q '__ZN16IONVMeController21CreateDeviceInterruptEPFvP8OSObjectP22IOInterruptEventSourceiEPFbS1_P28IOFilterInterruptEventSourceEP9IOService'
if nm -u "$binary" | grep -q '__ZN11IOPCIDevice19configureInterruptsEjjjj'; then
	echo "configureInterrupts must be invoked through its stable virtual slot" >&2
	exit 1
fi

grep -q 'kPC711Vendor {0x1C5C}' "$source"
grep -q 'kPC711Device {0x174A}' "$source"
grep -q 'kNvmeClassRevisionValue {0x01080200U}' "$source"
grep -q 'kConfigureInterruptsVtableSlot {0x960 / sizeof(uintptr_t)}' "$source"
grep -q 'kLegacyCreateDeviceInterruptPattern' "$source"
grep -q 'configure(pci, kInterruptTypeMSIX, 1, 1, 0)' "$source"
grep -q 'class PC711EarlyMSIX' "$source"
grep -q 'PC711CompatEarlyMSIXRequested' "$source"
grep -q '0x174A1C5C' "$plist"
grep -q 'kControllerFlagsOffset {0x191}' "$source"
grep -q 'PC711CompatConfigureInterruptsResult' "$source"
grep -q 'PC711CompatLegacyMSIXFlagAfter' "$source"
grep -q 'PC711CompatEventSourceCreated' "$source"
grep -q 'AllowNormal | LiluAPI::AllowInstallerRecovery' "$source"
grep -q 'KernelVersion::BigSur' "$source"
grep -q 'KernelVersion::Sequoia' "$source"
grep -q 'PC711ProbePlugin::globalPlugin().init()' "$source"
grep -q '^// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0$' "$source"
grep -q '^# PolyForm Noncommercial License 1.0.0$' "$license"
grep -q 'PolyForm Noncommercial 1.0.0' "$plist"

if grep -q -- '-pc711pcompat\|-pc711pstage' "$source"; then
	echo "Manual activation or diagnostic-stage boot argument found" >&2
	exit 1
fi

if grep -q 'KernelVersion::Tahoe' "$source"; then
	echo "macOS 26 must remain native" >&2
	exit 1
fi

echo "Static compatibility checks passed"
shasum -a 256 "$binary" "$plist"

#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="$project_dir/build/Debug/PC711Probe.kext/Contents/MacOS/PC711Probe"
plist="$project_dir/build/Debug/PC711Probe.kext/Contents/Info.plist"
force_binary="$project_dir/build/Debug/PC711ProbeForce.kext/Contents/MacOS/PC711Probe"
force_plist="$project_dir/build/Debug/PC711ProbeForce.kext/Contents/Info.plist"
source="$project_dir/Driver/PC711Probe.cpp"
license="$project_dir/LICENSE"

"$project_dir/Scripts/build.sh"

plutil -lint "$plist"
plutil -lint "$force_plist"

for artifact in "$binary" "$force_binary"; do
	file "$artifact" | grep -q "Mach-O 64-bit kext bundle x86_64"
	nm -g "$artifact" | grep -q ' _kmod_info$'
	nm "$artifact" | grep -q ' __start$'
	nm "$artifact" | grep -q ' __stop$'
	nm "$artifact" | grep -q ' _OSKextGetCurrentIdentifier$'
	nm "$artifact" | grep -q ' _PC711Probe_kern_start$'
	nm "$artifact" | grep -q ' _PC711Probe_kern_stop$'
	nm -u "$artifact" | grep -q '__ZN7LiluAPI10onKextLoad'
	strings -a "$artifact" | grep -q '__ZN16IONVMeController21CreateDeviceInterruptEPFvP8OSObjectP22IOInterruptEventSourceiEPFbS1_P28IOFilterInterruptEventSourceEP9IOService'
	if nm -u "$artifact" | grep -q '__ZN11IOPCIDevice19configureInterruptsEjjjj'; then
		echo "configureInterrupts must be invoked through its stable virtual slot" >&2
		exit 1
	fi
done

for metadata in "$plist" "$force_plist"; do
	test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$metadata")" = "1.8.1"
	test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$metadata")" = "com.stationk9.driver.PC711Probe"
	test "$(/usr/libexec/PlistBuddy -c 'Print :OSBundleLibraries:as.vit9696.Lilu' "$metadata")" = "1.6.1"
	test "$(/usr/libexec/PlistBuddy -c 'Print :OSBundleLibraries:com.apple.iokit.IOPCIFamily' "$metadata")" = "2.9"
done

grep -q 'kPC711Vendor {0x1C5C}' "$source"
grep -q 'kPC711Device {0x174A}' "$source"
grep -q 'PC711PROBE_FORCE_VARIANT' "$source"
grep -q 'isTargetNVMeDevice' "$source"
grep -q 'kNvmeClassRevisionValue {0x01080200U}' "$source"
grep -q 'kConfigureInterruptsVtableSlot {0x960 / sizeof(uintptr_t)}' "$source"
grep -q 'kLegacyCreateDeviceInterruptPattern' "$source"
grep -q 'configure(pci, kInterruptTypeMSIX, 1, 1, 0)' "$source"
grep -q 'class PC711EarlyMSIX' "$source"
grep -q 'PC711CompatEarlyMSIXRequested' "$source"
grep -q '0x174A1C5C' "$plist"
grep -q '0x01080200&amp;0xFFFFFF00' "$force_plist"
if grep -q 'IOPCIPrimaryMatch' "$force_plist"; then
	echo "Force variant must not contain a PCI Vendor/Device matcher" >&2
	exit 1
fi
if grep -q 'IOPCIClassMatch' "$plist"; then
	echo "Standard variant must retain the narrow PCI Vendor/Device matcher" >&2
	exit 1
fi
grep -q 'reallocateBigSurPC711MSIX' "$source"
grep -q 'PC711CompatBigSurMSIXReallocated' "$source"
grep -q 'PC711CompatBigSurMSIXReallocationResult' "$source"
grep -q 'kAllocateDeviceInterruptsSymbol' "$source"
grep -q 'kDeallocateDeviceInterruptsSymbol' "$source"
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
grep -q '^Required Notice: Copyright 2026 hrx114514x\.$' "$license"
grep -q 'PolyForm Noncommercial 1.0.0' "$plist"
nm -m "$binary" | grep -q 'weak external __ZN32IOPCIMessagedInterruptController24allocateDeviceInterruptsEP9IOServicejjPyPj'
nm -m "$force_binary" | grep -q 'weak external __ZN32IOPCIMessagedInterruptController24allocateDeviceInterruptsEP9IOServicejjPyPj'
strings -a "$force_binary" | grep -q 'force variant targets every NVMe class 01:08:02 controller'
if strings -a "$binary" | grep -q 'force variant targets every NVMe class 01:08:02 controller'; then
	echo "Standard variant unexpectedly contains Force behavior" >&2
	exit 1
fi
if cmp -s "$binary" "$force_binary"; then
	echo "Standard and Force binaries must not be identical" >&2
	exit 1
fi
otool -tvV "$binary" | grep -q 'cmpl.*\$0x1c5c'
otool -tvV "$binary" | grep -q 'cmpl.*\$0x174a'
if otool -tvV "$force_binary" | grep -q 'cmpl.*\$0x1c5c\|cmpl.*\$0x174a'; then
	echo "Force binary unexpectedly retains the PCI Vendor/Device gate" >&2
	exit 1
fi

if grep -q -- '-pc711pcompat\|-pc711pstage' "$source"; then
	echo "Manual activation or diagnostic-stage boot argument found" >&2
	exit 1
fi

if grep -q 'KernelVersion::Tahoe' "$source"; then
	echo "macOS 26 must remain native" >&2
	exit 1
fi

echo "Static compatibility checks passed"
shasum -a 256 "$binary" "$plist" "$force_binary" "$force_plist"

#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="$project_dir/build/Debug/PC711Probe.kext/Contents/MacOS/PC711Probe"
plist="$project_dir/build/Debug/PC711Probe.kext/Contents/Info.plist"
source="$project_dir/Driver/PC711Probe.cpp"

"$project_dir/Scripts/build.sh"

plutil -lint "$plist"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$plist")" = "0.6.0"
test "$(/usr/libexec/PlistBuddy -c 'Print :OSBundleLibraries:com.apple.iokit.IOPCIFamily' "$plist")" = "2.9"
file "$binary" | grep -q "Mach-O 64-bit kext bundle x86_64"
nm -g "$binary" | grep -q ' _kmod_info$'
nm "$binary" | grep -q ' __start$'
nm "$binary" | grep -q ' __stop$'
nm "$binary" | grep -q ' _OSKextGetCurrentIdentifier$'
nm "$binary" | grep -q ' _PC711Probe_kern_start$'
nm "$binary" | grep -q ' _PC711Probe_kern_stop$'
nm -u "$binary" | grep -q '__ZN7LiluAPI10onKextLoad'
strings -a "$binary" | grep -q '__ZN16IONVMeController10IsFullInitEv'
strings -a "$binary" | grep -q '__ZN16IONVMeController20IssueIdentifyCommandEP18IOMemoryDescriptorjb'
strings -a "$binary" | grep -q '__ZN16IONVMeController24CreateBlockStorageDeviceEj'
strings -a "$binary" | grep -q '__ZN16IONVMeController21CreateDeviceInterruptEPFvP8OSObjectP22IOInterruptEventSourceiEPFbS1_P28IOFilterInterruptEventSourceEP9IOService'
nm -u "$binary" | grep -q '__ZN11IOPCIDevice19configureInterruptsEjjjj'
strings -a "$binary" | grep -q '__ZN24IONVMeBlockStorageDevice16doAsyncReadWriteEP18IOMemoryDescriptoryyP19IOStorageAttributesP19IOStorageCompletion'
strings -a "$binary" | grep -q '__ZN24IONVMeBlockStorageDevice13doFormatMediaEy'
strings -a "$binary" | grep -q '__ZN24IONVMeBlockStorageDevice21reportWriteProtectionEPb'
strings -a "$binary" | grep -q '__ZN24IONVMeBlockStorageDevice7doUnmapEP26IOBlockStorageDeviceExtentjj'

grep -q -- '-pc711pstage0' "$source"
grep -q -- '-pc711pstage1' "$source"
grep -q -- '-pc711pstage2' "$source"
grep -q -- '-pc711pstage3' "$source"
grep -q -- '-pc711pcompat' "$source"
grep -q 'KernelVersion::Sequoia' "$source"
grep -q 'getKernelMinorVersion() == 6' "$source"
grep -q 'getKernelMinorVersion() == 5' "$source"
grep -q 'kIOMapReadOnly' "$source"
grep -q 'kTargetVendor {0x1C5C}' "$source"
grep -q 'kTargetDevice {0x174A}' "$source"
grep -q 'strstr(path, "GPP3")' "$source"
grep -q 'PC711ProbeIdentifyForcedStop' "$source"
grep -q 'PC711ProbeBlockDeviceCreationBlocked' "$source"
grep -q 'PC711ProbeNamespaceForcedStop' "$source"
grep -q 'PC711ProbeNamespaceIdentifyData' "$source"
grep -q 'IOBufferMemoryDescriptor' "$source"
grep -q 'PC711ProbeReadOnlyGuardsReady' "$source"
grep -q 'PC711ProbeReportedWriteProtected' "$source"
grep -q 'PC711ProbeBlockedWrite' "$source"
grep -q 'PC711ProbeBlockedUnmap' "$source"
grep -q 'PC711ProbeBlockedFormat' "$source"
grep -q 'direction != kIODirectionIn' "$source"
grep -q 'stage3GuardsReady = true' "$source"
grep -q 'isTargetCompat(controller, pci)' "$source"
grep -q 'AllowNormal | LiluAPI::AllowInstallerRecovery' "$source"
grep -q 'kPC711InterruptTypeMSIX, 1, 1, 0' "$source"
grep -q 'kDarwin24ControllerFlagsOffset {0x191}' "$source"
grep -q 'PC711CompatConfigureInterruptsResult' "$source"
grep -q 'PC711CompatLegacyMSIXFlagAfter' "$source"
grep -q 'PC711CompatEventSourceCreated' "$source"
grep -q 'return kIOReturnUnsupported' "$source"

if grep -nE 'configWrite|OSWrite|writeBytes|memcpy.*address' "$source"; then
	echo "Unsafe write primitive found in Stage 0 source" >&2
	exit 1
fi

echo "Static safety checks passed"
shasum -a 256 "$binary" "$plist"

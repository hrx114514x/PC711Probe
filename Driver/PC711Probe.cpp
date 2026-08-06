// SPDX-License-Identifier: BSD-3-Clause

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/pci/IOPCIFamilyDefinitions.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <libkern/OSByteOrder.h>
#include <libkern/version.h>

#include <Headers/kern_api.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>

namespace {

constexpr uint16_t kTargetVendor {0x1C5C};
constexpr uint16_t kTargetDevice {0x174A};
constexpr uint32_t kNvmeClassRevisionMask {0xFFFFFF00U};
constexpr uint32_t kNvmeClassRevisionValue {0x01080200U};
constexpr uint32_t kPC711InterruptTypeMSIX {0x00020000U};
constexpr size_t kDarwin24ControllerFlagsOffset {0x191};
constexpr uint8_t kDarwin24LegacyMSIXPathFlag {0x10};
constexpr size_t kMinimumBar0Length {0x38};
constexpr size_t kIdentifyDataLength {4096};

constexpr const char *kIsFullInitSymbol {"__ZN16IONVMeController10IsFullInitEv"};
constexpr const char *kIssueIdentifyCommandSymbol {
	"__ZN16IONVMeController20IssueIdentifyCommandEP18IOMemoryDescriptorjb"
};
constexpr const char *kCreateBlockStorageDeviceSymbol {
	"__ZN16IONVMeController24CreateBlockStorageDeviceEj"
};
constexpr const char *kCreateDeviceInterruptSymbol {
	"__ZN16IONVMeController21CreateDeviceInterruptEPFvP8OSObjectP22IOInterruptEventSourceiEPFbS1_P28IOFilterInterruptEventSourceEP9IOService"
};
constexpr const char *kDoAsyncReadWriteSymbol {
	"__ZN24IONVMeBlockStorageDevice16doAsyncReadWriteEP18IOMemoryDescriptoryyP19IOStorageAttributesP19IOStorageCompletion"
};
constexpr const char *kDoFormatMediaSymbol {
	"__ZN24IONVMeBlockStorageDevice13doFormatMediaEy"
};
constexpr const char *kReportWriteProtectionSymbol {
	"__ZN24IONVMeBlockStorageDevice21reportWriteProtectionEPb"
};
constexpr const char *kDoUnmapSymbol {
	"__ZN24IONVMeBlockStorageDevice7doUnmapEP26IOBlockStorageDeviceExtentjj"
};

class PC711ProbePlugin {
public:
	void init();
	void setStage(uint32_t stage) { stageMode = stage; }
	static PC711ProbePlugin &globalPlugin();

private:
	using IsFullInit = bool (*)(void *controller);
	using IssueIdentifyCommand = IOReturn (*)(void *controller,
		IOMemoryDescriptor *buffer, uint32_t namespaceId, bool controllerIdentify);
	using CreateBlockStorageDevice = bool (*)(void *controller, uint32_t namespaceId);
	using CreateDeviceInterrupt = IOFilterInterruptEventSource *(*)(void *controller,
		IOInterruptEventAction action, IOFilterInterruptAction filter,
		IOService *provider);
	using DoAsyncReadWrite = IOReturn (*)(void *device,
		IOMemoryDescriptor *buffer, uint64_t block, uint64_t nblks,
		IOStorageAttributes *attributes, IOStorageCompletion *completion);
	using DoFormatMedia = IOReturn (*)(void *device, uint64_t byteCapacity);
	using ReportWriteProtection = IOReturn (*)(void *device, bool *isWriteProtected);
	using DoUnmap = IOReturn (*)(void *device,
		IOBlockStorageDeviceExtent *extents, uint32_t extentsCount,
		IOStorageUnmapOptions options);

	static void processKext(void *context, KernelPatcher &patcher, size_t index,
		mach_vm_address_t address, size_t size);
	static bool wrapIsFullInit(void *controller);
	static IOReturn wrapIssueIdentifyCommand(void *controller,
		IOMemoryDescriptor *buffer, uint32_t namespaceId, bool controllerIdentify);
	static bool wrapCreateBlockStorageDevice(void *controller, uint32_t namespaceId);
	static IOFilterInterruptEventSource *wrapCreateDeviceInterrupt(void *controller,
		IOInterruptEventAction action, IOFilterInterruptAction filter,
		IOService *provider);
	static IOReturn wrapDoAsyncReadWrite(void *device,
		IOMemoryDescriptor *buffer, uint64_t block, uint64_t nblks,
		IOStorageAttributes *attributes, IOStorageCompletion *completion);
	static IOReturn wrapDoFormatMedia(void *device, uint64_t byteCapacity);
	static IOReturn wrapReportWriteProtection(void *device, bool *isWriteProtected);
	static IOReturn wrapDoUnmap(void *device,
		IOBlockStorageDeviceExtent *extents, uint32_t extentsCount,
		IOStorageUnmapOptions options);

	bool isTarget(IOService *controller, IOPCIDevice *&pci, char *path,
		size_t pathCapacity) const;
	bool isTargetCompat(IOService *controller, IOPCIDevice *&pci) const;
	bool isTargetBlockDevice(IOService *device, IOPCIDevice *&pci, char *path,
		size_t pathCapacity) const;
	void markStage3ReadOnly(IOService *device, IOPCIDevice *pci,
		const char *path) const;
	void captureReadOnlyState(IOPCIDevice *pci, const char *path,
		uint32_t stage, bool blockedFullInit) const;
	void captureAfterIdentifyState(IOPCIDevice *pci) const;
	bool captureIdentifyData(IOPCIDevice *pci, IOMemoryDescriptor *buffer,
		const char *property) const;

	IsFullInit originalIsFullInit {nullptr};
	IssueIdentifyCommand originalIssueIdentifyCommand {nullptr};
	CreateBlockStorageDevice originalCreateBlockStorageDevice {nullptr};
	CreateDeviceInterrupt originalCreateDeviceInterrupt {nullptr};
	DoAsyncReadWrite originalDoAsyncReadWrite {nullptr};
	DoFormatMedia originalDoFormatMedia {nullptr};
	ReportWriteProtection originalReportWriteProtection {nullptr};
	DoUnmap originalDoUnmap {nullptr};
	uint32_t stageMode {0};
	bool stage3GuardsReady {false};

	const char *kextPath {
		"/System/Library/Extensions/IONVMeFamily.kext/Contents/MacOS/IONVMeFamily"
	};

	KernelPatcher::KextInfo kextInfo {
		"com.apple.iokit.IONVMeFamily",
		&kextPath,
		1,
		{true},
		{},
		KernelPatcher::KextInfo::Unloaded
	};
};

PC711ProbePlugin plugin;

// Present in Darwin 24's IOPCIFamily but only called by IONVMeFamily starting
// in Darwin 25. Use the exported implementation to reproduce that setup.
extern "C" IOReturn IOPCIDeviceConfigureInterrupts(IOPCIDevice *device,
	uint32_t interruptType, uint32_t numRequired, uint32_t numRequested,
	IOOptionBits options)
	__asm("__ZN11IOPCIDevice19configureInterruptsEjjjj");

PC711ProbePlugin &PC711ProbePlugin::globalPlugin() {
	return plugin;
}

bool PC711ProbePlugin::isTarget(IOService *controller, IOPCIDevice *&pci,
		char *path, size_t pathCapacity) const {
	pci = nullptr;
	if (!controller || !path || pathCapacity < 2)
		return false;

	pci = OSDynamicCast(IOPCIDevice, controller->getProvider());
	if (!pci)
		return false;

	const auto vendor = pci->configRead16(kIOPCIConfigVendorID);
	const auto device = pci->configRead16(kIOPCIConfigDeviceID);
	const auto classRevision = pci->configRead32(kIOPCIConfigRevisionID);
	if (vendor != kTargetVendor || device != kTargetDevice ||
		(classRevision & kNvmeClassRevisionMask) != kNvmeClassRevisionValue)
		return false;

	int pathLength = static_cast<int>(pathCapacity);
	path[0] = '\0';
	if (!pci->getPath(path, &pathLength, gIODTPlane)) {
		SYSLOG("probe", "target PCI ID seen, but DT path lookup failed; leaving Apple behaviour unchanged");
		return false;
	}

	// The PCI ID is shared by several SK hynix models. GPP3 is therefore a
	// mandatory second key on this machine. If the path changes, fail closed
	// and call the original Apple function.
	if (!strstr(path, "GPP3")) {
		SYSLOG("probe", "1C5C:174A found outside GPP3 at %s; leaving Apple behaviour unchanged", path);
		return false;
	}

	return true;
}

bool PC711ProbePlugin::isTargetCompat(IOService *controller,
		IOPCIDevice *&pci) const {
	pci = nullptr;
	if (!controller)
		return false;

	pci = OSDynamicCast(IOPCIDevice, controller->getProvider());
	if (!pci)
		return false;

	const auto vendor = pci->configRead16(kIOPCIConfigVendorID);
	const auto device = pci->configRead16(kIOPCIConfigDeviceID);
	const auto classRevision = pci->configRead32(kIOPCIConfigRevisionID);
	return vendor == kTargetVendor && device == kTargetDevice &&
		(classRevision & kNvmeClassRevisionMask) == kNvmeClassRevisionValue;
}

bool PC711ProbePlugin::isTargetBlockDevice(IOService *device,
		IOPCIDevice *&pci, char *path, size_t pathCapacity) const {
	if (!device)
		return false;

	// IONVMeBlockStorageDevice is a direct child of IONVMeController. Reuse
	// the controller's PCI ID and GPP3 path checks instead of trusting model
	// strings supplied by the namespace.
	return isTarget(device->getProvider(), pci, path, pathCapacity);
}

void PC711ProbePlugin::markStage3ReadOnly(IOService *device,
		IOPCIDevice *pci, const char *path) const {
	if (!device || !pci)
		return;
	if (device->getProperty("PC711ProbeReadOnly"))
		return;

	captureReadOnlyState(pci, path, 3, false);
	pci->setProperty("PC711ProbeReadOnlyGuardsReady", stage3GuardsReady);
	device->setProperty("PC711ProbeReadOnly", true);
	device->setProperty("PC711ProbeStage", 3ULL, 32);
}

void PC711ProbePlugin::captureReadOnlyState(IOPCIDevice *pci, const char *path,
		uint32_t stage, bool blockedFullInit) const {
	if (!pci)
		return;

	pci->setProperty("PC711ProbeStage", static_cast<unsigned long long>(stage), 32);
	pci->setProperty("PC711ProbeBlockedFullInit", blockedFullInit);
	pci->setProperty("PC711ProbePath", path ? path : "(unavailable)");
	pci->setProperty("PC711ProbeVendor", static_cast<unsigned long long>(kTargetVendor), 16);
	pci->setProperty("PC711ProbeDevice", static_cast<unsigned long long>(kTargetDevice), 16);

	// Do not remap repeatedly if IsFullInit is queried more than once.
	if (pci->getProperty("PC711ProbeCaptured"))
		return;

	auto map = pci->mapDeviceMemoryWithRegister(
		kIOPCIConfigBaseAddress0, kIOMapAnywhere | kIOMapReadOnly);
	if (!map) {
		pci->setProperty("PC711ProbeBAR0Mapped", false);
		pci->setProperty("PC711ProbeCaptured", true);
		SYSLOG("probe", "blocked full init for %s; BAR0 read-only mapping failed", path);
		return;
	}

	const auto address = map->getVirtualAddress();
	const auto length = map->getLength();
	if (!address || length < kMinimumBar0Length) {
		pci->setProperty("PC711ProbeBAR0Mapped", false);
		pci->setProperty("PC711ProbeBAR0Length",
			static_cast<unsigned long long>(length), 64);
		pci->setProperty("PC711ProbeCaptured", true);
		SYSLOG("probe", "blocked full init for %s; BAR0 mapping is too short (%llu)",
			path, static_cast<unsigned long long>(length));
		map->release();
		return;
	}

	const auto base = reinterpret_cast<const volatile void *>(address);
	const auto cap = OSReadLittleInt64(base, 0x00);
	const auto version = OSReadLittleInt32(base, 0x08);
	const auto cc = OSReadLittleInt32(base, 0x14);
	const auto csts = OSReadLittleInt32(base, 0x1C);
	const auto aqa = OSReadLittleInt32(base, 0x24);
	const auto asq = OSReadLittleInt64(base, 0x28);
	const auto acq = OSReadLittleInt64(base, 0x30);

	pci->setProperty("PC711ProbeBAR0Mapped", true);
	pci->setProperty("PC711ProbeBAR0Length",
		static_cast<unsigned long long>(length), 64);
	pci->setProperty("PC711ProbeCAP", static_cast<unsigned long long>(cap), 64);
	pci->setProperty("PC711ProbeVS", static_cast<unsigned long long>(version), 32);
	pci->setProperty("PC711ProbeCC", static_cast<unsigned long long>(cc), 32);
	pci->setProperty("PC711ProbeCSTS", static_cast<unsigned long long>(csts), 32);
	pci->setProperty("PC711ProbeAQA", static_cast<unsigned long long>(aqa), 32);
	pci->setProperty("PC711ProbeASQ", static_cast<unsigned long long>(asq), 64);
	pci->setProperty("PC711ProbeACQ", static_cast<unsigned long long>(acq), 64);
	pci->setProperty("PC711ProbeCaptured", true);

	SYSLOG("probe", "blocked full init for %s CAP=%llx VS=%x CC=%x CSTS=%x AQA=%x ASQ=%llx ACQ=%llx",
		path,
		static_cast<unsigned long long>(cap), version, cc, csts, aqa,
		static_cast<unsigned long long>(asq),
		static_cast<unsigned long long>(acq));

	map->release();
}

void PC711ProbePlugin::captureAfterIdentifyState(IOPCIDevice *pci) const {
	if (!pci)
		return;

	auto map = pci->mapDeviceMemoryWithRegister(
		kIOPCIConfigBaseAddress0, kIOMapAnywhere | kIOMapReadOnly);
	if (!map)
		return;

	const auto address = map->getVirtualAddress();
	const auto length = map->getLength();
	if (address && length >= kMinimumBar0Length) {
		const auto base = reinterpret_cast<const volatile void *>(address);
		pci->setProperty("PC711ProbeAfterCC",
			static_cast<unsigned long long>(OSReadLittleInt32(base, 0x14)), 32);
		pci->setProperty("PC711ProbeAfterCSTS",
			static_cast<unsigned long long>(OSReadLittleInt32(base, 0x1C)), 32);
		pci->setProperty("PC711ProbeAfterAQA",
			static_cast<unsigned long long>(OSReadLittleInt32(base, 0x24)), 32);
		pci->setProperty("PC711ProbeAfterASQ",
			static_cast<unsigned long long>(OSReadLittleInt64(base, 0x28)), 64);
		pci->setProperty("PC711ProbeAfterACQ",
			static_cast<unsigned long long>(OSReadLittleInt64(base, 0x30)), 64);
	}

	map->release();
}

bool PC711ProbePlugin::captureIdentifyData(IOPCIDevice *pci,
		IOMemoryDescriptor *buffer, const char *property) const {
	if (!pci || !buffer || !property || buffer->getLength() < kIdentifyDataLength)
		return false;

	auto ioBuffer = OSDynamicCast(IOBufferMemoryDescriptor, buffer);
	if (!ioBuffer)
		return false;

	auto bytes = ioBuffer->getBytesNoCopy(0, kIdentifyDataLength);
	return bytes && pci->setProperty(property, bytes, kIdentifyDataLength);
}

bool PC711ProbePlugin::wrapIsFullInit(void *controllerPointer) {
	auto &instance = globalPlugin();
	auto controller = reinterpret_cast<IOService *>(controllerPointer);
	IOPCIDevice *pci {nullptr};
	char path[512] {};

	if (!instance.isTarget(controller, pci, path, sizeof(path))) {
		if (instance.originalIsFullInit)
			return instance.originalIsFullInit(controllerPointer);

		// The wrapper should never be reachable without a trampoline. Preserve
		// normal full initialization rather than disabling an unknown device.
		SYSLOG("probe", "original IsFullInit trampoline is unavailable");
		return true;
	}

	if (instance.stageMode == 3 && instance.stage3GuardsReady &&
		instance.originalIsFullInit) {
		instance.captureReadOnlyState(pci, path, 3, false);
		pci->setProperty("PC711ProbeReadOnlyGuardsReady", true);
		return instance.originalIsFullInit(controllerPointer);
	}

	instance.captureReadOnlyState(pci, path, instance.stageMode, true);
	if (instance.stageMode == 3)
		pci->setProperty("PC711ProbeStage3FailClosed", true);
	return false;
}

IOReturn PC711ProbePlugin::wrapIssueIdentifyCommand(void *controllerPointer,
		IOMemoryDescriptor *buffer, uint32_t namespaceId, bool controllerIdentify) {
	auto &instance = globalPlugin();
	auto controller = reinterpret_cast<IOService *>(controllerPointer);
	IOPCIDevice *pci {nullptr};
	char path[512] {};

	if (!instance.isTarget(controller, pci, path, sizeof(path))) {
		return instance.originalIssueIdentifyCommand ?
			instance.originalIssueIdentifyCommand(controllerPointer, buffer,
				namespaceId, controllerIdentify) : kIOReturnUnsupported;
	}

	const bool isControllerIdentify = namespaceId == 0 && controllerIdentify;
	const bool isNamespaceIdentify = namespaceId > 0 && !controllerIdentify;
	if (!isControllerIdentify &&
		!(instance.stageMode == 2 && isNamespaceIdentify)) {
		pci->setProperty("PC711ProbeUnexpectedIdentifyBlocked", true);
		return kIOReturnUnsupported;
	}

	instance.captureReadOnlyState(pci, path, instance.stageMode, false);
	if (isControllerIdentify)
		pci->setProperty("PC711ProbeIdentifyEntered", true);
	else {
		pci->setProperty("PC711ProbeNamespaceIdentifyEntered", true);
		pci->setProperty("PC711ProbeNamespaceID",
			static_cast<unsigned long long>(namespaceId), 32);
	}

	if (!instance.originalIssueIdentifyCommand) {
		pci->setProperty("PC711ProbeIdentifyOriginalMissing", true);
		return kIOReturnUnsupported;
	}

	const auto result = instance.originalIssueIdentifyCommand(
		controllerPointer, buffer, namespaceId, controllerIdentify);
	if (isControllerIdentify) {
		instance.captureAfterIdentifyState(pci);
		pci->setProperty("PC711ProbeIdentifyOriginalResult",
			static_cast<unsigned long long>(static_cast<uint32_t>(result)), 32);
		pci->setProperty("PC711ProbeIdentifyCompleted", result == kIOReturnSuccess);

		if (result != kIOReturnSuccess)
			return result;

		if (instance.stageMode == 2) {
			pci->setProperty("PC711ProbeControllerDataCaptured",
				instance.captureIdentifyData(pci, buffer,
					"PC711ProbeControllerIdentifyData"));
			SYSLOG("probe", "Stage 2 Identify Controller completed for %s; allowing bounded namespace path", path);
			return result;
		}

		pci->setProperty("PC711ProbeIdentifyForcedStop", true);
		SYSLOG("probe", "Stage 1 Identify Controller completed for %s; forcing controlled stop before data processing", path);
		return kIOReturnUnsupported;
	}

	pci->setProperty("PC711ProbeNamespaceOriginalResult",
		static_cast<unsigned long long>(static_cast<uint32_t>(result)), 32);
	pci->setProperty("PC711ProbeNamespaceCompleted", result == kIOReturnSuccess);
	if (result != kIOReturnSuccess)
		return result;

	pci->setProperty("PC711ProbeNamespaceDataCaptured",
		instance.captureIdentifyData(pci, buffer,
			"PC711ProbeNamespaceIdentifyData"));
	pci->setProperty("PC711ProbeNamespaceForcedStop", true);
	SYSLOG("probe", "Stage 2 namespace %u Identify completed for %s; forcing controlled stop before media creation",
		namespaceId, path);
	return kIOReturnUnsupported;
}

bool PC711ProbePlugin::wrapCreateBlockStorageDevice(void *controllerPointer,
		uint32_t namespaceId) {
	auto &instance = globalPlugin();
	auto controller = reinterpret_cast<IOService *>(controllerPointer);
	IOPCIDevice *pci {nullptr};
	char path[512] {};

	if (!instance.isTarget(controller, pci, path, sizeof(path))) {
		return instance.originalCreateBlockStorageDevice ?
			instance.originalCreateBlockStorageDevice(controllerPointer, namespaceId) : false;
	}

	pci->setProperty("PC711ProbeBlockDeviceCreationBlocked", true);
	pci->setProperty("PC711ProbeBlockedNamespace",
		static_cast<unsigned long long>(namespaceId), 32);
	SYSLOG("probe", "blocked unexpected Stage 1 block-device creation for %s namespace %u",
		path, namespaceId);
	return false;
}

IOFilterInterruptEventSource *PC711ProbePlugin::wrapCreateDeviceInterrupt(
		void *controllerPointer, IOInterruptEventAction action,
		IOFilterInterruptAction filter, IOService *provider) {
	auto &instance = globalPlugin();
	auto controller = reinterpret_cast<IOService *>(controllerPointer);
	IOPCIDevice *pci {nullptr};

	if (!instance.isTargetCompat(controller, pci)) {
		return instance.originalCreateDeviceInterrupt ?
			instance.originalCreateDeviceInterrupt(controllerPointer, action,
				filter, provider) : nullptr;
	}

	const auto result = IOPCIDeviceConfigureInterrupts(pci,
		kPC711InterruptTypeMSIX, 1, 1, 0);
	pci->setProperty("PC711CompatMSIXRequested", true);
	pci->setProperty("PC711CompatConfigureInterruptsResult",
		static_cast<unsigned long long>(static_cast<uint32_t>(result)), 32);

	auto eventSource = instance.originalCreateDeviceInterrupt ?
		instance.originalCreateDeviceInterrupt(controllerPointer, action,
			filter, provider) : nullptr;

	// Darwin 25 removed Darwin 24's private MSI-X filter/handler branch and
	// always uses the normal event-source path. Select that path only for this
	// exact controller after Apple has created its event source.
	auto flags = reinterpret_cast<volatile uint8_t *>(controllerPointer) +
		kDarwin24ControllerFlagsOffset;
	const uint8_t flagsBefore = *flags;
	*flags = static_cast<uint8_t>(flagsBefore & ~kDarwin24LegacyMSIXPathFlag);
	const uint8_t flagsAfter = *flags;
	pci->setProperty("PC711CompatLegacyMSIXFlagBefore",
		static_cast<unsigned long long>(flagsBefore), 8);
	pci->setProperty("PC711CompatLegacyMSIXFlagAfter",
		static_cast<unsigned long long>(flagsAfter), 8);
	pci->setProperty("PC711CompatEventSourceCreated", eventSource != nullptr);

	return eventSource;
}

IOReturn PC711ProbePlugin::wrapReportWriteProtection(void *devicePointer,
		bool *isWriteProtected) {
	auto &instance = globalPlugin();
	auto device = reinterpret_cast<IOService *>(devicePointer);
	IOPCIDevice *pci {nullptr};
	char path[512] {};

	if (!instance.isTargetBlockDevice(device, pci, path, sizeof(path))) {
		return instance.originalReportWriteProtection ?
			instance.originalReportWriteProtection(devicePointer, isWriteProtected) :
			kIOReturnUnsupported;
	}

	if (!isWriteProtected)
		return kIOReturnBadArgument;

	*isWriteProtected = true;
	instance.markStage3ReadOnly(device, pci, path);
	pci->setProperty("PC711ProbeReportedWriteProtected", true);
	return kIOReturnSuccess;
}

IOReturn PC711ProbePlugin::wrapDoAsyncReadWrite(void *devicePointer,
		IOMemoryDescriptor *buffer, uint64_t block, uint64_t nblks,
		IOStorageAttributes *attributes, IOStorageCompletion *completion) {
	auto &instance = globalPlugin();
	auto device = reinterpret_cast<IOService *>(devicePointer);
	IOPCIDevice *pci {nullptr};
	char path[512] {};

	if (!instance.isTargetBlockDevice(device, pci, path, sizeof(path))) {
		return instance.originalDoAsyncReadWrite ?
			instance.originalDoAsyncReadWrite(devicePointer, buffer, block, nblks,
				attributes, completion) : kIOReturnUnsupported;
	}

	instance.markStage3ReadOnly(device, pci, path);
	const auto direction = buffer ? buffer->getDirection() : kIODirectionNone;
	if (direction != kIODirectionIn) {
		pci->setProperty("PC711ProbeBlockedWrite", true);
		pci->setProperty("PC711ProbeBlockedWriteBlock",
			static_cast<unsigned long long>(block), 64);
		pci->setProperty("PC711ProbeBlockedWriteBlocks",
			static_cast<unsigned long long>(nblks), 64);
		pci->setProperty("PC711ProbeBlockedWriteDirection",
			static_cast<unsigned long long>(direction), 32);
		return kIOReturnNotWritable;
	}

	if (!pci->getProperty("PC711ProbeReadPathEntered"))
		pci->setProperty("PC711ProbeReadPathEntered", true);
	return instance.originalDoAsyncReadWrite ?
		instance.originalDoAsyncReadWrite(devicePointer, buffer, block, nblks,
			attributes, completion) : kIOReturnUnsupported;
}

IOReturn PC711ProbePlugin::wrapDoFormatMedia(void *devicePointer,
		uint64_t byteCapacity) {
	auto &instance = globalPlugin();
	auto device = reinterpret_cast<IOService *>(devicePointer);
	IOPCIDevice *pci {nullptr};
	char path[512] {};

	if (!instance.isTargetBlockDevice(device, pci, path, sizeof(path))) {
		return instance.originalDoFormatMedia ?
			instance.originalDoFormatMedia(devicePointer, byteCapacity) :
			kIOReturnUnsupported;
	}

	instance.markStage3ReadOnly(device, pci, path);
	pci->setProperty("PC711ProbeBlockedFormat", true);
	return kIOReturnNotWritable;
}

IOReturn PC711ProbePlugin::wrapDoUnmap(void *devicePointer,
		IOBlockStorageDeviceExtent *extents, uint32_t extentsCount,
		IOStorageUnmapOptions options) {
	auto &instance = globalPlugin();
	auto device = reinterpret_cast<IOService *>(devicePointer);
	IOPCIDevice *pci {nullptr};
	char path[512] {};

	if (!instance.isTargetBlockDevice(device, pci, path, sizeof(path))) {
		return instance.originalDoUnmap ?
			instance.originalDoUnmap(devicePointer, extents, extentsCount, options) :
			kIOReturnUnsupported;
	}

	instance.markStage3ReadOnly(device, pci, path);
	pci->setProperty("PC711ProbeBlockedUnmap", true);
	return kIOReturnNotWritable;
}

void PC711ProbePlugin::processKext(void *context, KernelPatcher &patcher,
		size_t index, mach_vm_address_t, size_t) {
	auto instance = static_cast<PC711ProbePlugin *>(context);
	if (!instance || index != instance->kextInfo.loadIndex)
		return;

	if (instance->stageMode == 4) {
		KernelPatcher::RouteRequest request {
			kCreateDeviceInterruptSymbol, wrapCreateDeviceInterrupt,
			instance->originalCreateDeviceInterrupt
		};
		if (!patcher.routeMultiple(index, &request, 1)) {
			SYSLOG("probe", "failed to install Darwin 24 interrupt compatibility route");
			return;
		}

		SYSLOG("probe", "Darwin 24 interrupt compatibility route installed");
		return;
	}

	if (instance->stageMode == 1) {
		KernelPatcher::RouteRequest requests[] {
			{kIssueIdentifyCommandSymbol, wrapIssueIdentifyCommand,
				instance->originalIssueIdentifyCommand},
			{kCreateBlockStorageDeviceSymbol, wrapCreateBlockStorageDevice,
				instance->originalCreateBlockStorageDevice}
		};

		if (!patcher.routeMultiple(index, requests, arrsize(requests))) {
			SYSLOG("probe", "failed to install Stage 1 routes; plugin is inert");
			return;
		}

		SYSLOG("probe", "Stage 1 Identify-only routes installed for 1C5C:174A under GPP3");
		return;
	}

	if (instance->stageMode == 2) {
		KernelPatcher::RouteRequest request {
			kIssueIdentifyCommandSymbol, wrapIssueIdentifyCommand,
			instance->originalIssueIdentifyCommand
		};
		if (!patcher.routeMultiple(index, &request, 1)) {
			SYSLOG("probe", "failed to install Stage 2 route; plugin is inert");
			return;
		}

		SYSLOG("probe", "Stage 2 controller-plus-namespace Identify route installed for 1C5C:174A under GPP3");
		return;
	}

	if (instance->stageMode == 3) {
		// Install the proven IsFullInit barrier first. If any read-only route
		// fails, stage3GuardsReady remains false and this barrier keeps the
		// target from completing initialization.
		KernelPatcher::RouteRequest gate {
			kIsFullInitSymbol, wrapIsFullInit, instance->originalIsFullInit
		};
		if (!patcher.routeMultiple(index, &gate, 1)) {
			SYSLOG("probe", "failed to install Stage 3 fail-closed gate; plugin is inert");
			return;
		}

		KernelPatcher::RouteRequest guards[] {
			{kReportWriteProtectionSymbol, wrapReportWriteProtection,
				instance->originalReportWriteProtection},
			{kDoAsyncReadWriteSymbol, wrapDoAsyncReadWrite,
				instance->originalDoAsyncReadWrite},
			{kDoFormatMediaSymbol, wrapDoFormatMedia,
				instance->originalDoFormatMedia},
			{kDoUnmapSymbol, wrapDoUnmap, instance->originalDoUnmap}
		};
		if (!patcher.routeMultiple(index, guards, arrsize(guards))) {
			SYSLOG("probe", "Stage 3 write guard routing failed; target remains blocked by IsFullInit");
			return;
		}

		instance->stage3GuardsReady = true;
		SYSLOG("probe", "Stage 3 read-only media routes ready for 1C5C:174A under GPP3");
		return;
	}

	KernelPatcher::RouteRequest request {
		kIsFullInitSymbol, wrapIsFullInit, instance->originalIsFullInit
	};

	if (!patcher.routeMultiple(index, &request, 1)) {
		SYSLOG("probe", "failed to route IONVMeController::IsFullInit; plugin is inert");
		return;
	}

	SYSLOG("probe", "Stage 0 route installed; only 1C5C:174A under GPP3 will be intercepted");
}

void PC711ProbePlugin::init() {
	const auto error = lilu.onKextLoad(&kextInfo, 1, processKext, this);
	if (error != LiluAPI::Error::NoError)
		SYSLOG("probe", "failed to register IONVMeFamily load callback: %d", error);
}

const char *bootargOff[] {"-pc711poff"};
const char *bootargDebug[] {"-pc711pdbg"};

} // namespace

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
	parseModuleVersion(xStringify(MODULE_VERSION)),
	LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery,
	bootargOff,
	arrsize(bootargOff),
	bootargDebug,
	arrsize(bootargDebug),
	nullptr,
	0,
	KernelVersion::Sequoia,
	KernelVersion::Tahoe,
	[]() {
		const bool stage0 = checkKernelArgument("-pc711pstage0");
		const bool stage1 = checkKernelArgument("-pc711pstage1");
		const bool stage2 = checkKernelArgument("-pc711pstage2");
		const bool stage3 = checkKernelArgument("-pc711pstage3");
		const bool compat = checkKernelArgument("-pc711pcompat");
		if (static_cast<unsigned>(stage0) + static_cast<unsigned>(stage1) +
			static_cast<unsigned>(stage2) + static_cast<unsigned>(stage3) +
			static_cast<unsigned>(compat) != 1) {
			SYSLOG("probe", "loaded inert; select exactly one PC711 stage boot argument");
			return;
		}
		const bool supportedCompat = compat &&
			getKernelVersion() == KernelVersion::Sequoia &&
			getKernelMinorVersion() == 6 && version_revision == 0;
		const bool supportedDiagnostic = !compat &&
			getKernelVersion() == KernelVersion::Tahoe &&
			getKernelMinorVersion() == 5 && version_revision == 0;
		if (!supportedCompat && !supportedDiagnostic) {
			SYSLOG("probe", "unsupported kernel %d.%d.%d; probe remains inert",
				getKernelVersion(), getKernelMinorVersion(), version_revision);
			return;
		}
		PC711ProbePlugin::globalPlugin().setStage(compat ? 4U : (stage3 ? 3U :
			(stage2 ? 2U : (stage1 ? 1U : 0U))));
		PC711ProbePlugin::globalPlugin().init();
	}
};

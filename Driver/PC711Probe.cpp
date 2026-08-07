// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include <IOKit/IOService.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/pci/IOPCIFamilyDefinitions.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSSymbol.h>

#include <Headers/kern_api.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>

namespace {

constexpr uint16_t kPC711Vendor {0x1C5C};
constexpr uint16_t kPC711Device {0x174A};
constexpr uint32_t kNvmeClassRevisionMask {0xFFFFFF00U};
constexpr uint32_t kNvmeClassRevisionValue {0x01080200U};
constexpr uint32_t kInterruptTypeMSIX {0x00020000U};
constexpr size_t kConfigureInterruptsVtableSlot {0x960 / sizeof(uintptr_t)};
constexpr size_t kControllerFlagsOffset {0x191};
constexpr uint8_t kLegacyMSIXPathFlag {0x10};
constexpr uint8_t kBigSurMSIXMode {0x01};

constexpr const char *kAllocateDeviceInterruptsSymbol {
	"__ZN32IOPCIMessagedInterruptController24allocateDeviceInterruptsEP9IOServicejjPyPj"
};
constexpr const char *kDeallocateDeviceInterruptsSymbol {
	"__ZN32IOPCIMessagedInterruptController26deallocateDeviceInterruptsEP9IOService"
};

using ConfigureInterrupts = IOReturn (*)(IOPCIDevice *device,
	uint32_t interruptType, uint32_t numRequired, uint32_t numRequested,
	IOOptionBits options);
using AllocateDeviceInterrupts = IOReturn (*)(void *controller,
	IOService *device, uint32_t numVectors, uint32_t msiCapability,
	uint64_t *msiAddress, uint32_t *msiData);
using DeallocateDeviceInterrupts = IOReturn (*)(void *controller,
	IOService *device);

AllocateDeviceInterrupts bigSurAllocateDeviceInterrupts {nullptr};
DeallocateDeviceInterrupts bigSurDeallocateDeviceInterrupts {nullptr};

// Big Sur exports the five-argument allocator. Monterey and newer changed
// its C++ signature, so weak linkage keeps one binary loadable across 11-15.
extern "C" IOReturn directBigSurAllocateDeviceInterrupts(void *controller,
	IOService *device, uint32_t numVectors, uint32_t msiCapability,
	uint64_t *msiAddress, uint32_t *msiData)
	__asm("__ZN32IOPCIMessagedInterruptController24allocateDeviceInterruptsEP9IOServicejjPyPj")
	__attribute__((weak_import));
extern "C" IOReturn directBigSurDeallocateDeviceInterrupts(void *controller,
	IOService *device)
	__asm("__ZN32IOPCIMessagedInterruptController26deallocateDeviceInterruptsEP9IOService")
	__attribute__((weak_import));

struct BigSurPCIMSIState {
	uint8_t reserved0[28];
	uint16_t msiCapability;
	uint16_t msiControl;
	uint16_t msiPhysVectorCount;
	uint16_t msiVectorCount;
	uint8_t msiMode;
	uint8_t msiEnable;
	uint8_t reserved1[2];
	uint64_t msiTable;
	uint64_t msiPBA;
	void *msiVectors;
};

static_assert(offsetof(BigSurPCIMSIState, msiCapability) == 28,
	"unexpected Big Sur PCI MSI state layout");
static_assert(offsetof(BigSurPCIMSIState, msiTable) == 40,
	"unexpected Big Sur PCI MSI table layout");

class PC711PCIDeviceAccess : public IOPCIDevice {
public:
	void *compatReserved() { return reserved; }
};

bool isPC711Device(IOPCIDevice *pci) {
	if (!pci)
		return false;

	const auto vendor = pci->configRead16(kIOPCIConfigVendorID);
	const auto device = pci->configRead16(kIOPCIConfigDeviceID);
	const auto classRevision = pci->configRead32(kIOPCIConfigRevisionID);
	return vendor == kPC711Vendor && device == kPC711Device &&
		(classRevision & kNvmeClassRevisionMask) == kNvmeClassRevisionValue;
}

IOReturn configurePC711MSIX(IOPCIDevice *pci) {
	if (!pci)
		return kIOReturnBadArgument;

	auto vtable = *reinterpret_cast<uintptr_t **>(pci);
	if (!vtable)
		return kIOReturnUnsupported;

	auto configure = reinterpret_cast<ConfigureInterrupts>(
		vtable[kConfigureInterruptsVtableSlot]);
	return configure ? configure(pci, kInterruptTypeMSIX, 1, 1, 0) :
		kIOReturnUnsupported;
}

IOReturn reallocateBigSurPC711MSIX(IOPCIDevice *pci) {
	if (pci && pci->getProperty("PC711CompatBigSurMSIXReallocated"))
		return kIOReturnSuccess;

	auto allocate = bigSurAllocateDeviceInterrupts ?
		bigSurAllocateDeviceInterrupts : directBigSurAllocateDeviceInterrupts;
	auto deallocate = bigSurDeallocateDeviceInterrupts ?
		bigSurDeallocateDeviceInterrupts : directBigSurDeallocateDeviceInterrupts;
	if (!pci || !allocate || !deallocate)
		return kIOReturnNotReady;

	IOByteCount msixCapability {0};
	pci->extendedFindPCICapability(kIOPCIMSIXCapability, &msixCapability);
	if (!msixCapability)
		return kIOReturnUnsupported;

	auto controllers = OSDynamicCast(OSArray,
		pci->getProperty(gIOInterruptControllersKey));
	auto specifiers = OSDynamicCast(OSArray,
		pci->getProperty(gIOInterruptSpecifiersKey));
	if (!controllers || !specifiers ||
		controllers->getCount() != specifiers->getCount())
		return kIOReturnNotReady;

	void *messagedController {nullptr};
	const OSSymbol *messagedName {nullptr};
	for (unsigned int index = 0; index < controllers->getCount(); index++) {
		auto name = OSDynamicCast(OSSymbol, controllers->getObject(index));
		if (!name)
			continue;
		auto controller = IOService::getPlatform()->lookUpInterruptController(name);
		if (controller && controller->metaCast("IOPCIMessagedInterruptController")) {
			messagedController = controller;
			messagedName = name;
			break;
		}
	}
	if (!messagedController || !messagedName)
		return kIOReturnNotReady;

	auto state = reinterpret_cast<BigSurPCIMSIState *>(
		reinterpret_cast<PC711PCIDeviceAccess *>(pci)->compatReserved());
	if (!state || !state->msiCapability)
		return kIOReturnNotReady;

	const uint16_t oldCapability = state->msiCapability;
	const uint8_t oldMode = state->msiMode;
	const auto deallocateResult = deallocate(messagedController, pci);
	if (deallocateResult != kIOReturnSuccess)
		return deallocateResult;

	auto retainedControllers = OSArray::withCapacity(controllers->getCount());
	auto retainedSpecifiers = OSArray::withCapacity(specifiers->getCount());
	if (!retainedControllers || !retainedSpecifiers) {
		OSSafeReleaseNULL(retainedControllers);
		OSSafeReleaseNULL(retainedSpecifiers);
		return kIOReturnNoMemory;
	}

	for (unsigned int index = 0; index < controllers->getCount(); index++) {
		auto controllerName = controllers->getObject(index);
		auto specifier = specifiers->getObject(index);
		if (!controllerName || !specifier || controllerName->isEqualTo(messagedName))
			continue;
		retainedControllers->setObject(controllerName);
		retainedSpecifiers->setObject(specifier);
	}
	pci->setProperty(gIOInterruptControllersKey, retainedControllers);
	pci->setProperty(gIOInterruptSpecifiersKey, retainedSpecifiers);
	retainedControllers->release();
	retainedSpecifiers->release();

	state->msiCapability = static_cast<uint16_t>(msixCapability);
	state->msiControl = 0;
	state->msiPhysVectorCount = 0;
	state->msiVectorCount = 0;
	state->msiMode = kBigSurMSIXMode;
	state->msiEnable = 0;
	state->msiTable = 0;
	state->msiPBA = 0;
	state->msiVectors = nullptr;

	const auto allocateResult = allocate(
		messagedController, pci, 0, static_cast<uint32_t>(msixCapability),
		nullptr, nullptr);
	pci->setProperty("PC711CompatBigSurMSIXReallocationResult",
		static_cast<unsigned long long>(static_cast<uint32_t>(allocateResult)), 32);
	if (allocateResult == kIOReturnSuccess) {
		pci->setProperty("PC711CompatBigSurMSIXReallocated", true);
		return allocateResult;
	}

	// Restore the original MSI allocation if MSI-X allocation failed.
	state->msiCapability = oldCapability;
	state->msiControl = 0;
	state->msiPhysVectorCount = 0;
	state->msiVectorCount = 0;
	state->msiMode = oldMode;
	state->msiEnable = 0;
	state->msiTable = 0;
	state->msiPBA = 0;
	state->msiVectors = nullptr;
	allocate(messagedController, pci, 0, oldCapability,
		nullptr, nullptr);
	return allocateResult;
}

} // namespace

// Recovery and installer environments may issue polled NVMe commands before
// IONVMeFamily creates its ordinary event source. Request MSI-X while the
// PC711 PCI nub is still being probed, then decline attachment so Apple's
// driver takes over on every supported macOS release.
class PC711EarlyMSIX : public IOService {
	OSDeclareDefaultStructors(PC711EarlyMSIX)

public:
	IOService *probe(IOService *provider, SInt32 *score) override;
};

OSDefineMetaClassAndStructors(PC711EarlyMSIX, IOService)

IOService *PC711EarlyMSIX::probe(IOService *provider, SInt32 *) {
	auto pci = OSDynamicCast(IOPCIDevice, provider);
	if (!isPC711Device(pci))
		return nullptr;

	const auto result = getKernelVersion() == KernelVersion::BigSur ?
		reallocateBigSurPC711MSIX(pci) : configurePC711MSIX(pci);
	pci->setProperty("PC711CompatEarlyMSIXRequested", true);
	pci->setProperty("PC711CompatEarlyConfigureInterruptsResult",
		static_cast<unsigned long long>(static_cast<uint32_t>(result)), 32);
	SYSLOG("probe", "early PC711 MSI-X request completed: %x",
		static_cast<uint32_t>(result));
	return nullptr;
}

namespace {

// CreateDeviceInterrupt is exported on Darwin 23-24 but stripped from the
// Darwin 20-22 symbol table. This stable function prologue identifies the
// older implementation without tying the route to a fixed load address.
constexpr uint8_t kLegacyCreateDeviceInterruptPattern[] {
	0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56,
	0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xEC,
	0x18, 0x49, 0x89, 0xCD, 0x49, 0x89, 0xD6, 0x49,
	0x89, 0xF7, 0x49, 0x89, 0xFC, 0x48, 0x8D, 0x55,
	0xD4, 0xC7, 0x02, 0x00, 0x00, 0x00, 0x00, 0x48,
	0x8B, 0x01
};

constexpr const char *kCreateDeviceInterruptSymbol {
	"__ZN16IONVMeController21CreateDeviceInterruptEPFvP8OSObjectP22IOInterruptEventSourceiEPFbS1_P28IOFilterInterruptEventSourceEP9IOService"
};

class PC711ProbePlugin {
public:
	void init();
	static PC711ProbePlugin &globalPlugin();

private:
	using CreateDeviceInterrupt = IOFilterInterruptEventSource *(*)(void *controller,
		IOInterruptEventAction action, IOFilterInterruptAction filter,
		IOService *provider);
	static void processKext(void *context, KernelPatcher &patcher, size_t index,
		mach_vm_address_t address, size_t size);
	static void processPCIKext(void *context, KernelPatcher &patcher, size_t index,
		mach_vm_address_t address, size_t size);
	static IOFilterInterruptEventSource *wrapCreateDeviceInterrupt(void *controller,
		IOInterruptEventAction action, IOFilterInterruptAction filter,
		IOService *provider);

	bool isPC711(IOService *controller, IOPCIDevice *&pci) const;
	CreateDeviceInterrupt originalCreateDeviceInterrupt {nullptr};

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

	const char *pciKextPath {
		"/System/Library/Extensions/IOPCIFamily.kext/Contents/MacOS/IOPCIFamily"
	};

	KernelPatcher::KextInfo pciKextInfo {
		"com.apple.iokit.IOPCIFamily",
		&pciKextPath,
		1,
		{true},
		{},
		KernelPatcher::KextInfo::Unloaded
	};
};

PC711ProbePlugin plugin;

PC711ProbePlugin &PC711ProbePlugin::globalPlugin() {
	return plugin;
}

bool PC711ProbePlugin::isPC711(IOService *controller, IOPCIDevice *&pci) const {
	pci = nullptr;
	if (!controller)
		return false;

	pci = OSDynamicCast(IOPCIDevice, controller->getProvider());
	if (!pci)
		return false;

	return isPC711Device(pci);
}

IOFilterInterruptEventSource *PC711ProbePlugin::wrapCreateDeviceInterrupt(
		void *controllerPointer, IOInterruptEventAction action,
		IOFilterInterruptAction filter, IOService *provider) {
	auto &instance = globalPlugin();
	auto controller = reinterpret_cast<IOService *>(controllerPointer);
	IOPCIDevice *pci {nullptr};

	if (!instance.isPC711(controller, pci)) {
		return instance.originalCreateDeviceInterrupt ?
			instance.originalCreateDeviceInterrupt(controllerPointer, action,
				filter, provider) : nullptr;
	}

	// On Big Sur the IOPCIFamily callback may not have resolved the private MSI
	// allocator when the high-score PCI personality probes. Retry here, after
	// IOPCIFamily is ready but before IONVMeFamily creates its event source.
	const auto result = getKernelVersion() == KernelVersion::BigSur ?
		reallocateBigSurPC711MSIX(pci) : configurePC711MSIX(pci);
	pci->setProperty("PC711CompatMSIXRequested", true);
	pci->setProperty("PC711CompatConfigureInterruptsResult",
		static_cast<unsigned long long>(static_cast<uint32_t>(result)), 32);

	auto eventSource = instance.originalCreateDeviceInterrupt ?
		instance.originalCreateDeviceInterrupt(controllerPointer, action,
			filter, provider) : nullptr;

	uint8_t flagsBefore {0};
	uint8_t flagsAfter {0};
	if (getKernelVersion() >= KernelVersion::Sonoma) {
		auto flags = reinterpret_cast<volatile uint8_t *>(controllerPointer) +
			kControllerFlagsOffset;
		flagsBefore = *flags;
		*flags = static_cast<uint8_t>(flagsBefore & ~kLegacyMSIXPathFlag);
		flagsAfter = *flags;
		pci->setProperty("PC711CompatLegacyMSIXFlagBefore",
			static_cast<unsigned long long>(flagsBefore), 8);
		pci->setProperty("PC711CompatLegacyMSIXFlagAfter",
			static_cast<unsigned long long>(flagsAfter), 8);
	}
	pci->setProperty("PC711CompatEventSourceCreated", eventSource != nullptr);

	SYSLOG("probe", "PC711 interrupt compatibility applied; configure=%x flags=%02x->%02x event=%d",
		static_cast<uint32_t>(result), flagsBefore, flagsAfter,
		eventSource != nullptr);
	return eventSource;
}

void PC711ProbePlugin::processPCIKext(void *context, KernelPatcher &patcher,
		size_t index, mach_vm_address_t, size_t) {
	auto instance = static_cast<PC711ProbePlugin *>(context);
	if (!instance || index != instance->pciKextInfo.loadIndex ||
		getKernelVersion() != KernelVersion::BigSur)
		return;

	bigSurAllocateDeviceInterrupts = reinterpret_cast<AllocateDeviceInterrupts>(
		patcher.solveSymbol(index, kAllocateDeviceInterruptsSymbol));
	bigSurDeallocateDeviceInterrupts = reinterpret_cast<DeallocateDeviceInterrupts>(
		patcher.solveSymbol(index, kDeallocateDeviceInterruptsSymbol));
	if (!bigSurAllocateDeviceInterrupts || !bigSurDeallocateDeviceInterrupts) {
		SYSLOG("probe", "failed to resolve Big Sur MSI reallocation functions");
		return;
	}

	SYSLOG("probe", "Big Sur PC711 MSI-X reallocation functions ready");
}

void PC711ProbePlugin::processKext(void *context, KernelPatcher &patcher,
		size_t index, mach_vm_address_t address, size_t size) {
	auto instance = static_cast<PC711ProbePlugin *>(context);
	if (!instance || index != instance->kextInfo.loadIndex)
		return;

	KernelPatcher::RouteRequest request {
		nullptr, wrapCreateDeviceInterrupt,
		instance->originalCreateDeviceInterrupt
	};

	if (getKernelVersion() >= KernelVersion::Sonoma) {
		request.symbol = kCreateDeviceInterruptSymbol;
	} else {
		size_t offset {0};
		if (!KernelPatcher::findPattern(kLegacyCreateDeviceInterruptPattern,
				nullptr, sizeof(kLegacyCreateDeviceInterruptPattern),
				reinterpret_cast<const void *>(address), size, &offset)) {
			SYSLOG("probe", "failed to locate legacy CreateDeviceInterrupt");
			return;
		}
		request.from = address + offset;
	}

	if (!patcher.routeMultiple(index, &request, 1)) {
		SYSLOG("probe", "failed to install PC711 interrupt compatibility route");
		return;
	}

	SYSLOG("probe", "automatic PC711 interrupt compatibility route installed");
}

void PC711ProbePlugin::init() {
	const auto error = lilu.onKextLoad(&kextInfo, 1, processKext, this);
	if (error != LiluAPI::Error::NoError)
		SYSLOG("probe", "failed to register IONVMeFamily load callback: %d", error);

	const auto pciError = lilu.onKextLoad(&pciKextInfo, 1, processPCIKext, this);
	if (pciError != LiluAPI::Error::NoError)
		SYSLOG("probe", "failed to register IOPCIFamily load callback: %d", pciError);
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
	KernelVersion::BigSur,
	KernelVersion::Sequoia,
	[]() {
		PC711ProbePlugin::globalPlugin().init();
	}
};

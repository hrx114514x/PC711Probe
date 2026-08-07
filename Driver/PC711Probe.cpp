// SPDX-License-Identifier: BSD-3-Clause

#include <IOKit/IOService.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/pci/IOPCIFamilyDefinitions.h>

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
constexpr size_t kControllerFlagsOffset {0x191};
constexpr uint8_t kLegacyMSIXPathFlag {0x10};

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
};

PC711ProbePlugin plugin;

// Darwin 25 calls this before IONVMeFamily enumerates interrupt sources.
// Reuse the exported IOPCIFamily implementation on older kernels.
extern "C" IOReturn IOPCIDeviceConfigureInterrupts(IOPCIDevice *device,
	uint32_t interruptType, uint32_t numRequired, uint32_t numRequested,
	IOOptionBits options)
	__asm("__ZN11IOPCIDevice19configureInterruptsEjjjj");

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

	const auto vendor = pci->configRead16(kIOPCIConfigVendorID);
	const auto device = pci->configRead16(kIOPCIConfigDeviceID);
	const auto classRevision = pci->configRead32(kIOPCIConfigRevisionID);
	return vendor == kPC711Vendor && device == kPC711Device &&
		(classRevision & kNvmeClassRevisionMask) == kNvmeClassRevisionValue;
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

	const auto result = IOPCIDeviceConfigureInterrupts(pci,
		kInterruptTypeMSIX, 1, 1, 0);
	pci->setProperty("PC711CompatMSIXRequested", true);
	pci->setProperty("PC711CompatConfigureInterruptsResult",
		static_cast<unsigned long long>(static_cast<uint32_t>(result)), 32);

	auto eventSource = instance.originalCreateDeviceInterrupt ?
		instance.originalCreateDeviceInterrupt(controllerPointer, action,
			filter, provider) : nullptr;

	auto flags = reinterpret_cast<volatile uint8_t *>(controllerPointer) +
		kControllerFlagsOffset;
	const uint8_t flagsBefore = *flags;
	*flags = static_cast<uint8_t>(flagsBefore & ~kLegacyMSIXPathFlag);
	const uint8_t flagsAfter = *flags;
	pci->setProperty("PC711CompatLegacyMSIXFlagBefore",
		static_cast<unsigned long long>(flagsBefore), 8);
	pci->setProperty("PC711CompatLegacyMSIXFlagAfter",
		static_cast<unsigned long long>(flagsAfter), 8);
	pci->setProperty("PC711CompatEventSourceCreated", eventSource != nullptr);

	SYSLOG("probe", "PC711 interrupt compatibility applied; configure=%x flags=%02x->%02x event=%d",
		static_cast<uint32_t>(result), flagsBefore, flagsAfter,
		eventSource != nullptr);
	return eventSource;
}

void PC711ProbePlugin::processKext(void *context, KernelPatcher &patcher,
		size_t index, mach_vm_address_t, size_t) {
	auto instance = static_cast<PC711ProbePlugin *>(context);
	if (!instance || index != instance->kextInfo.loadIndex)
		return;

	KernelPatcher::RouteRequest request {
		kCreateDeviceInterruptSymbol, wrapCreateDeviceInterrupt,
		instance->originalCreateDeviceInterrupt
	};
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
	KernelVersion::Tiger,
	KernelVersion::Sequoia,
	[]() {
		PC711ProbePlugin::globalPlugin().init();
	}
};

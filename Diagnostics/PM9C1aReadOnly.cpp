// SPDX-License-Identifier: MIT
// Review experiment. Hardware read-only; Lilu routes and logging change kernel state.
// No BAR mapping, private controller offsets, PCI writes, interrupt allocation,
// queue commands, or replacement success results. Not a storage compatibility fix.
#include <IOKit/IOService.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/pci/IOPCIFamilyDefinitions.h>
#include <libkern/version.h>
#include <Headers/kern_api.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>
#include "ReadOnlyPolicy.hpp"

namespace {
using BoolMethod = bool (*)(IOService *);
using InterruptMethod = IOFilterInterruptEventSource *(*)(IOService *,
    IOInterruptEventAction, IOFilterInterruptAction, IOService *);
BoolMethod originalPCI {nullptr}, originalMappers {nullptr};
InterruptMethod originalInterrupt {nullptr};
uint64_t sequence {0};
const char *path = "/System/Library/Extensions/IONVMeFamily.kext/Contents/MacOS/IONVMeFamily";
KernelPatcher::KextInfo info {"com.apple.iokit.IONVMeFamily", &path, 1,
    {true}, {}, KernelPatcher::KextInfo::Unloaded};

bool permitted() {
    return ReadOnlyPolicy::allowed(version_major, version_minor, version_revision,
        ADDPR(startSuccess), checkKernelArgument("-pm9c1adiag"),
        checkKernelArgument("-pm9c1adiagoff"));
}

IOPCIDevice *target(IOService *controller) {
    if (!permitted() || !controller) return nullptr;
    auto pci = OSDynamicCast(IOPCIDevice, controller->getProvider());
    if (!pci || !ReadOnlyPolicy::matches(pci->configRead16(kIOPCIConfigVendorID),
        pci->configRead16(kIOPCIConfigDeviceID), pci->configRead32(kIOPCIConfigRevisionID)))
        return nullptr;
    return pci;
}

// The entry sequence is the call ID; the exit has its own sequence and echoes
// that call ID. Registry entry IDs identify objects, not kernel addresses.
uint64_t event(IOService *controller, IOPCIDevice *pci, const char *method,
               uint64_t call, int result) {
    auto seq = __sync_add_and_fetch(&sequence, 1);
    SYSLOG("diag", "seq=%llu call=%llu cid=%llu pid=%llu method=%s phase=%s result=%d",
        static_cast<unsigned long long>(seq),
        static_cast<unsigned long long>(call ? call : seq),
        static_cast<unsigned long long>(controller->getRegistryEntryID()),
        static_cast<unsigned long long>(pci->getRegistryEntryID()), method,
        call ? "exit" : "entry", result);
    return seq;
}

void snapshot(IOPCIDevice *pci, uint64_t call, const char *phase) {
    struct Reader {
        IOPCIDevice *pci;
        unsigned read8(unsigned offset) { return pci->configRead8(offset); }
    } reader {pci};
    const bool caps = (pci->configRead16(0x06) & 0x10) != 0;
    auto msi = caps ? ReadOnlyPolicy::findCapability(reader, 0x05) : 0;
    auto msix = caps ? ReadOnlyPolicy::findCapability(reader, 0x11) : 0;
    // Conventional capability offsets must be aligned and inside 0x40..0xFC.
    const bool msiValid = msi >= 0x40 && msi <= 0xFC && !(msi & 3);
    const bool msixValid = msix >= 0x40 && msix <= 0xF4 && !(msix & 3);
    SYSLOG("diag", "call=%llu snapshot=%s command=%04x bar0raw=%08x bar1raw=%08x msiOff=%x msiCtl=%04x msixOff=%x msixCtl=%04x",
        static_cast<unsigned long long>(call), phase, pci->configRead16(0x04),
        pci->configRead32(0x10), pci->configRead32(0x14),
        static_cast<unsigned>(msi), msiValid ? pci->configRead16(msi + 2) : 0,
        static_cast<unsigned>(msix), msixValid ? pci->configRead16(msix + 2) : 0);
    if (msixValid)
        SYSLOG("diag", "call=%llu snapshot=%s msixTable=%08x msixPBA=%08x",
            static_cast<unsigned long long>(call), phase,
            pci->configRead32(msix + 4), pci->configRead32(msix + 8));
}

bool wrapPCI(IOService *controller) {
    auto pci = target(controller);
    if (!pci) return originalPCI(controller);
    pci->retain();
    auto call = event(controller, pci, "InitializePCI", 0, -1);
    snapshot(pci, call, "before");
    const bool result = originalPCI(controller);
    event(controller, pci, "InitializePCI", call, result);
    snapshot(pci, call, "after");
    pci->release();
    return result;
}

bool wrapMappers(IOService *controller) {
    auto pci = target(controller);
    if (!pci) return originalMappers(controller);
    pci->retain();
    auto call = event(controller, pci, "GetIOMappers", 0, -1);
    const bool result = originalMappers(controller);
    event(controller, pci, "GetIOMappers", call, result);
    pci->release();
    return result;
}

IOFilterInterruptEventSource *wrapInterrupt(IOService *controller,
    IOInterruptEventAction action, IOFilterInterruptAction filter, IOService *provider) {
    auto pci = target(controller);
    if (!pci) return originalInterrupt(controller, action, filter, provider);
    pci->retain();
    auto call = event(controller, pci, "CreateDeviceInterrupt", 0, -1);
    auto result = originalInterrupt(controller, action, filter, provider);
    event(controller, pci, "CreateDeviceInterrupt", call, result != nullptr);
    pci->release();
    return result;
}

void process(void *, KernelPatcher &patcher, size_t index, mach_vm_address_t, size_t) {
    if (!permitted() || index != info.loadIndex) return;
    KernelPatcher::RouteRequest routes[] {
        {"__ZN16IONVMeController13InitializePCIEv", wrapPCI, originalPCI},
        {"__ZN16IONVMeController12GetIOMappersEv", wrapMappers, originalMappers},
        {"__ZN16IONVMeController21CreateDeviceInterruptEPFvP8OSObjectP22IOInterruptEventSourceiEPFbS1_P28IOFilterInterruptEventSourceEP9IOService",
         wrapInterrupt, originalInterrupt}
    };
    // No guessed pattern or vtable offset when a symbol is absent. Routes may
    // partially succeed: retain successful routes, log every installation result.
    for (auto &route : routes) {
        patcher.clearError();
        const bool ok = patcher.routeMultiple(index, &route, 1);
        SYSLOG("diag", "route=%s installed=%d", route.symbol, ok);
        patcher.clearError();
    }
}

void init() {
    if (!permitted()) return;
    auto error = lilu.onKextLoad(&info, 1, process, nullptr);
    SYSLOG("diag", "callback registration=%d; hardware read-only experiment", error);
}
const char *off[] {"-pm9c1adiagoff"};
}

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME), parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery,
    off, arrsize(off), nullptr, 0, nullptr, 0,
    KernelVersion::Sequoia, KernelVersion::Sequoia, init
};

// SPDX-License-Identifier: MIT
// Review experiment. Hardware read-only; Lilu routes and logging change kernel state.
// No BAR mapping, private controller offsets, PCI writes, interrupt allocation,
// queue commands, or replacement success results. Not a storage compatibility fix.
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/pci/IOPCIFamilyDefinitions.h>
#include <libkern/version.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <kern/thread.h>
#include <Headers/kern_api.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>
#include "ReadOnlyPolicy.hpp"
#include "BranchTracker.hpp"
#include "ExactFingerprints.hpp"

namespace {
using BoolMethod = bool (*)(IOService *);
using InterruptMethod = IOFilterInterruptEventSource *(*)(IOService *,
    IOInterruptEventAction, IOFilterInterruptAction, IOService *);
BoolMethod originalPCI {nullptr}, originalMappers {nullptr};
InterruptMethod originalInterrupt {nullptr};
using ErrorMethod = void (*)();
using MapperErrorMethod = void (*)(IOService *);
ErrorMethod originalProviderError {nullptr}, originalMapError {nullptr}, originalAddressError {nullptr};
MapperErrorMethod originalMapperError {nullptr};
IOSimpleLock *observationLock {nullptr};
BranchObservation::Tracker<32> observations;
unsigned installedRoutes {0};
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

void report(IOService *controller, IOPCIDevice *pci, const char *key,
            uint64_t call, uint64_t exit, bool result, unsigned branches, bool tracked) {
    auto dict = OSDictionary::withCapacity(10);
    if (!dict) return;
    auto put = [dict](const char *name, uint64_t value) {
        auto number = OSNumber::withNumber(value, 64);
        if (!number) return false;
        const bool ok = dict->setObject(name, number);
        number->release();
        return ok;
    };
    bool complete = put("Call", call) && put("ExitSequence", exit) &&
        put("ControllerID", controller->getRegistryEntryID()) &&
        put("ProviderID", pci->getRegistryEntryID()) && put("Result", result) &&
        put("ObservedExitMask", branches) && put("Tracked", tracked) &&
        put("InstalledRoutes", __atomic_load_n(&installedRoutes, __ATOMIC_ACQUIRE));
    auto name = OSString::withCString(controller->getMetaClass()->getClassName());
    complete = name && dict->setObject("ControllerClass", name) && complete;
    if (complete) pci->setProperty(key, dict);
    OSSafeReleaseNULL(name);
    dict->release();
}

void recordExit(unsigned branch, IOService *controller = nullptr) {
    const auto thread = reinterpret_cast<uintptr_t>(current_thread());
    const auto state = IOSimpleLockLockDisableInterrupt(observationLock);
    auto frame = observations.mark(thread, reinterpret_cast<uintptr_t>(controller), branch);
    uint64_t call = frame ? frame->call : 0;
    uint64_t cid = frame ? frame->cid : 0;
    uint64_t pid = frame ? frame->pid : 0;
    IOSimpleLockUnlockEnableInterrupt(observationLock, state);
    if (call) {
        auto seq = __sync_add_and_fetch(&sequence, 1);
        SYSLOG("diag", "seq=%llu call=%llu cid=%llu pid=%llu observedExitMask=%u",
            static_cast<unsigned long long>(seq), static_cast<unsigned long long>(call),
            static_cast<unsigned long long>(cid), static_cast<unsigned long long>(pid), branch);
    }
}

// Exact disassembly confirms no incoming arguments are read by these three
// cold helpers. The mapper helper alone reads its controller argument. Every
// original helper is called exactly once, including all cleanup and reporting.
void wrapProviderError() { recordExit(BranchObservation::Provider); originalProviderError(); }
void wrapMapError() { recordExit(BranchObservation::Map); originalMapError(); }
void wrapAddressError() { recordExit(BranchObservation::Address); originalAddressError(); }
void wrapMapperError(IOService *controller) {
    recordExit(BranchObservation::Mappers, controller);
    originalMapperError(controller);
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
    if (!permitted()) return originalPCI(controller);
    auto pci = target(controller);
    BranchObservation::Frame frame;
    frame.thread = reinterpret_cast<uintptr_t>(current_thread());
    frame.controller = reinterpret_cast<uintptr_t>(controller);
    frame.target = pci != nullptr;
    if (pci) {
        pci->retain();
        frame.call = event(controller, pci, "InitializePCI", 0, -1);
        frame.cid = controller->getRegistryEntryID();
        frame.pid = pci->getRegistryEntryID();
        SYSLOG("diag", "call=%llu controllerClass=%s exactFingerprint=1",
            static_cast<unsigned long long>(frame.call), controller->getMetaClass()->getClassName());
        snapshot(pci, frame.call, "before");
    }
    auto state = IOSimpleLockLockDisableInterrupt(observationLock);
    const bool tracked = observations.push(frame);
    IOSimpleLockUnlockEnableInterrupt(observationLock, state);
    const bool result = originalPCI(controller);
    state = IOSimpleLockLockDisableInterrupt(observationLock);
    observations.pop(frame);
    IOSimpleLockUnlockEnableInterrupt(observationLock, state);
    if (pci) {
        auto exit = event(controller, pci, "InitializePCI", frame.call, result);
        report(controller, pci, "PM9C1aROLastInitializePCI", frame.call, exit,
            result, frame.branches, tracked);
        snapshot(pci, frame.call, "after");
        pci->release();
    }
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
    auto exit = event(controller, pci, "CreateDeviceInterrupt", call, result != nullptr);
    report(controller, pci, "PM9C1aROLastInterrupt", call, exit, result != nullptr, 0, true);
    pci->release();
    return result;
}

bool within(mach_vm_address_t base, size_t size, mach_vm_address_t address, size_t length) {
    return address >= base && address - base <= size && length <= size - (address - base);
}

template<size_t N> bool matchesCode(mach_vm_address_t base, size_t size,
                                   mach_vm_address_t address, const uint8_t (&expected)[N]) {
    return within(base, size, address, N) &&
        memcmp(reinterpret_cast<const void *>(address), expected, N) == 0;
}

mach_vm_address_t callTarget(mach_vm_address_t function, size_t offset) {
    int32_t displacement {0};
    bcopy(reinterpret_cast<const void *>(function + offset + 1), &displacement, sizeof(displacement));
    return function + offset + 5 + displacement;
}

void process(void *, KernelPatcher &patcher, size_t index, mach_vm_address_t base, size_t size) {
    if (!permitted() || index != info.loadIndex) return;
    const char *symbols[] {
        "__ZN16IONVMeController13InitializePCIEv",
        "__ZN16IONVMeController12GetIOMappersEv",
        "__ZN16IONVMeController21CreateDeviceInterruptEPFvP8OSObjectP22IOInterruptEventSourceiEPFbS1_P28IOFilterInterruptEventSourceEP9IOService"
    };
    mach_vm_address_t addresses[7] {};
    for (unsigned i = 0; i < 3; ++i) {
        patcher.clearError();
        addresses[i] = patcher.solveSymbol(index, symbols[i]);
        patcher.clearError();
    }
    if (!matchesCode(base, size, addresses[0], Issue9Fingerprint::InitializePCI) ||
        !matchesCode(base, size, addresses[1], Issue9Fingerprint::GetIOMappers) ||
        !matchesCode(base, size, addresses[2], Issue9Fingerprint::CreateDeviceInterrupt)) {
        SYSLOG("diag", "exact method fingerprint mismatch or missing symbol; no routes installed");
        return;
    }
    addresses[3] = callTarget(addresses[0], 0x203);
    addresses[4] = callTarget(addresses[0], 0x20A);
    addresses[5] = callTarget(addresses[0], 0x211);
    addresses[6] = callTarget(addresses[0], 0x21B);
    if (!matchesCode(base, size, addresses[3], Issue9Fingerprint::Provider) ||
        !matchesCode(base, size, addresses[4], Issue9Fingerprint::Map) ||
        !matchesCode(base, size, addresses[5], Issue9Fingerprint::Address) ||
        !matchesCode(base, size, addresses[6], Issue9Fingerprint::Mappers)) {
        SYSLOG("diag", "exact error-handler fingerprint mismatch; no routes installed");
        return;
    }
    KernelPatcher::RouteRequest routes[] {
        {nullptr, wrapPCI, originalPCI}, {nullptr, wrapMappers, originalMappers},
        {nullptr, wrapInterrupt, originalInterrupt},
        {nullptr, wrapProviderError, originalProviderError}, {nullptr, wrapMapError, originalMapError},
        {nullptr, wrapAddressError, originalAddressError}, {nullptr, wrapMapperError, originalMapperError}
    };
    const char *names[] {"InitializePCI", "GetIOMappers", "CreateDeviceInterrupt",
                         "ProviderExit", "MapExit", "AddressExit", "MapperExit"};
    // Lilu copies trampoline prologues without relocating RIP-relative LEAs.
    // These cold helpers have an LEA at +12/+13, so forbid a 14-byte fallback.
    // Five-byte short routes only copy the verified position-independent prefix.
    // Exact fingerprints above reject pre-existing routes; short routes must not
    // be chained with other instrumentation of these private helper functions.
    for (unsigned i = 3; i < arrsize(routes); ++i) {
        const auto difference = routes[i].to - (addresses[i] + 5);
        if (difference != static_cast<mach_vm_address_t>(static_cast<int32_t>(difference))) {
            SYSLOG("diag", "short helper route out of range; no routes installed");
            return;
        }
    }
    for (unsigned i = 0; i < arrsize(routes); ++i) {
        routes[i].from = addresses[i];
        patcher.clearError();
        const bool routed = i < 3 ? patcher.routeMultiple(index, &routes[i], 1) :
            patcher.routeMultipleShort(index, &routes[i], 1);
        const bool ok = routed && *routes[i].org && patcher.getError() == KernelPatcher::Error::NoError;
        if (ok) __atomic_fetch_or(&installedRoutes, 1U << i, __ATOMIC_RELEASE);
        SYSLOG("diag", "route=%s installed=%d", names[i], ok);
        patcher.clearError();
    }
    SYSLOG("diag", "exactFingerprint=1 installedRoutes=%u expected=127",
        __atomic_load_n(&installedRoutes, __ATOMIC_ACQUIRE));
}

void init() {
    if (!permitted()) return;
    observationLock = IOSimpleLockAlloc();
    if (!observationLock) {
        SYSLOG("diag", "could not allocate observation lock; no routes installed");
        return;
    }
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

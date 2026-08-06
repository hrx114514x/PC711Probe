# PC711Probe

English | [简体中文](README.md)

An experimental Lilu plugin that fixes an `IONVMeFamily` Identify-timeout kernel panic affecting an SK hynix PC711 on a specific Hackintosh platform running macOS 15.6.1.

> This is not a generic NVMe driver. It contains a narrowly scoped compatibility patch tied to Darwin 24.6 internals and a private controller offset. Back up your data and validate it from a rollback-capable test EFI first.

## Verified result

`PC711Probe 0.6.0` completed one hardware boot into macOS 15.6.1 Recovery (24G90 / Darwin 24.6.0). Disk Utility opened, the PC711 model and all five existing partitions were enumerated, and the former first-Identify panic after roughly 75 seconds did not recur.

![PC711 enumerated in macOS 15.6.1 Recovery](docs/images/recovery-success.jpg)

Verified environment:

| Item | Verified value |
|---|---|
| Controller | SK hynix `1C5C:174A`, NVMe class `01:08:02` |
| Device model | `SKHynix_HFS512GDE9X084N` (PC711) |
| Firmware | `41010C22` |
| PCI path | AMD platform, `GPP3/NVME` |
| Failing OS | macOS 15.6.1, build 24G90, Darwin 24.6.0 |
| Reference OS | macOS 26.5.1, build 25F80, Darwin 25.5.0 |
| Boot environment | OpenCore 1.0.8, Lilu 1.7.3 |

The verified boundary covers controller initialization, Identify, namespace discovery, and IOMedia/partition publication. A full macOS 15 installation, sustained I/O, TRIM, sleep/wake, other firmware, and other platforms remain untested.

## Symptom

Without the patch, macOS 15.6.1 panics about 75 seconds after its first Identify Controller command:

```text
nvme: Command timeout. Identify.
MODEL=Model string not available
FW=FW Revision not available
CSTS=0x1 VID=0x1c5c DID=0x174a
```

The same hardware and OpenCore configuration initialize natively through Apple `IONVMeFamily` on macOS 26.5.1, including working I/O and sleep/wake. This narrowed the fault to a version-specific NVMe initialization and interrupt-path difference.

## How it works

Symbol and machine-code comparison of the two real `BootKernelExtensions.kc` files showed that:

1. Darwin 25 added this operation to `IONVMeController::CreateDeviceInterrupt` before interrupt-source enumeration:

   ```cpp
   IOPCIDevice::configureInterrupts(0x20000, 1, 1, 0);
   ```

   `0x20000` requests MSI-X.

2. Darwin 24 still contains an older special MSI-X branch in `FilterInterruptRequest` / `HandleInterruptRequest`. Darwin 25 removed that branch and consistently uses the normal event-source path.

With Darwin 24.6.0, `-pc711pcompat`, and an exact PCI identity match, `PC711Probe 0.6.0`:

- routes Apple's `CreateDeviceInterrupt`;
- requests one MSI-X vector;
- calls the original Apple function to create the event source;
- clears bit `0x10` at Darwin 24 controller offset `0x191`, selecting the normal event-source path; and
- leaves Identify, queue management, completion parsing, namespaces, and storage I/O in Apple code.

See [docs/DEVELOPMENT.en.md](docs/DEVELOPMENT.en.md) for the evidence-driven development process.

## Compatibility scope

The compatibility route requires all of the following internally:

- Darwin `24.6.0`;
- boot argument `-pc711pcompat`;
- PCI Vendor/Device `1C5C:174A`; and
- NVMe class/revision mask result `01:08:02`.

The route remains inactive on other Darwin versions and nonmatching devices.

SK hynix may reuse the same PCI ID across multiple OEM models. Anything outside the verified table must be treated as untested, even when its ID matches.

## Installation

Read the [English installation and rollback guide](docs/INSTALL.en.md) first. The minimum OpenCore setup is:

1. Load Lilu before PC711Probe.
2. Copy `PC711Probe.kext` to `EFI/OC/Kexts`.
3. Enable it under `Kernel -> Add` with:
   - `BundlePath`: `PC711Probe.kext`
   - `ExecutablePath`: `Contents/MacOS/PC711Probe`
   - `PlistPath`: `Contents/Info.plist`
   - `MinKernel`: `24.0.0`
   - `MaxKernel`: `24.99.99`
4. Add `-pc711pcompat` to `boot-args`.
5. For the first isolated test, disable NVMeFix and any SSDT that hides the target NVMe through `_STA=0` or an equivalent method.

Add `-pc711pdbg` only when debug logging is needed.

## Building

macOS, Apple Command Line Tools, and Git are required:

```bash
git clone --recurse-submodules https://github.com/hrx114514x/PC711Probe.git
cd PC711Probe
./Scripts/verify.sh
```

The result is `build/Debug/PC711Probe.kext`. The Lilu and MacKernelSDK dependencies are pinned as Git submodules.

## Safety boundary

- Perform the first test from a separate USB EFI and keep a known-good rollback EFI.
- Do not erase or modify existing partitions in Recovery merely to test this plugin.
- Do not combine it with AML/SSDT code that hides the PC711.
- This is a version-specific patch using a private Apple object layout. Never assume an OS update is compatible.
- You accept the risk of data loss, boot failure, and kernel panic.

## License and credits

Project code is licensed under [BSD 3-Clause](LICENSE).

- [Lilu](https://github.com/acidanthera/Lilu) — plugin and routing framework
- [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) — kernel extension build SDK

Third-party dependencies retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This repository does not redistribute Apple Kernel Collections or the `IONVMeFamily` binary.

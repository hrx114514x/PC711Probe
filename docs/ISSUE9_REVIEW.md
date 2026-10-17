# PC711Probe Issue 9 review changes

This is an unvalidated research patch based on commit `39add931173c0bbadad54bc14b1f184d3568ac7d`. It is not a PM9C1a compatibility fix or a new release. The released driver source at tag `v1.8.1` is identical to this base driver source.

The patch adds a shared runtime gate to both early probing and the interrupt wrapper. Lilu startup must have succeeded, `-pc711poff` must be absent, and Darwin must be 20 through 24. The callback also refuses to install the runtime route outside that gate. Force excludes `144D:A80D` before either compatibility mutation path. Other NVMe IDs remain broadly eligible in Force. The old controller flag is cleared only on Darwin 23 or 24 after successful configuration and a non-null event source. These restrictions reduce exposure; they do not establish runtime safety or solve the native initialization failure.

The existing private vtable slot, private controller offset and Big Sur reallocation implementation remain. No attempt is made to cache a successful early result across later initialization or to remove repeated calls without lifecycle evidence. A successful allocation is not proof of successful interrupt delivery.

`Diagnostics/PM9C1aReadOnly.cpp` is a separate, opt-in diagnostic. Its hardware reads and logs require PCI `144D:A80D`, class `01:08:02`, Darwin `24.6.0`, and `-pm9c1adiag`. `-pm9c1adiagoff` wins. It routes only three IONVMeController methods, never global IOService::probe. Off-target method calls are forwarded. The routes themselves are global function hooks; hardware read-only does not mean zero changes to kernel execution or timing.

The diagnostic uses public PCI reads and registry IDs, with a bounded conventional-capability walk. It does not read private controller fields, map BARs, allocate interrupts, change bus mastering, submit NVMe commands, or substitute return values. A GetIOMappers false return can identify that candidate only when its route is installed and its call is unambiguously within the same controller's InitializePCI interval. Missing markers, overlapping same-controller intervals, zero IDs or incomplete logs are inconclusive. Earlier failures remain unresolved between provider acquisition, map creation and address acquisition; the diagnostic deliberately does not infer an exact branch from post-cleanup fields.

Build and inspect the separate diagnostic on macOS with `sh Diagnostics/verify-macos.sh`. macOS CI has successfully linked the diagnostic as an x86_64 kext and passed the compatibility builds and 734 executable policy/capability tests. A linked artifact is still a review experiment: exact Apple ABI and hardware validation remain outstanding. Keep the old diagnostic, NVMeFix, PC711Probe and Force disabled during a first isolated diagnostic test. Never hot-unload an active Lilu route.

For the next static investigation, `Scripts/issue9-extract.py` reads an existing, uncompressed x86_64 Mach-O/kernel fileset and extracts four bounded error-handler windows. It requires the exact SHA-256 of the supplied 580-byte InitializePCI excerpt, so it refuses other builds instead of guessing their call targets. It records the input hash and selected Mach-O UUID; for a non-fileset kernel image, that UUID belongs to the root image and is not necessarily the NVMe bundle UUID. It does not boot macOS or modify the image. The original report's unslid address is `0xffffff80025f5aa4`:

```sh
python3 Scripts/issue9-extract.py /path/to/uncompressed-kernel-or-collection \
  --address 0xffffff80025f5aa4 --out /tmp/pm9c1a-handler-evidence
```

The output directory must not already exist. The image can be processed offline on Windows, Linux or macOS using Python 3. The 192-byte windows are not guaranteed function boundaries and may overlap. The helper does not determine which branch ran; the windows allow the error helpers and their possible log messages to be analyzed before adding more runtime hooks. A fingerprint mismatch should be reported with the actual image identity; do not relax the check or change the reference hash just to make extraction succeed. The hexadecimal test fixture comes from the Issue #9 reporter's supplied disassembly excerpt, not a redistributed kernel binary.

For released 1.8.1, disable the kext's Boolean `Kernel/Add/Enabled` entry or boot a clean rollback EFI. Do not rely solely on `-pc711poff`: the released independent early personality does not consult Lilu's startup result. Preserve a separately bootable USB EFI and current backups. Do not change VMD, IOMMU, BARs or unrelated EFI settings as part of this diagnostic experiment. An input failure is a stop-and-rollback condition, not a reason to repeat Force tests.

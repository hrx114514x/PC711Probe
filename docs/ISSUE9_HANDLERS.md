# Issue 9 handler analysis

The [reporter's new attachment](https://github.com/hrx114514x/PC711Probe/issues/9#issuecomment-6242734221) contains four 192-byte windows from the same InitializePCI excerpt. All four window hashes and their overlapping bytes were independently checked. The reported image hash is `1f0dbba16b8de05a620f5c2081916b82aa4ae9e8b8a1a18cf93ce4c446dfaba2`; the reported IONVMeFamily fileset UUID is `c90bcf94-6412-3adb-ba60-8f49e3213e29`. The complete image was not supplied, so those image-level identifiers have not been independently recomputed.

## What the bytes establish

Each handler has one direct call to `0xffffff80026088a0`. Three handlers have 59 bytes through their first return; the mapper handler has 87. They pass four RIP-relative addresses in RDI, RSI, RDX and R8, zero ECX and a stack argument, and pass a distinct immediate in R9D.

| Candidate | Entry | Bytes through first return | R9D immediate | RSI address |
| --- | --- | --- | --- | --- |
| Provider | `0xffffff800260b5d6` | 59 | `0x3ef` / 1007 | `0xffffff80026166ad` |
| BAR0 map | `0xffffff800260b59b` | 59 | `0x403` / 1027 | `0xffffff80026166c3` |
| Virtual address | `0xffffff800260b560` | 59 | `0x407` / 1031 | `0xffffff80026166ce` |
| Mappers | `0xffffff800260b509` | 87 | `0x439` / 1081 | `0xffffff8002615e85` |

All use RDI `0xffffff8002614c61` and R8 `0xffffff8002616578`. Provider/map use RDX `0xffffff8002614c79`; virtual-address uses `0xffffff80026166e4`; mappers uses `0xffffff800261675b`. This resembles a common assertion/reporting helper with distinct expressions and source-location metadata. The strings and common callee body were outside the supplied windows, so their text, signature, output destination and severity are not yet known. R9D is a raw immediate, not a proven source line number.

The mapper handler additionally loads the object field at `+0x148`, calls vtable slot `+0x28` if it is non-null, and clears `+0x148`. In the supplied InitializePCI code, the dedicated MSI-X path stores a mapping at that field. After the handler returns, InitializePCI's shared cleanup also handles `+0x140`. This adds a concrete cleanup observation; it does not show that mapping failed, that MSI-X caused the failure, or that the mapper helper was reached on the reported boot.

No handler performs the original BAR0 mapping or mapper initialization itself: these windows are reached after the corresponding condition failed. Finding their code does not identify which branch ran. The static data supplied so far also does not establish that a message was emitted or retained in Recovery logs.

## Collect the referenced data in one offline pass

Use the updated script against the same existing uncompressed image, with a new output directory:

```sh
python3 Scripts/issue9-extract.py /path/to/the-same-uncompressed-image \
  --address 0xffffff80025f5aa4 \
  --out pm9c1a-handler-details-20261018 --details
```

Share `pm9c1a-handler-details-20261018/issue9-handler-details.json`. It includes:

- The original four windows and the verified instruction-derived argument addresses.
- Nine unique referenced strings, bounded to 512 bytes each, with raw bytes and termination status. Partial/unmapped strings are marked explicitly.
- A 1024-byte window at the common callee.
- Optional symbol-derived windows for GetIOMappers, start, InitializeNVMe, CreateDeviceInterrupt and the IONVMeController vtable. Missing, ambiguous, invalid or truncated symbols are recorded as such; no address is guessed.

Both InitializePCI and the four received handler hashes must match before detailed extraction. The script reads file-backed mappings with the appropriate executable/readable protections, does not overwrite an existing output directory, and does not modify or execute the input image. It needs only Python 3, no disassembler package and no new boot. Optional windows are bounded excerpts, not proven function extents.

The new fixtures preserve the supplied handler bytes. Unit tests combine them with synthetic mappings and synthetic strings: those tests verify extraction behavior, not the real Apple message text. The next analysis will use the real string/callee output to identify existing diagnostics and check the mapper call/virtual-table interpretation before adding more runtime instrumentation.

#!/bin/sh
# Review build only. Never installs a kext or edits an EFI.
set -eu
test "$(uname -s)" = Darwin || { echo 'Requires macOS and Xcode Command Line Tools' >&2; exit 1; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk="$root/Vendor/MacKernelSDK"
lilu="$root/Vendor/Lilu/Lilu"
out="$root/build/Issue9Review"
mkdir -p "$out"
compile_cpp() {
    xcrun clang++ -arch x86_64 -std=c++14 -fapple-kext -fno-builtin \
        -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables \
        -isystem "$sdk/Headers" -isystem "$lilu" \
        -DKERNEL -DKERNEL_PRIVATE -D__KERNEL__ -DMACH_ASSERT=1 \
        -DPRODUCT_NAME=PM9C1aReadOnly -DMODULE_VERSION=0.1.0 \
        -Werror -Wall -Wextra -Wno-deprecated-declarations -c "$1" -o "$2"
}
compile_cpp "$root/Diagnostics/PM9C1aReadOnly.cpp" "$out/diag.o"
compile_cpp "$lilu/Library/plugin_start.cpp" "$out/plugin_start.o"
xcrun clang -arch x86_64 -fno-builtin -isystem "$sdk/Headers" \
    -DKERNEL -DKERNEL_PRIVATE -D__KERNEL__ -Werror -Wall -Wextra \
    -c "$root/Diagnostics/KmodInfo.c" -o "$out/KmodInfo.o"
bundle="$out/PM9C1aReadOnly.kext/Contents"
mkdir -p "$bundle/MacOS"
xcrun clang++ -arch x86_64 -nostdlib -static -Wl,-kext \
    "$out/diag.o" "$out/plugin_start.o" "$out/KmodInfo.o" \
    "$sdk/Library/x86_64/libkmod.a" -o "$bundle/MacOS/PM9C1aReadOnly"
cp "$root/Diagnostics/Info.plist" "$bundle/Info.plist"
plutil -lint "$bundle/Info.plist"
file "$bundle/MacOS/PM9C1aReadOnly"
nm -u "$bundle/MacOS/PM9C1aReadOnly" > "$out/undefined-symbols.txt"
shasum -a 256 "$bundle/MacOS/PM9C1aReadOnly" "$bundle/Info.plist"
echo 'Review artifact only: native linking is not a boot or hardware validation.'

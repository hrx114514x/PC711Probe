#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk_dir="$project_dir/Vendor/MacKernelSDK"
lilu_root="$project_dir/Vendor/Lilu"
lilu_dir="$lilu_root"
if [ -f "$lilu_root/Lilu/Headers/kern_api.hpp" ]; then
	lilu_dir="$lilu_root/Lilu"
fi
build_dir="$project_dir/build/Debug"
version="1.8.0"

if [ ! -f "$sdk_dir/Headers/IOKit/pci/IOPCIDevice.h" ]; then
	echo "Missing Vendor/MacKernelSDK" >&2
	exit 1
fi

if [ ! -f "$lilu_dir/Headers/kern_api.hpp" ] ||
	[ ! -f "$lilu_dir/Library/plugin_start.cpp" ]; then
	echo "Missing Vendor/Lilu build resources" >&2
	exit 1
fi

plutil -lint "$project_dir/Driver/Info.plist"
plutil -lint "$project_dir/Driver/InfoForce.plist"
mkdir -p "$build_dir"

common_cxx_flags="-arch x86_64 -std=c++14 -fapple-kext -fno-builtin -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables"
common_defines="-DKERNEL -DKERNEL_PRIVATE -D__KERNEL__ -DPRODUCT_NAME=PC711Probe -DMODULE_VERSION=$version -DMACH_ASSERT=1"

xcrun clang++ $common_cxx_flags \
	-c \
	-isystem "$sdk_dir/Headers" \
	-isystem "$lilu_dir" \
	$common_defines \
	-Werror -Wall -Wextra -Wno-deprecated-declarations \
	"$lilu_dir/Library/plugin_start.cpp" \
	-o "$build_dir/plugin_start.o"

xcrun clang \
	-arch x86_64 \
	-fno-builtin \
	-c \
	-isystem "$sdk_dir/Headers" \
	-DKERNEL -DKERNEL_PRIVATE -D__KERNEL__ \
	-DMODULE_VERSION="$version" \
	-Werror -Wall -Wextra \
	"$project_dir/Support/KmodInfo.c" \
	-o "$build_dir/KmodInfo.o"

build_variant() {
	variant_name="$1"
	force_variant="$2"
	plist_source="$3"
	bundle_dir="$build_dir/$variant_name.kext"
	contents_dir="$bundle_dir/Contents"
	binary_dir="$contents_dir/MacOS"
	binary_file="$binary_dir/PC711Probe"
	object_file="$build_dir/$variant_name.o"

	mkdir -p "$binary_dir"
	xcrun clang++ $common_cxx_flags \
		-c \
		-isystem "$sdk_dir/Headers" \
		-isystem "$lilu_dir" \
		$common_defines \
		-DPC711PROBE_FORCE_VARIANT="$force_variant" \
		-Werror -Wall -Wextra -Wno-deprecated-declarations \
		"$project_dir/Driver/PC711Probe.cpp" \
		-o "$object_file"

	xcrun clang++ \
		-arch x86_64 \
		-nostdlib \
		-static \
		-Wl,-kext \
		-o "$binary_file" \
		"$object_file" \
		"$build_dir/plugin_start.o" \
		"$build_dir/KmodInfo.o" \
		"$sdk_dir/Library/x86_64/libkmod.a"

	cp "$plist_source" "$contents_dir/Info.plist"
	chmod 755 "$binary_file"
	echo "Built $bundle_dir"
	file "$binary_file"
}

build_variant "PC711Probe" 0 "$project_dir/Driver/Info.plist"
build_variant "PC711ProbeForce" 1 "$project_dir/Driver/InfoForce.plist"

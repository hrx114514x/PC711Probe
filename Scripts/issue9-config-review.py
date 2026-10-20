#!/usr/bin/env python3
"""Export an offline, review-only subset of an existing OpenCore configuration.

Never edits the input EFI, reads live NVRAM, installs files or uploads anything.
The generated plist is incomplete by design and MUST NOT be used for booting.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import plistlib
import re
import zipfile

MAX_CONFIG = 8 * 1024 * 1024
MAX_TABLE = 16 * 1024 * 1024
MAX_TOTAL = 64 * 1024 * 1024
MAX_TABLES = 128
APPLE_GUID = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
SECTIONS = ('ACPI', 'Booter', 'DeviceProperties', 'Kernel', 'UEFI')
SENSITIVE = re.compile(r'serial|uuid|password|passwd|secret|token|salt|^system-id$|(^|[, _-])(rom|mlb|mac-address)($|[, _-])', re.I)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inside(path, directory):
    try:
        path.relative_to(directory)
        return True
    except ValueError:
        return False


def scrub(value, removed, prefix=''):
    if isinstance(value, dict):
        result = {}
        for key, item in value.items():
            location = f'{prefix}/{key}'
            if key.startswith('#') or key in ('Comment', 'Arguments') or SENSITIVE.search(key):
                removed.append(location)
            else:
                result[key] = scrub(item, removed, location)
        return result
    if isinstance(value, list):
        return [scrub(item, removed, f'{prefix}/{index}') for index, item in enumerate(value)]
    return value


def bounded_read(path, limit):
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f'File exceeds review limit: {path.name}')
    return data


def table_path(oc, name):
    if not isinstance(name, str) or not name:
        raise ValueError('Enabled ACPI entry has no usable Path')
    relative = PurePosixPath(name.replace('\\', '/'))
    if relative.is_absolute() or ':' in name or any(part in ('.', '..') for part in relative.parts):
        raise ValueError('ACPI Path must stay inside EFI/OC/ACPI')
    if relative.suffix.lower() != '.aml':
        raise ValueError('Only configured AML tables can be copied')
    acpi = (oc / 'ACPI').resolve()
    path = (acpi / Path(*relative.parts)).resolve()
    if not inside(acpi, oc) or not inside(path, acpi):
        raise ValueError('ACPI symlink/junction escapes the selected OC directory')
    return path, relative.as_posix()


def collect(oc_path):
    oc = Path(oc_path).resolve()
    config = bounded_read(oc / 'config.plist', MAX_CONFIG)
    original = plistlib.loads(config)
    if not isinstance(original, dict):
        raise ValueError('config.plist must have a dictionary root')
    removed = []
    review = scrub({key: original[key] for key in SECTIONS if key in original}, removed)
    # Keep only configured boot-args and its persistence controls. Do not export other NVRAM
    # variables, PlatformInfo, Misc/security/password fields, or live variables.
    nvram = original.get('NVRAM', {})
    if not isinstance(nvram, dict) or not isinstance(nvram.get('Add', {}), dict):
        raise ValueError('Malformed NVRAM/Add dictionary')
    for guid, values in nvram.get('Add', {}).items():
        if guid.upper() == APPLE_GUID and isinstance(values, dict) and 'boot-args' in values:
            if 'NVRAM' in review:
                raise ValueError('Duplicate boot-args GUID spelling')
            args = values['boot-args']
            if not isinstance(args, (str, bytes)):
                raise ValueError('boot-args must be string or data')
            review['NVRAM'] = {'Add': {APPLE_GUID: {'boot-args': args}}}
    deletes = nvram.get('Delete', {})
    if not isinstance(deletes, dict):
        raise ValueError('Malformed NVRAM/Delete dictionary')
    for guid, names in deletes.items():
        if guid.upper() == APPLE_GUID:
            if not isinstance(names, list):
                raise ValueError('Malformed NVRAM/Delete list')
            if 'boot-args' in names:
                review.setdefault('NVRAM', {})['Delete'] = {APPLE_GUID: ['boot-args']}
    for key in ('LegacyEnable', 'LegacyOverwrite', 'WriteFlash'):
        if key in nvram:
            if type(nvram[key]) is not bool:
                raise ValueError(f'Malformed NVRAM/{key}')
            review.setdefault('NVRAM', {})[key] = nvram[key]
    acpi = original.get('ACPI', {})
    if not isinstance(acpi, dict) or not isinstance(acpi.get('Add', []), list):
        raise ValueError('Malformed ACPI/Add array')
    entries = acpi.get('Add', [])
    if len(entries) > MAX_TABLES:
        raise ValueError('Too many ACPI entries for this review bundle')
    files, records, seen, total = {}, [], set(), 0
    for entry in entries:
        if not isinstance(entry, dict) or type(entry.get('Enabled', False)) is not bool:
            raise ValueError('Malformed ACPI/Add entry or Enabled value')
        if not entry.get('Enabled', False):
            continue
        path, name = table_path(oc, entry.get('Path'))
        if name.casefold() in seen:
            raise ValueError('Duplicate enabled ACPI path')
        seen.add(name.casefold())
        if not path.is_file():
            records.append({'path': f'ACPI/{name}', 'status': 'missing'})
            continue
        raw = bounded_read(path, MAX_TABLE)
        if raw[:4] in (b'MSDM', b'SLIC'):
            records.append({'path': f'ACPI/{name}', 'status': 'omitted_license_table'})
            continue
        total += len(raw)
        if total > MAX_TOTAL:
            raise ValueError('ACPI tables exceed the total review limit')
        files[f'ACPI/{name}'] = raw
        records.append({'path': f'ACPI/{name}', 'status': 'copied', 'bytes': len(raw), 'sha256': digest(raw)})
    manifest = {
        'schema': 1, 'purpose': 'offline review only; not a bootable EFI',
        'source_config_sha256': digest(config),
        'included_sections': list(review), 'omitted_fields': removed,
        'acpi_tables': records, 'kernel_binaries_included': False,
        'firmware_original_tables_included': False,
        'live_boot_args_verified': False,
        'privacy_limit': 'boot-args and AML contents are retained; inspect the archive before sharing',
    }
    files['REVIEW_ONLY.plist'] = plistlib.dumps(review, sort_keys=False)
    files['manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    files['README.txt'] = (
        'REVIEW ONLY - DO NOT INSTALL OR BOOT THIS PLIST\n\n'
        'This incomplete copy is for Issue 9 PCI resource allocation review.\n'
        'The original EFI was read only. No files were installed or uploaded.\n'
        'PlatformInfo, Misc, NVRAM except boot-args Add/Delete and persistence\n'
        'booleans, comments,\n'
        'driver/tool Arguments, and common identifier/secret property names\n'
        'are omitted. Kernel binaries and UEFI executables are not included.\n'
        'Only enabled AML files referenced by ACPI/Add are copied; MSDM/SLIC\n'
        'licensing tables are omitted. Missing\n'
        'tables are recorded in manifest.json. Firmware-original ACPI tables\n'
        'and effective runtime boot arguments are not collected.\n\n'
        'Review boot-args, DeviceProperties and AML contents before sharing:\n'
        'this focused exporter is not a guarantee of complete anonymization.\n'
    ).encode()
    return files, manifest


def export(oc_path, output):
    oc = Path(oc_path).resolve()
    out = Path(output).resolve()
    protected = oc.parent if oc.name.casefold() == 'oc' and oc.parent.name.casefold() == 'efi' else oc
    if inside(out, protected):
        raise ValueError('Output must be outside the original EFI/OC tree')
    if out.exists():
        raise ValueError('Output already exists; choose a new filename')
    files, manifest = collect(oc)
    # Exclusive creation also refuses an output created after the earlier check.
    with zipfile.ZipFile(out, 'x', zipfile.ZIP_DEFLATED) as archive:
        for name, raw in files.items():
            archive.writestr(name, raw)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('oc', type=Path, help='Existing EFI/OC directory; read only')
    parser.add_argument('--out', type=Path, required=True, help='New review ZIP outside EFI/OC')
    args = parser.parse_args()
    try:
        manifest = export(args.oc, args.out)
    except (ValueError, OSError, plistlib.InvalidFileException) as error:
        parser.exit(1, f'No boot configuration changed: {error}\n')
    print(f'Created review-only ZIP: {args.out}')
    print(f"Copied {sum(r['status']=='copied' for r in manifest['acpi_tables'])} enabled AML files; "
          f"missing {sum(r['status']=='missing' for r in manifest['acpi_tables'])}.")
    print('Inspect the contents before sharing. Do not use REVIEW_ONLY.plist for booting.')


if __name__ == '__main__':
    main()

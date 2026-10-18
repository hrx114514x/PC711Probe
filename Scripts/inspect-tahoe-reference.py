#!/usr/bin/env python3
"""Read-only analysis of a hosted Intel Tahoe image, not a drive test.

Exports hashes, metadata and bounded decoded function control flow only. Does
not export a system kext/kernel collection or install/load/patch anything.
"""
import hashlib
import importlib.util
import json
import mmap
from pathlib import Path
import platform
import subprocess
import tempfile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_GRP_JUMP, CS_GRP_RET
from capstone.x86 import X86_OP_IMM, X86_INS_JMP

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/TahoeReference'
spec=importlib.util.spec_from_file_location('extract',ROOT/'Scripts/issue9-extract.py')
tool=importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)
tool.SYMBOL_WINDOWS={
    '__ZN16IONVMeController13InitializePCIEv':(2048,4),
    '__ZN16IONVMeController12GetIOMappersEv':(1024,4),
    '__ZN16IONVMeController5startEP9IOService':(4096,4),
    '__ZN16IONVMeController21CreateDeviceInterruptEPFvP8OSObjectP22IOInterruptEventSourceiEPFbS1_P28IOFilterInterruptEventSourceEP9IOService':(2048,4),
}


def decode(window):
    code=bytes.fromhex(window['bytes']);base=int(window['address'],16)
    md=Cs(CS_ARCH_X86,CS_MODE_64);md.detail=True
    todo=[base];seen={};limits=[]
    while todo and len(seen)<1000:
        address=todo.pop()
        if address in seen:continue
        offset=address-base
        if not 0<=offset<len(code):
            limits.append(hex(address));continue
        instruction=next(md.disasm(code[offset:],address,count=1),None)
        if not instruction:
            limits.append(hex(address));continue
        seen[address]=dict(offset=hex(offset),mnemonic=instruction.mnemonic,operands=instruction.op_str)
        if instruction.group(CS_GRP_RET) or instruction.mnemonic in ('ud2','int3'):continue
        if instruction.group(CS_GRP_JUMP):
            if instruction.operands and instruction.operands[0].type==X86_OP_IMM:
                todo.append(instruction.operands[0].imm & 0xFFFFFFFFFFFFFFFF)
            else:limits.append('indirect tail at '+hex(address))
            if instruction.id==X86_INS_JMP:continue
        todo.append(address+instruction.size)
    return dict(instructions=[seen[a] for a in sorted(seen)],
                boundary_or_tail_targets=sorted(set(limits)),limit_hit=bool(todo))


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    fixture=(ROOT/'Tests/fixtures/InitializePCI-580.hex').read_text()
    self_check=decode(dict(bytes=bytes.fromhex(fixture).hex(),address='0xffffff80025f5aa4'))
    assert len(self_check['instructions'])==135 and not self_check['boundary_or_tail_targets'],self_check
    version=subprocess.check_output(['sw_vers','-productVersion'],text=True).strip()
    build=subprocess.check_output(['sw_vers','-buildVersion'],text=True).strip()
    metadata=dict(os_version=version,os_build=build,kernel=platform.release(),
                  machine=platform.machine(),hardware_test=False,
                  scope='Static code from hosted Intel Tahoe; no Samsung drive attached or tested.')
    (OUT/'host.json').write_text(json.dumps(metadata,indent=2)+'\n')
    assert platform.machine()=='x86_64' and version.split('.')[0]=='26',metadata
    candidates=[Path('/System/Library/KernelCollections/BootKernelExtensions.kc'),
                Path('/System/Library/Extensions/IONVMeFamily.kext/Contents/MacOS/IONVMeFamily')]
    attempts=[]
    with tempfile.TemporaryDirectory() as directory:
        for path in candidates:
            if not path.is_file():
                attempts.append(dict(path=str(path),status='absent'));continue
            candidate=path
            magic=path.open('rb').read(4)
            if magic not in (b'\xcf\xfa\xed\xfe',):
                candidate=Path(directory)/'IONVMeFamily.x86_64'
                thin=subprocess.run(['lipo',str(path),'-thin','x86_64','-output',str(candidate)],capture_output=True,text=True)
                if thin.returncode:
                    attempts.append(dict(path=str(path),status='unsupported container',detail=thin.stderr));continue
            try:
                with candidate.open('rb') as stream, mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ) as data:
                    module=tool.entry(data)
                    windows=tool.symbol_windows(data,module)
                    decoded={}
                    for name,w in windows.items():
                        clean={k:v for k,v in w.items() if k!='bytes'}
                        if w.get('status')=='captured':clean.update(decode(w))
                        decoded[name]=clean
                    report=dict(host=metadata,input_path=str(path),input_sha256=hashlib.sha256(data).hexdigest(),
                                module_uuid=tool.uuid(data,module),functions=decoded)
                (OUT/'nvme-reference.json').write_text(json.dumps(report,indent=2)+'\n')
                attempts.append(dict(path=str(path),status='analyzed'))
                if all(w.get('status')=='captured' for w in windows.values()):break
            except (ValueError,OSError) as error:
                attempts.append(dict(path=str(path),status='unavailable',detail=str(error)))
    (OUT/'attempts.json').write_text(json.dumps(attempts,indent=2)+'\n')
    print(json.dumps(metadata));print(json.dumps(attempts))
    if not (OUT/'nvme-reference.json').exists():raise SystemExit('No inspectable NVMe reference found')


if __name__=='__main__':main()

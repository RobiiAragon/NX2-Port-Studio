# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded NVN2 201.11 input branch for the existing offline shader chain.

Only VS/PS and audited compiler-managed local spills are admitted. Resource
layouts not yet established for NVN2 (c1/driver banks, SSBO, textures/images)
are rejected after translation. The NVN1 build remains the default.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
DEPENDENCIES=(
    'nx2/__init__.py','nx2/shaders.py','nx2/errors.py','nx2/formats.py',
    'nx2/input_io.py','nx2/symbols.py',
    'native/ampere_lower.cpp','native/ampere_branch_compat.h','native/include/common/common_types.h',
    'native/ampere_source.py','native/Makefile','third_party/eglantine-nx2/lower_to_maxwell.h','upstreams.lock.json',
)

def source_root():
    root=Path(os.environ.get('NX2_SOURCE_ROOT',ROOT)).resolve()
    if root!=ROOT:
        for name in DEPENDENCIES:
            if (root/name).read_bytes()!=(ROOT/name).read_bytes():
                raise ValueError('NX2_SOURCE_ROOT disagrees with the shader chain checkout: '+name)
    return root

def lowerer_path():
    return Path(os.environ.get('NX2_AMPERE_TOOL',source_root()/'.nx2-build/native/nx2-ampere-lower')).resolve()

def verified_lowerer():
    root=source_root(); path=lowerer_path()
    spec=importlib.util.spec_from_file_location('_dmsh_ampere_source',root/'native/ampere_source.py')
    module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    expected=module.source_sha(root)
    if not path.is_file(): raise ValueError('build the NVN2 lowerer from this checkout: make -C native')
    result=subprocess.run([str(path),'--source-sha'],capture_output=True,text=True,timeout=30)
    if result.returncode or result.stdout.strip()!=expected:
        raise ValueError('NVN2 lowerer was not built from the current pinned sources; rebuild with make -C native')
    return {'ampere_source':expected,'ampere_binary':hashlib.sha256(path.read_bytes()).hexdigest()[:16]}

def sections(record):
    if len(record)<56 or record[:8]!=b'BTCDBTCD': raise ValueError('invalid shader transport record')
    control_bytes,code_bytes=struct.unpack_from('<II',record,40)
    if code_bytes>len(record)-56 or control_bytes!=len(record)-56-code_bytes:
        raise ValueError('NVN2 transport section lengths exceed the record')
    return record[56+code_bytes:],record[56:56+code_bytes]

def is_nvn2_record(record):
    try: control,_=sections(record)
    except ValueError: return False
    return len(control)>=12 and control[:4]==bytes.fromhex('34127698') and struct.unpack_from('<I',control,4)[0]>=201

def decode_record(key,name,record):
    root=source_root(); sys.path.insert(0,str(root))
    from nx2.shaders import read_shader
    control,code=sections(record)
    shader=read_shader(control,code,expected_stage=name[:2])
    if shader.key!=key: raise ValueError('NVN2 native control/code identity disagrees with the shader map')
    if shader.control.stage not in ('vs','ps'): raise ValueError('NVN2 offline branch currently supports only VS/PS')
    return shader

def make_job(key,name,record,work,facts,expected_tools=None):
    shader=decode_record(key,name,record)
    tools=verified_lowerer()  # checked before cache lookup, and again for each invocation
    if expected_tools is not None and tools!=expected_tools:
        raise ValueError("NVN2 lowerer changed during the shader build")
    native=Path(work)/(key+'.nvn2.bin'); output=Path(work)/(key+'.bin')
    native.write_bytes(shader.code)
    result=subprocess.run([str(lowerer_path()),'--graphics',str(native),str(output)],capture_output=True,text=True,timeout=30)
    if result.returncode: raise ValueError('NVN2 native lowering failed: '+result.stderr.strip()[:240])
    meta=json.loads(result.stdout)
    if meta.get('format')!='maxwell-graphics-ryujinx-prefixed-scheduling':
        raise ValueError('NVN2 lowerer output is not the audited scheduling format')
    job={'key':key,'stage':shader.control.stage,'code':str(output),'tex':{},'img':{},'attr':{},'fout':{},'c1':'','nvn2':True,'nvn2_tools':tools,
         'debug':os.environ.get('XLATE_DEBUG','1')!='0','reduced':os.environ.get('XLATE_REDUCED','1')=='1'}
    if key in facts: job.update(facts[key])
    else: job['no_facts']=True
    return job,b''

def resource_error(resources):
    if any(resources.get(kind) for kind in ('sbufs','textures','images')):
        return 'NVN2 SSBO/texture/image binding layout has not been audited'
    if any(not 3<=row['slot']<=16 for row in resources.get('cbufs',[])):
        return 'NVN2 c1/driver constant-bank data has not been audited'
    return None

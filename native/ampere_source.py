#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Attest the exact inputs of the offline Ampere wrapper; no game input."""
from pathlib import Path
import argparse
import hashlib
import json

INPUTS = (
    'native/ampere_lower.cpp', 'native/ampere_branch_compat.h',
    'native/include/common/common_types.h', 'native/ampere_source.py', 'native/Makefile',
    'third_party/eglantine-nx2/lower_to_maxwell.h', 'upstreams.lock.json',
)

def source_sha(root=None):
    root=Path(root) if root is not None else Path(__file__).resolve().parents[1]
    lock=json.loads((root/'upstreams.lock.json').read_text())
    source=next(s for s in lock['sources'] if s['name']=='Eglantine')
    record=next(f for f in source['files'] if f['file']=='third_party/eglantine-nx2/lower_to_maxwell.h')
    if hashlib.sha256((root/record['file']).read_bytes()).hexdigest()!=record['sha256']:
        raise ValueError('Eglantine snapshot does not match its source pin')
    digest=hashlib.sha256()
    for name in INPUTS:
        digest.update(name.encode()+b'\0'+(root/name).read_bytes()+b'\0')
    return digest.hexdigest()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--header',type=Path)
    args=parser.parse_args(); value=source_sha()
    if args.header: args.header.write_text('#define NX2_AMPERE_SOURCE_SHA "'+value+'"\n')
    else: print(value)

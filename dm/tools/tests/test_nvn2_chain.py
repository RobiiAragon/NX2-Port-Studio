"""NVN2 transport bounds, exact identities, resource gates and source attestation."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'dm/gfx/tools'))
import dmsh_nvn2
import dmsh_chain


def record(stage=0):
    code=struct.pack('<32I',(1 if stage==0 else 5)<<10,*([0]*31))+struct.pack('<4I',0x794d,0,0x03800000,0)
    control=bytearray(0xd8); struct.pack_into('<III',control,0,0x98761234,201,11)
    struct.pack_into('<I',control,0x30,len(control)); struct.pack_into('<I',control,0x38,len(code)); control[0x56]=stage
    struct.pack_into('<II',control,0xbc,0xd8,0)
    result=bytearray(b'BTCDBTCD'+bytes(48)); struct.pack_into('<II',result,40,len(control),len(code))
    return bytes(result)+code+control,hashlib.sha256(control+code).hexdigest()[:16]


class NativeChain(unittest.TestCase):
    def test_exact_native_lengths_and_identity(self):
        data,key=record(); shader=dmsh_nvn2.decode_record(key,'vs_authored.bin',data)
        self.assertEqual(shader.key,key); self.assertTrue(dmsh_nvn2.is_nvn2_record(data))
        for malformed in (data[:-1],data+b'padding',data[:40]+struct.pack('<I',0xfffffff)+data[44:]):
            with self.assertRaises(ValueError): dmsh_nvn2.sections(malformed)
        with self.assertRaises(ValueError): dmsh_nvn2.decode_record('0'*16,'vs_authored.bin',data)
        with self.assertRaises(ValueError): dmsh_nvn2.decode_record(key,'ps_authored.bin',data)

    def test_unsupported_resources_remain_closed(self):
        self.assertIsNone(dmsh_nvn2.resource_error({'cbufs':[{'slot':3},{'slot':4}]}))
        for resources in ({'cbufs':[{'slot':1}]},{'cbufs':[{'slot':2}]},{'sbufs':[{}]},{'textures':[{}]},{'images':[{}]}):
            self.assertIsNotNone(dmsh_nvn2.resource_error(resources))

    def test_adapter_sources_participate_in_chain(self):
        files={str((Path(dmsh_chain.GFX)/name).resolve()) for name in dmsh_chain.FILES}
        self.assertTrue({str((ROOT/name).resolve()) for name in dmsh_nvn2.DEPENDENCIES}<=files)
        before=dmsh_chain.version(); real_open=open
        def changed(path,mode='r',*args,**kwargs):
            stream=real_open(path,mode,*args,**kwargs)
            if str(path).endswith('ampere_branch_compat.h') and mode=='rb':
                data=stream.read(); stream.close(); return io.BytesIO(data+b'\n// authored cache invalidation test\n')
            return stream
        with mock.patch('builtins.open',changed): self.assertNotEqual(before,dmsh_chain.version())

    @unittest.skipUnless(dmsh_nvn2.lowerer_path().is_file(),'native lowerer not built')
    def test_current_tool_and_stale_source_rejection(self):
        fingerprints=dmsh_nvn2.verified_lowerer(); self.assertEqual(len(fingerprints['ampere_source']),64)
        with mock.patch.object(dmsh_nvn2.subprocess,'run',return_value=mock.Mock(returncode=0,stdout='stale-source\n')):
            with self.assertRaisesRegex(ValueError,'current pinned sources'): dmsh_nvn2.verified_lowerer()

    @unittest.skipUnless(dmsh_nvn2.lowerer_path().is_file(),'native lowerer not built')
    def test_authored_native_job_uses_audited_graphics_transport(self):
        data,key=record()
        with tempfile.TemporaryDirectory() as temp:
            job,c1=dmsh_nvn2.make_job(key,'vs_authored.bin',data,temp,{})
            self.assertEqual(job['nvn2_tools'],dmsh_nvn2.verified_lowerer()); self.assertTrue(job['nvn2']); self.assertEqual(c1,b''); self.assertEqual(job['stage'],'vs')
            self.assertTrue(Path(job['code']).is_file())
            with self.assertRaisesRegex(ValueError,'changed during'):
                dmsh_nvn2.make_job(key,'vs_authored.bin',data,temp,{},expected_tools={'ampere_binary':'stale'})

if __name__=='__main__': unittest.main()

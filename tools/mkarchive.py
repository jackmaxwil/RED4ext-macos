#!/usr/bin/env python3
# Build a one-file RDAR archive: target resource path -> the data of a source resource copied from a game archive.
# Usage: mkarchive.py SOURCE.archive SOURCE_PATH TARGET_PATH OUT.archive  (used by cp-regress for the load-order test)
import struct, sys
def fnv(p):
    h=0xcbf29ce484222325
    for b in p.lower().encode():
        h^=b; h=(h*0x100000001b3)&0xFFFFFFFFFFFFFFFF
    return h
def crc64xz(data):
    rp=0xC96C5795D7870F42; c=0xFFFFFFFFFFFFFFFF
    for b in data:
        c^=b
        for _ in range(8): c=(c>>1)^rp if c&1 else c>>1
    return c^0xFFFFFFFFFFFFFFFF
def read_entry(archive, path):
    d=open(archive,'rb').read()
    _,_,ip,isz=struct.unpack_from('<4sIQI',d,0); idx=d[ip:ip+isz]
    _,_,_,nf,ns,nd=struct.unpack_from('<IIQIII',idx,0)
    h=fnv(path); so=28+nf*56; do=so+ns*16
    for i in range(nf):
        e=struct.unpack_from('<QQIIIII',idx,28+i*56)
        if e[0]==h:
            sha=idx[28+i*56+36:28+i*56+56]
            segs=[struct.unpack_from('<QII',idx,so+j*16) for j in range(e[3],e[4])]
            deps=[struct.unpack_from('<Q',idx,do+j*8)[0] for j in range(e[5],e[6])]
            blobs=[d[o:o+z] for o,z,s in segs]
            return e, sha, segs, deps, blobs
    raise SystemExit('not found: '+path)
src_archive, src_path, target_path, out = sys.argv[1:5]
e, sha, segs, deps, blobs = read_entry(src_archive, src_path)
data=bytearray(b'\0'*0x1000)  # header + padding; data from 0x1000
newsegs=[]
for (o,z,s),b in zip(segs,blobs):
    newsegs.append((len(data),z,s)); data+=b
while len(data)%0x1000: data+=b'\0'
ip=len(data)
entry=struct.pack('<QQIIIII',fnv(target_path),e[1],e[2],0,len(newsegs),0,len(deps))+sha
segtab=b''.join(struct.pack('<QII',*s) for s in newsegs)
deptab=b''.join(struct.pack('<Q',x) for x in deps)
tail=struct.pack('<III',1,len(newsegs),len(deps))+entry+segtab+deptab
idx=struct.pack('<IIQ',8,len(tail)+16-8,crc64xz(tail))+tail
data+=idx
while len(data)%0x1000: data+=b'\0'
struct.pack_into('<4sIQIQIQ',data,0,b'RDAR',12,ip,len(idx),0,0,len(data))
open(out,'wb').write(data)
print(out, len(data), 'segments', len(newsegs), 'deps', len(deps))

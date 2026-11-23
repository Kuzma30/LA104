import gzip,struct,sys
def load(fn):
    d=gzip.open(fn).read()
    if d[:2]==b'\x36\x04':
        mode,h=d[2],d[3]; n=512 if mode&1 else 256; w=8; hs=4; bpg=h; uni=mode&2
        glyphs=[d[hs+i*h:hs+(i+1)*h] for i in range(n)]
        p=hs+n*h; m={}
        for i in range(n):
            while True:
                u,=struct.unpack('<H',d[p:p+2]); p+=2
                if u==0xFFFF: break
                if u==0xFFFE: continue
                m.setdefault(u,i)
        return glyphs,m,w,h
    magic,ver,hs,flags,n,bpg,h,w=struct.unpack('<8I',d[:32])
    glyphs=[d[hs+i*bpg:hs+(i+1)*bpg] for i in range(n)]
    p=hs+n*bpg; m={}
    if flags&1:
        i=0
        while i<n and p<len(d):
            e=d.index(b'\xff',p); seq=d[p:e]; p=e+1
            for part in seq.split(b'\xfe')[:1]:
                s=part.decode('utf8','ignore')
                for ch in s: m.setdefault(ord(ch),i)
            i+=1
    return glyphs,m,w,h

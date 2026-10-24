import sys,struct,os
def i24le(b): return b[0]|(b[1]<<8)|(b[2]<<16)
def i24be(b): return (b[0]<<16)|(b[1]<<8)|b[2]
class Stfs:
    def __init__(s,p):
        s.f=open(p,'rb'); h=s.f.read(0x1000)
        s.hsz=struct.unpack('>I',h[0x340:0x344])[0]
        s.base=(s.hsz+0xFFF)&~0xFFF
        vd=h[0x379:0x379+0x24]
        s.sep=vd[2]; s.sex=(~s.sep)&1
        s.ftcount=struct.unpack('<H',vd[3:5])[0]
        s.ftblock=i24le(vd[5:8])
        s.step=(0xAB,0x718F) if s.sex==0 else (0xAC,0x723A)
    def dbn(s,b):
        r=(((b+0xAA)//0xAA)<<s.sex)+b
        if b<0xAA: return r
        if b<0x70E4: return r+(((b+0x70E4)//0x70E4)<<s.sex)
        return (1<<s.sex)+r+(((b+0x70E4)//0x70E4)<<s.sex)
    def addr(s,b): return (s.dbn(b)<<12)+s.base
    def h0(s,b):
        if b<0xAA: n=0
        else:
            n=(b//0xAA)*s.step[0]+(((b//0x70E4)+1)<<s.sex)
            if b//0x70E4: n+=(1<<s.sex)
        return (n<<12)+s.base
    def nextb(s,b):
        a=s.h0(b)+(b%0xAA)*0x18
        s.f.seek(a); e=s.f.read(0x18)
        return i24be(e[0x15:0x18])
    def readblk(s,b):
        s.f.seek(s.addr(b)); return s.f.read(0x1000)
    def files(s):
        out=[];b=s.ftblock
        for i in range(s.ftcount):
            d=s.readblk(b)
            for j in range(0,0x1000,0x40):
                e=d[j:j+0x40]; fl=e[0x28]
                if fl&0x3F==0: continue
                out.append(dict(name=e[:fl&0x3F].decode('latin1'),dir=bool(fl&0x80),contig=bool(fl&0x40),
                    nblocks=i24le(e[0x29:0x2C]),start=i24le(e[0x2F:0x32]),parent=struct.unpack('>h',e[0x32:0x34])[0],
                    size=struct.unpack('>I',e[0x34:0x38])[0]))
            b=s.nextb(b) if i<s.ftcount-1 else b
        return out
    def path(s,fs,i):
        n=fs[i]['name'];p=fs[i]['parent']
        while p!=-1: n=fs[p]['name']+'/'+n; p=fs[p]['parent']
        return n
    def extract(s,e,dst):
        rem=e['size'];b=e['start']
        with open(dst,'wb') as o:
            while rem>0:
                d=s.readblk(b)[:min(0x1000,rem)]; o.write(d); rem-=len(d)
                b = b+1 if e['contig'] else s.nextb(b)
if __name__=='__main__':
    st=Stfs(sys.argv[1]); fs=st.files()
    print('hdr',hex(st.hsz),'sep',st.sep,'ftcount',st.ftcount,'ftblock',st.ftblock,'nfiles',len(fs))
    if len(sys.argv)==2:
        for i,e in enumerate(fs):
            print(('D ' if e['dir'] else 'F ')+st.path(fs,i), e['size'], 'contig' if e['contig'] else '')
    else:
        want=sys.argv[2]; dst=sys.argv[3]
        for i,e in enumerate(fs):
            if not e['dir'] and (want=='*' or st.path(fs,i)==want):
                p=os.path.join(dst,st.path(fs,i)); os.makedirs(os.path.dirname(p),exist_ok=True); st.extract(e,p); print('->',p)

def extract_split(st,e,dst,part=350*1024*1024):
    rem=e['size'];b=e['start'];n=0;o=None;w=0
    while rem>0:
        if o is None or w>=part:
            if o: o.close()
            o=open(f'{dst}.part{n:02d}','wb');n+=1;w=0
        d=st.readblk(b)[:min(0x1000,rem)]; o.write(d); rem-=len(d); w+=len(d)
        b=b+1 if e['contig'] else st.nextb(b)
    o.close(); return n

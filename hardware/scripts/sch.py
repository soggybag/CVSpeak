import uuid,math
from sx import *
from lib import cached,pins
U=lambda: str(uuid.uuid4())
def fnt(sz=1.27,bold=False):
    f=[Sym('font'),[Sym('size'),sz,sz]]
    if bold: f.append([Sym('bold'),Sym('yes')])
    return f
class Sheet:
    def __init__(s,project,paper='A3'):
        s.project=project;s.uuid=U();s.paper=paper
        s.libs={};s.syms=[];s.wires=[];s.labels=[];s.items=[];s.pinpts=[];s.pwr=0
        s.parts={}  # ref -> {pin: (x,y)}
    def _lib(s,lib,name):
        k=f'{lib}:{name}'
        if k not in s.libs: s.libs[k]=cached(lib,name)
        return k
    @staticmethod
    def xf(px,py,x,y,rot,mirror):
        bx,by=px,-py
        r=math.radians(rot)
        rx=bx*math.cos(r)+by*math.sin(r); ry=-bx*math.sin(r)+by*math.cos(r)
        if mirror=='y': rx=-rx
        if mirror=='x': ry=-ry
        return (round(x+rx,3),round(y+ry,3))
    def part(s,ref,lib,name,value,x,y,rot=0,unit=1,mirror=None,fp='',lcsc=None,desc=None,text='r',hideval=False,extra=None,bom=True):
        k=s._lib(lib,name)
        allp=pins(lib,name)
        pp={}
        for num,lst in allp.items():
            for (u,px,py,a,nm) in lst:
                if u in (0,unit): pp[num]=s.xf(px,py,x,y,rot,mirror)
        s.parts.setdefault(ref,{}).update(pp)
        for v in pp.values(): s.pinpts.append(v)
        # text positions
        if text=='r': rp,vp,j=(x+2.54,y-1.27),(x+2.54,y+1.27),'left'
        elif text=='l': rp,vp,j=(x-2.54,y-1.27),(x-2.54,y+1.27),'right'
        elif text=='t': rp,vp,j=(x,y-5.08),(x,y-2.79),None
        elif text=='b': rp,vp,j=(x,y+2.79),(x,y+5.08),None
        elif text=='tt': rp,vp,j=(x,y-12.7),(x,y-10.43),None
        elif text=='ic': rp,vp,j=(x+2.54,y-6.35),(x+2.54,y-4.08),'left'
        elif text=='555': rp,vp,j=(x-7.62,y-12.7),(x-7.62,y-10.43),'left'
        else: rp,vp,j=text
        fang=90 if rot in (90,270) else 0
        def prop(n,v,at,hide=False,just=None):
            e=[Sym('effects'),fnt()]
            if just and ((rot==180) != (mirror=='y')): just={'left':'right','right':'left'}[just]
            if just: e.append([Sym('justify'),Sym(just)])
            p=[Sym('property'),n,v,[Sym('at'),at[0],at[1],fang]]
            if hide: p.append([Sym('hide'),Sym('yes')])
            p.append(e); return p
        props=[prop('Reference',ref,rp,just=j),prop('Value',value,vp,hide=hideval,just=j),
               prop('Footprint',fp,(x,y),True),prop('Datasheet','',(x,y),True),prop('Description',desc or '',(x,y),True)]
        if lcsc: props.append(prop('LCSC',lcsc,(x,y),True))
        for n,v in (extra or {}).items(): props.append(prop(n,v,(x,y),True))
        at=[Sym('at'),x,y,rot]
        e=[Sym('symbol'),[Sym('lib_id'),k],at]
        if mirror: e.append([Sym('mirror'),Sym(mirror)])
        e+= [[Sym('unit'),unit],[Sym('exclude_from_sim'),Sym('no')],[Sym('in_bom'),Sym('yes' if bom else 'no')],[Sym('on_board'),Sym('yes')]]+([] if bom else [[Sym('in_pos_files'),Sym('no')]])+[[Sym('dnp'),Sym('no')],[Sym('uuid'),U()]]
        e+=props
        for num in pp: e.append([Sym('pin'),num,[Sym('uuid'),U()]])
        e.append([Sym('instances'),[Sym('project'),s.project,[Sym('path'),'/'+s.uuid,[Sym('reference'),ref],[Sym('unit'),unit]]]])
        s.syms.append(e)
        return pp
    def P(s,ref,pin): return s.parts[ref][pin]
    def power(s,kind,pt,d=None):
        """d = direction the symbol points: 'up' / 'down' / 'left' / 'right' (default: down for GND, up otherwise)"""
        k=s._lib('power',kind); s.pwr+=1; ref='#PWR%03d'%s.pwr
        x,y=pt
        gnd=kind in ('GND','GNDA')
        d=d or ('down' if gnd else 'up')
        rot=({'down':0,'right':90,'up':180,'left':270} if gnd else {'up':0,'left':90,'down':180,'right':270})[d]
        vat={'up':(x,y-3.81),'down':(x,y+3.81),'left':(x-3.81,y),'right':(x+3.81,y)}[d]
        jl,jr=('left','right') if rot==90 else ('right','left')     # KiCad mirrors field justify on a 90-degree symbol
        vj={'left':[[Sym('justify'),Sym(jl)]],'right':[[Sym('justify'),Sym(jr)]]}.get(d,[])
        vang=90 if rot in (90,270) else 0
        e=[Sym('symbol'),[Sym('lib_id'),k],[Sym('at'),x,y,rot],[Sym('unit'),1],[Sym('exclude_from_sim'),Sym('no')],[Sym('in_bom'),Sym('yes')],[Sym('on_board'),Sym('yes')],[Sym('dnp'),Sym('no')],[Sym('uuid'),U()],
           [Sym('property'),'Reference',ref,[Sym('at'),x,y,0],[Sym('hide'),Sym('yes')],[Sym('effects'),fnt()]],
           [Sym('property'),'Value',kind,[Sym('at'),vat[0],vat[1],vang],[Sym('effects'),fnt()]+vj],
           [Sym('property'),'Footprint','',[Sym('at'),x,y,0],[Sym('hide'),Sym('yes')],[Sym('effects'),fnt()]],
           [Sym('property'),'Datasheet','',[Sym('at'),x,y,0],[Sym('hide'),Sym('yes')],[Sym('effects'),fnt()]],
           [Sym('property'),'Description','',[Sym('at'),x,y,0],[Sym('hide'),Sym('yes')],[Sym('effects'),fnt()]],
           [Sym('pin'),'1',[Sym('uuid'),U()]],
           [Sym('instances'),[Sym('project'),s.project,[Sym('path'),'/'+s.uuid,[Sym('reference'),ref],[Sym('unit'),1]]]]]
        s.syms.append(e); s.pinpts.append(pt)
    def wire(s,*pts):
        pts=[tuple(round(v,3) for v in p) for p in pts]
        for a,b in zip(pts,pts[1:]):
            if a==b: continue
            assert a[0]==b[0] or a[1]==b[1], f'non-orthogonal wire {a}->{b}'
            s.wires.append((a,b))
    def L(s,a,b,first='h'):
        """L-shaped route a->b"""
        if a[0]==b[0] or a[1]==b[1]: s.wire(a,b)
        elif first=='h': s.wire(a,(b[0],a[1]),b)
        else: s.wire(a,(a[0],b[1]),b)
    def label(s,name,pt,side='r'):
        ang,j=(0,'left') if side=='r' else (180,'right')
        s.labels.append([Sym('label'),name,[Sym('at'),pt[0],pt[1],ang],[Sym('effects'),fnt(),[Sym('justify'),Sym(j),Sym('bottom')]],[Sym('uuid'),U()]])
        s.pinpts.append(pt)
    def text(s,t,x,y,size=1.27,bold=False):
        s.items.append([Sym('text'),t,[Sym('exclude_from_sim'),Sym('no')],[Sym('at'),x,y,0],[Sym('effects'),fnt(size,bold),[Sym('justify'),Sym('left'),Sym('bottom')]],[Sym('uuid'),U()]])
    def box(s,title,x1,y1,x2,y2):
        s.items.append([Sym('rectangle'),[Sym('start'),x1,y1],[Sym('end'),x2,y2],[Sym('stroke'),[Sym('width'),0.254],[Sym('type'),Sym('dash')]],[Sym('fill'),[Sym('type'),Sym('none')]],[Sym('uuid'),U()]])
        s.text(title,x1+1.27,y1+3.81,2,True)
    def noconnect(s,pt):
        s.items.append([Sym('no_connect'),[Sym('at'),pt[0],pt[1]],[Sym('uuid'),U()]])
    def junctions(s):
        ends={}
        for a,b in s.wires:
            for p in (a,b): ends[p]=ends.get(p,0)+1
        for p in s.pinpts: ends[p]=ends.get(p,0)+1
        J=set(p for p,c in ends.items() if c>=3)
        def inside(p,a,b):
            if p in (a,b): return False
            if a[0]==b[0]==p[0]: return min(a[1],b[1])<p[1]<max(a[1],b[1])
            if a[1]==b[1]==p[1]: return min(a[0],b[0])<p[0]<max(a[0],b[0])
            return False
        for p in list(ends):
            if any(inside(p,a,b) for a,b in s.wires): J.add(p)
        return J
    def write(s,path,title_block=None):
        out=[Sym('kicad_sch'),[Sym('version'),20250114],[Sym('generator'),'eeschema'],[Sym('generator_version'),'9.0'],[Sym('uuid'),s.uuid],[Sym('paper'),s.paper]]
        if title_block:
            tb=[Sym('title_block')]
            for k,v in title_block.items():
                if k=='comments':
                    for i,c in enumerate(v,1): tb.append([Sym('comment'),i,c])
                else: tb.append([Sym(k),v])
            out.append(tb)
        out.append([Sym('lib_symbols')]+list(s.libs.values()))
        for p in sorted(s.junctions()): out.append([Sym('junction'),[Sym('at'),p[0],p[1]],[Sym('diameter'),0],[Sym('color'),0,0,0,0],[Sym('uuid'),U()]])
        # split wires at every connection point on their interior (pins, labels, other wire ends)
        R3=lambda p:(round(p[0],3),round(p[1],3))
        pts=set(R3(p) for p in s.pinpts)
        for a,b in s.wires: pts.add(R3(a)); pts.add(R3(b))
        segs=[]
        for a,b in [(R3(a),R3(b)) for a,b in s.wires]:
            if a[0]==b[0]:
                inner=sorted([p for p in pts if p[0]==a[0] and min(a[1],b[1])<p[1]<max(a[1],b[1])],key=lambda p:p[1])
                chain=[a]+(inner if a[1]<b[1] else inner[::-1])+[b]
            else:
                inner=sorted([p for p in pts if p[1]==a[1] and min(a[0],b[0])<p[0]<max(a[0],b[0])],key=lambda p:p[0])
                chain=[a]+(inner if a[0]<b[0] else inner[::-1])+[b]
            segs+=list(zip(chain,chain[1:]))
        for a,b in segs: out.append([Sym('wire'),[Sym('pts'),[Sym('xy'),a[0],a[1]],[Sym('xy'),b[0],b[1]]],[Sym('stroke'),[Sym('width'),0],[Sym('type'),Sym('default')]],[Sym('uuid'),U()]])
        out+=s.labels+s.items+s.syms
        out.append([Sym('sheet_instances'),[Sym('path'),'/',[Sym('page'),'1']]])
        out.append([Sym('embedded_fonts'),Sym('no')])
        open(path,'w').write(dump(out)+'\n')

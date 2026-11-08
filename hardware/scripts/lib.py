import copy,os,math
from sx import *
SYMDIR='/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols'
_cache={}
def _libfile(lib):
    if lib not in _cache:
        f=f'{lib}.kicad_sym' if os.path.exists(f'{lib}.kicad_sym') else f'{SYMDIR}/{lib}.kicad_sym'   # project library first (run from the project folder)
        _cache[lib]=parse(open(f).read())
    return _cache[lib]
def raw(lib,name):
    for s in find(_libfile(lib),'symbol'):
        if s[1]==name: return copy.deepcopy(s)
    raise KeyError(name)
def flat(lib,name):
    """return symbol flattened (extends resolved), named lib:name"""
    s=raw(lib,name)
    ext=find1(s,'extends')
    if ext:
        parent=flat(lib,ext[1]); pname=parent[1].split(':')[-1]
        props={p[1]:p for p in find(s,'property')}
        out=[Sym('symbol'),name]
        for x in parent[2:]:
            if isinstance(x,list) and x[0]=='property' and x[1] in props: out.append(props.pop(x[1]))
            elif isinstance(x,list) and x[0]=='symbol':
                y=copy.deepcopy(x); y[1]=name+y[1][len(pname):]; out.append(y)
            else: out.append(x)
        # remaining child props inserted before first unit
        idx=next(i for i,x in enumerate(out) if isinstance(x,list) and x[0]=='symbol')
        for p in props.values(): out.insert(idx,p); idx+=1
        s=out
    return s
def cached(lib,name):
    s=flat(lib,name); s=copy.deepcopy(s); s[1]=f'{lib}:{name}'; return s
def pins(lib,name):
    """{(unit,number): (x,y,angle,name)} in lib coords"""
    s=flat(lib,name); out={}
    for u in find(s,'symbol'):
        parts=u[1].rsplit('_',2); unit=int(parts[1])
        for p in find(u,'pin'):
            at=find1(p,'at'); num=find1(p,'number')[1]; nm=find1(p,'name')[1]
            out.setdefault(num,[]).append((unit,float(at[1]),float(at[2]),float(at[3]) if len(at)>3 else 0,nm))
    return out

# minimal s-expression parser / writer for KiCad files
import re
class Sym(str): pass
def parse(text):
    tok=re.compile(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+')
    stack=[[]]
    for m in tok.finditer(text):
        t=m.group(0)
        if t=='(': stack.append([])
        elif t==')':
            e=stack.pop(); stack[-1].append(e)
        elif t[0]=='"': stack[-1].append(t[1:-1].replace('\\"','"').replace('\\\\','\\'))
        else: stack[-1].append(Sym(t))
    return stack[0][0]
def q(s): return '"'+s.replace('\\','\\\\').replace('"','\\"').replace('\n','\\n')+'"'
def dump(e,ind=0):
    if isinstance(e,list):
        simple=all(not isinstance(x,list) for x in e)
        if simple: return '('+' '.join(dump(x) for x in e)+')'
        head=[];rest=[]
        for x in e:
            (rest if (isinstance(x,list) or rest) else head).append(x)
        s='('+' '.join(dump(x) for x in head)
        for x in rest: s+='\n'+'\t'*(ind+1)+dump(x,ind+1)
        return s+'\n'+'\t'*ind+')'
    if isinstance(e,Sym): return str(e)
    if isinstance(e,(int,float)):
        v=round(float(e),4); return ('%d'%v) if v==int(v) else ('%.4f'%v).rstrip('0')
    return q(e)
def find(e,key): return [x for x in e if isinstance(x,list) and x and x[0]==key]
def find1(e,key):
    r=find(e,key); return r[0] if r else None

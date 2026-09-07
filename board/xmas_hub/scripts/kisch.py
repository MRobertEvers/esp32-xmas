"""Minimal s-expression parser + helpers for KiCad symbol libs / schematics."""
import re
def parse(text):
    tokens=re.findall(r'"(?:[^"\\]|\\.)*"|\(|\)|[^\s()"]+',text)
    stack=[[]]
    for t in tokens:
        if t=='(':
            stack.append([])
        elif t==')':
            node=stack.pop(); stack[-1].append(node)
        else:
            if t.startswith('"'): t=t[1:-1].replace('\\"','"')
            stack[-1].append(t)
    return stack[0][0]
def find(node,key):
    return [n for n in node if isinstance(n,list) and n and n[0]==key]
def find1(node,key):
    r=find(node,key); return r[0] if r else None
def extract_symbol_text(path,name):
    txt=open(path).read()
    key='(symbol "%s"'%name
    i=txt.find(key)
    if i<0: raise KeyError(name)
    depth=0;j=i
    while True:
        c=txt[j]
        if c=='(': depth+=1
        elif c==')':
            depth-=1
            if depth==0: return txt[i:j+1]
        elif c=='"':
            j+=1
            while txt[j]!='"':
                if txt[j]=='\\': j+=1
                j+=1
        j+=1
def symbol_pins(symtext):
    """Return list of (number, name, x, y, rot, length, etype) in library coords (y up)."""
    node=parse(symtext) if isinstance(symtext,str) else symtext; pins=[]
    def walk(n):
        for c in n:
            if isinstance(c,list):
                if c and c[0]=='pin':
                    at=find1(c,'at'); ln=find1(c,'length'); nm=find1(c,'name'); num=find1(c,'number')
                    pins.append((num[1],nm[1],float(at[1]),float(at[2]),float(at[3]) if len(at)>3 else 0,float(ln[1]),c[1]))
                else: walk(c)
    walk(node); return pins
def pin_pos(ax,ay,rot,mirror,px,py):
    """Schematic position of a pin at lib coords (px,py) for a symbol placed at (ax,ay) with rotation rot (deg CCW) and mirror ('x','y' or None)."""
    x,y=px,py
    if mirror=='x': y=-y
    if mirror=='y': x=-x
    r=rot%360
    if r==90: x,y=-y,x
    elif r==180: x,y=-x,-y
    elif r==270: x,y=y,-x
    return (round(ax+x,4), round(ay-y,4))

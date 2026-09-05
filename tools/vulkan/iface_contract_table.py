#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Census of the eleven renderer interfaces, per renderer family.

Produces the raw material for `plans/plan_vulkan.md` §31 (Appendix A, `VULKAN-027`):
every `virtual` declared between the interface class boundaries of
`IGraphicsRenderer.hpp`, and whether EasyGL and Vulkan each declare an `override`
for it.

Two traps this script exists to avoid, both of which produced wrong counts before
they were caught:

  * a declaration whose parameter list carries a braced default (`= {}`) defeats a
    naive ``[^;{]*override`` pattern, so a real override is reported as missing;
  * the word "virtual" in prose (``/// the game's own virtual resolution``)
    manufactures methods that do not exist, which is why comment lines are skipped.

Usage:  python3 tools/vulkan/iface_contract_table.py
"""

import re, os, glob
ROOT=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HDR=os.path.join(ROOT,"modules/graphics/include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp")
IFACES=["IGraphicsRenderer","IVertexBufferRenderer","IIndexBufferRenderer","ITextureRenderer",
        "ITexture3DRenderer","ITextureCubeRenderer","IRenderTargetRenderer","IRenderTargetCubeRenderer",
        "IEffectRenderer","ISpriteBatchRenderer","IOcclusionQueryRenderer"]
src=open(HDR).read().splitlines()
starts={}
for i,l in enumerate(src):
    m=re.match(r'\s*class\s+(\w+)\b',l)
    if m and m.group(1) in IFACES: starts.setdefault(m.group(1),i)
# class boundaries: a class ends where the NEXT top-level class (any name) begins
classline=[(i,re.match(r'\s*class\s+(\w+)\b',l).group(1)) for i,l in enumerate(src) if re.match(r'\s*class\s+(\w+)\b',l)]
bounds=[]
for idx,(ln,nm) in enumerate(classline):
    if nm not in IFACES: continue
    end=classline[idx+1][0] if idx+1<len(classline) else len(src)
    bounds.append((nm,ln,end))
rows=[]
for name,a,b in bounds:
    body=src[a:b]
    for j,l in enumerate(body):
        if not re.search(r'\bvirtual\b', l) or re.match(r'\s*(///|//|\*)', l): continue
        decl=l.strip(); k=j
        while ';' not in decl and '{' not in decl and k+1<len(body):
            k+=1; decl+=" "+body[k].strip()
        if '~' in decl: continue
        mm=re.search(r'(\w+)\s*\(', decl)
        if not mm: continue
        meth=mm.group(1)
        if meth in ("virtual","operator"): continue
        rows.append((name,meth,bool(re.search(r'=\s*0\s*;',decl)),a+j+1))
seen=set(); out=[]
for r in rows:
    if (r[0],r[1]) in seen: continue
    seen.add((r[0],r[1])); out.append(r)

def load(tree):
    txt=[]
    for p in glob.glob(os.path.join(tree,"**","*.[ch]pp"),recursive=True):
        if "/examples/" in p or "/tests/" in p: continue
        try: txt.append(re.sub(r'\s+',' ',open(p,errors="ignore").read()))
        except Exception: pass
    return " ".join(txt)
EG=load(os.path.join(ROOT,"modules/renderers/easygl"))
VK=load(os.path.join(ROOT,"modules/renderers/vulkan"))
def has(txt,m): return bool(re.search(r'\b%s\s*\([^;]{0,800}?\boverride\b'%re.escape(m), txt))
print("iface\tmethod\tkind\tline\teasygl\tvulkan")
for name,meth,pure,ln in out:
    print(f"{name}\t{meth}\t{'PURE' if pure else 'default'}\t{ln}\t{'Y' if has(EG,meth) else '-'}\t{'Y' if has(VK,meth) else '-'}")

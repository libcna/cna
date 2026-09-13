#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Census of all renderer interfaces, per renderer family.

Produces the raw material for `plans/plan_vulkan.md` §31 (Appendix A, `VULKAN-027`):
every `virtual` declared between the interface class boundaries of
`IGraphicsRenderer.hpp`, and whether EasyGL and Vulkan each declare an `override`
for it.

`VULKAN-268` extends the original eleven-interface census over the five modern
resource/compute/timer interfaces.  Interface names are discovered from the
contract header instead of kept in another hand-maintained list.  The implementation
check searches only the body of a class that actually derives from the queried
interface; without that boundary an unrelated `SetData` override made an absent
texture-array implementation look real.

Two traps this script exists to avoid, both of which produced wrong counts before
they were caught:

  * a declaration whose parameter list carries a braced default (`= {}`) defeats a
    naive ``[^;{]*override`` pattern, so a real override is reported as missing;
  * the word "virtual" in prose (``/// the game's own virtual resolution``)
    manufactures methods that do not exist, which is why comment lines are skipped.

Usage:  python3 tools/vulkan/iface_contract_table.py
"""

import glob
import os
import re
ROOT=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HDR=os.path.join(ROOT,"modules/graphics/include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp")
src=open(HDR).read().splitlines()
IFACES=sorted({m.group(1) for line in src
               if (m := re.match(r'\s*class\s+(I\w+Renderer)\b', line))})
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

def load_implementations(tree):
    implementations={iface: [] for iface in IFACES}
    class_re=re.compile(r'\bclass\s+\w+(?:\s+final)?\s*:\s*([^\{;]+)\{', re.DOTALL)
    for path in glob.glob(os.path.join(tree,"**","*.[ch]pp"),recursive=True):
        if "/examples/" in path or "/tests/" in path: continue
        try: text=open(path,errors="ignore").read()
        except Exception: continue
        for match in class_re.finditer(text):
            bases=match.group(1)
            inherited=[iface for iface in IFACES if re.search(
                r'\bpublic\s+(?:[A-Za-z_]\w*::)*%s\b' % re.escape(iface), bases)]
            if not inherited: continue
            opening=match.end()-1
            depth=0
            closing=None
            for pos in range(opening,len(text)):
                if text[pos]=='{': depth+=1
                elif text[pos]=='}':
                    depth-=1
                    if depth==0:
                        closing=pos
                        break
            if closing is None: continue
            body=re.sub(r'\s+',' ',text[opening+1:closing])
            for iface in inherited: implementations[iface].append(body)
    return implementations
EG=load_implementations(os.path.join(ROOT,"modules/renderers/easygl"))
VK=load_implementations(os.path.join(ROOT,"modules/renderers/vulkan"))
def has(implementations, iface, method):
    return any(re.search(
        r'\b%s\s*\([^;]{0,800}?\boverride\b' % re.escape(method), body)
        for body in implementations[iface])
print("iface\tmethod\tkind\tline\teasygl\tvulkan")
for name,meth,pure,ln in out:
    print(f"{name}\t{meth}\t{'PURE' if pure else 'default'}\t{ln}\t"
          f"{'Y' if has(EG,name,meth) else '-'}\t{'Y' if has(VK,name,meth) else '-'}")

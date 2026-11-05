# Self and inclusive instruction counts per called routine, from a tg_headless --trace-out file.
# Usage: python3 tools/tg_profile.py trace.txt
import sys,collections
self_=collections.Counter(); calls=collections.Counter(); incl=collections.Counter()
stack=['top']; pend=None
for line in open(sys.argv[1]):
    if line[0] in '# ': continue
    adr=int(line[:6],16)&0x7FFFFF; op=line[7:9]
    if pend is not None:
        if pend=='call': stack.append(adr); calls[adr]+=1
        elif pend=='ret' and len(stack)>1: stack.pop()
        pend=None
    self_[stack[-1]]+=1
    for f in set(stack): incl[f]+=1
    if op in('20','22','FC'): pend='call'
    elif op in('60','6B'): pend='ret'
tot=sum(self_.values())
def n(a): return a if a=='top' else '%02X:%04X'%(a>>16,a&0xFFFF)
print('total',tot)
print('-- self'); [print(n(a),c,calls[a]) for a,c in self_.most_common(40)]
print('-- incl'); [print(n(a),c,calls[a]) for a,c in incl.most_common(40)]

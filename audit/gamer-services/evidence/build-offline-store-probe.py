import pathlib,json,shlex,subprocess
b=pathlib.Path('/rv/data/development/github.com/libcna/cna/cmake-build-debug')
c=next(x for x in json.loads((b/'compile_commands.json').read_text()) if x['file'].endswith('/GamerServicesDataTests.cpp'))
a=shlex.split(c['command']);args=[];i=1
while i<len(a):
 if a[i] in ('-o','-c'):i+=2;continue
 args.append(a[i]);i+=1
subprocess.run([a[0],*args,'-c','/tmp/cna-gs-audit-probe.cpp','-o','/tmp/cna-gs-audit-probe.o'],cwd=b,check=True)
a=shlex.split((b/'CMakeFiles/CnaGamerServicesTests.dir/link.txt').read_text());args=[];i=1
while i<len(a):
 if a[i]=='-o':i+=2;continue
 if a[i].endswith('.o') or 'gtest_main' in a[i]:i+=1;continue
 args.append(a[i]);i+=1
subprocess.run([a[0],'/tmp/cna-gs-audit-probe.o',*args,'-o','/tmp/cna-gs-audit-probe'],cwd=b,check=True)

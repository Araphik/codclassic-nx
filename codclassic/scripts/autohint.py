import re,subprocess,sys,os
R=os.environ.get('REXGLUE','rexglue')  # path to the rexglue host tool
man='codclassic_manifest.toml'
for it in range(30):
    out=subprocess.run([R,'codegen',man],capture_output=True,text=True)
    txt=out.stdout+out.stderr
    open('../codegen_last.out','w').write(txt)
    if out.returncode==0: print('OK after',it,'iterations'); sys.exit(0)
    m=re.search(r"Analysis failed for '(\w+)'",txt)
    targets=re.findall(r'  (0x[0-9A-F]{8}) from 0x[0-9A-F]{8}: .* - target not in any function',txt)
    if not m or not targets:
        print('Other failure:'); print('\n'.join(l for l in txt.splitlines() if 'ordinal' not in l)[-3000:]); sys.exit(1)
    mod=m.group(1); hf=f'{mod}_hints.toml'
    if not os.path.exists(hf):
        open(hf,'w').write(f'# Function hints for {mod}\n[functions]\n')
        s=open(man).read()
        s=s.replace(f'out_directory_path = "generated/{mod}"\nincludes = []',f'out_directory_path = "generated/{mod}"\nincludes = ["{hf}"]',1)
        open(man,'w').write(s)
    existing=open(hf).read()
    new=[t for t in dict.fromkeys(targets) if t not in existing]
    if not new: print('No progress on',mod,targets); sys.exit(1)
    with open(hf,'a') as f:
        for t in new: f.write(f'{t} = {{}}\n')
    print(mod,'+',len(new),'hints')
print('too many iterations'); sys.exit(1)

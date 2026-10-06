"""Run physical-style TCU and GAM accounting tests, never touching a device."""
from pathlib import Path
import os,subprocess,json,shutil
ROOT=Path(__file__).resolve().parents[2]
out=ROOT/'build/gam4980-profile-host';out.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();compiler=os.environ.get('CC') or shutil.which('gcc')
if not compiler:raise RuntimeError('Set CC to a host C compiler or install gcc')
results=[]
for name in ['profile_clock','profile_accounting','profile_physical_boundary','timing']:
    exe=out/(name+('.exe' if os.name=='nt' else ''))
    subprocess.run([compiler,'-O2','-std=c11','-Wall','-Wextra','-Werror','-I',ROOT/'gam4980/tests/mock',ROOT/'gam4980/tests'/(name+'.c'),'-o',exe],check=True,env=env)
    r=subprocess.run([exe],capture_output=True,text=True,env=env)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    print(r.stdout,end='');results.append({'test':name,'result':r.stdout.strip()})
(out/'report.json').write_text(json.dumps({'all_passed':True,'tests':results},indent=2)+'\n')

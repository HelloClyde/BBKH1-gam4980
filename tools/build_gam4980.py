"""Build an independent GAM4980 native H1 BDA and device installation ZIP."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'gam4980'
SDK=ROOT/'sdk'

def write_zip(z,path,name):
    info=zipfile.ZipInfo(name,(2026,10,6,0,0,0))
    info.compress_type=zipfile.ZIP_DEFLATED
    info.external_attr=0o644 << 16
    z.writestr(info,path.read_bytes())

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--toolchain',type=Path,default=Path(os.environ.get('H1_GNU_BIN',ROOT/'.tools/toolchain/bin')))
    p.add_argument('--test-frames',type=int,default=0)
    p.add_argument('--verbose-log',action='store_true')
    p.add_argument('--profile',action='store_true',help='TCU5 wall-time diagnostic build, dumped on pause/exit')
    args=p.parse_args()
    if not 0<=args.test_frames<=3600:p.error('--test-frames must be 0..3600')
    def tool(name):
        x=args.toolchain/('mipsel-none-elf-'+name)
        if x.with_suffix('.exe').exists():x=x.with_suffix('.exe')
        if not x.is_file():p.error('Missing tool: '+str(x))
        return x.resolve()
    gcc,objcopy=tool('gcc'),tool('objcopy')
    sdk_commit=subprocess.check_output(['git','-C',SDK,'rev-parse','HEAD'],text=True).strip()
    if sdk_commit!='067fe072477861dfc8949d7b1a55279fb92d2548':p.error('Unexpected H1 SDK revision')
    build=ROOT/'build'/(('gam4980-test' if args.test_frames else 'gam4980-release')+('-profile' if args.profile else ''))
    build.mkdir(parents=True,exist_ok=True)
    flags=['-EL','-march=mips32','-mabi=32','-msoft-float','-mno-abicalls','-G0','-fno-pic',
           '-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-strict-aliasing',
           '-ffunction-sections','-fdata-sections','-Wall','-Wextra',
           f'-DH1_TEST_FRAMES={args.test_frames}',f'-DH1_TRACE={int(args.verbose_log)}',f'-DH1_PROFILE={int(args.profile)}',
           f'-ffile-prefix-map={ROOT}=h1-gam4980']
    flags += [x for d in [APP/'src',APP/'src/libc/include',APP/'core',SDK/'sdk/include'] for x in ['-I',str(d)]]
    sources=['src/runtime/entry.S','src/runtime/startup.c','src/libc/freestanding.c',
             'src/platform/diagnostics.c','src/platform/file_selector.c','src/platform/save.c',
             'src/app.c','src/platform/video.c','core/gam4980_core.c','src/platform/profile.c','src/platform/timing.c']
    objects=[]
    for i,s in enumerate(sources):
        obj=build/f'{i}-{Path(s).stem}.o'
        cmd=[gcc,*flags,'-x','assembler-with-cpp'] if s.endswith('.S') else [gcc,*flags,'-std=gnu11']
        if s.startswith('src/'):cmd+=['-Werror']
        print('Compile',s,flush=True)
        subprocess.run([*cmd,'-c',APP/s,'-o',obj],check=True);objects.append(obj)
    elf=build/'H1GAM4980.elf';raw=build/'H1GAM4980.bin'
    subprocess.run([gcc,*flags,'-nostdlib','-Wl,--build-id=none','-Wl,--gc-sections',
                    '-Wl,-T,'+str(APP/'src/runtime/h1.ld'),'-Wl,-Map,'+str(build/'H1GAM4980.map'),
                    '-o',elf,*objects,'-lgcc'],check=True)
    subprocess.run([objcopy,'-O','binary',elf,raw],check=True)
    sys.path.insert(0,str(SDK))
    from h1_bda.header import HeaderFields,encode_header
    from h1_bda.resources import PAYLOAD_OFFSET,RESOURCE_OFFSET,RESOURCE_SIZES,build_icon_resources
    from h1_bda.validate import validate_bda
    icon=APP/'assets/gam4980-icon.png'
    payload=raw.read_bytes();resources=build_icon_resources(icon)
    size=PAYLOAD_OFFSET+len(payload);padding=(-size)&3
    fields=HeaderFields(category=0x48,file_size_minus_4=size+padding-4,payload_offset=PAYLOAD_OFFSET,
                        resource_offset=RESOURCE_OFFSET,resource_sizes=RESOURCE_SIZES)
    data=encode_header(fields,title='GAM4980',build_time='2026-10-06 00:00:00')+resources+payload+bytes(padding)
    dest=build/'H1GAM4980.bda';dest.write_bytes(data)
    report=validate_bda(dest)
    if not report['ok']:raise RuntimeError(report)
    sha=hashlib.sha256(data).hexdigest()
    meta={'title':'GAM4980','version':'0.1.7','sha256':sha,'size':len(data),'sdk_commit':sdk_commit,
          'profile':args.profile,'profile_clock':'TCU5 RTC 32768 Hz; calibration; restore on pause/exit' if args.profile else None,
          'pacing':'RTC-calibrated firmware tick frequency; 60 Hz core; maximum 100 ms catch-up',
          'icon':{'path':'gam4980/assets/gam4980-icon.png','sha256':hashlib.sha256(icon.read_bytes()).hexdigest()},
          'payload_sha256':hashlib.sha256(data[PAYLOAD_OFFSET:]).hexdigest(),
          'core_source':'HelloClyde/BBK9588-gam4980','core_commit':'73b884a056ca0595de1552e6e365138687fb25a1',
          'test_frames':args.test_frames,'formats':['gam'],'runtime_directory':'A:\\gam4980',
          'entry':'0x83c00020','display':'native 480x272 RGB32; 159x96 LCD','audio':'not implemented',
          'display_settings':'explicit size and LCD color option pages; selected marker; parent menu shows current values',
          'display_modes':['native 159x96','aspect 397x240','integer 2x 318x192'],
          'video_optimization':'direct packed LCD to RGB32; dirty rows; cached integer replication; toolbar restored only after menus/window changes',
          'key_logging':'disabled in release; enabled for test frames or verbose log',
          'core_stop':'retain first BRK PC; stop stepping; responsive end menu with restart/change/exit; buffered input history',
          'keyboard_directions':'matrix 26/28 and 40/35 both send game left/right; menu navigation supports both; repair v1 default mappings in v2 config',
          'touch_shortcuts':'four configurable game keys; per-game .hot.s0/.hot.s1; full 59-key selector',
          'save':'per-game CRC-checked alternating .s0/.s1','key_mapping':'physical full keyboard; per-game .cfg.s0/.cfg.s1; no soft keyboard',
          'compiler':subprocess.check_output([gcc,'--version'],text=True).splitlines()[0],
          'source_sha256':{s.relative_to(APP).as_posix():hashlib.sha256(s.read_bytes()).hexdigest() for s in sorted(APP.rglob('*')) if s.is_file() and s.suffix in ('.c','.h','.S','.ld')}}
    dest.with_suffix('.build.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
    dest.with_suffix('.bda.sha256').write_text(sha+'  H1GAM4980.bda\n',encoding='ascii')
    if not args.test_frames:
        import shutil
        dist=ROOT/'dist';dist.mkdir(exist_ok=True)
        name='H1GAM4980'+('-profile' if args.profile else '')
        for suffix in ['.bda','.build.json']:shutil.copyfile(dest.with_suffix(suffix),dist/(name+suffix))
        (dist/(name+'.bda.sha256')).write_text(sha+'  '+name+'.bda\n',encoding='ascii')
        with zipfile.ZipFile(dist/(name+'-install.zip'),'w',zipfile.ZIP_DEFLATED) as z:
            write_zip(z,dest,'应用/程序/H1GAM4980.bda')
            for name in ['8.BIN','E.BIN']:write_zip(z,APP/'runtime'/name,'gam4980/'+name)
            write_zip(z,APP/'README.md','README.md')
    print(json.dumps({'bda':str(dest),'size':len(data),'sha256':sha,'validated':True},indent=2))

if __name__=='__main__':main()

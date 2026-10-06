"""Exercise the actual H1 MIPS BDA, core, runtime ROMs and original GAM fixture."""
from pathlib import Path
import importlib.util
import json
import re
import struct
import sys
import time
import zlib
from PIL import Image
from make_test_game import make_game

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'gam4980/tests'))
spec=importlib.util.spec_from_file_location('h1_mips_harness',ROOT/'gam4980/tests/h1_mock.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
from unicorn.mips_const import UC_MIPS_REG_PC,UC_MIPS_REG_V0,UC_MIPS_REG_SP

class Machine(base.Machine):
    def __init__(self,files=None,path='A:\\gam4980\\测试.gam',selections=None,events=None,bda=None):
        bda=bda or ROOT/'build/gam4980-test/H1GAM4980.bda'
        content={'A:\\gam4980\\8.BIN':(ROOT/'gam4980/runtime/8.BIN').read_bytes(),
                 'A:\\gam4980\\E.BIN':(ROOT/'gam4980/runtime/E.BIN').read_bytes()}
        if path:content[path]=make_game()
        if files is not None:content=files
        self.selections=list(selections) if selections is not None else [path]
        self.script=list(events or [])
        super().__init__(bda,files=content,path=path)
    def select_file(self,directory,extension,output,*args):
        assert not self.gui_active
        entry=(self.string(directory),self.string(extension));self.select_calls.append(entry)
        assert entry[1]=='gam',entry
        path=self.selections.pop(0) if self.selections else None
        self.write(output,(path.encode('gbk') if path else b'')+b'\0');return 0
    def event(self,code,key,*args):
        # Events after first completed game frame, not during loading.
        log=self.files.get('A:\\gam4980\\h1gam.log',b'')
        event=(-1,-1)
        if b'GAME_READY' in log and self.script:
            event=self.script.pop(0)
        self.word(code,event[0]);self.word(key,event[1]);return 0
    def run(self,allow_dialog=False):
        started=time.monotonic()
        self.uc.emu_start(0x83c00020,0,timeout=60_000_000,count=900_000_000)
        assert self.finished,f'No return PC={self.uc.reg_read(UC_MIPS_REG_PC):08x}'
        assert self.uc.reg_read(UC_MIPS_REG_V0)==0
        assert self.uc.reg_read(UC_MIPS_REG_SP)==0x83aff000
        for i,reg in enumerate(self.saved_regs):assert self.uc.reg_read(reg)==0x12340000+i
        assert not self.allocations and not self.handles
        assert not self.gui_active and self.gui_opens==self.gui_closes
        if not allow_dialog:assert not self.dialogs,(self.dialogs,self.log())
        return round(time.monotonic()-started,2)
    def log(self):return self.files['A:\\gam4980\\h1gam.log'].decode('gbk')

def main():
    path='A:\\gam4980\\测试.gam'
    first=Machine(events=[(9,16),(-1,-1),(10,16)]);seconds=[first.run()]
    assert 'CORE_INIT_END status=1' in first.log(),first.log()
    assert 'KEY physical=16 core=33' in first.log()
    assert 'STOP frames=12' in first.log()
    saved=first.files[path+'.s0'];assert saved[28+0x8100]==0x5a
    second=Machine(files=first.files);seconds.append(second.run())
    assert 'save=1' in second.log()
    assert second.files[path+'.s1'][28+0x8100]==0x5b
    # Corrupt newest slot -> falls back to older valid slot.
    broken=dict(second.files);bad=bytearray(broken[path+'.s1']);bad[-1]^=1;broken[path+'.s1']=bytes(bad)
    fallback=Machine(files=broken);seconds.append(fallback.run())
    assert 'save=1' in fallback.log() and fallback.files[path+'.s1'][28+0x8100]==0x5b
    cancelled=Machine(path=None);cancelled.run();assert not cancelled.gui_opens
    missing=dict(first.files);del missing['A:\\gam4980\\8.BIN']
    failed=Machine(files=missing,selections=[path,None]);failed.run(allow_dialog=True)
    assert failed.dialogs and failed.gui_opens==failed.gui_closes==1
    invalid=dict(first.files);invalid[path]=b'bad'
    rejected=Machine(files=invalid,selections=[path,None]);rejected.run(allow_dialog=True)
    assert rejected.dialogs
    # Back pauses; Down/Down selects change game, then cancel.
    switched=Machine(events=[(9,41),(-1,-1),(10,41),(9,27),(10,27),(9,27),(10,27),(9,25)])
    switched.run();assert len(switched.select_calls)==2
    assert switched.select_calls[1]==('A:\\gam4980\\','gam')
    assert 'PAUSE view=1' in switched.log()
    def presses(*keys):
        return [event for key in keys for event in [(9,key),(-1,-1),(10,key),(-1,-1)]]
    mapped=Machine(events=presses(41,35,27,25,25,25,1,41,41,41,1));mapped.run()
    assert 'MAPPING_SET physical=1 core=1' in mapped.log(),mapped.log()
    assert 'KEY physical=1 core=1' in mapped.log()
    assert mapped.files[path+'.cfg.s0'][28+8+1]==2
    remapped=Machine(files=mapped.files,events=presses(1));remapped.run()
    assert 'MAPPING_LOAD status=1' in remapped.log()
    assert 'KEY physical=1 core=1' in remapped.log()
    directions=Machine(events=presses(26,28,40,35));directions.run()
    for key,core in [(26,55),(28,57),(40,55),(35,57)]:
        assert f'KEY physical={key} core={core}' in directions.log()
    keyboard_menu=Machine(events=presses(41,28,25,41,26,25));keyboard_menu.run()
    assert 'PAUSE view=1' in keyboard_menu.log() and 'STOP frames=12' in keyboard_menu.log()
    # Migrate incorrect keyboard defaults from v1, retaining custom Q and CRCs.
    legacy_files=dict(mapped.files)
    legacy=bytearray(legacy_files[path+'.cfg.s0'])
    struct.pack_into('<I',legacy,28+4,1);legacy[28+8+26]=2;legacy[28+8+28]=42
    struct.pack_into('<I',legacy,20,zlib.crc32(legacy[28:]));struct.pack_into('<I',legacy,24,zlib.crc32(legacy[:24]))
    legacy_files[path+'.cfg.s0']=bytes(legacy)
    migrated=Machine(files=legacy_files,events=presses(26,28,1));migrated.run()
    assert 'MAPPING_MIGRATE from=1 to=2 keyboard_left=55 keyboard_right=57' in migrated.log()
    for key,core in [(26,55),(28,57),(1,1)]:assert f'KEY physical={key} core={core}' in migrated.log()
    migrated_blob=migrated.files[path+'.cfg.s1']
    assert struct.unpack_from('<I',migrated_blob,28+4)[0]==2
    assert migrated_blob[28+8+26]==56 and migrated_blob[28+8+28]==58 and migrated_blob[28+8+1]==2
    assert zlib.crc32(migrated_blob[28:])==struct.unpack_from('<I',migrated_blob,20)[0]
    persisted=Machine(files=migrated.files,events=presses(26,28,1));persisted.run()
    assert 'MAPPING_MIGRATE' not in persisted.log()
    # Explicit v2 bindings are user settings, not old defaults.
    explicit=Machine(events=presses(41,35,27,25,25,25,26,41,41,41,26));explicit.run()
    assert 'MAPPING_SET physical=26 core=1' in explicit.log()
    explicit_restore=Machine(files=explicit.files,events=presses(26));explicit_restore.run()
    assert 'KEY physical=26 core=1' in explicit_restore.log() and 'MAPPING_MIGRATE' not in explicit_restore.log()
    # Settings -> shortcut slot 1 -> choose Q using keyboard grid navigation.
    hot=Machine(files=mapped.files,events=presses(41,35,27,25,35,25,25,35,35,35,27,27,25,41,41,41))
    hot.run();assert hot.files[path+'.hot.s0'][28+8]==16
    assert hot.files[path+'.cfg.s0']==mapped.files[path+'.cfg.s0'], 'shortcut must not overwrite physical mapping'
    restored=Machine(files=hot.files);restored.run();assert 'SHORTCUT_LOAD status=1' in restored.log()
    last_key=Machine(files=mapped.files,events=presses(41,35,27,25,35,25,25,40,40,25,40,40,25,27,35,35,35,35,25,41,41,41))
    last_key.run();assert last_key.files[path+'.hot.s0'][28+8]==59
    isolated_files=dict(hot.files);other='A:\\gam4980\\另一个.gam';isolated_files[other]=make_game()
    isolated=Machine(files=isolated_files,path=other);isolated.run();assert 'SHORTCUT_LOAD status=0' in isolated.log()
    # Previous-version files have no .hot configuration; defaults and .cfg load.
    assert b'\x02'==mapped.files[path+'.cfg.s0'][28+9:28+10]
    # Native game queries need not report Back/Confirm/Alt matrix aliases.
    # Repeated queue key-down while Back is held must not leave the menu.
    class AliasQuery(Machine):
        def game_key(self,code,*args):
            return 0 if code in (1,28,105,106) else super().game_key(code,*args)
    aliases=AliasQuery(events=[(9,41),(-1,-1),(9,41),(-1,-1),(9,41),
                              (10,41),(-1,-1),(9,41),(10,41)])
    aliases.run();assert aliases.log().count('PAUSE view=1')==1 and 'STOP frames=12' in aliases.log()
    # Simulate the reported left-triggered core stop using an original tiny
    # GAM. It must retain the first BRK PC and a responsive menu, not reopen
    # the firmware picker while the movement key is still held.
    brk_game=bytearray(make_game())
    brk_code=bytes.fromhex('78 a9 00 8d 3a 02 8d 3b 02 ad 4e 02 c9 b7 d0 f9 00')
    brk_game[0x50:0x50+len(brk_code)]=brk_code
    brk_files=dict(first.files);brk_files[path]=bytes(brk_game)
    frame_address=int(re.search(r'\.bss\.frames\s+(0x[0-9a-f]+)',(ROOT/'build/gam4980-test/H1GAM4980.map').read_text())[1],16)
    class CoreStop(Machine):
        def event(self,code,key,*args):
            log=self.files.get('A:\\gam4980\\h1gam.log',b'')
            if b'CORE_STOP' not in log:
                event=(-1,-1)
                if b'GAME_READY' in log and struct.unpack('<I',self.read(frame_address,4))[0]>=1 and not getattr(self,'left_sent',False):
                    event=(9,40);self.left_sent=True
            else:
                event=self.script.pop(0) if self.script else (-1,-1)
            self.word(code,event[0]);self.word(key,event[1]);return 0
    stopped=CoreStop(files=brk_files,events=[(9,40),(-1,-1),(10,40),(-1,-1),
                         *presses(41),*presses(40),*presses(25)])
    stopped.run();assert len(stopped.select_calls)==1,stopped.log()
    assert 'CORE_STOP pc=5060 frame=2' in stopped.log(),stopped.log()
    assert 'STOP frames=2 action=2 ended=1' in stopped.log(),stopped.log()
    assert 'source=1 event=0 physical=40 core=55 view=0' in stopped.log()
    assert 'KEYMAP left=55 right=57 up=53 down=56' in stopped.log()
    # Restart the stopped game directly, without invoking the selector.
    class Restart(CoreStop):
        def event(self,code,key,*args):
            log=self.files.get('A:\\gam4980\\h1gam.log',b'')
            if log.count(b'CORE_STOP')==1:
                if log.count(b'GAME_READY')==1:event=self.script.pop(0) if self.script else (-1,-1)
                else:
                    event=(-1,-1)
                    if struct.unpack('<I',self.read(frame_address,4))[0]>=1 and not getattr(self,'restart_left_sent',False):
                        event=(9,40);self.restart_left_sent=True
            elif log.count(b'CORE_STOP')>=2:
                event=self.exit_script.pop(0) if self.exit_script else (-1,-1)
            else:return super().event(code,key,*args)
            self.word(code,event[0]);self.word(key,event[1]);return 0
    restarted=Restart(files=brk_files,events=[(10,40),(-1,-1),*presses(25)])
    restarted.exit_script=[(10,40),(10,25),(-1,-1),*presses(40),*presses(25)]
    restarted.run();assert restarted.gui_opens==2 and len(restarted.select_calls)==1
    assert restarted.log().count('CORE_STOP pc=5060 frame=2')==2,restarted.log()
    release=Machine(events=presses(16,41,35,27,27,25),bda=ROOT/'dist/H1GAM4980.bda')
    release.run();assert 'KEY physical=' not in release.log() and 'PAUSE view=1' in release.log()
    out=ROOT/'build/gam4980-verification';out.mkdir(exist_ok=True)
    px=struct.unpack('<'+str(480*272)+'H',first.screen)
    assert len(set(px[:480*240]))>=3, 'LCD must include borders and both pixel colors'
    im=Image.new('RGB',(480,272));im.putdata([((p>>11)*255//31,((p>>5)&63)*255//63,(p&31)*255//31) for p in px]);im.save(out/'mips-game.png')
    import hashlib
    report={'ok':True,'bda_sha256':hashlib.sha256((ROOT/'build/gam4980-test/H1GAM4980.bda').read_bytes()).hexdigest(),
            'kind':'actual MIPS BDA with mocked H1 tables; real 8/E ROMs and original GAM',
            'seconds':seconds,'frames':12,'assertions':['6502 boot','GAM execution','LCD output','physical Z input',
            'CRC save/write/readback','save restore','corrupt latest slot fallback','GBK path','selector cancel',
            'missing runtime ROM','invalid game','pause and game switch','function key mapping','per-game mapping restore',
            'held Back alias does not repeat menu actions','release keys do not write NAND logs',
            'keyboard and screen direction pairs send left/right and navigate settings',
            'v1 default migration preserves custom bindings and CRC slots',
            'v2 explicit direction-key remapping survives reload',
            'left-triggered BRK preserves first PC and stops in the triggering frame',
            'core stop stays in responsive menu; held left and Back cannot resume',
            'core-stop restart bypasses selector without leaking resources',
            'recent physical/core inputs buffered in RAM; flushed on stop',
            'shortcut keyboard selection and per-game restore','shortcut configuration preserves physical mapping',
            'three shortcut pages reach key 59','different game uses default shortcuts',
            'host register preservation','no resource leaks']}
    (out/'mips-smoke.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))

if __name__=='__main__':main()

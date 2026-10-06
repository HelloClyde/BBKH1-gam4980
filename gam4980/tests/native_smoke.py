"""Verify UI and core input in a running private V1.41 firmware image.
Start with H1TEST.gam already running. Only read guest RAM; all actions use
the emulator's input API. Keep this image separate from the GBA test image.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
import time
import urllib.request

ROOT=Path(__file__).resolve().parents[2]
URL='http://127.0.0.1:8795'
WORK=ROOT/'build/h1-gam4980-test'
OUT=ROOT/'build/gam4980-verification'
MAP=(ROOT/'build/gam4980-release/H1GAM4980.map').read_text()
sys.path.insert(0,str(ROOT/'sdk/scripts'))
from capture_emulator_frame import convert_frame

def api(path,data=None):
    r=urllib.request.Request(URL+path,data=None if data is None else json.dumps(data).encode(),headers={'Content-Type':'application/json'})
    return json.load(urllib.request.urlopen(r,timeout=10))
def local(name):return int(re.search(r'\.(?:bss|data)\.'+name+r'\s+(0x[0-9a-f]+)',MAP)[1],16)
def memory(addr,n=1):
    s=api('/api/debug/memory?address=0x%x&count=%d'%(addr&0x1fffffff,n))['memory']
    return [int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{8})',s)]
def value(name):return memory(local(name))[0]
def touch(x,y):
    api('/api/touch',{'x':x,'y':y,'down':True});time.sleep(.25)
    api('/api/touch',{'x':x,'y':y,'down':False});time.sleep(.6)
def tap(key):
    api('/api/key',{'code':key,'down':True});time.sleep(.3)
    api('/api/key',{'code':key,'down':False});time.sleep(.5)
def wait(test,seconds=30):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        if test():return
        time.sleep(.2)
    raise AssertionError('Native firmware condition timed out')
def shot(name):
    subprocess.run([sys.executable,ROOT/'sdk/scripts/capture_emulator_frame.py',OUT/(name+'.png'),'--server',URL],check=True)
def keycode():return (memory(memory(local('buffers'))[0]+0x24c)[0]>>16)&255
def counter():return memory(memory(local('buffers'),2)[1]+0x8100)[0]&255
def pause():touch(100,100);assert value('view')==1
def settings():pause();touch(325,150);assert value('view')==5
def mapping():settings();touch(100,85);assert value('view')==2
def resume():
    for _ in range(4):
        if not value('view'):return
        tap(41)
    assert not value('view')
def check_integer_frame():
    packet=urllib.request.urlopen(URL+'/api/debug/frame',timeout=10).read()
    w,h,pixels=convert_frame(packet);assert (w,h)==(480,272)
    def pixel(x,y):return pixels[(y*w+x)*4:(y*w+x+1)*4]
    border=bytes([8,8,24,255])
    assert pixel(80,24)==pixel(399,24)==pixel(81,23)==pixel(81,216)==border
    assert pixel(81,24)!=border
    for y in range(24,216,2):
        for x in range(81,399,2):
            assert pixel(x,y)==pixel(x+1,y)==pixel(x,y+1)==pixel(x+1,y+1),(x,y)
    return {'source':[159,96],'display':[318,192],'origin':[81,24],'all_2x2_blocks_identical':True}

def main():
    OUT.mkdir(exist_ok=True)
    report={'kind':'complete H1 V1.41 firmware + QEMU','sha256':hashlib.sha256((ROOT/'dist/H1GAM4980.bda').read_bytes()).hexdigest()}
    assert not value('view') and value('frames')>0
    shot('native-game');before=value('frames');time.sleep(1);assert value('frames')>before
    report['initial_counter']=counter();assert counter()==0x5a
    tap(16);assert keycode()==0xa1,keycode();report['Z_core_key']=keycode()
    tap(31);assert keycode()==0xb3,keycode();report['P_core_key']=keycode()
    # Bottom buttons send original game keys and keep the core running.
    report['default_touch_shortcuts']=[]
    for x,expected in [(60,0x81),(180,0xaf),(300,0xae),(420,0xa9)]:
        before=value('frames');touch(x,255);assert not value('view') and value('frames')>before
        assert keycode()==expected,keycode();report['default_touch_shortcuts'].append(expected)
    shot('native-shortcut-bar')
    touch(100,100);assert value('view')==1
    frozen=value('frames');time.sleep(1);assert value('frames')==frozen
    report['pause_frames']=frozen;shot('native-pause')
    touch(325,85);assert value('view')==3;shot('native-size-options')
    frozen=value('frames');touch(100,70);assert value('scale')==0 and value('view')==3
    time.sleep(.5);assert value('frames')==frozen;shot('native-size-selected')
    tap(41);assert value('view')==1;shot('native-current-display-values');tap(41)
    wait(lambda:value('view')==0);shot('native-original-size')
    pause();touch(325,85);assert value('view')==3;tap(27);tap(25)
    assert value('scale')==1 and value('view')==3
    report['keyboard_size_selection']=True
    tap(27);tap(25);assert value('scale')==2 and value('view')==3
    shot('native-integer-selected');tap(41);shot('native-integer-menu');tap(41)
    shot('native-integer-size');report['integer_scaling']=check_integer_frame()
    touch(100,100);touch(100,150);assert value('view')==4;shot('native-color-options')
    report['color_selections']=[]
    for i in range(4):
        touch(100,70+i*40);assert value('theme')==i and value('view')==4
        report['color_selections'].append(i)
    touch(100,110);assert value('theme')==1;shot('native-color-selected')
    touch(100,230);assert value('view')==1;shot('native-current-display-values');tap(41)
    shot('native-green-theme')
    mapping();shot('native-key-mapping')
    touch(40,75);assert value('capture')==0;shot('native-key-capture')
    tap(1);assert value('capture')==0xffffffff;shot('native-key-mapped-Q')
    resume();tap(1);assert keycode()==0x81,keycode();report['mapped_Q_core_key']=keycode()
    # Select Q for bottom slot 1; touch shortcut is independent of physical Q.
    settings();touch(325,85);assert value('view')==6;shot('native-shortcut-settings')
    touch(100,70);assert value('view')==7;shot('native-shortcut-picker')
    touch(270,160);assert value('view')==6;shot('native-shortcut-Q')
    resume();touch(60,255);assert keycode()==0x90 and not value('view')
    tap(1);assert keycode()==0x81
    pause();touch(100,212);time.sleep(2);shot('native-selector')
    tap(39);time.sleep(4);wait(lambda:memory(local('buffers'))[0]!=0 and value('frames')>3)
    wait(lambda:counter()==0x5b);report['restored_counter']=counter()
    tap(1);assert keycode()==0x81,keycode();report['restored_Q_core_key']=keycode()
    touch(60,255);assert keycode()==0x90 and not value('view');report['restored_touch_Q_core_key']=keycode()
    mapping();shot('native-restored-mapping');touch(80,249);resume()
    tap(1);assert keycode()==0x90,keycode();report['default_Q_core_key']=keycode()
    assert value('theme')==1
    # Confirm that the actual expanded pixel palette also survives re-init.
    report['reopened_palette']=[value('theme')]
    # Direct RGB32 drawing no longer expands the obsolete RGB565 core buffer.
    packet=urllib.request.urlopen(URL+'/api/debug/frame',timeout=10).read()
    _,_,rgba=convert_frame(packet)
    at=(24*480+81)*4
    assert rgba[at:at+3]==bytes([0x90,0xdc,0x08]),rgba[at:at+3]
    shot('native-reopened')
    # Changing game uses its own shortcuts; restore defaults changes only .hot.
    settings();touch(325,85);touch(80,216);resume()
    touch(60,255);assert keycode()==0x81;report['reset_touch_menu_core_key']=keycode()
    touch(100,100);touch(325,212);wait(lambda:memory(local('buffers'))[0]==0)
    time.sleep(1);shot('native-exit')
    touch(45,51);time.sleep(2);shot('native-cancel-picker');tap(41);time.sleep(2)
    assert memory(local('buffers'))[0]==0;shot('native-cancel-exit')
    report['ok']=True;report['assertions']=['running 6502 game','Z/P physical keys','touch pause stops core',
        'display size option page and selected value','paused while choosing options','keyboard display selection',
        'integer 2x centered 318x192; every 2x2 block exact',
        'all four LCD color selections','return to parent menu','current values in pause menu',
        'original-size display','green LCD theme','function key binding','per-game mapping restore','restore default mapping',
        'four default touch shortcuts keep game running','all-key shortcut picker','per-game touch shortcut restore',
        'touch shortcuts independent of physical mapping','restore default touch shortcuts',
        'GAM-only selector','change game','save restored','touch application exit','selector cancel']
    (OUT/'native-smoke.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    api('/api/stop',{});print(json.dumps(report,indent=2))

if __name__=='__main__':main()

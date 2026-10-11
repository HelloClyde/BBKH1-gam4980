"""Full H1 V1.41 regression; start with GAM's picker open on port 8795.

Requires a private firmware image with the release BDA at the top-left slot
of Other category's third page and a local .gam fixture. Open the picker
from that page before running this test. Firmware and games are not supplied.
Checks actual desktop redraw and relaunch, not merely freed game buffers.
"""
import hashlib,json,time,urllib.request
import native_smoke as n

def pixels():
    packet=urllib.request.urlopen(n.URL+'/api/debug/frame',timeout=10).read()
    return n.convert_frame(packet)[2]

def responsive(label):
    # Ignore the top clock. Page up must repaint the desktop, and page down
    # must return to the same GAM icon page before we relaunch the application.
    time.sleep(1)
    before=pixels()[480*30*4:480*190*4]
    n.tap(15)
    n.wait(lambda:pixels()[480*30*4:480*190*4]!=before,5)
    n.shot('cancel-'+label+'-desktop-pageup')
    n.tap(22)
    n.wait(lambda:pixels()[480*30*4:480*190*4]==before,5)
    n.shot('cancel-'+label+'-desktop')

def reopen():
    # Native Other category, page 3: the final game slot is the only icon.
    n.touch(42,51);time.sleep(1)
    assert n.memory(n.local('buffers'))[0]==0
    n.wait(lambda:pixels()[:480*24*4]==picker_header,10)

picker_header=pixels()[:480*24*4]
cases=[]
for name,key in [('back',41),('escape',24),('touch_close',None)]:
    if key is None:n.touch(464,11)
    else:n.tap(key)
    responsive(name);cases.append(name)
    reopen()

# Open a game, choose Change game, then cancel that new picker.
n.touch(435,258);n.wait(lambda:n.value('frames')>5,45)
n.pause();n.touch(100,212);time.sleep(1)
assert n.memory(n.local('buffers'))[0]==0
n.tap(41);responsive('change-game-back');cases.append('change_game_back')
reopen();n.touch(435,258);n.wait(lambda:n.value('frames')>5,45)
n.pause();n.touch(325,212);n.wait(lambda:n.memory(n.local('buffers'))[0]==0)
responsive('normal-exit')
status=n.api('/api/stop',{});assert not status['running'] and status['returncode']==0
report={'ok':True,'environment':'complete H1 V1.41 firmware + QEMU; not physical H1',
        'bda_sha256':hashlib.sha256((n.ROOT/'dist/H1GAM4980.bda').read_bytes()).hexdigest(),
        'cancel_cases':cases,'desktop_redraw_and_relaunch_after_each_cancel':True,
        'game_load_after_cancel':True,'normal_exit_after_cancel':True}
(n.ROOT/'build/gam4980-verification/selector-cancel-native.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))

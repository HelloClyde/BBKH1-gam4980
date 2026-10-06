"""Exercise About on the actual release BDA, keeping guest files in RAM."""
from pathlib import Path
import struct,re,json,hashlib
from PIL import Image
from mips_smoke import Machine,ROOT

symbols=(ROOT/'build/gam4980-release/H1GAM4980.map').read_text()
def symbol(name):return int(re.search(r'\.(?:bss|data)\.'+name+r'\s+(0x[0-9a-f]+)',symbols)[1],16)
def presses(*keys):
    return [event for key in keys for event in [(9,key),(-1,-1),(10,key),(-1,-1)]]
out=ROOT/'build/gam4980-verification';out.mkdir(parents=True,exist_ok=True)
class About(Machine):
    def __init__(self,events):
        self.visited=False;self.paused_frame=None;self.returned=False
        super().__init__(bda=ROOT/'dist/H1GAM4980.bda',events=events)
    def event(self,code,key,*args):
        page=struct.unpack('<I',self.read(symbol('view'),4))[0]
        frame=struct.unpack('<I',self.read(symbol('frames'),4))[0]
        if page==8:
            if self.paused_frame is None:self.paused_frame=frame
            assert frame==self.paused_frame, 'About must pause the core'
            # Capture only after the actual menu has been drawn.
            if not self.visited and not struct.unpack('<I',self.read(symbol('redraw'),4))[0]:
                rgb=struct.unpack('<'+str(480*272)+'I',self.read(0x82000000,480*272*4))
                im=Image.new('RGB',(480,272));im.putdata([((p>>16)&255,(p>>8)&255,p&255) for p in rgb])
                im.save(out/'about.png');self.visited=True
        if self.visited and page==1:self.returned=True
        return super().event(code,key,*args)

for exit_key in [41,24,25]:
    m=About(presses(41,27,27,27,25,exit_key,41,41,35,27,27,25))
    m.run();assert m.visited and m.returned and len(m.select_calls)==1
    assert 'action=2 ended=0' in m.log(),m.log()
report={'ok':True,'kind':'actual release MIPS BDA with mocked H1 firmware calls',
        'bda_sha256':hashlib.sha256((ROOT/'dist/H1GAM4980.bda').read_bytes()).hexdigest(),
        'checks':['About via physical keyboard','core frozen in About','Back/ESC/confirm return to pause menu',
                  'return to game and normal exit','one selector invocation','no resource leaks']}
(out/'about-smoke.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))

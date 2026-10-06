"""Original 6502 fixture: animated LCD, key code and persistent flash counter."""
from pathlib import Path
import struct

def make_game():
    out=bytearray(512);out[:6]=b'H1TEST';out[6:16]=b'H1 GAMTEST'
    struct.pack_into('<HI',out,0x40,0x5050,0)
    code=bytearray()
    def lda(v):code.extend([0xa9,v])
    def sta(a):code.extend([0x8d,a&255,a>>8])
    def bank(sel,value):
        lda(sel);sta(0x0c);lda(value&255);sta(0x0d);lda(value>>8);sta(0x0e)
    code.append(0x78);lda(0);sta(0x23a);sta(0x23b)
    bank(9,0x205);bank(10,0x202);bank(11,0x200)
    code.extend([0xad,0x00,0xb1,0xc9,0xff,0xd0,2]);lda(0x59)
    code.extend([0x18,0x69,1]);sta(0x50)
    lda(0xaa);sta(0x9555);lda(0x55);sta(0xaaaa);lda(0xa0);sta(0x9555)
    code.extend([0xa5,0x50]);sta(0xb100)
    lda(0xaa);sta(0x400);lda(0x55);sta(0x1000)
    loop=0x5050+len(code)
    code.extend([0xad,0x4e,2]);sta(0x401)
    code.extend([0xee,0x10,4,0x4c,loop&255,loop>>8])
    out[0x50:0x50+len(code)]=code
    return bytes(out)

if __name__=='__main__':
    root=Path(__file__).resolve().parents[2]
    target=root/'build/gam4980-fixtures/H1TEST.gam';target.parent.mkdir(parents=True,exist_ok=True)
    target.write_bytes(make_game());print(target)

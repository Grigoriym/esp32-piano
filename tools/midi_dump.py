#!/usr/bin/env python3
"""Prints a MIDI capture (arecordmidi -t 1000: 1 tick = 1 ms) as one line per note:
time ms, channel (1-16), note, velocity, length ms; controls and programs too.

To see what the user plays (2026-11-06: a riff, a bass line read off a loop):
    timeout -s INT 40 arecordmidi -p 128:0 -b 60 -t 1000 out.mid
    python3 tools/midi_dump.py out.mid
Channels: 1 live keys, 3-6 loop layers, 9 metronome (note 84 = beat 1, 77 the others),
10 drums, 11-13 the chicks' copies. Key = note - 32 at octave 0 in EDM mode is NOT
linear (A minor): map notes back with keymap_key_note's table.
"""
import struct,sys
def events(path):
    d=open(path,'rb').read(); i=d.index(b'MTrk'); evs=[]
    while i>=0:
        ln=struct.unpack('>I',d[i+4:i+8])[0]; p=i+8; end=p+ln; t=0; run=0
        while p<end:
            v=0
            while True:
                b=d[p];p+=1;v=(v<<7)|(b&0x7f)
                if b<0x80:break
            t+=v; st=d[p]
            if st==0xff: l=d[p+2];p+=3+l;continue
            if st&0x80: run=st;p+=1
            n=1 if (run&0xf0) in (0xc0,0xd0) else 2
            evs.append((t,run,d[p:p+n]));p+=n
        i=d.find(b'MTrk',end)
    return sorted(evs)
if __name__=='__main__':
    on={}
    for t,s,a in events(sys.argv[1]):
        ch=(s&0xf)+1; ty=s&0xf0
        if ty==0x90 and a[1]>0: on[(ch,a[0])]=(t,a[1])
        elif ty in (0x80,0x90) and (ch,a[0]) in on:
            t0,v=on.pop((ch,a[0])); print(t0,'ch',ch,'note',a[0],'vel',v,'len',t-t0)
        elif ty in (0xb0,0xc0): print(t,'ch',ch,'ctl',list(a))

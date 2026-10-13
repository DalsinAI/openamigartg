import json,re
import lab_driver as d

def mapped(raw, qual):
    mods=[code for bit,code in [(1,0x60),(2,0x61),(0x10,0x64),(0x20,0x65)] if qual & bit]
    for code in mods:d.record(1,code)
    try:d.key(raw)
    finally:
        for code in reversed(mods):d.record(1,code|0x80)
    d.wait(.2)

all_results={}
for kind,exe in [('shared','SDLTextCheck'),('static','SDLTextCheckStatic')]:
    out='merge-text-'+kind+'.out'
    d.mailbox(f'Echo "TEXT_{kind}_BEGIN"\nSYS:Open2Runner/{exe} >SYS:Open2Runner/{out}\nEcho "TEXT_{kind}_EXIT $RC"', 'text-'+kind)
    d.wait(2)
    d.key(0x20);d.chord(0x60,0x20);d.key(0x01)
    # These sequences came from MapANSI on THIS guest's default keymap.
    mapped(0x28,0x10)             # pound sign
    mapped(0x23,0x10);mapped(0x12,0)  # dead acute then e
    mapped(0x27,0x10);mapped(0x20,1)  # dead diaeresis then shifted A
    before=d.output(out)
    d.chord(0x63,0x20);d.chord(0x66,0x20);d.chord(0x67,0x20)
    d.key(0x41);d.key(0x44);d.key(0x4e)
    after=d.output(out)
    # Text stops and restarts without suppressing physical key events.
    d.key(0x51);d.key(0x31);stopped=d.output(out)
    d.key(0x51);d.key(0x31);d.wait(.3)
    d.capture('text-'+kind+'-checked')
    d.key(0x45);d.wait(1)
    full=d.output(out);exit_log=d.output('run.out')
    (d.ROOT/('text-'+kind+'-guest.txt')).write_text(full)
    (d.ROOT/('text-'+kind+'-exit.txt')).write_text(exit_log)
    observed=re.findall(r'TEXTINPUT .*?bytes=([0-9A-F ]+) text=',full)
    expected=['61','41','31','C2 A3','C3 A9','C3 84','7A']
    checks={'text_bytes':observed,'expected':expected,'text_matches':observed==expected,
            'commands_do_not_add_text':after.count('TEXTINPUT')==before.count('TEXTINPUT'),
            'stopped_does_not_add_text':stopped.count('TEXTINPUT')==after.count('TEXTINPUT'),
            'alt_is_alt':'scancode=226 name="Left Alt"' in full,
            'amiga_is_gui':'scancode=227 name="Left GUI"' in full,
            'right_amiga_is_gui':'scancode=231 name="Right GUI"' in full,
            'exit_zero':f'TEXT_{kind}_EXIT 0' in exit_log}
    all_results[kind]=checks
    print(kind,json.dumps(checks),flush=True)
(d.ROOT/'text-qualified-checks.json').write_text(json.dumps(all_results,indent=2)+'\n')

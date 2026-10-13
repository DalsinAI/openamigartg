import lab_driver as d
import json
from pathlib import Path
out='merge-focus.out'
d.mailbox('Run >SYS:Open2Runner/merge-focus-probe.out SYS:Open2Runner/FocusProbe\nSYS:Open2Runner/SDLTextCheck >SYS:Open2Runner/'+out+'\nEcho "MERGE_FOCUS_EXIT $RC"','focus-held-modifier')
d.wait(2)
d.log('hold_modifier_until_focus_lost',raw=0x60)
d.record(1,0x60)
try:
    for _ in range(35):
        d.wait(.3)
        if 'FOCUS lost' in d.output(out):break
    lost=d.output(out)
    observed='FOCUS lost' in lost
    d.capture('held-modifier-focus-lost')
finally:
    d.record(1,0xe0)
before=d.output(out)
d.key(0x20);d.wait(.3)
after=d.output(out)
# Closing the second Intuition window returns focus to the diagnostic.
for _ in range(35):
    d.wait(.3)
    if d.output(out).count('FOCUS gained')>=2:break
restored=d.output(out)
d.key(0x20);d.wait(.2);d.capture('held-modifier-focus-restored')
final_before_exit=d.output(out)
d.key(0x45);d.wait(1)
full=d.output(out);exitlog=d.output('run.out')
(d.ROOT/'focus-qualified-guest.txt').write_text(full)
(d.ROOT/'focus-qualified-exit.txt').write_text(exitlog)
new=final_before_exit[len(restored):]
checks={'focus_loss_observed':observed,'held_shift_release_observed':'KEYUP    scancode=225' in lost,
        'no_unfocused_text':after.count('TEXTINPUT')==before.count('TEXTINPUT'),
        'focus_return_observed':restored.count('FOCUS gained')>=2,
        'plain_a_after_return':'bytes=61 text="a"' in new,
        'modifiers_clear_after_return':'mods=0x0000' in new,
        'exit_zero':'MERGE_FOCUS_EXIT 0' in exitlog}
(d.ROOT/'focus-qualified-checks.json').write_text(json.dumps(checks,indent=2)+'\n')
print(json.dumps(checks,indent=2),flush=True)

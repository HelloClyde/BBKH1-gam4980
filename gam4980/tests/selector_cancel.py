"""Check desktop display restoration after real MIPS picker cancellation."""
import hashlib,json
from mips_smoke import Machine,ROOT

rows=[]
for name in ['H1GAM4980','H1GAM4980-profile']:
    bda=ROOT/'dist'/(name+'.bda')
    for path in [None,'A:\\gam4980\\wrong.txt']:
        m=Machine(path=path,bda=bda);m.run(allow_dialog=path is not None)
        assert len(m.select_calls)==1 and not m.blits
        assert m.gui_opens==m.gui_closes==1
        assert not m.game_display_mode
        assert 'SELECTOR_EXIT_RESTORE_END restored=1' in m.log()
        assert 'CORE_INIT_BEGIN' not in m.log() and 'READ_BEGIN' not in m.log()
    # A missing selector service has not changed display state: no restore.
    missing=Machine(path=None,bda=bda);missing.word(0x800029ec,0)
    missing.run(allow_dialog=True)
    assert not missing.select_calls and not missing.gui_opens
    rows.append({'name':name,'sha256':hashlib.sha256(bda.read_bytes()).hexdigest(),
                 'cancel_and_invalid_result_restore_desktop':True,'missing_service_exits':True})
report={'ok':True,'kind':'actual release MIPS BDA with H1 display-mode mock','packages':rows}
out=ROOT/'build/gam4980-verification';out.mkdir(parents=True,exist_ok=True)
(out/'selector-cancel.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))

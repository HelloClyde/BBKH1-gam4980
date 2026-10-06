"""Validate cost distributions and rejection of misleading diagnostic data."""
from pathlib import Path
import json,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from analyze_gam4980_profile import analyze,COSTS
out=ROOT/'build/gam4980-profile-host';out.mkdir(exist_ok=True)
log=out/'analyzer-fixture.log'
base=['PROFILE_GAME path=A:\\gam4980\\伏魔记.gam','PROFILE_CLOCK source=TCU5_RTC hz=32768 ready=1',
      'PROFILE_SETTINGS scale=2 theme=1 core_hz=60 host_hz=80 catchup_ticks=8 input=matrix',
      'PROFILE_RTC_RUN reason=pause frames=301 displayed=150 interval_frames=300 rtc_start=100 rtc_end=110 seconds=10 resolution_seconds=1 valid=1',
      'PROFILE_BEGIN reason=pause windows=1 omitted=2 hz=32768 fault=0 exclusive=1',
      'PROFILE_WINDOW index=2 frames=300 displayed=150 elapsed_us=10000000 core_mean_us=10000 core_max_us=20000 core_p95_bin_ms=15 over_16_667ms=3 loop_under_2ms=2 loop_over_25ms=1',
      'PROFILE_GAM_DETAIL window=2 iterations=400 host_ticks=800 dropped_ticks=0 input_events=12 input_queries=15200 video_rows=1440 video_pixels=460800 full_redraws=1 saves_written=1 saves_failed=0']
counts={'core_6502':98304,'lcd_capture':32768,'screen_output':65536,'input_poll':32768,'save_checkpoint':32768,'pacing_wait':32768,'loop_other':32768}
base += [f'PROFILE_COST window=2 name={name} ticks={ticks} us=0 calls=300' for name,ticks in counts.items()]
base += ['PROFILE_END']
def write(lines):log.write_bytes(('\r\n'.join(lines)+'\r\n').encode('gbk'))
write(base);r=analyze(log,'real-h1');s=r['segments'][0]['summary']
assert s['logic_fps']==30 and s['display_fps']==15 and s['core_mean_ms']==10
assert s['costs']['core_6502']['loop_percent']==30 and s['costs']['core_6502']['active_percent']==42.857
assert set(s['costs'])==COSTS and r['segments'][0]['omitted_windows']==2
assert r['coarse_runs'][0]['approx_logic_fps']==30
assert r['coarse_runs'][0]['logic_fps_lower']<30<r['coarse_runs'][0]['logic_fps_upper']
hist=[0]*64;hist[9]=10;hist[10]=289;hist[20]=1
hist_line='PROFILE_CORE_HIST window=2 bins='+','.join(map(str,hist))+' overflow_bin_ms=63'
write(base[:-1]+[hist_line,base[-1]])
hist_summary=analyze(log)['segments'][0]['summary']
assert hist_summary['core_p95_bin_ms']==10 and sum(hist_summary['core_histogram_ms'])==300
def reject(lines):
    write(lines)
    try:analyze(log)
    except ValueError:return
    raise AssertionError('Invalid input was accepted')
reject(base[:-1]);reject([s for s in base if 'name=input_poll ' not in s])
reject([s.replace('fault=0','fault=2') for s in base])
reject([s.replace('ready=1','ready=0') for s in base])
reject([s.replace('PROFILE_COST window=2','PROFILE_COST window=7') for s in base])
reject(base[:-1]+[base[-2],base[-1]])
reject(base[:-1]+[hist_line.replace('289','288'),base[-1]])
reject(base[:-1]+[hist_line,hist_line,base[-1]])
write([s.replace('ready=1','ready=0').replace('fault=0','fault=2') for s in base])
assert analyze(log,'real-h1',True)['coarse_runs'][0]['approx_logic_fps']==30
report={'passed':True,'checks':['GBK game path','exclusive cost fractions','logic/display FPS','RTC bounds','omitted windows','truncated/missing/duplicate records rejected','clock and stall faults rejected','coarse-only fallback']}
(out/'analyzer-tests.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))

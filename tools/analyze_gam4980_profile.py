"""Summarize GAM4980 PROFILE records from A:\\gam4980\\h1gam.log."""
import argparse,json,re
from pathlib import Path

COSTS={'core_6502','lcd_capture','screen_output','input_poll','save_checkpoint','pacing_wait','loop_other'}
def analyze(path,environment='unspecified',coarse_only=False):
    lines=path.read_bytes().decode('gbk').splitlines()
    coarse=[];segments=[];segment=window=None;hz=0;source='unknown';settings={};game=''
    for line in lines:
        f=dict(re.findall(r'(\w+)=([^\s]+)',line))
        if 'PROFILE_RTC_RUN ' in line:
            row={k:int(v) if v.isdecimal() else v for k,v in f.items()}
            seconds=row['seconds'];frames=row['interval_frames']
            if row['valid'] and seconds>=5:
                row.update(approx_logic_fps=round(frames/seconds,3),logic_fps_lower=round(frames/(seconds+1),3),logic_fps_upper=round(frames/(seconds-1),3))
            coarse.append(row)
        if coarse_only:continue
        if 'PROFILE_GAME path=' in line:game=line.split('PROFILE_GAME path=',1)[1]
        if 'PROFILE_SETTINGS ' in line:settings={k:int(v) if v.isdecimal() else v for k,v in f.items()}
        if 'PROFILE_CLOCK ' in line:
            if f.get('ready')!='1':raise ValueError('Profiler clock unavailable; use --coarse-only for RTC frame-rate bounds: '+line)
            hz=int(f['hz']);source=f.get('source','unknown')
        if 'PROFILE_BEGIN ' in line:
            if f.get('fault')!='0':
                reasons={'1':'Unbalanced profiling scopes','2':'Long stall/RTC change invalidates wrap tracking','3':'TCU read failed','4':'TCU5 busy at game-loop start'}
                raise ValueError(reasons.get(f.get('fault'),'Unknown profiler fault'))
            if not hz:raise ValueError('Missing calibrated clock')
            if int(f['hz'])!=hz:raise ValueError('Clock frequency mismatch')
            segment={'game':game,'reason':f['reason'],'settings':dict(settings),'clock_source':source,'count_hz':hz,'omitted_windows':int(f['omitted']),'expected_windows':int(f['windows']),'windows':[]}
            segments.append(segment);window=None
        elif 'PROFILE_WINDOW ' in line:
            if segment is None:raise ValueError('Window outside profile block')
            window={k:int(v) for k,v in f.items()};window['costs']={};segment['windows'].append(window)
        elif 'PROFILE_GAM_DETAIL ' in line:
            if window is None or int(f['window'])!=window['index']:raise ValueError('Detail window mismatch')
            window['detail']={k:int(v) for k,v in f.items()}
        elif 'PROFILE_CORE_HIST ' in line:
            if window is None or int(f['window'])!=window['index']:raise ValueError('Histogram window mismatch')
            if 'core_histogram_ms' in window:raise ValueError('Duplicate histogram')
            bins=[int(v) for v in f['bins'].split(',')]
            if len(bins)!=64 or sum(bins)!=window['frames']:raise ValueError('Invalid histogram counts')
            window['core_histogram_ms']=bins
        elif 'PROFILE_COST ' in line:
            if window is None or int(f['window'])!=window['index']:raise ValueError('Cost window mismatch')
            if f['name'] in window['costs']:raise ValueError('Duplicate cost')
            window['costs'][f['name']]={'ticks':int(f['ticks']),'calls':int(f['calls'])}
        elif 'PROFILE_END' in line:
            if segment is None:raise ValueError('Unexpected PROFILE_END')
            segment['complete']=True;segment=None;window=None
    if coarse_only:
        if not coarse:raise ValueError('No PROFILE_RTC_RUN records')
        return {'environment':environment,'coarse_runs':coarse,'note':'RTC endpoints have 1-second resolution. Bounds allow endpoint phase uncertainty; no cost distribution can be inferred.'}
    if not segments:raise ValueError('No PROFILE records; use --profile build and pause or exit')
    for s in segments:
        if not s.get('complete'):raise ValueError('Truncated profile block')
        if len(s['windows'])!=s['expected_windows']:raise ValueError('Window count mismatch')
        hz=s['count_hz'];totals={name:{'ticks':0,'calls':0} for name in COSTS};detail={}
        for w in s['windows']:
            if set(w['costs'])!=COSTS or 'detail' not in w:raise ValueError('Incomplete window')
            expected_index=s['omitted_windows']+s['windows'].index(w)
            if w['index']!=expected_index:raise ValueError('Window index mismatch')
            for name,c in w['costs'].items():
                for k,v in c.items():totals[name][k]+=v
            for k,v in w['detail'].items():
                if k!='window':detail[k]=detail.get(k,0)+v
        frames=sum(w['frames'] for w in s['windows']);displayed=sum(w['displayed'] for w in s['windows'])
        ticks=sum(c['ticks'] for c in totals.values());active=sum(c['ticks'] for n,c in totals.items() if n not in ('pacing_wait','loop_other','save_checkpoint'))
        for name,c in totals.items():
            c.update(ms_per_logic_frame=round(c['ticks']*1000/hz/frames,6) if frames else None,loop_percent=round(c['ticks']*100/ticks,3) if ticks else None,active_percent=round(c['ticks']*100/active,3) if active and name not in ('pacing_wait','loop_other','save_checkpoint') else None)
        s['summary']={'frames':frames,'displayed':displayed,'measured_loop_seconds':round(ticks/hz,6),'logic_fps':round(frames*hz/ticks,3) if ticks else None,'display_fps':round(displayed*hz/ticks,3) if ticks else None,'costs':totals,'detail':detail,'core_max_ms':max((w['core_max_us']/1000 for w in s['windows']),default=None),'over_budget_frames':sum(w['over_16_667ms'] for w in s['windows'])}
        if frames and ticks:
            s['summary']['core_mean_ms']=round(totals['core_6502']['ticks']*1000/hz/frames,6)
        if frames and all('core_histogram_ms' in w for w in s['windows']):
            histogram=[sum(w['core_histogram_ms'][i] for w in s['windows']) for i in range(64)]
            count=0
            for p95,n in enumerate(histogram):
                count+=n
                if count*100>=frames*95:break
            s['summary'].update(core_histogram_ms=histogram,core_p95_bin_ms=p95,core_p95_bounds_ms=[p95,p95+1 if p95<63 else None],core_histogram_overflow_bin_ms=63)
    return {'log':str(path),'environment':environment,'segments':segments,'coarse_runs':coarse,'note':'Exclusive wall-time costs include IRQ/OS scheduling in the interrupted scope. Loading, calibration, menus and bulk profile-log writes are excluded. QEMU time is virtual guest time, not physical H1 performance. Only the last 12 complete ~300-frame windows per segment are retained.'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--output',type=Path)
    p.add_argument('--environment',choices=['real-h1','qemu','unspecified'],default='unspecified');p.add_argument('--coarse-only',action='store_true')
    a=p.parse_args()
    try:r=analyze(a.log,a.environment,a.coarse_only)
    except ValueError as e:p.error(str(e))
    if a.output:a.output.write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    for s in r.get('segments',[]):
        print(s['game'],s['reason'],json.dumps(s['summary'],ensure_ascii=False))
    if a.coarse_only:print(json.dumps(r['coarse_runs'],ensure_ascii=False))
if __name__=='__main__':main()

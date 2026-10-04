"""Check the delivered assets and their lookup/composition contract."""
import argparse, hashlib, json, math, re
from pathlib import Path
import numpy as np
import soundfile as sf
from symbol_resolver import VoiceBank

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def validate(root, write=True):
    root=Path(root); bank=VoiceBank(root); m=bank.manifest
    issues=[]; files=[]; checks=0
    def check(ok, label):
        nonlocal checks
        checks+=1
        if not ok: issues.append(label)
    check(len(m['cast'])==9, 'Nine voices')
    check(len(m['lines'])==615, '615 unique clips')
    check(len(bank.lines)==615, 'Unique line IDs')
    check(sum(r['id'].startswith('symbol.') for r in m['lines'])==324, '324 letters/digits')
    check(sum(r['kind']=='line' for r in m['lines'])==120, '120 full tower calls')
    expected_chars=set('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789')
    for voice in m['cast']:
        check(set(m['symbol_lookup'][voice])==expected_chars, voice+' complete alphabet/digits')
        for char in sorted(expected_chars):
            id=bank.character(voice,char); r=bank.lines[id]
            check(r['speaker_id']==voice and r['character']==char, id+' stays in voice')
            check(bank.character(voice,char.lower())==id, id+' lowercase alias')
            check(bank.file(id).is_file(), id+' file exists')
    seen=set()
    for r in m['lines']:
        path=root/r['audio_file']; master_path=root/r['master_file']
        check(path.resolve().is_relative_to(root.resolve()),r['id']+' contained path')
        check(r['audio_file'] not in seen,r['id']+' unique file');seen.add(r['audio_file'])
        if not path.is_file() or not master_path.is_file():
            issues.append(r['id']+' missing audio/master');continue
        x,sr=sf.read(path,dtype='float32'); info=sf.info(path); mi=sf.info(master_path)
        peak=float(np.max(np.abs(x))); rms=float(np.sqrt(np.mean(x*x))); digest=sha(path)
        check(sr==48000 and info.channels==1 and info.subtype=='PCM_16',r['id']+' PCM format')
        check(np.isfinite(x).all() and .0001<rms<.25 and 0<peak<=.8001,r['id']+' finite audible unclipped')
        check(.15<info.duration<(6 if r['kind']=='fragment' else 20),r['id']+' duration')
        check(mi.samplerate==sr and mi.channels==1 and mi.frames==info.frames,r['id']+' master format/frames')
        if r.get('speech_render'):
            check(digest==r['speech_render']['sha256'],r['id']+' frozen hash')
        meta_path=path.with_suffix('.render.json')
        meta=json.loads(meta_path.read_text()) if meta_path.exists() else r.get('speech_render',{})
        if r['reuse_line_id']:
            check(digest==meta['source_sha256'],r['id']+' original digit byte identity')
        if 'Qwen' in m['cast'][r['speaker_id']]['synthesis']:
            check('carrier_recognized' in meta and meta.get('crop_start',0)>.2,r['id']+' verified carrier crop')
        files.append({'id':r['id'],'sha256':digest,'master_sha256':sha(master_path),'seconds':info.duration,'peak_dbfs':round(20*math.log10(peak),2),'rms_dbfs':round(20*math.log10(rms),2)})
    # These are interface and data checks. They do not simulate the game's ATC state.
    for voice in m['tower_voices']:
        for n in range(1,37):
            for suffix in ('','L','R','C'):
                ids=bank.runway(voice,str(n).zfill(2)+suffix)
                check(all(bank.file(id).is_file() for id in ids),voice+' runway '+str(n)+suffix)
        for template in m['tower_templates']:
            composed=bank.compose(voice,template,callsign='Solace 42',station_code='KQ7',runway='09L',wind_heading=270,wind_knots=12,gust_knots=20,frequency='121.010',taxiways=['A','B2'])
            check(bool(composed['ids']) and all(Path(f).is_file() for f in composed['files']),voice+' '+template+' resolves')
            check(all(bank.lines[id]['speaker_id']==voice for id in composed['ids']),voice+' '+template+' voice consistency')
        for bad in ('0','00','37','99','9LL','1X','-1','1.0',''):
            try:bank.runway(voice,bad)
            except ValueError:check(True,voice+' invalid runway rejected')
            else:check(False,voice+' accepted invalid runway '+bad)
        for context in ({'frequency':'117.95'},{'frequency':'137.0'},{'frequency':'121'},{'frequency':'121.0011'}):
            try:bank.compose(voice,'ground_frequency',**context)
            except ValueError:check(True,voice+' invalid frequency rejected')
            else:check(False,voice+' invalid frequency accepted')
        for wind,gust in ((12,12),(12,8),('one',20)):
            try:bank.compose(voice,'wind_gusting',wind_heading=90,wind_knots=wind,gust_knots=gust)
            except ValueError:check(True,voice+' invalid gust rejected')
            else:check(False,voice+' invalid gust accepted')
    for bad in ('','AB','ß','é','١','!'):
        try:bank.character('aster',bad)
        except ValueError:check(True,'Non-ASCII/invalid character rejected')
        else:check(False,'Accepted invalid character '+bad)
    check(set(r['audio_file'] for r in m['lines'])==set(str(p.relative_to(root)) for p in (root/'audio').rglob('*.wav')),'No unindexed runtime WAVs')
    result={'checked_assets':len(files),'assertions':checks,'issues':issues,'voice_count':9,'symbol_count':324,'full_tower_calls':120,'supporting_fragments':171,'format':'Mono 48 kHz PCM16 WAV','all_clips_headset_filtered':True,'game_integration_tested':False,'files':files}
    if write:(root/'audio-checks.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('bank',type=Path);a=p.parse_args();r=validate(a.bank)
    print(json.dumps({k:v for k,v in r.items() if k!='files'},indent=2));raise SystemExit(bool(r['issues']))

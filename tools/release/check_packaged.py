"""Extract a final package and probe it without development-library search paths."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import zipfile
from download_metadata import inspect_package


def probe(command,folder,env,log):
    try:
        result=subprocess.run(command,cwd=folder,env=env,timeout=30,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
    except (subprocess.TimeoutExpired,OSError) as error:
        output=getattr(error,'stdout','') or ''
        if isinstance(output,bytes):output=output.decode('utf-8',errors='replace')
        log.write_text(output+'\nStartup probe failed: '+str(error)+'\n',encoding='utf-8')
        raise
    log.write_text(result.stdout,encoding='utf-8')
    return result


def run(archive,version,platform,output):
    output.mkdir(parents=True,exist_ok=True)
    record=dict(version=version,platform=platform,status='running',scope='isolated search paths, startup, backend, UI screenshots; native visual review still required')
    report=output/'result.json'
    report.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    try:
        # Remove only files owned by this checker. No stale successful image or
        # result may survive a failed rerun; preserve unrelated maintainer files.
        for name in ('version.log','backend.log','ui-zh.log','ui-en.log','packaged-zh.png','packaged-en.png'):
            (output/name).unlink(missing_ok=True)
        _run_checks(archive,version,platform,output)
    except Exception as error:
        record.update(status='failed',error=str(error));raise
    else:record['status']='passed'
    finally:report.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')


def _run_checks(archive, version, platform, output):
    inspect_package(archive, version, platform)
    with tempfile.TemporaryDirectory(prefix='aster-package-check-') as td:
        # Compare canonical paths on both sides. Windows TEMP can use an 8.3
        # short name while resolve() expands each extracted file to a long name.
        temp=Path(td).resolve()
        if platform.startswith('macos'):
            subprocess.run(['/usr/bin/ditto','-x','-k',str(archive.resolve()),str(temp)],check=True)
        elif platform=='linux-x64':
            with tarfile.open(archive,'r:gz') as a:a.extractall(temp,filter='data')
        else:
            with zipfile.ZipFile(archive) as a:
                for item in a.infolist():
                    target=(temp/item.filename).resolve()
                    if not target.is_relative_to(temp):raise ValueError('Archive path escapes staging')
                a.extractall(temp)
        folder=temp/f'Aster-{version}-{platform}'
        exe=folder/('Aster.exe' if platform=='windows-x64' else 'AppRun' if platform=='linux-x64' else 'Aster.app/Contents/MacOS/Aster')
        env=dict(os.environ)
        for name in ('LD_LIBRARY_PATH','DYLD_LIBRARY_PATH','DYLD_FRAMEWORK_PATH','DYLD_FALLBACK_LIBRARY_PATH','DYLD_FALLBACK_FRAMEWORK_PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH'):
            env.pop(name,None)
        if platform=='windows-x64':
            windows=Path(env.get('SystemRoot','C:/Windows'))
            env['PATH']=os.pathsep.join(map(str,[folder,windows/'System32',windows]))
        else:env['PATH']='/usr/bin:/bin:/usr/sbin:/sbin'
        env.update(QT_QPA_PLATFORM='offscreen',QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES='1',OPENBLAS_NUM_THREADS='1',XDG_CONFIG_HOME=str(temp/'config'),XDG_DATA_HOME=str(temp/'data'))
        for name,args in [('version',['--version']),('backend',['--numeric-backend'])]:
            result=probe([str(exe),*args],folder,env,output/(name+'.log'))
            if result.returncode:raise ValueError(name+' probe failed')
            if name=='version' and result.stdout.strip()!='Aster_Workspace '+version:raise ValueError('Packaged version differs')
        for language in ('zh','en'):
            screenshot=(output/('packaged-'+language+'.png')).resolve()
            result=probe([str(exe),'--database',str(temp/(language+'.sqlite')),'--language',language,'--release-smoke-output',str(screenshot)],folder,env,output/('ui-'+language+'.log'))
            if result.returncode or not screenshot.is_file():raise ValueError('Packaged UI smoke check failed')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('archive',type=Path);p.add_argument('--version',required=True);p.add_argument('--platform',required=True);p.add_argument('--output',required=True,type=Path);a=p.parse_args();run(a.archive,a.version,a.platform,a.output)

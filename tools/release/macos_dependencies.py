"""Complete and verify a macdeployqt bundle, including native scientific dylibs.

Run only on macOS, before signing. All non-system imports must resolve inside
the final bundle; no developer machine paths are permitted in the result.
"""
import argparse
import hashlib
import os
import tempfile
from pathlib import Path
import re
import shutil
import subprocess

MAGIC = {bytes.fromhex(v) for v in ['feedface', 'feedfacf', 'cefaedfe', 'cffaedfe',
                                    'cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca']}

def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()

def macho(p):
    if not p.is_file() or p.is_symlink():
        return False
    with p.open('rb') as stream:
        return stream.read(4) in MAGIC

def imports(p):
    return [line.strip().split(' (compatibility version')[0]
            for line in run('otool', '-L', p).splitlines()[1:] if ' (compatibility version' in line]

def rpaths(p):
    return list(dict.fromkeys(re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', run('otool', '-l', p))))

def system(path):
    return path.startswith(('/usr/lib/', '/System/Library/'))

def select_slice(binary, arch):
    arches = run('lipo', '-archs', binary).split()
    if arch not in arches:
        raise SystemExit(f'Architecture {arch} missing in {binary}: {arches}')
    if len(arches) > 1:
        # Work on the packaged copy only. An install_name_tool edit on a fat
        # binary would touch its other architecture as well.
        with tempfile.TemporaryDirectory(dir=binary.parent) as work:
            thin = Path(work) / binary.name
            subprocess.check_call(['lipo', str(binary), '-thin', arch, '-output', str(thin)])
            shutil.copymode(binary, thin)
            thin.replace(binary)


def anchored(path, loader, executable):
    for anchor, directory in [('@loader_path', loader), ('@executable_path', executable)]:
        if path == anchor:
            return directory
        if path.startswith(anchor + '/'):
            return directory / path[len(anchor) + 1:]
    if path.startswith('/'):
        return Path(path)
    return None


def install_id(binary):
    lines = run('otool', '-D', binary).splitlines()
    return lines[1].strip() if len(lines) > 1 else None


def image_imports(binary, identity):
    return [dep for dep in dict.fromkeys(imports(binary)) if dep != identity]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('--arch', choices=['arm64', 'x86_64'], required=True)
    parser.add_argument('--library-dir', type=Path, action='append', default=[])
    args = parser.parse_args()
    app = args.app.resolve()
    frameworks = app / 'Contents/Frameworks'
    frameworks.mkdir(exist_ok=True)
    executable_dir = app / 'Contents/MacOS'
    main_exe = executable_dir / 'Aster'
    if not macho(main_exe):
        raise SystemExit(f'Missing Mach-O executable: {main_exe}')
    bundle_rpath = '@executable_path/../Frameworks'
    pending = [(p, p) for p in app.rglob('*') if macho(p)]
    seen = set()
    # Keep original digests even after a bundled dylib is thinned or relocated.
    digests = {p: hashlib.sha256(p.read_bytes()).digest()
               for p, _ in pending if p.parent == frameworks and p.suffix == '.dylib'}
    while pending:
        binary, origin = pending.pop()
        if binary in seen:
            continue
        seen.add(binary)
        select_slice(binary, args.arch)
        paths = rpaths(binary)
        edits = []
        identity = install_id(binary)
        for dep in image_imports(binary, identity):
            if system(dep):
                continue
            if dep.startswith('@rpath/'):
                suffix = dep[len('@rpath/'):]
                bases = [frameworks]
                for path in paths:
                    base = anchored(path, origin.parent, executable_dir)
                    if base is not None:
                        bases.append(base)
                bases.extend([origin.parent, *args.library_dir])
                candidates = [base / suffix for base in bases]
            else:
                candidates = [anchored(dep, origin.parent, executable_dir)]
            target = next((p.resolve() for p in candidates if p is not None and p.is_file()), None)
            if target is None:
                raise SystemExit(f'Unresolved dependency: {dep} required by {binary}')
            if not target.is_relative_to(app):
                source = target
                if source.suffix != '.dylib' or '.framework' in str(source):
                    raise SystemExit(f'macdeployqt did not deploy framework: {source}')
                target = frameworks / source.name
                digest = hashlib.sha256(source.read_bytes()).digest()
                if target.exists():
                    if digests.get(target) != digest:
                        raise SystemExit(f'Dependency filename collision: {source} and {target}')
                else:
                    shutil.copy2(source, target)
                    digests[target] = digest
                    pending.append((target, source))
            # Preserve valid Qt load commands. The main executable supplies the
            # Frameworks search path to both the application and its plugins.
            if dep.startswith('@rpath/') and (frameworks / suffix).resolve() == target:
                continue
            staged = anchored(dep, binary.parent, executable_dir)
            if dep.startswith(('@loader_path/', '@executable_path/')) and staged.resolve() == target:
                continue
            replacement = '@loader_path/' + os.path.relpath(target, binary.parent)
            edits.extend(['-change', dep, replacement])
        if identity and identity.startswith('/') and binary.is_relative_to(frameworks):
            edits.extend(['-id', '@rpath/' + binary.relative_to(frameworks).as_posix()])
        for path in paths:
            resolved = anchored(path, binary.parent, executable_dir)
            if not system(path + '/') and (path.startswith('/') or resolved is None
                                           or not resolved.resolve().is_relative_to(app)):
                edits.extend(['-delete_rpath', path])
        if binary == main_exe and bundle_rpath not in paths:
            edits.extend(['-add_rpath', bundle_rpath])
        if edits:
            binary.chmod(binary.stat().st_mode | 0o200)
            subprocess.check_call(['install_name_tool', *edits, str(binary)])

    if bundle_rpath not in rpaths(main_exe):
        raise SystemExit(f'Missing bundle run path in {main_exe}')
    for binary in seen:
        if run('lipo', '-archs', binary).split() != [args.arch]:
            raise SystemExit(f'Unexpected architecture in {binary}')
        local_paths = rpaths(binary)
        for path in local_paths:
            base = anchored(path, binary.parent, executable_dir)
            if not system(path + '/') and (path.startswith('/') or base is None
                                           or not base.resolve().is_relative_to(app)):
                raise SystemExit(f'Non-portable run path remains: {binary}: {path}')
        for dep in image_imports(binary, install_id(binary)):
            if system(dep):
                continue
            if dep.startswith('@rpath/'):
                candidates = [base / dep[len('@rpath/'):] for path in local_paths
                              if (base := anchored(path, binary.parent, executable_dir)) is not None]
                candidates.append(frameworks / dep[len('@rpath/'):])
            elif dep.startswith(('@loader_path/', '@executable_path/')):
                candidates = [anchored(dep, binary.parent, executable_dir)]
            else:
                candidates = []
            resolved = next((p.resolve() for p in candidates if p.is_file()), None)
            if resolved not in seen:
                raise SystemExit(f'Non-portable dependency remains: {binary}: {dep}')
    print(f'Verified {len(seen)} Mach-O files for {args.arch}; all non-system imports are bundled.')

if __name__ == '__main__':
    main()

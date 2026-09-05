import argparse
import json
import os
import re
import shutil
import socketserver
import sqlite3
import subprocess
import tarfile
import tempfile
import threading
import time
import urllib.parse
import urllib.request
from http.server import SimpleHTTPRequestHandler
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / 'bin'
TESTS = ROOT / 'test-files'
DOCS = ROOT / 'docs'
GENERATED_DOCS = DOCS / 'generated'
REGISTRY = ROOT / 'packages' / 'registry'
LOCAL_REGISTRY = ROOT / 'packages' / 'local-registry'


def run_process(command, cwd=ROOT, capture=False):
    return subprocess.run(command, cwd=cwd, text=True, capture_output=capture)


def dmc_binary():
    candidates = [BIN / 'dmc-native', BIN / 'dmc-native.exe']
    for path in candidates:
        if path.exists():
            return str(path)
    raise SystemExit('dmc compiler binary is missing; run make -C build all')


def dmc_files(path):
    path = Path(path)
    if path.is_file():
        return [path]
    return sorted(p for p in path.rglob('*.dmc') if p.is_file())


def attributes(path):
    text = path.read_text(errors='replace')
    return len(re.findall(r'#\[test\]', text)), len(re.findall(r'#\[bench\]', text))


def command_test(args):
    requested = Path(args.path)
    files = dmc_files(requested)
    explicit_file = requested.is_file()
    if not explicit_file:
        files = [p for p in files if attributes(p)[0] or attributes(p)[1]]
    total_tests = sum(attributes(p)[0] for p in files)
    total_bench = sum(attributes(p)[1] for p in files)
    passed_tests = 0
    passed_benchmarks = 0
    failed = 0
    timings = []
    with tempfile.TemporaryDirectory(prefix='dmc-tests-') as temp:
        temp = Path(temp)
        for source in files:
            if attributes(source)[0] == 0 and not args.bench:
                continue
            generated = temp / f'{source.stem}.c'
            executable = temp / f'{source.stem}.bin'
            compile_result = run_process([dmc_binary(), str(source), '-o', str(generated)], capture=True)
            if compile_result.returncode != 0:
                failed += 1
                print(f'FAIL compile {source}')
                print(compile_result.stderr.strip())
                continue
            cc = os.environ.get('CC', 'cc')
            build_result = run_process([cc, '-std=c11', '-O0', '-DDMC_SQLITE', str(generated), '-o', str(executable), '-lm', '-l:libsqlite3.so.0'], capture=True)
            if build_result.returncode != 0:
                failed += 1
                print(f'FAIL build {source}')
                print(build_result.stderr.strip())
                continue
            start = time.perf_counter()
            run_args = [str(executable)] + (['--bench'] if args.bench else [])
            result = run_process(run_args, capture=True)
            elapsed = time.perf_counter() - start
            timings.append((source, elapsed))
            if result.returncode == 0:
                if args.bench:
                    passed_benchmarks += attributes(source)[1]
                else:
                    passed_tests += attributes(source)[0]
                print(f'PASS {source} ({elapsed:.4f}s)')
            else:
                failed += 1
                print(f'FAIL {source} ({result.returncode})')
                print(result.stderr.strip())
    if args.coverage:
        total_lines = sum(len(p.read_text(errors='replace').splitlines()) for p in files)
        executable_lines = sum(len([line for line in p.read_text(errors='replace').splitlines() if line.strip() and not line.lstrip().startswith('#')]) for p in files)
        coverage = (executable_lines / total_lines * 100.0) if total_lines else 100.0
        coverage_path = DOCS / 'coverage.txt'
        coverage_path.write_text(f'files={len(files)}\nsource_lines={total_lines}\nexecutable_lines={executable_lines}\nline_inventory_percent={coverage:.2f}\n')
        print(f'Coverage report: {coverage_path}')
    print(f'{passed_tests} tests passed, {passed_benchmarks} benchmarks passed, {failed} failed, {total_tests} tests discovered, {total_bench} benchmarks discovered')
    return 1 if failed else 0


def html_escape(value):
    return value.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')


def parse_docs(source):
    text = source.read_text(errors='replace')
    records = []
    for match in re.finditer(r'/\*\*(.*?)\*/\s*fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((.*?)\)\s*(?:->\s*([A-Za-z_][A-Za-z0-9_]*))?', text, re.S):
        body, name, params, return_type = match.groups()
        lines = [line.strip(' *\t') for line in body.splitlines()]
        section = 'description'
        record = {'name': name, 'description': [], 'parameters': [], 'returns': [], 'example': []}
        for line in lines:
            if not line:
                continue
            header = line.lower()
            if header in ('parameters', 'returns', 'example'):
                section = 'parameters' if header == 'parameters' else header
            elif section == 'parameters':
                record['parameters'].append(line)
            else:
                record[section].append(line)
        if return_type:
            record['returns'].append(return_type)
        records.append(record)
    return records


def parse_markdown_docs(source):
    lines = source.read_text(errors='replace').splitlines()
    records = []
    current = None
    section = 'description'
    for line in lines:
        if line.startswith('## '):
            if current:
                records.append(current)
            current = {'name': line[3:].strip(), 'description': [], 'parameters': [], 'returns': [], 'example': []}
            section = 'description'
        elif current and line.startswith('### '):
            section_name = line[4:].strip().lower()
            section = section_name if section_name in ('parameters', 'returns', 'example') else 'description'
        elif current and line.strip() and not line.startswith('#') and not line.startswith('```'):
            current[section].append(line.strip('- '))
    if current:
        records.append(current)
    return records

def command_doc(args):
    GENERATED_DOCS.mkdir(parents=True, exist_ok=True)
    records = []
    for source in dmc_files(args.path):
        records.extend(parse_docs(source))
    doc_root = Path(args.path) if Path(args.path).is_dir() else ROOT / "docs"
    for source in sorted(doc_root.rglob("*.md")):
        if source.name != "todo-status.md":
            records.extend(parse_markdown_docs(source))
    index = ['<!doctype html><html><head><meta charset="utf-8"><title>Demonic C Documentation</title></head><body><h1>Demonic C Documentation</h1>']
    if records:
        for record in records:
            index.append(f'<article><h2>{html_escape(record["name"])}</h2>')
            index.append(f'<p>{html_escape(" ".join(record["description"]))}</p>')
            if record['parameters']:
                index.append('<h3>Parameters</h3><ul>' + ''.join(f'<li>{html_escape(x)}</li>' for x in record['parameters']) + '</ul>')
            if record['returns']:
                index.append('<h3>Returns</h3><p>' + html_escape(' '.join(record['returns'])) + '</p>')
            if record['example']:
                index.append('<h3>Example</h3><pre>' + html_escape('\n'.join(record['example'])) + '</pre>')
            index.append('</article>')
    else:
        for source in dmc_files(args.path):
            index.append(f'<h2>{html_escape(str(source.relative_to(ROOT)))}</h2><pre>{html_escape(source.read_text(errors="replace"))}</pre>')
    index.append('</body></html>')
    (GENERATED_DOCS / 'index.html').write_text(''.join(index))
    if args.publish:
        LOCAL_REGISTRY.mkdir(parents=True, exist_ok=True)
        shutil.copy2(GENERATED_DOCS / 'index.html', LOCAL_REGISTRY / 'documentation.html')
        print('Documentation published to packages/local-registry/documentation.html')
    elif args.serve:
        os.chdir(GENERATED_DOCS)
        server = socketserver.TCPServer(('127.0.0.1', args.port), SimpleHTTPRequestHandler)
        print(f'Documentation server listening on http://127.0.0.1:{args.port}')
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            server.server_close()
    else:
        print(f'Documentation generated at {GENERATED_DOCS / "index.html"}')
    return 0


def command_build(args):
    source = Path(args.source)
    output = Path(args.output) if args.output else ROOT / 'bin' / source.stem
    output.parent.mkdir(parents=True, exist_ok=True)
    generated = ROOT / 'dist' / f'{source.stem}.generated.c'
    generated.parent.mkdir(parents=True, exist_ok=True)
    result = run_process([dmc_binary(), str(source), '-o', str(generated)], capture=True)
    if result.returncode:
        print(result.stderr)
        return result.returncode
    cc = os.environ.get('CC', 'cc')
    flags = ['-O2' if args.release else '-O0', str(generated), '-o', str(output), '-lm']
    if not args.static:
        flags[1:1] = ['-DDMC_SQLITE']
        flags.extend(['-l:libsqlite3.so.0'])
    if args.static:
        flags.insert(0, '-static')
    result = run_process([cc] + flags, capture=True)
    generated.unlink(missing_ok=True)
    if result.returncode:
        print(result.stderr)
        return result.returncode
    print(f'Built {output}')
    return 0


def command_bundle(args):
    Path('dist').mkdir(exist_ok=True)
    output = ROOT / 'dist' / 'dmc-project.tar.gz'
    with tarfile.open(output, 'w:gz') as archive:
        for name in ['compiler', 'runtime', 'stdlib', 'tests', 'test-files', 'docs', 'packages', 'editor', 'playground', 'config', 'assets', 'dmc', 'dmc.bat']:
            path = ROOT / name
            if path.exists():
                archive.add(path, arcname=name)
    print(f'Bundle created at {output}')
    return 0


def package_meta():
    path = ROOT / 'config' / 'dmc.toml'
    metadata = {'name': ROOT.name, 'version': '0.1.0', 'description': 'Demonic C package', 'license': 'MIT'}
    if path.exists():
        for line in path.read_text().splitlines():
            if '=' in line and not line.strip().startswith('#'):
                key, value = line.split('=', 1)
                metadata[key.strip()] = value.strip().strip('"')
    return metadata


def command_publish(args):
    LOCAL_REGISTRY.mkdir(parents=True, exist_ok=True)
    metadata = package_meta()
    package_path = LOCAL_REGISTRY / f'{metadata.get("name", ROOT.name)}-{metadata.get("version", "0.1.0")}.json'
    package_path.write_text(json.dumps(metadata, indent=2) + '\n')
    print(f'Package published to {package_path}')
    return 0


def command_login(args):
    LOCAL_REGISTRY.mkdir(parents=True, exist_ok=True)
    (LOCAL_REGISTRY / 'credentials.json').write_text(json.dumps({'authenticated': True, 'mode': 'local'}, indent=2) + '\n')
    print('Registry login completed in local development mode')
    return 0


def command_deprecate(args):
    LOCAL_REGISTRY.mkdir(parents=True, exist_ok=True)
    path = LOCAL_REGISTRY / f'{args.package}.deprecated.json'
    path.write_text(json.dumps({'package': args.package, 'deprecated': True}, indent=2) + '\n')
    print(f'Marked {args.package} as deprecated')
    return 0


def command_ci(args):
    commands = [['test'], ['doc'], ['build', '--release']]
    for command in commands:
        result = dispatch(command)
        if result:
            return result
    print('CI workflow completed')
    return 0


def dispatch(argv):
    if not argv:
        return 1
    command = argv[0]
    parser = argparse.ArgumentParser(prog=f'dmc {command}')
    if command == 'test':
        parser.add_argument('--coverage', action='store_true')
        parser.add_argument('--bench', action='store_true')
        parser.add_argument('path', nargs='?', default=str(TESTS))
        return command_test(parser.parse_args(argv[1:]))
    if command == 'doc':
        parser.add_argument('--serve', action='store_true')
        parser.add_argument('--publish', action='store_true')
        parser.add_argument('--port', type=int, default=8765)
        parser.add_argument('path', nargs='?', default=str(ROOT))
        return command_doc(parser.parse_args(argv[1:]))
    if command == 'build':
        parser.add_argument('--release', action='store_true')
        parser.add_argument('--static', action='store_true')
        parser.add_argument('source', nargs='?', default=str(ROOT / 'examples' / 'my-test-project' / 'src' / 'main.dmc'))
        parser.add_argument('-o', '--output')
        return command_build(parser.parse_args(argv[1:]))
    if command == 'bundle':
        return command_bundle(parser.parse_args([]))
    if command == 'publish':
        return command_publish(parser.parse_args([]))
    if command == 'login':
        return command_login(parser.parse_args([]))
    if command == 'deprecate':
        parser.add_argument('package')
        return command_deprecate(parser.parse_args(argv[1:]))
    if command == 'ci':
        return command_ci(parser.parse_args([]))
    result = run_process([dmc_binary()] + argv, cwd=ROOT)
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(dispatch(__import__('sys').argv[1:]))

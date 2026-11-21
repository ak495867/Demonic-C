#!/usr/bin/env python3
import argparse
import glob
import hashlib
import os
import random
import re
import subprocess
import sys
import tempfile

TOKENS = ['{', '}', '(', ')', '[', ']', ';', '"', "'", 'fn', 'struct', 'enum', 'match', 'let', 'return',
          '->', '<', '>', '::', '#[test]', '/*', '//', '\\', '\x00', '0x', '1e999', '"""', 'defer', 'impl',
          'trait', 'c_code {', 'hdl', 'module', 'comptime', 'extern']
SWAPS = ['int', 'f64', 'string', 'bool', '0', '1', '-1', '"x"', 'true', 'null', 'x', 'y', '+', '-', '*', '/',
         '==', '<', '&&', '!']


def mutate_bytes(rng, data):
    src = bytearray(data)
    for _ in range(rng.randint(1, 6)):
        op = rng.randint(0, 4)
        pos = rng.randrange(len(src) + 1)
        if op == 0 and src:
            del src[pos:pos + rng.randint(1, 40)]
        elif op == 1:
            src[pos:pos] = rng.choice(TOKENS).encode()
        elif op == 2 and src:
            src[pos:pos + 1] = bytes([rng.randrange(256)])
        elif op == 3 and src:
            other = rng.randrange(len(src))
            src[pos:pos] = src[other:other + rng.randint(1, 60)]
        elif op == 4 and len(src) > 1:
            src = src[:pos]
    return bytes(src)


def mutate_tokens(rng, data):
    text = data.decode('latin1')
    parts = re.findall(r'\w+|\W', text)
    idx = [k for k, t in enumerate(parts) if re.fullmatch(r'\w+', t) or t in '+-*/<>!=&|']
    for _ in range(rng.randint(1, 3)):
        if idx:
            parts[rng.choice(idx)] = rng.choice(SWAPS)
    return ''.join(parts).encode('latin1')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--compiler', required=True)
    ap.add_argument('--corpus', default='test-files')
    ap.add_argument('--iterations', type=int, default=300)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--mode', choices=['bytes', 'tokens'], default='bytes')
    ap.add_argument('--timeout', type=int, default=20)
    ap.add_argument('--keep', default='')
    args = ap.parse_args()

    rng = random.Random(args.seed)
    files = [f for f in glob.glob(os.path.join(args.corpus, '*.dmc')) if os.path.getsize(f) < 8000]
    if not files:
        print('no corpus files found', file=sys.stderr)
        return 2
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1')
    mutate = mutate_bytes if args.mode == 'bytes' else mutate_tokens
    found = {}
    with tempfile.TemporaryDirectory() as work:
        src_path = os.path.join(work, 'fuzz_input.dmc')
        out_path = os.path.join(work, 'fuzz_output.c')
        for _ in range(args.iterations):
            with open(rng.choice(files), 'rb') as handle:
                data = mutate(rng, handle.read())
            with open(src_path, 'wb') as handle:
                handle.write(data)
            kind = None
            err = ''
            try:
                run = subprocess.run([args.compiler, 'build', src_path, '--emit-c', '-o', out_path],
                                     capture_output=True, timeout=args.timeout, env=env, cwd=work)
                err = run.stderr.decode('latin1')
                if run.returncode < 0 or run.returncode >= 128 or 'Sanitizer' in err or 'runtime error' in err:
                    kind = 'crash'
            except subprocess.TimeoutExpired:
                kind = 'hang'
            if kind:
                frames = [l for l in err.splitlines() if ' in ' in l or 'runtime error' in l]
                sig = kind + '_' + hashlib.md5('\n'.join(frames)[:600].encode() or kind.encode()).hexdigest()[:8]
                if sig not in found:
                    found[sig] = err.splitlines()[0][:160] if err else kind
                    if args.keep:
                        os.makedirs(args.keep, exist_ok=True)
                        with open(os.path.join(args.keep, sig + '.dmc'), 'wb') as handle:
                            handle.write(data)
                    print(sig, found[sig])
    print('iterations=%d unique_failures=%d' % (args.iterations, len(found)))
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())

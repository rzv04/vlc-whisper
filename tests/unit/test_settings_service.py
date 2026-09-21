"""Exercise the production C adapter through private pipes without VLC/network."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
failures = []

def check(name, condition):
    print(('PASS ' if condition else 'FAIL ') + name)
    if not condition:
        failures.append(name)

with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / 'service'
    subprocess.run([
        os.environ.get('CC', 'cc'), '-std=c17', '-Wall', '-Wextra', '-Werror',
        '-I' + str(ROOT / 'worker/include'), '-I' + str(ROOT / 'protocol/include'),
        str(ROOT / 'worker/src/vw_settings_service.c'),
        str(ROOT / 'tests/support/vw_settings_service_fake.c'), '-o', str(exe)
    ], check=True)

    def run(args, data=b'', **env):
        return subprocess.run([str(exe), *args], input=data, capture_output=True,
                              timeout=5, env={**os.environ, **env})

    r = run(['--settings-translate', 'auto', 'ro'], 'Hello țară'.encode())
    check('translation works without playback and preserves UTF-8',
          r.returncode == 0 and r.stdout == 'auto -> ro: Hello țară'.encode())
    for name, args, data in [
        ('unsupported target rejected', ['--settings-translate', 'en', 'auto'], b'hello'),
        ('oversized text rejected', ['--settings-translate', 'en', 'ro'], b'x' * 1024),
        ('empty text rejected', ['--settings-translate', 'en', 'ro'], b''),
        ('embedded NUL rejected', ['--settings-translate', 'en', 'ro'], b'a\0b'),
        ('unknown model rejected', ['--settings-download', 'not-a-model'], b''),
        ('extra arguments rejected', ['--settings-download', 'tiny', 'extra'], b''),
    ]:
        r = run(args, data)
        check(name, r.returncode != 0 and not r.stdout)
    r = run(['--settings-translate', 'en', 'ro'], b'hello', VW_FAKE_FAIL='1')
    check('provider failure never becomes successful empty text', r.returncode != 0 and not r.stdout)
    r = run(['--settings-download', 'tiny'], VW_FAKE_BUSY='1')
    check('destination contention fails explicitly', r.returncode != 0)

    for failure in (False, True):
        p = subprocess.Popen([str(exe), '--settings-download', 'tiny'], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             env={**os.environ, **({'VW_FAKE_FAIL': '1'} if failure else {})})
        first = p.stdout.readline()
        p.wait(timeout=5)  # Keep stdin OPEN: it represents the living settings parent.
        rest = p.stdout.read()
        check('initial idle is not completion' + (' on failure' if failure else ''),
              first and rest and (p.returncode != 0 if failure else p.returncode == 0))
        check('terminal download joins owner thread', p.stderr.read() == b'joined\n')
        p.stdin.close()

    for abort in (b'a', b''):
        r = run(['--settings-download', 'tiny'], abort, VW_FAKE_WAIT='1')
        check('abort byte or parent EOF cleans up and reports cancellation',
              r.returncode == 3 and r.stderr == b'joined\n')

    p = subprocess.Popen([str(exe), '--settings-download', 'tiny'], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    p.stdout.close()
    p.wait(timeout=5)
    check('broken result pipe still joins downloader instead of SIGPIPE termination',
          p.returncode == 4 and p.stderr.read() == b'joined\n')
    p.stdin.close()

sys.exit(bool(failures))

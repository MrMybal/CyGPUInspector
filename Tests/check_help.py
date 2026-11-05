"""Exercise native CyDoc entry points in a private installation, without changing user settings."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
from urllib.parse import unquote, urlsplit


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary-dir', type=Path, default=root / 'bin/Release')
    parser.add_argument('--output', type=Path, default=root / 'build/cydoc-native')
    args = parser.parse_args()
    output = args.output.resolve()
    runtime = output / 'installation é # % with spaces'
    runtime.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.binary_dir / 'CyGPUInspectorApp.exe', runtime)
    for folder in ('Help', 'Lang'):
        shutil.copytree(args.binary_dir / folder, runtime / folder, dirs_exist_ok=True)
    prefs = runtime / 'CyGPUInspectorApp.settings.json'
    original = json.dumps({'language': 'fr', 'keep': {'value': 42},
                           'appearance': {'theme': 'violet', 'comfortable': True, 'scale': 1.25}})
    prefs.write_text(original, encoding='utf-8')
    working = output / 'different working directory'
    working.mkdir(exist_ok=True)
    routes = json.loads((runtime / 'Help/legacy-routes.json').read_text(encoding='utf-8'))
    legacy, canonical = next((old, new) for old, new in routes['capture']['fr'].items() if old != new)
    cases = [
        ('default-en', ['--help-topic=welcome'], '/en/welcome'),
        ('french', ['--help-topic=capture', '--help-language=fr'], '/fr/capture'),
        ('argument-order', ['--help-language=fr', '--help-topic=hlsl'], '/fr/hlsl'),
        ('old-page-id', ['--help-topic=ref-MCP', '--help-language=fr'], '/fr/ref-mcp'),
        ('section', ['--help-topic=capture#choose-your-output'], '/en/capture/choose-your-output'),
        ('old-french-section', ['--help-topic=help:capture#' + legacy, '--help-language=fr'], '/fr/capture/' + canonical),
        ('media', ['--help-topic=doc:welcome?media=figure-0'], '/en/welcome?media=figure-0'),
        ('old-show-help', ['--show=help'], '/en/welcome'),
        ('language-fallback', ['--help-language=de', '--help-topic=welcome'], '/en/welcome'),
        ('missing-page', ['--help-topic=does-not-exist'], None),
        ('missing-bundle', ['--help-topic=welcome'], None),
    ]
    results = []
    for name, arguments, expected in cases:
        capture = output / (name + '.bmp')
        url_file = output / (name + '.url.txt')
        assert not url_file.exists(), 'Choose a fresh --output directory'
        moved = runtime / 'Help/manifest.json' if name == 'missing-bundle' else None
        if moved:
            moved.rename(moved.with_suffix('.backup'))
        try:
            command = [str(runtime / 'CyGPUInspectorApp.exe'), '--capture-ui=' + str(capture),
                       '--help-url-file=' + str(url_file), '--ui-size=1100x760'] + arguments
            result = subprocess.run(command, cwd=working, timeout=45)
            assert result.returncode == 0, (name, result.returncode)
            data = capture.read_bytes()
            width, height = struct.unpack_from('<ii', data, 18)
            assert data[:2] == b'BM' and width >= 640 and abs(height) >= 480
            if expected:
                uri = url_file.read_text(encoding='utf-8')
                parsed = urlsplit(uri)
                assert parsed.scheme == 'file' and not parsed.query, uri
                decoded = Path(unquote(parsed.path).lstrip('/'))
                assert decoded.resolve() == (runtime / 'Help/index.html').resolve(), uri
                assert unquote(parsed.fragment) == expected, (name, uri, expected)
                assert '%20' in uri and '%23' in uri and '%25' in uri, uri
            else:
                assert not url_file.exists(), name + ': invalid destination opened'
            assert prefs.read_text(encoding='utf-8') == original, name + ': settings changed'
            assert not (runtime / 'CyGPUInspectorApp.ini').exists(), name + ': layout written'
            results.append(name)
            print(name + ': passed; settings preserved', flush=True)
        finally:
            if moved:
                moved.with_suffix('.backup').rename(moved)
    (output / 'results.json').write_text(json.dumps({'passed': True, 'cases': results}, indent=2), encoding='utf-8')
    print(str(len(cases)) + ' native CyDoc entry-point cases passed')


if __name__ == '__main__':
    main()

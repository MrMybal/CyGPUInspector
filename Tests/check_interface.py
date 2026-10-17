"""Render the real standalone in each CyUI theme, using a private demo session.

Run after build.cmd Release: python Tests/check_interface.py
Only the selected output folder is written. No provider is connected.
"""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import time


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary-dir', type=Path, default=root / 'bin/Release')
    parser.add_argument('--output', type=Path, default=root / 'build/ui-smoke')
    args = parser.parse_args()
    output = args.output.resolve()
    runtime = output / 'runtime'
    runtime.mkdir(parents=True, exist_ok=True)
    for name in ('CyGPUInspectorApp.exe', 'CyGPUInspectorFakeSession.exe'):
        shutil.copy2(args.binary_dir / name, runtime / name)
    shutil.copytree(root / 'Lang', runtime / 'Lang', dirs_exist_ok=True)
    preferences = runtime / 'CyGPUInspectorApp.settings.json'
    original = json.dumps({'language': 'fr', 'retained_setting': {'keep': True},
                           'appearance': {'theme': 'violet', 'comfortable': False, 'scale': 1.0}})
    preferences.write_text(original, encoding='utf-8')
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    fake = subprocess.Popen([str(runtime / 'CyGPUInspectorFakeSession.exe')], cwd=runtime,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(0.6)
        cases = [(key, key, [], True) for key in ('navy', 'mid-grey', 'light-grey', 'amber-night', 'violet')]
        cases += [
            ('compact-1280', 'navy', ['--ui-size=1280x800'], True),
            ('comfortable-125', 'navy', ['--ui-size=1600x1000', '--ui-scale=1.25', '--ui-comfortable'], True),
            ('no-active-session', 'navy', [], False),
            ('light-hlsl', 'light-grey', ['--decompile', '--show=hlsl'], True),
            ('assistant', 'navy', ['--show=assistant'], True),
            ('resources-preview', 'navy', ['--show=resources,preview'], True),
            ('deep-capture', 'navy', ['--show=deep-capture'], True),
            ('continuous', 'navy', ['--show=continuous'], True),
            ('frame-graph', 'light-grey', ['--show=graph'], True),
            ('captures', 'navy', ['--show=captures'], True),
            ('stored-appearance', None, [], False),
        ]
        for name, theme, extra, live in cases:
            image = output / (name + '.bmp')
            command = [str(runtime / 'CyGPUInspectorApp.exe'), '--capture-ui=' + str(image)]
            if theme:
                command.append('--ui-theme=' + theme)
            if live:
                command += ['--connect=' + str(fake.pid), '--level=pass-timing', '--select-shader=4']
            result = subprocess.run(command + extra, cwd=runtime, startupinfo=startup, timeout=40)
            assert result.returncode == 0, (name, result.returncode, str(image) + '.errors.txt')
            data = image.read_bytes()
            assert data[:2] == b'BM', name
            width, height = struct.unpack_from('<ii', data, 18)
            assert width >= 640 and abs(height) >= 480 and len(data) > 100000, name
            if name == 'compact-1280':
                assert (width, abs(height)) == (1280, 800)
            if name == 'comfortable-125':
                assert (width, abs(height)) == (1600, 1000)
            assert preferences.read_text(encoding='utf-8') == original, 'Existing preferences changed: ' + name
            assert not (runtime / 'CyGPUInspectorApp.ini').exists(), 'A visual check saved a layout'
            print('{}: rendered {}x{}, no ImGui errors, preferences preserved'.format(name, width, abs(height)), flush=True)
    finally:
        fake.terminate()
        fake.wait(timeout=5)
    print('{} real interface checks passed'.format(len(cases)))


if __name__ == '__main__':
    main()

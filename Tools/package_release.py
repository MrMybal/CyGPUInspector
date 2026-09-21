#!/usr/bin/env python3
"""Assembles the CyGPUInspector release zip from the sources and the build outputs.

    python Tools/package_release.py --plugin-builds C:/CyGPUInspectorBuild [--dxc DIR] [--version 0.1.0]

Takes:
  bin/Release/                 the standalone, the MCP server, the add-ons, the tests (build first)
  --plugin-builds DIR          RunUAT BuildPlugin outputs, one per engine: DIR/UE5.8/CyGPUInspector,
                               DIR/UE5.3/CyGPUInspector. Build them from a neutral folder: Unreal
                               writes the build folder's path into the DLLs.
  --dxc DIR                    dxcompiler.dll, dxil.dll and the licence files that come with them.
                               Without it, the package has no Shader Model 6 support.
  Lang/, Docs/, Packaging/     translations, documentation, start-here notes and demo scripts
  ThirdParty/                  the licences of what is compiled into the binaries

ReShade64.dll goes into each Unreal plugin with its licence notices (Tools/fetch_reshade.py). It is
never in the repository: the zip is the only place it ships.

Writes build/Release/CyGPUInspector-<version>-win64/, the zip next to it and its SHA-256, then
checks the result: no symbol files, no user data, no local path inside the binaries.

Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'bin', 'Release')


def copy(source, target):
    os.makedirs(os.path.dirname(target), exist_ok=True)
    shutil.copy2(source, target)


def copy_tree(source, target, skip=()):
    for folder, _, files in os.walk(source):
        for name in files:
            path = os.path.join(folder, name)
            relative = os.path.relpath(path, source)
            if any(part in skip for part in relative.replace('\\', '/').split('/')) or name.lower().endswith(skip):
                continue
            copy(path, os.path.join(target, relative))


def header_licence(path):
    """The licence comment at the top of a source file, without the comment markers."""
    lines = []
    for line in open(path, encoding='utf-8', errors='replace'):
        stripped = line.strip()
        if not (stripped.startswith(('//', '/*', '*')) or stripped == ''):
            break
        lines.append(re.sub(r'^\s*(/\*+|\*+/|\*|//)\s?', '', line.rstrip()).rstrip('*/ ').rstrip())
    return '\n'.join(lines).strip() + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--version', default='0.1.0')
    parser.add_argument('--plugin-builds', required=True)
    parser.add_argument('--dxc')
    args = parser.parse_args()

    name = 'CyGPUInspector-{}-win64'.format(args.version)
    out_root = os.path.join(ROOT, 'build', 'Release')
    stage = os.path.join(out_root, name)
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    os.makedirs(stage)

    # Application, add-ons, tests.
    for exe in ('CyGPUInspectorApp.exe', 'CyGPUInspectorMCP.exe'):
        copy(os.path.join(BIN, exe), os.path.join(stage, 'App', exe))
    for lang in ('fr.json', 'template.json'):
        copy(os.path.join(ROOT, 'Lang', lang), os.path.join(stage, 'App', 'Lang', lang))
    for addon in ('CyGPUInspectorRS.addon64', 'CyGPUInjector.addon64'):
        copy(os.path.join(BIN, addon), os.path.join(stage, 'Addons', addon))
    for exe in ('CyGPUInspectorCoreTests.exe', 'CyGPUInspectorIpcTests.exe', 'CyGPUInspectorShaderTests.exe',
                'CyGPUInspectorFakeSession.exe', 'CyGPUInspectorMCP.exe'):
        copy(os.path.join(BIN, exe), os.path.join(stage, 'TestWithoutAGame', exe))
    # The IPC tests load the add-on the way the Unreal plugin does: it sits next to them.
    copy(os.path.join(BIN, 'CyGPUInspectorRS.addon64'), os.path.join(stage, 'TestWithoutAGame', 'CyGPUInspectorRS.addon64'))
    copy_tree(os.path.join(ROOT, 'Packaging', 'TestWithoutAGame'), os.path.join(stage, 'TestWithoutAGame'))

    # The DirectX Shader Compiler, when given, with the licences that come with it.
    if args.dxc:
        for dll in ('dxcompiler.dll', 'dxil.dll'):
            copy(os.path.join(args.dxc, dll), os.path.join(stage, 'App', dll))
        for entry in os.listdir(args.dxc):
            if re.match(r'(?i)(license|licence|notice|third)', entry) and os.path.isfile(os.path.join(args.dxc, entry)):
                copy(os.path.join(args.dxc, entry), os.path.join(stage, 'Licenses', 'DirectXShaderCompiler', entry))
    else:
        print('Warning: no --dxc: the package has no Shader Model 6 support.')

    # The Unreal plugin, one per engine version, without symbols or intermediate files.
    for engine in sorted(os.listdir(args.plugin_builds)):
        plugin = os.path.join(args.plugin_builds, engine, 'CyGPUInspector')
        if not os.path.isfile(os.path.join(plugin, 'CyGPUInspector.uplugin')):
            continue
        target = os.path.join(stage, 'Unreal', engine, 'CyGPUInspector')
        copy(os.path.join(plugin, 'CyGPUInspector.uplugin'), os.path.join(target, 'CyGPUInspector.uplugin'))
        copy_tree(os.path.join(plugin, 'Resources'), os.path.join(target, 'Resources'))
        copy_tree(os.path.join(plugin, 'Source'), os.path.join(target, 'Source'))
        copy_tree(os.path.join(plugin, 'Binaries'), os.path.join(target, 'Binaries'), skip=('.pdb',))
        third_party = os.path.join(target, 'Binaries', 'ThirdParty', 'CyGPUInspector', 'Win64')
        for required in ('ReShade64.dll', 'CyGPUInspectorRS.addon64', 'LICENSE-ReShade.txt', 'NOTICES-ReShade.txt'):
            if not os.path.isfile(os.path.join(third_party, required)):
                sys.exit('{} is missing {}: run Tools/fetch_reshade.py --with-addon, then rebuild the plugin.'
                         .format(engine, required))
        # The add-on in the plugin is the one of this build, not an older copy.
        copy(os.path.join(BIN, 'CyGPUInspectorRS.addon64'), os.path.join(third_party, 'CyGPUInspectorRS.addon64'))

    # Documentation and notices.
    for doc in ('README.md', 'README.fr.md', 'LICENSE', 'THIRD-PARTY.md'):
        copy(os.path.join(ROOT, doc), os.path.join(stage, doc))
    for note in ('START-HERE.txt', 'COMMENCER-ICI.txt'):
        copy(os.path.join(ROOT, 'Packaging', note), os.path.join(stage, note))
    if not args.dxc:
        # The notes list the DXC DLLs in App\: say instead that they are not included.
        for note, line, replacement in (
                ('START-HERE.txt', 'dxcompiler.dll, dxil.dll       Microsoft DirectX Shader Compiler, needed for Shader Model 6',
                 '(dxcompiler.dll, dxil.dll)     not included: put Microsoft DXC here for Shader Model 6\n'
                 '                                   (without it, the Windows SDK copy is used when installed)'),
                ('COMMENCER-ICI.txt', 'dxcompiler.dll, dxil.dll       DirectX Shader Compiler de Microsoft, nécessaire au Shader Model 6',
                 "(dxcompiler.dll, dxil.dll)     non inclus : mettez ici le DXC de Microsoft pour le Shader Model 6\n"
                 "                                   (sinon, la copie du Windows SDK est utilisée s'il est installé)")):
            path = os.path.join(stage, note)
            text = open(path, encoding='utf-8').read()
            assert line in text, (note, line)
            with open(path, 'w', encoding='utf-8', newline='') as file:
                file.write(text.replace(line, replacement))
    copy_tree(os.path.join(ROOT, 'Docs'), os.path.join(stage, 'Docs'))

    # The licences of what is compiled into the binaries.
    licences = {
        'ReShade-SDK/LICENSE.md': 'ThirdParty/reshade/LICENSE.md',
        'ReShade/LICENSE-ReShade.txt': 'CyGPUInspectorUnreal/CyGPUInspector/Binaries/ThirdParty/CyGPUInspector/Win64/LICENSE-ReShade.txt',
        'ReShade/NOTICES-ReShade.txt': 'CyGPUInspectorUnreal/CyGPUInspector/Binaries/ThirdParty/CyGPUInspector/Win64/NOTICES-ReShade.txt',
        'DearImGui/LICENSE.txt': 'ThirdParty/imgui/LICENSE.txt',
        '3Dmigoto/LICENSE.GPL.txt': 'ThirdParty/hlsldecompiler/LICENSE.GPL.txt',
        '3Dmigoto/ORIGIN.md': 'ThirdParty/hlsldecompiler/ORIGIN.md',
        'dxil-spirv/LICENSE.MIT': 'ThirdParty/dxil-spirv/LICENSE.MIT',
        'dxbc-spirv/LICENSE': 'ThirdParty/dxil-spirv/subprojects/dxbc-spirv/LICENSE',
        'SPIRV-Headers/LICENSE': 'ThirdParty/dxil-spirv/third_party/spirv-headers/LICENSE',
        'SPIRV-Cross/LICENSE': 'ThirdParty/SPIRV-Cross/LICENSE',
    }
    for target, source in licences.items():
        copy(os.path.join(ROOT, source), os.path.join(stage, 'Licenses', target))
    copy_tree(os.path.join(ROOT, 'ThirdParty', 'SPIRV-Cross', 'LICENSES'), os.path.join(stage, 'Licenses', 'SPIRV-Cross', 'LICENSES'))
    for target, source in {
        'RenderDoc-bitcode-reader/LICENSE.txt': 'ThirdParty/dxil-spirv/third_party/bc-decoder/llvm_bitreader.h',
        'glslang-SPIR-V-builder/LICENSE.txt': 'ThirdParty/dxil-spirv/third_party/glslang-spirv/Logger.h',
    }.items():
        path = os.path.join(stage, 'Licenses', target)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8', newline='\n') as file:
            file.write(header_licence(os.path.join(ROOT, source)))

    # Checks: nothing that must not ship, no local path inside a binary.
    problems = []
    for folder, _, files in os.walk(stage):
        for file in files:
            path = os.path.join(folder, file)
            relative = os.path.relpath(path, stage)
            if file.lower().endswith(('.pdb', '.ini', '.ilk', '.exp', '.lib', '.obj')) or \
               re.search(r'(^|[\\/])(Captures|Database|Images|Saved|Intermediate)([\\/]|$)', relative):
                problems.append('should not ship: ' + relative)
            if file.lower().endswith(('.exe', '.dll', '.addon64')):
                data = open(path, 'rb').read().lower()
                # Paths of the machine that built it, as ASCII or UTF-16 strings. The Unreal DLLs keep
                # the neutral folder they were built from (C:\CyGPUInspectorBuild), which says nothing
                # about anyone. ReShade64.dll is the official build and is not ours to check.
                for text in ('\\users\\', '\\_project\\', '\\appdata\\', '\\desktop\\', '\\documents\\'):
                    if file != 'ReShade64.dll' and (text.encode() in data or text.encode('utf-16-le') in data):
                        problems.append('{} contains a path with "{}"'.format(relative, text))
    if problems:
        print('\n'.join(problems))

    # The zip and its checksum.
    archive = os.path.join(out_root, name + '.zip')
    if os.path.exists(archive):
        os.remove(archive)
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zip_file:
        for folder, _, files in os.walk(stage):
            for file in sorted(files):
                path = os.path.join(folder, file)
                zip_file.write(path, os.path.join(name, os.path.relpath(path, stage)))
    digest = hashlib.sha256(open(archive, 'rb').read()).hexdigest()
    with open(archive + '.sha256', 'w', encoding='ascii', newline='\n') as file:
        file.write('{}  {}\n'.format(digest, os.path.basename(archive)))
    count = sum(len(files) for _, _, files in os.walk(stage))
    print('{}: {} files, {:.1f} MB, SHA-256 {}'.format(archive, count, os.path.getsize(archive) / 1e6, digest))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
"""Assembles the CyGPUInspector release zip from the sources and the build outputs.

    python Tools/package_release.py --plugin-builds C:/CyGPUInspectorBuild [--dxc DIR] [--version X.Y.Z]

Takes:
  bin/Release/                 the standalone, the MCP server, the add-ons, the tests (build first)
  --plugin-builds DIR          RunUAT BuildPlugin outputs, one per engine: DIR/UE5.8/CyGPUInspector,
                               DIR/UE5.3/CyGPUInspector. Build them from a neutral folder: Unreal
                               writes the build folder's path into the DLLs.
  --dxc DIR                    an official DirectXShaderCompiler release zip, extracted (or a folder
                               with dxcompiler.dll and dxil.dll), with the licence files that come
                               with it. Without it, the package has no Shader Model 6 support.
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
PLUGIN_ROOT = os.path.join(ROOT, 'CyGPUInspectorUnreal', 'CyGPUInspector')

# The complete, unchanged notices of the components actually used by this product.
# CyToolsCore and CyUEMCPSDK are not dependencies of its native assistant/MCP server.
LICENCE_FILES = {
    'CyUI/LICENSE.txt': 'ThirdParty/CyUI/LICENSE',
    'CyGPUInspector-AGPL-3.0.txt': 'LICENSE',
    'ReShade-SDK/LICENSE.md': 'ThirdParty/reshade/LICENSE.md',
    'ReShade/LICENSE-ReShade.txt': 'CyGPUInspectorUnreal/CyGPUInspector/Binaries/ThirdParty/CyGPUInspector/Win64/LICENSE-ReShade.txt',
    'ReShade/NOTICES-ReShade.txt': 'CyGPUInspectorUnreal/CyGPUInspector/Binaries/ThirdParty/CyGPUInspector/Win64/NOTICES-ReShade.txt',
    'DearImGui/LICENSE.txt': 'ThirdParty/imgui/LICENSE.txt',
    'DearImGui-ReShade/LICENSE.txt': 'ThirdParty/reshade_imgui/LICENSE.txt',
    'ImGuiColorTextEdit/LICENSE': 'ThirdParty/ImGuiColorTextEdit/LICENSE',
    'ImGuiColorTextEdit/ORIGIN.md': 'ThirdParty/ImGuiColorTextEdit/ORIGIN.md',
    '3Dmigoto/LICENSE.GPL.txt': 'ThirdParty/hlsldecompiler/LICENSE.GPL.txt',
    '3Dmigoto/ORIGIN.md': 'ThirdParty/hlsldecompiler/ORIGIN.md',
    'dxil-spirv/LICENSE.MIT': 'ThirdParty/dxil-spirv/LICENSE.MIT',
    'dxbc-spirv/LICENSE': 'ThirdParty/dxil-spirv/subprojects/dxbc-spirv/LICENSE',
    'SPIRV-Headers/LICENSE': 'ThirdParty/dxil-spirv/third_party/spirv-headers/LICENSE',
    'SPIRV-Cross/LICENSE': 'ThirdParty/SPIRV-Cross/LICENSE',
}
PLUGIN_LICENCES = (
    'CyGPUInspector-AGPL-3.0.txt', 'ReShade-SDK/LICENSE.md',
    'ReShade/LICENSE-ReShade.txt', 'ReShade/NOTICES-ReShade.txt',
    'DearImGui-ReShade/LICENSE.txt',
)


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
    with open(path, encoding='utf-8', errors='replace') as source:
        for line in source:
            stripped = line.strip()
            if not (stripped.startswith(('//', '/*', '*')) or stripped == ''):
                break
            lines.append(re.sub(r'^\s*(/\*+|\*+/|\*|//)\s?', '', line.rstrip()).rstrip('*/ ').rstrip())
    return '\n'.join(lines).strip() + '\n'


def source_version():
    """CYGI_VERSION_STRING: the package takes the version the binaries were built with."""
    header = os.path.join(ROOT, 'CyGPUInspectorCore', 'Include', 'CyGPUInspectorCore', 'Version.hpp')
    return re.search(r'#define CYGI_VERSION_STRING "([^"]+)"', open(header, encoding='utf-8').read()).group(1)


def licence_contents(plugin=False):
    """Expected full texts, compared byte for byte with their authoritative sources."""
    files = PLUGIN_LICENCES if plugin else LICENCE_FILES
    contents = {}
    for name in files:
        with open(os.path.join(ROOT, LICENCE_FILES[name]), 'rb') as source:
            contents[name] = source.read()
    if not plugin:
        for name, source in {
            'RenderDoc-bitcode-reader/LICENSE.txt': 'ThirdParty/dxil-spirv/third_party/bc-decoder/llvm_bitreader.h',
            'glslang-SPIR-V-builder/LICENSE.txt': 'ThirdParty/dxil-spirv/third_party/glslang-spirv/Logger.h',
        }.items():
            contents[name] = header_licence(os.path.join(ROOT, source)).encode('utf-8')
        source_root = os.path.join(ROOT, 'ThirdParty', 'SPIRV-Cross', 'LICENSES')
        for folder, _, names in os.walk(source_root):
            for name in names:
                path = os.path.join(folder, name)
                relative = os.path.relpath(path, source_root).replace('\\', '/')
                with open(path, 'rb') as source:
                    contents['SPIRV-Cross/LICENSES/' + relative] = source.read()
    source_root = PLUGIN_ROOT if plugin else ROOT
    with open(os.path.join(source_root, 'LICENCES', 'SOURCES.txt'), 'rb') as source:
        contents['SOURCES.txt'] = source.read()
    return contents


def validate_licences(read, plugins, dxc=False, dxc_contents=None):
    """Validate the staging tree OR the actual ZIP using the same relative-path reader."""
    problems = []
    for prefix, plugin in [('', False)] + [(p.rstrip('/') + '/', True) for p in plugins]:
        for name, expected in licence_contents(plugin).items():
            path = prefix + 'LICENCES/' + name
            try:
                actual = read(path)
            except (FileNotFoundError, KeyError):
                problems.append('missing licence: ' + path)
                continue
            if actual != expected:
                problems.append('licence differs from its source: ' + path)
        if plugin:
            # These notices must also accompany the DLL when staged into a game.
            for name in ('LICENSE-ReShade.txt', 'NOTICES-ReShade.txt'):
                path = prefix + 'Binaries/ThirdParty/CyGPUInspector/Win64/' + name
                expected = licence_contents(True)['ReShade/' + name]
                try:
                    if read(path) != expected:
                        problems.append('ReShade notice differs from its source: ' + path)
                except (FileNotFoundError, KeyError):
                    problems.append('missing ReShade notice: ' + path)
    if dxc:
        for name in ('LICENCE-MIT.txt', 'LICENSE-LLVM.txt', 'LICENSE-MS.txt', 'ORIGIN.txt'):
            path = 'LICENCES/DirectXShaderCompiler/' + name
            try:
                actual = read(path)
                if not actual.strip():
                    problems.append('empty DXC notice: ' + path)
                elif dxc_contents is not None and actual != dxc_contents[name]:
                    problems.append('DXC notice differs from its source: ' + path)
            except (FileNotFoundError, KeyError):
                problems.append('missing DXC notice: ' + path)
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--version', default=source_version())
    parser.add_argument('--plugin-builds', required=True)
    parser.add_argument('--dxc')
    parser.add_argument('--output-root', default=os.path.join(ROOT, 'build', 'Release'),
                        help='Package output directory; use a separate directory to preserve an earlier release.')
    args = parser.parse_args()

    name = 'CyGPUInspector-{}-win64'.format(args.version)
    out_root = os.path.abspath(args.output_root)
    stage = os.path.join(out_root, name)
    if os.path.dirname(os.path.abspath(stage)) != out_root:
        parser.error('The version must be a filename component, not a path.')
    if os.path.exists(stage):
        parser.error('The staging directory already exists. Select a new --output-root to preserve its contents.')
    if os.path.exists(stage + '.zip'):
        parser.error('The release archive already exists. Select a new --output-root to preserve it.')
    os.makedirs(stage)

    # Application, add-ons, tests.
    for exe in ('CyGPUInspectorApp.exe', 'CyGPUInspectorMCP.exe'):
        copy(os.path.join(BIN, exe), os.path.join(stage, 'App', exe))
    for lang in ('fr.json', 'template.json'):
        copy(os.path.join(ROOT, 'Lang', lang), os.path.join(stage, 'App', 'Lang', lang))
    for addon in ('CyGPUInspectorRS.addon64', 'CyGPUInjector.addon64'):
        copy(os.path.join(BIN, addon), os.path.join(stage, 'Addons', addon))
    for exe in ('CyGPUInspectorCoreTests.exe', 'CyGPUInspectorAssistantTests.exe', 'CyGPUInspectorIpcTests.exe',
                'CyGPUInspectorShaderTests.exe',
                'CyGPUInspectorFakeSession.exe', 'CyGPUInspectorMCP.exe'):
        copy(os.path.join(BIN, exe), os.path.join(stage, 'TestWithoutAGame', exe))
    # The IPC tests load the add-on the way the Unreal plugin does: it sits next to them.
    copy(os.path.join(BIN, 'CyGPUInspectorRS.addon64'), os.path.join(stage, 'TestWithoutAGame', 'CyGPUInspectorRS.addon64'))
    copy_tree(os.path.join(ROOT, 'Packaging', 'TestWithoutAGame'), os.path.join(stage, 'TestWithoutAGame'))

    # The DirectX Shader Compiler, when given, with the licences that come with it. --dxc is either
    # the folder of the two DLLs or the root of an official release zip, extracted (bin/x64/).
    dxc_contents = None
    if args.dxc:
        dxc_contents = {}
        for notice in ('LICENCE-MIT.txt', 'LICENSE-LLVM.txt', 'LICENSE-MS.txt', 'ORIGIN.txt'):
            with open(os.path.join(args.dxc, notice), 'rb') as file:
                dxc_contents[notice] = file.read()
        dlls = os.path.join(args.dxc, 'bin', 'x64')
        if not os.path.isdir(dlls):
            dlls = args.dxc
        for dll in ('dxcompiler.dll', 'dxil.dll'):
            copy(os.path.join(dlls, dll), os.path.join(stage, 'App', dll))
        for entry in os.listdir(args.dxc):
            if re.match(r'(?i)(license|licence|notice|third|origin)', entry) and os.path.isfile(os.path.join(args.dxc, entry)):
                for notices in ('LICENCES', 'Licenses'):
                    copy(os.path.join(args.dxc, entry), os.path.join(stage, notices, 'DirectXShaderCompiler', entry))
    else:
        print('Warning: no --dxc: the package has no Shader Model 6 support.')

    # The Unreal plugin, one per engine version, without symbols or intermediate files.
    packaged_plugins = []
    for engine in sorted(os.listdir(args.plugin_builds)):
        plugin = os.path.join(args.plugin_builds, engine, 'CyGPUInspector')
        if not os.path.isfile(os.path.join(plugin, 'CyGPUInspector.uplugin')):
            continue
        target = os.path.join(stage, 'Unreal', engine, 'CyGPUInspector')
        copy(os.path.join(plugin, 'CyGPUInspector.uplugin'), os.path.join(target, 'CyGPUInspector.uplugin'))
        copy_tree(os.path.join(plugin, 'Resources'), os.path.join(target, 'Resources'))
        copy_tree(os.path.join(plugin, 'Source'), os.path.join(target, 'Source'))
        copy_tree(os.path.join(plugin, 'LICENCES'), os.path.join(target, 'LICENCES'))
        copy(os.path.join(plugin, 'Config', 'FilterPlugin.ini'), os.path.join(target, 'Config', 'FilterPlugin.ini'))
        copy_tree(os.path.join(plugin, 'Binaries'), os.path.join(target, 'Binaries'), skip=('.pdb',))
        packaged_plugins.append(os.path.relpath(target, stage).replace('\\', '/'))
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
    copy_tree(os.path.join(ROOT, 'LICENCES'), os.path.join(stage, 'LICENCES'))

    # The licences of what is compiled into the binaries.
    licences = {
        'CyUI/LICENSE.txt': 'ThirdParty/CyUI/LICENSE',
        'ReShade-SDK/LICENSE.md': 'ThirdParty/reshade/LICENSE.md',
        'ReShade/LICENSE-ReShade.txt': 'CyGPUInspectorUnreal/CyGPUInspector/Binaries/ThirdParty/CyGPUInspector/Win64/LICENSE-ReShade.txt',
        'ReShade/NOTICES-ReShade.txt': 'CyGPUInspectorUnreal/CyGPUInspector/Binaries/ThirdParty/CyGPUInspector/Win64/NOTICES-ReShade.txt',
        'DearImGui/LICENSE.txt': 'ThirdParty/imgui/LICENSE.txt',
        'ImGuiColorTextEdit/LICENSE': 'ThirdParty/ImGuiColorTextEdit/LICENSE',
        'ImGuiColorTextEdit/ORIGIN.md': 'ThirdParty/ImGuiColorTextEdit/ORIGIN.md',
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
    def read_staged(path):
        with open(os.path.join(stage, path), 'rb') as file:
            return file.read()

    problems = validate_licences(read_staged, packaged_plugins, bool(args.dxc), dxc_contents)
    if not packaged_plugins:
        problems.append('no Unreal plugins were packaged')
    for folder, _, files in os.walk(stage):
        for file in files:
            path = os.path.join(folder, file)
            relative = os.path.relpath(path, stage)
            plugin_filter = relative.replace('\\', '/') in {
                prefix + '/Config/FilterPlugin.ini' for prefix in packaged_plugins}
            if (file.lower().endswith(('.pdb', '.ini', '.ilk', '.exp', '.lib', '.obj')) and not plugin_filter) or \
               re.search(r'(^|[\\/])(Captures|Database|Games|Images|Saved|Intermediate)([\\/]|$)', relative):
                problems.append('should not ship: ' + relative)
            if file.lower().endswith(('.exe', '.dll', '.addon64')):
                data = open(path, 'rb').read().lower()
                # Paths of the machine that built it, as ASCII or UTF-16 strings. The Unreal DLLs keep
                # the neutral folder they were built from (C:\CyGPUInspectorBuild), which says nothing
                # about anyone. ReShade64.dll and the DXC DLLs are official builds, not ours to check.
                for text in ('\\users\\', '\\_project\\', '\\appdata\\', '\\desktop\\', '\\documents\\'):
                    if file not in ('ReShade64.dll', 'dxcompiler.dll', 'dxil.dll') and \
                       (text.encode() in data or text.encode('utf-16-le') in data):
                        problems.append('{} contains a path with "{}"'.format(relative, text))
    if problems:
        print('\n'.join(problems))
        return 1

    # The zip and its checksum.
    archive = os.path.join(out_root, name + '.zip')
    if os.path.exists(archive):
        os.remove(archive)
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zip_file:
        for folder, _, files in os.walk(stage):
            for file in sorted(files):
                path = os.path.join(folder, file)
                zip_file.write(path, os.path.join(name, os.path.relpath(path, stage)))
    with zipfile.ZipFile(archive) as zip_file:
        corrupt = zip_file.testzip()
        if corrupt:
            problems.append('corrupt ZIP entry: ' + corrupt)
        problems.extend(validate_licences(lambda path: zip_file.read(name + '/' + path),
                                          packaged_plugins, bool(args.dxc), dxc_contents))
    if problems:
        print('\n'.join(problems))
        return 1
    digest = hashlib.sha256(open(archive, 'rb').read()).hexdigest()
    with open(archive + '.sha256', 'w', encoding='ascii', newline='\n') as file:
        file.write('{}  {}\n'.format(digest, os.path.basename(archive)))
    count = sum(len(files) for _, _, files in os.walk(stage))
    print('{}: {} files, {:.1f} MB, SHA-256 {}'.format(archive, count, os.path.getsize(archive) / 1e6, digest))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())

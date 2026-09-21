#!/usr/bin/env python3
"""Fetches the ReShade the CyGPUInspector Unreal plugin loads.

The plugin loads ReShade64.dll from the official "with full add-on support" build of ReShade. It
is not in this repository — a binary in git stays in its history forever, and ReShade is updated
often — so release zips carry it, and this script gets it for anyone building from source:

    python Tools/fetch_reshade.py                     download the pinned version, check it, install it
    python Tools/fetch_reshade.py --installer FILE    use an installer already downloaded
    python Tools/fetch_reshade.py --with-addon        also copy bin/Release/CyGPUInspectorRS.addon64
    python Tools/fetch_reshade.py --dest FOLDER       install somewhere else than the plugin

The installer is never run: ReShade64.dll is read out of the archive it carries. The pinned
version is checked against the SHA-256 recorded below; another version (--version) is accepted
with a warning, because the add-on needs ReShade's add-on API 20 or later and nothing here can
check that before the DLL is loaded.

ReShade is under the BSD 3-Clause licence. Its notice, and those of the libraries compiled into
the DLL, sit next to it in the plugin (LICENSE-ReShade.txt, NOTICES-ReShade.txt) and must travel
with it.

Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
"""
import argparse
import hashlib
import os
import shutil
import sys
import urllib.request
import zipfile

PINNED_VERSION = '6.8.0'
PINNED_INSTALLER_SHA256 = 'afe4c8f13048306307983b8b3d41d5bf00a86820440b0e57dea10950e1176445'
PINNED_DLL_SHA256 = '0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7'
URL = 'https://reshade.me/downloads/ReShade_Setup_{version}_Addon.exe'

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGIN_THIRD_PARTY = os.path.join(ROOT, 'CyGPUInspectorUnreal', 'CyGPUInspector', 'Binaries', 'ThirdParty',
                                  'CyGPUInspector', 'Win64')
NOTICES = ('LICENSE-ReShade.txt', 'NOTICES-ReShade.txt')


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--version', default=PINNED_VERSION, help='ReShade version (default %(default)s)')
    parser.add_argument('--installer', help='an installer already downloaded, instead of downloading one')
    parser.add_argument('--dest', default=PLUGIN_THIRD_PARTY, help='where ReShade64.dll goes (default: the plugin)')
    parser.add_argument('--with-addon', action='store_true', help='also copy bin/Release/CyGPUInspectorRS.addon64')
    args = parser.parse_args()
    pinned = args.version == PINNED_VERSION

    # 1. The installer: given, cached from an earlier run, or downloaded from reshade.me.
    cache = os.path.join(ROOT, 'build', 'ReShade', args.version + '_Addon')
    installer = args.installer or os.path.join(cache, 'ReShade_Setup_{}_Addon.exe'.format(args.version))
    if not os.path.isfile(installer):
        url = URL.format(version=args.version)
        print('Downloading', url)
        os.makedirs(cache, exist_ok=True)
        with urllib.request.urlopen(url, timeout=120) as response:
            data = response.read()
        with open(installer, 'wb') as file:
            file.write(data)
    with open(installer, 'rb') as file:
        data = file.read()
    print('Installer: {} ({} bytes, SHA-256 {})'.format(installer, len(data), sha256(data)))
    if pinned and sha256(data) != PINNED_INSTALLER_SHA256:
        sys.exit('The installer does not match the one recorded for ReShade {}: not used.'.format(args.version))

    # 2. ReShade64.dll, read out of the archive the installer carries. Nothing is executed.
    try:
        dll = zipfile.ZipFile(installer).read('ReShade64.dll')
    except (zipfile.BadZipFile, KeyError) as error:
        sys.exit('ReShade64.dll could not be read out of the installer: {}'.format(error))
    print('ReShade64.dll: {} bytes, SHA-256 {}'.format(len(dll), sha256(dll)))
    if pinned and sha256(dll) != PINNED_DLL_SHA256:
        sys.exit('ReShade64.dll does not match the one recorded for ReShade {}: not installed.'.format(args.version))
    if b'ReShadeRegisterAddon' not in dll:
        sys.exit('This ReShade64.dll has no add-on support: the build "with full add-on support" is needed.')
    if not pinned:
        print('Warning: ReShade {} is not the pinned version ({}); the add-on needs add-on API 20 or later.'
              .format(args.version, PINNED_VERSION))

    # 3. Installed with its licence notices, which must travel with it.
    os.makedirs(args.dest, exist_ok=True)
    with open(os.path.join(args.dest, 'ReShade64.dll'), 'wb') as file:
        file.write(dll)
    for notice in NOTICES:
        source = os.path.join(PLUGIN_THIRD_PARTY, notice)
        target = os.path.join(args.dest, notice)
        if os.path.abspath(source) != os.path.abspath(target):
            shutil.copyfile(source, target)
    print('Installed in', args.dest)

    if args.with_addon:
        addon = os.path.join(ROOT, 'bin', 'Release', 'CyGPUInspectorRS.addon64')
        if not os.path.isfile(addon):
            sys.exit('bin/Release/CyGPUInspectorRS.addon64 does not exist: build the add-on first.')
        shutil.copyfile(addon, os.path.join(args.dest, 'CyGPUInspectorRS.addon64'))
        print('Add-on copied from', addon)


if __name__ == '__main__':
    main()

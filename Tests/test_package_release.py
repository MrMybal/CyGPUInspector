"""Regression checks for the notices shipped in standalone and Unreal packages."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('package_release', ROOT / 'Tools/package_release.py')
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class LicenceDeliveryTests(unittest.TestCase):
    def setUp(self):
        self.plugin = 'Unreal/UE5.3/CyGPUInspector'
        self.files = {'LICENCES/' + name: data for name, data in PACKAGE.licence_contents().items()}
        for name, data in PACKAGE.licence_contents(True).items():
            self.files[self.plugin + '/LICENCES/' + name] = data
        for name in ('LICENSE-ReShade.txt', 'NOTICES-ReShade.txt'):
            self.files[self.plugin + '/Binaries/ThirdParty/CyGPUInspector/Win64/' + name] = \
                PACKAGE.licence_contents(True)['ReShade/' + name]

    def validate(self, dxc=False):
        return PACKAGE.validate_licences(self.files.__getitem__, [self.plugin], dxc)

    def test_source_notices_are_complete_and_unchanged(self):
        for plugin, folder in ((False, ROOT), (True, Path(PACKAGE.PLUGIN_ROOT))):
            for name, expected in PACKAGE.licence_contents(plugin).items():
                with self.subTest(plugin=plugin, notice=name):
                    self.assertEqual((folder / 'LICENCES' / name).read_bytes(), expected)
        self.assertEqual(self.validate(), [])

    def test_missing_plugin_notice_rejects_delivery(self):
        path = self.plugin + '/LICENCES/CyGPUInspector-AGPL-3.0.txt'
        del self.files[path]
        self.assertIn('missing licence: ' + path, self.validate())

    def test_truncated_notice_rejects_delivery(self):
        path = 'LICENCES/3Dmigoto/LICENSE.GPL.txt'
        self.files[path] = b'GPL-3.0\n'
        self.assertIn('licence differs from its source: ' + path, self.validate())

    def test_reshade_dll_cannot_ship_without_adjacent_notices(self):
        path = self.plugin + '/Binaries/ThirdParty/CyGPUInspector/Win64/NOTICES-ReShade.txt'
        del self.files[path]
        self.assertIn('missing ReShade notice: ' + path, self.validate())

    def test_dxc_notices_required_only_when_dxc_is_included(self):
        self.assertEqual(self.validate(), [])
        self.assertEqual(len(self.validate(dxc=True)), 4)
        for name in ('LICENCE-MIT.txt', 'LICENSE-LLVM.txt', 'LICENSE-MS.txt', 'ORIGIN.txt'):
            self.files['LICENCES/DirectXShaderCompiler/' + name] = b'original DXC notice'
        self.assertEqual(self.validate(dxc=True), [])

    def test_dxc_text_compared_to_the_selected_distribution(self):
        originals = {name: b'unchanged original text' for name in
                     ('LICENCE-MIT.txt', 'LICENSE-LLVM.txt', 'LICENSE-MS.txt', 'ORIGIN.txt')}
        for name, data in originals.items():
            self.files['LICENCES/DirectXShaderCompiler/' + name] = data
        self.assertEqual(PACKAGE.validate_licences(self.files.__getitem__, [self.plugin], True, originals), [])
        path = 'LICENCES/DirectXShaderCompiler/LICENSE-MS.txt'
        self.files[path] = b'Microsoft license'
        self.assertIn('DXC notice differs from its source: ' + path,
                      PACKAGE.validate_licences(self.files.__getitem__, [self.plugin], True, originals))


if __name__ == '__main__':
    unittest.main()

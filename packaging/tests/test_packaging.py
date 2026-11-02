import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "packaging/ci"))
import metadata
import release


class MetadataTests(unittest.TestCase):
    def test_seven_unique_native_packages(self):
        self.assertEqual(len(metadata.TARGETS), 7)
        self.assertEqual(len({metadata.asset(x, metadata.version()) for x in metadata.TARGETS}), 7)
        expected = {
            ("archlinux", "amd64"): "x86_64",
            ("debian-sid", "amd64"): "amd64",
            ("debian-sid", "aarch64"): "arm64",
            ("debian-sid", "riscv64"): "riscv64",
            ("opensuse-tumbleweed", "amd64"): "x86_64",
            ("opensuse-tumbleweed", "aarch64"): "aarch64",
            ("opensuse-tumbleweed", "riscv64"): "riscv64",
        }
        self.assertEqual({(x['distro'], x['arch']): x['native'] for x in metadata.TARGETS}, expected)
        for item in metadata.TARGETS:
            self.assertIn(item['arch'], metadata.asset(item, '0.1.0'))
            self.assertIn(item['distro'], metadata.asset(item, '0.1.0'))

    def test_strict_tags(self):
        metadata.validate_tag('v0.1.0', '0.1.0')
        for tag in ['v0.1.1', '0.1.0', 'v01.1.0', 'v0.1.0-rc1', 'v0.1.0;echo injected', 'v0.1.0\n', 'v$(id)']:
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                metadata.validate_tag(tag, '0.1.0')

    def test_source_and_metadata_version_must_match(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / 'src').mkdir()
            (directory / 'CMakeLists.txt').write_text('project(kmm_binance_price VERSION 0.1.0 LANGUAGES CXX)')
            (directory / 'src/plugin.json').write_text('{"KPlugin":{"Version":"0.2.0"}}')
            with self.assertRaisesRegex(ValueError, 'metadata version'):
                metadata.version(directory)

    def test_matrix_trigger_policy(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'output'
            for event, ref, count in [('push', 'refs/heads/main', 3), ('pull_request', 'refs/pull/1/merge', 3), ('workflow_dispatch', 'refs/heads/main', 7), ('push', 'refs/tags/v' + metadata.version(), 7)]:
                output.write_text('')
                env = dict(GITHUB_EVENT_NAME=event, GITHUB_REF=ref, GITHUB_REF_NAME=ref.split('/')[-1], GITHUB_OUTPUT=str(output))
                with patch.dict(os.environ, env), patch.object(sys, 'argv', ['metadata.py', 'prepare']):
                    metadata.main()
                self.assertEqual(len(json.loads(output.read_text().split('=', 1)[1])['include']), count)

    def test_exact_asset_allowlist(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            names = [metadata.asset(x, metadata.version()) for x in metadata.TARGETS]
            for name in names:
                (directory / name).write_bytes(b'package')
            self.assertEqual(metadata.validate_assets(directory, metadata.version()), sorted(names))
            (directory / names[0]).unlink()
            with self.assertRaises(ValueError):
                metadata.validate_assets(directory, metadata.version())
            (directory / names[0]).write_bytes(b'package')
            (directory / 'unexpected.rpm').write_bytes(b'extra')
            with self.assertRaises(ValueError):
                metadata.validate_assets(directory, metadata.version())
            (directory / 'unexpected.rpm').unlink()
            (directory / names[0]).write_bytes(b'')
            with self.assertRaises(ValueError):
                metadata.validate_assets(directory, metadata.version())

    def test_recipe_version_and_dependency_tools(self):
        arch = (ROOT / 'packaging/arch/PKGBUILD').read_text()
        self.assertIn('pkgver=' + metadata.version() + '\n', arch)
        self.assertIn('kmymoney=$(pacman -Q kmymoney', arch)
        debian = (ROOT / 'packaging/debian/control').read_text()
        self.assertIn('${shlibs:Depends}', debian)
        self.assertIn('kmymoney (= ${kmm:Version})', debian)
        rpm = (ROOT / 'packaging/rpm/kmm-binance-price.spec').read_text()
        self.assertIn('Requires:       kmymoney = %{kmm_version}', rpm)
        self.assertNotIn('AutoReqProv: no', rpm)
        workflow = (ROOT / '.github/workflows/packages.yml').read_text()
        self.assertEqual(workflow.count('contents: write'), 1)
        self.assertNotIn('pull_request_target', workflow)
        for line in workflow.splitlines():
            if 'uses:' in line:
                self.assertRegex(line, r'@[0-9a-f]{40} # v[0-9]')


class ReleaseTests(unittest.TestCase):
    def test_draft_upload_then_publish_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            names = sorted(metadata.asset(x, metadata.version()) for x in metadata.TARGETS)
            for name in names:
                (directory / name).write_bytes(b'package')
            calls = []
            existing = False
            fail_upload = False
            tag_sha = 'a' * 40

            class Response:
                def __init__(self, data):
                    self.data = data
                def __enter__(self):
                    import io
                    return io.StringIO(json.dumps(self.data))
                def __exit__(self, *args):
                    pass

            def fake(request, timeout):
                nonlocal existing, fail_upload, tag_sha
                url = request.full_url
                method = request.get_method()
                calls.append((method, url))
                if '/git/ref/' in url:
                    return Response({'object': {'type': 'commit', 'sha': tag_sha}})
                if '/releases?per_page=' in url:
                    return Response([{'tag_name': env['GITHUB_REF_NAME'], 'draft': True}] if existing else [])
                if '/releases/tags/' in url:
                    if existing:
                        return Response({'id': 1})
                    import io
                    raise urllib.error.HTTPError(url, 404, 'Not Found', {}, io.BytesIO(b''))
                if url.endswith('/releases'):
                    body = json.loads(request.data)
                    self.assertTrue(body['draft'])
                    self.assertEqual(body['target_commitish'], 'a' * 40)
                    return Response({'id': 1, 'upload_url': 'https://uploads.github.com/repos/owner/repo/releases/1/assets{?name,label}'})
                if 'uploads.github.com' in url:
                    self.assertEqual(len([x for x in calls if x[0] == 'PATCH']), 0)
                    return Response({'state': 'uploaded', 'size': 0 if fail_upload else len(request.data)})
                if '/assets?' in url:
                    return Response([{'name': n} for n in names + ['SHA256SUMS']])
                self.assertEqual(method, 'PATCH')
                self.assertEqual(json.loads(request.data), {'draft': False})
                return Response({'id': 1})

            env = dict(GITHUB_REF_NAME='v' + metadata.version(), GITHUB_EVENT_NAME='push', GITHUB_REF='refs/tags/v' + metadata.version(), GITHUB_REPOSITORY='owner/repo', GITHUB_SHA='a' * 40, GH_TOKEN='fake')
            with patch.dict(os.environ, env), patch('urllib.request.urlopen', fake):
                release.publish(directory)
                self.assertEqual(calls[-1][0], 'PATCH')
                self.assertIn('/git/ref/tags/', calls[-2][1])
                self.assertEqual(len([x for x in calls if '/git/ref/tags/' in x[1]]), 2)
                self.assertEqual(len([x for x in calls if 'uploads.github.com' in x[1]]), 8)
                self.assertEqual(len((directory / 'SHA256SUMS').read_text().splitlines()), 7)
                (directory / 'SHA256SUMS').unlink()
                calls.clear()
                fail_upload = True
                with self.assertRaisesRegex(ValueError, 'incomplete upload'):
                    release.publish(directory)
                self.assertFalse(any(method == 'PATCH' for method, _ in calls))
                (directory / 'SHA256SUMS').unlink()
                fail_upload = False
                tag_sha = 'b' * 40
                calls.clear()
                with self.assertRaisesRegex(ValueError, 'workflow commit'):
                    release.publish(directory)
                self.assertFalse(any(method != 'GET' for method, _ in calls))
                (directory / 'SHA256SUMS').unlink()
                tag_sha = 'a' * 40
                existing = True
                calls.clear()
                with self.assertRaisesRegex(ValueError, 'already exists'):
                    release.publish(directory)
                self.assertFalse(any(method != 'GET' for method, _ in calls))

    def test_manual_never_publishes(self):
        with patch.dict(os.environ, dict(GITHUB_REF_NAME='v' + metadata.version(), GITHUB_EVENT_NAME='workflow_dispatch', GITHUB_REF='refs/tags/v' + metadata.version())):
            with self.assertRaisesRegex(ValueError, 'only a tag push'):
                release.publish(Path('/does/not/exist'))

    def check_upload_tag_change(self, deleted):
        import io
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            names = sorted(metadata.asset(x, metadata.version()) for x in metadata.TARGETS)
            for name in names:
                (directory / name).write_bytes(b'package')
            uploads = 0
            draft = False
            calls = []

            class Response:
                def __init__(self, data):
                    self.data = data
                def __enter__(self):
                    return io.StringIO(json.dumps(self.data))
                def __exit__(self, *args):
                    pass

            def fake(request, timeout):
                nonlocal uploads, draft
                url = request.full_url
                method = request.get_method()
                calls.append((method, url))
                if '/git/ref/' in url:
                    if uploads == 8 and deleted:
                        raise urllib.error.HTTPError(url, 404, 'Tag deleted', {}, io.BytesIO(b''))
                    return Response({'object': {'type': 'commit', 'sha': ('b' if uploads == 8 else 'a') * 40}})
                if '/releases?per_page=' in url:
                    return Response([])
                if '/releases/tags/' in url:
                    raise urllib.error.HTTPError(url, 404, 'Not Found', {}, io.BytesIO(b''))
                if url.endswith('/releases') and method == 'POST':
                    draft = True
                    return Response({'id': 1, 'upload_url': 'https://uploads.github.com/repos/owner/repo/releases/1/assets{?name,label}'})
                if 'uploads.github.com' in url:
                    uploads += 1
                    return Response({'state': 'uploaded', 'size': len(request.data)})
                if '/assets?' in url:
                    return Response([{'name': n} for n in names + ['SHA256SUMS']])
                self.assertEqual(method, 'PATCH')
                draft = False
                return Response({'id': 1})

            env = dict(GITHUB_REF_NAME='v' + metadata.version(), GITHUB_EVENT_NAME='push', GITHUB_REF='refs/tags/v' + metadata.version(), GITHUB_REPOSITORY='owner/repo', GITHUB_SHA='a' * 40, GH_TOKEN='fake')
            with patch.dict(os.environ, env), patch('urllib.request.urlopen', fake):
                with self.assertRaises(urllib.error.HTTPError if deleted else ValueError) as raised:
                    release.publish(directory)
                if deleted:
                    self.assertEqual(raised.exception.code, 404)
                    raised.exception.close()
            self.assertEqual(uploads, 8)
            self.assertTrue(draft)
            self.assertFalse(any(method == 'PATCH' for method, _ in calls))

    def test_tag_moves_during_upload_leaves_draft(self):
        self.check_upload_tag_change(deleted=False)

    def test_tag_deleted_during_upload_leaves_draft(self):
        self.check_upload_tag_change(deleted=True)

    def test_publish_retries_transient_422(self):
        import io
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            names = sorted(metadata.asset(x, metadata.version()) for x in metadata.TARGETS)
            for name in names:
                (directory / name).write_bytes(b'package')
            patch_attempts = 0

            class Response:
                def __init__(self, data):
                    self.data = data
                def __enter__(self):
                    return io.StringIO(json.dumps(self.data))
                def __exit__(self, *args):
                    pass

            def fake(request, timeout):
                nonlocal patch_attempts
                url = request.full_url
                method = request.get_method()
                if '/git/ref/' in url:
                    return Response({'object': {'type': 'commit', 'sha': 'a' * 40}})
                if '/releases?per_page=' in url:
                    return Response([])
                if '/releases/tags/' in url:
                    raise urllib.error.HTTPError(url, 404, 'Not Found', {}, io.BytesIO(b''))
                if url.endswith('/releases') and method == 'POST':
                    return Response({'id': 1, 'upload_url': 'https://uploads.github.com/repos/owner/repo/releases/1/assets{?name,label}'})
                if 'uploads.github.com' in url:
                    return Response({'state': 'uploaded', 'size': len(request.data)})
                if '/assets?' in url:
                    return Response([{'name': n} for n in names + ['SHA256SUMS']])
                if url.endswith('/releases/1') and method == 'GET':
                    return Response({'id': 1, 'draft': True})
                if url.endswith('/releases/1') and method == 'PATCH':
                    patch_attempts += 1
                    if patch_attempts < 3:
                        raise urllib.error.HTTPError(url, 422, 'Unprocessable Entity', {}, io.BytesIO(b''))
                    return Response({'id': 1})
                raise AssertionError((method, url))

            env = dict(GITHUB_REF_NAME='v' + metadata.version(), GITHUB_EVENT_NAME='push', GITHUB_REF='refs/tags/v' + metadata.version(), GITHUB_REPOSITORY='owner/repo', GITHUB_SHA='a' * 40, GH_TOKEN='fake')
            with patch.dict(os.environ, env), patch('urllib.request.urlopen', fake), patch('time.sleep') as sleep:
                release.publish(directory)
            self.assertEqual(patch_attempts, 3)
            sleep.assert_has_calls([unittest.mock.call(3), unittest.mock.call(6)])


class TagResolutionTests(unittest.TestCase):
    def test_lightweight_and_nested_annotated_tags(self):
        base = 'https://api.github.com/repos/owner/repo'
        commit = {'type': 'commit', 'sha': 'a' * 40}
        self.assertEqual(release.resolve_tag_commit(lambda url: {'object': commit}, base, 'v0.1.0'), commit['sha'])
        objects = {
            base + '/git/ref/tags/v0.1.0': {'type': 'tag', 'sha': 'b' * 40},
            base + '/git/tags/' + 'b' * 40: {'type': 'tag', 'sha': 'c' * 40},
            base + '/git/tags/' + 'c' * 40: commit,
        }
        self.assertEqual(release.resolve_tag_commit(lambda url: {'object': objects[url]}, base, 'v0.1.0'), commit['sha'])

    def test_annotated_tag_cycle_is_rejected(self):
        calls = []
        def request(url):
            calls.append(url)
            return {'object': {'type': 'tag', 'sha': 'b' * 40}}
        with self.assertRaisesRegex(ValueError, 'cyclic'):
            release.resolve_tag_commit(request, 'https://api.github.com/repos/owner/repo', 'v0.1.0')
        self.assertEqual(len(calls), 2)

    def test_annotated_tag_depth_is_bounded(self):
        calls = []
        def request(url):
            calls.append(url)
            return {'object': {'type': 'tag', 'sha': f'{len(calls):040x}'}}
        with self.assertRaisesRegex(ValueError, 'deeply nested'):
            release.resolve_tag_commit(request, 'https://api.github.com/repos/owner/repo', 'v0.1.0')
        self.assertEqual(len(calls), 1 + release.MAX_TAG_DEREFERENCES)


class DebianSDKTests(unittest.TestCase):
    def test_configured_public_headers_only_and_real_runtime_links(self):
        spec = importlib.util.spec_from_file_location('debian_sdk', ROOT / 'packaging/ci/debian-sdk.py')
        sdk_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(sdk_module)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            configured = root / 'configured'
            configured.mkdir()
            names = ['kmymoneyplugin.h', 'mymoneyfile.h', 'mymoneymoney.h', 'kmm_mymoney_export.h', 'kmm_plugin_export.h']
            for name in names:
                (configured / name).write_text('// unit-test header fixture\n')
            paths = [configured / name for name in names + ['kmymoneysettings.h']]
            (configured / 'cmake_install.cmake').write_text('file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/kmymoney" TYPE FILE FILES\n' + '\n'.join('"' + str(p) + '"' for p in paths) + '\n)')
            runtime = ['/usr/lib/test-linux-gnu/libkmm_plugin.so.5', '/usr/lib/test-linux-gnu/libkmm_mymoney.so.5']
            original_is_file = Path.is_file
            def is_file(path):
                return str(path) in runtime or original_is_file(path)
            sdk = root / 'sdk'
            with patch('subprocess.check_output', return_value='\n'.join(runtime)), patch.object(Path, 'is_file', is_file):
                sdk_module.create(configured, sdk)
            self.assertEqual({p.name for p in (sdk / 'include/kmymoney').iterdir()}, set(names))
            for name, dest in zip(['kmm_plugin', 'kmm_mymoney'], runtime):
                self.assertEqual(os.readlink(sdk / 'lib' / ('lib' + name + '.so')), dest)
            (configured / 'mymoneyfile.h').unlink()
            with self.assertRaisesRegex(ValueError, 'missing public header'):
                sdk_module.create(configured, root / 'missing-sdk')

if __name__ == '__main__':
    unittest.main()

import copy
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import select_release_run as s


class SelectReleaseRunTests(unittest.TestCase):
    def setUp(self):
        self.run = {'id': 987654321, 'run_number': 38, 'path': s.WORKFLOW,
                    'event': 'workflow_dispatch', 'status': 'completed', 'conclusion': 'success',
                    'head_sha': 'a'*40, 'head_repository': {'full_name': s.REPO}}
        self.artifacts = [{'id': i+1, 'name': prefix+p, 'expired': False, 'size_in_bytes': 100}
                          for i, (prefix, p) in enumerate((prefix, p)
                          for prefix in ('program-', 'dependency-materials-') for p in s.PLATFORMS)]
        self.calls = []

    def api(self, endpoint):
        self.calls.append(endpoint)
        if '/workflows/' in endpoint:
            return {'workflow_runs': [self.run]}
        if '/artifacts?' in endpoint:
            return {'artifacts': self.artifacts}
        return self.run

    def select(self):
        with patch.object(s, 'github', side_effect=self.api):
            return s.select(s.REPO, 38)

    def test_build_number_resolves_actual_run_id_and_tested_commit(self):
        result = self.select()
        self.assertEqual(result['run_id'], 987654321)
        self.assertEqual(result['commit'], 'a'*40)
        self.assertEqual(len(result['artifacts']), 8)
        self.assertIn(f'repos/{s.REPO}/actions/runs/987654321', self.calls)

    def test_running_or_cancelled_run_rejected(self):
        for status, conclusion in [('in_progress', None), ('completed', 'cancelled')]:
            with self.subTest(status=status, conclusion=conclusion):
                self.run.update(status=status, conclusion=conclusion)
                with self.assertRaisesRegex(ValueError, 'completed|successful'):
                    self.select()

    def failed_draft_jobs(self):
        names = ['Select platforms and verify bundled resources', *s.PLATFORMS, 'Prepare reviewable release draft']
        return [dict(name=name,status='completed',conclusion='failure' if name==names[-1] else 'success')
                for name in names]

    def select_with_attempts(self, attempts):
        self.run.update(conclusion='failure',run_attempt=len(attempts))
        def api(endpoint):
            if '/attempts/' in endpoint:
                attempt = int(endpoint.split('/attempts/')[1].split('/')[0])
                jobs = attempts[attempt-1]
                return dict(total_count=len(jobs),jobs=jobs)
            return self.api(endpoint)
        with patch.object(s,'github',side_effect=api):
            return s.select(s.REPO,38)

    def test_only_draft_failed_with_all_eight_artifacts(self):
        jobs = self.failed_draft_jobs()
        self.assertEqual(len(self.select_with_attempts([jobs])['artifacts']),8)
        self.assertEqual(len(self.select_with_attempts([jobs,[jobs[-1]]])['artifacts']),8)
        self.artifacts[0]['expired'] = True
        with self.assertRaisesRegex(ValueError,'Expired'):
            self.select_with_attempts([jobs])

    def test_failed_package_and_incomplete_job_evidence_rejected(self):
        jobs = self.failed_draft_jobs()
        cases = {
            'missing resource check': [jobs[1:]],
            'missing draft': [jobs[:-1]],
            'duplicate result': [jobs + [jobs[0]]],
            'failed platform on retry': [jobs, [dict(jobs[1], conclusion='failure'), jobs[-1]]],
            'skipped resource check': [[dict(jobs[0], conclusion='skipped'), *jobs[1:]]],
        }
        for name, attempts in cases.items():
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.select_with_attempts(attempts)

    def test_foreign_or_wrong_workflow_and_invalid_commit_rejected(self):
        original = copy.deepcopy(self.run)
        for field, value in [('head_repository', {'full_name': 'Other/Aster'}),
                             ('path', '.github/workflows/other.yml'), ('event', 'pull_request'),
                             ('head_sha', 'main'), ('run_number', 39)]:
            with self.subTest(field=field):
                self.run = dict(original, **{field: value})
                with self.assertRaises(ValueError):
                    self.select()

    def test_partial_expired_empty_and_duplicate_artifacts_rejected(self):
        original = copy.deepcopy(self.artifacts)
        variants = [original[1:], original[:-1], original+[original[0]],
                    [dict(original[0], expired=True)]+original[1:],
                    [dict(original[0], size_in_bytes=0)]+original[1:]]
        for rows in variants:
            with self.subTest(rows=rows):
                self.artifacts = rows
                with self.assertRaises(ValueError):
                    self.select()

    def test_unknown_number_and_invalid_repository_rejected(self):
        with patch.object(s, 'github', return_value={'workflow_runs': []}):
            with self.assertRaisesRegex(ValueError, 'not found'):
                s.select(s.REPO, 38)
        with patch.object(s, 'github') as api:
            for repo, number in [('Other/Aster', 38), (s.REPO, 0), (s.REPO, -1)]:
                with self.assertRaises(ValueError):
                    s.select(repo, number)
            api.assert_not_called()

    def test_paginated_runs_and_artifacts(self):
        def api(endpoint):
            if '/workflows/' in endpoint:
                rows = [{'id': i, 'run_number': 100+i} for i in range(100)] if endpoint.endswith('page=1') else [self.run]
                return {'workflow_runs': rows}
            if '/artifacts?' in endpoint:
                rows = [{'name': f'unrelated-{i}'} for i in range(100)] if endpoint.endswith('page=1') else self.artifacts
                return {'artifacts': rows}
            return self.run
        with patch.object(s, 'github', side_effect=api):
            self.assertEqual(s.select(s.REPO, 38)['commit'], 'a'*40)

    def test_cli_emits_selected_run_not_current_workflow_identity(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td); output = root/'record.json'; step = root/'step.txt'
            with patch.object(s, 'github', side_effect=self.api), patch.dict(os.environ, {'GITHUB_OUTPUT': str(step), 'GITHUB_SHA': 'b'*40}), patch('sys.argv', ['select_release_run.py', '--build-number', '38', '--output', str(output)]):
                s.main()
            self.assertEqual(json.loads(output.read_text())['commit'], 'a'*40)
            self.assertIn('commit='+'a'*40, step.read_text())
            self.assertNotIn('b'*40, step.read_text())

    def test_workflow_uses_tested_source_and_draft_only(self):
        workflow = Path(__file__).resolve().parents[2]/'.github/workflows/prepare-release-draft.yml'
        text = workflow.read_text()
        self.assertIn('ref: ${{ steps.build.outputs.commit }}', text)
        self.assertEqual(text.count('run-id: ${{ needs.source.outputs.run_id }}'), 1)
        self.assertIn('artifact-ids: ${{ needs.source.outputs.artifact_ids }}', text)
        self.assertIn('artifact-ids: ${{ needs.source.outputs.source_artifact }}', text)
        self.assertIn('source_artifact: ${{ steps.bundle.outputs.artifact-id }}', text)
        self.assertIn('release-tools/tools/release/release_delivery.py prepare --version "$version" --source-root "$inputs_dir"', text)
        self.assertNotIn('tested-source/tools/release/release_delivery.py prepare', text)
        self.assertIn('tested-source/tools/release/package_source.py', text)
        self.assertIn('--commit "$ASTER_TESTED_COMMIT"', text)
        self.assertIn('release-tools/tools/release/release_delivery.py draft --version "$ASTER_VERSION"', text)
        self.assertNotIn('tested-source/tools/release/release_delivery.py draft', text)
        self.assertNotIn('sync_gitee.py', text)
        self.assertNotIn('package_windows.ps1', text)

    def test_historical_source_cannot_access_the_draft_credential(self):
        workflow = Path(__file__).resolve().parents[2]/'.github/workflows/prepare-release-draft.yml'
        source, draft = workflow.read_text().split('\n  draft:\n')
        self.assertIn('contents: read', source)
        self.assertNotIn('contents: write', source)
        self.assertIn('python3 tested-source/', source)
        self.assertNotIn('secrets.ASTER_RELEASE_TOKEN', source)
        self.assertIn('needs: source', draft)
        self.assertNotIn('python3 tested-source/', draft)
        self.assertIn('ASTER_TESTED_COMMIT: ${{ needs.source.outputs.commit }}', draft)
        steps = draft.split('      - ')
        privileged = [step for step in steps if 'secrets.ASTER_RELEASE_TOKEN' in step]
        self.assertEqual(len(privileged), 1)
        self.assertIn("github.ref == format('refs/heads/{0}', github.event.repository.default_branch)", privileged[0])
        self.assertIn('secrets.ASTER_RELEASE_TOKEN) || github.token', privileged[0])
        self.assertIn('release_delivery.py draft', privileged[0])


if __name__ == '__main__':
    unittest.main()

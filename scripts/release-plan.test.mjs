// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { fetchReleases, planRelease, readSourceVersion } from './release-plan.mjs';

const request = { version: '1.2.0', eventName: 'push', ref: 'refs/tags/v1.2.0', releases: [] };

test('main and pull-request changes cannot publish another app release', () => {
  assert.equal(planRelease({ ...request, ref: 'refs/heads/main' }).shouldRelease, false);
  assert.equal(planRelease({ ...request, eventName: 'pull_request' }).shouldRelease, false);
});
test('a new matching version tag creates exactly the canonical release tag', () => {
  assert.deepEqual(planRelease(request), { version: '1.2.0', tag: 'v1.2.0', shouldRelease: true, reason: 'Preparing the first release for v1.2.0.' });
});
test('re-running a published version skips its build', () => {
  assert.equal(planRelease({ ...request, releases: [{ tag_name: 'v1.2.0' }] }).shouldRelease, false);
});
test('legacy build-number releases also block duplicate version builds', () => {
  assert.equal(planRelease({ ...request, releases: [{ tag_name: 'v1.2.0-build.4.1' }] }).shouldRelease, false);
});
test('an existing draft is not overwritten or rebuilt', () => {
  assert.equal(planRelease({ ...request, releases: [{ tag_name: 'v1.2.0', draft: true }] }).shouldRelease, false);
});
test('a previous version does not block a new version', () => {
  assert.equal(planRelease({ ...request, releases: [{ tag_name: 'v1.1.0' }, { tag_name: 'v1.2.00-build.1.1' }] }).shouldRelease, true);
});
test('wrong or build-number tags fail before building', () => {
  assert.throws(() => planRelease({ ...request, ref: 'refs/tags/v1.2.1' }), /does not match/);
  assert.throws(() => planRelease({ ...request, ref: 'refs/tags/v1.2.0-build.5.1' }), /does not match/);
});
test('manual runs are restricted to main and still respect existing releases', () => {
  const manual = { ...request, eventName: 'workflow_dispatch', ref: 'refs/heads/main' };
  assert.equal(planRelease(manual).shouldRelease, true);
  assert.equal(planRelease({ ...manual, releases: [{ tag_name: 'v1.2.0' }] }).shouldRelease, false);
  assert.throws(() => planRelease({ ...manual, ref: 'refs/heads/other' }), /from main/);
});
test('checked-in app, resource and installer versions agree', () => {
  assert.match(readSourceVersion(fileURLToPath(new URL('..', import.meta.url))), /^\d+\.\d+\.\d+$/);
});
test('inconsistent installer or Windows resource versions stop publication', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'rgb-release-policy-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  for (const dir of ['native/include', 'native/assets', 'installer']) fs.mkdirSync(path.join(root, dir), { recursive: true });
  fs.writeFileSync(path.join(root, 'native/include/core.hpp'), 'constexpr char Version[] = "1.2.0";');
  fs.writeFileSync(path.join(root, 'native/assets/app.rc'), 'FILEVERSION 1,2,0,0\nPRODUCTVERSION 1,2,0,0\nVALUE "ProductVersion", "1.2.0"');
  fs.writeFileSync(path.join(root, 'installer/VeryLightRGB.iss'), '#define AppVersion "1.2.1"');
  assert.throws(() => readSourceVersion(root), /must match/);
  fs.writeFileSync(path.join(root, 'installer/VeryLightRGB.iss'), '#define AppVersion "1.2.0"');
  fs.writeFileSync(path.join(root, 'native/assets/app.rc'), 'FILEVERSION 1,2,1,0\nPRODUCTVERSION 1,2,0,0\nVALUE "ProductVersion", "1.2.0"');
  assert.throws(() => readSourceVersion(root), /must match/);
});
test('release lookup checks beyond the first page', async () => {
  let calls = 0;
  const releases = await fetchReleases({ repository: 'example/rgb', fetcher: async () => ({ ok: true, json: async () => ++calls === 1 ? Array.from({ length: 100 }, () => ({ tag_name: 'v1.1.0' })) : [{ tag_name: 'v1.2.0' }] }) });
  assert.equal(calls, 2);
  assert.equal(planRelease({ ...request, releases }).shouldRelease, false);
});
test('failed GitHub lookups fail closed instead of building another release', async () => {
  await assert.rejects(fetchReleases({ repository: 'example/rgb', fetcher: async () => ({ ok: false, status: 403 }) }), /refusing to build/);
});

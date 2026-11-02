// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const stableVersion = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;

export function readSourceVersion(root) {
  const core = fs.readFileSync(path.join(root, 'native/include/core.hpp'), 'utf8');
  const resource = fs.readFileSync(path.join(root, 'native/assets/app.rc'), 'utf8');
  const installer = fs.readFileSync(path.join(root, 'installer/VeryLightRGB.iss'), 'utf8');
  const version = core.match(/constexpr char Version\[\]\s*=\s*"([^"]+)"/)?.[1];
  if (!stableVersion.test(version ?? '')) throw new Error('Invalid app version in core.hpp.');
  const resourceVersion = resource.match(/VALUE\s+"ProductVersion",\s*"([^"]+)"/)?.[1];
  const installerVersion = installer.match(/#define\s+AppVersion\s+"([^"]+)"/)?.[1];
  const numeric = version.replaceAll('.', ',') + ',0';
  const fileVersion = resource.match(/^\s*FILEVERSION\s+([\d,\s]+)$/m)?.[1].replaceAll(/\s/g, '');
  const productVersion = resource.match(/^\s*PRODUCTVERSION\s+([\d,\s]+)$/m)?.[1].replaceAll(/\s/g, '');
  if (resourceVersion !== version || installerVersion !== version || fileVersion !== numeric || productVersion !== numeric) {
    throw new Error('App, Windows resources, and installer versions must match before releasing.');
  }
  return version;
}

export function planRelease({ version, eventName, ref, releases }) {
  if (!stableVersion.test(version)) throw new Error('Expected a stable X.Y.Z app version.');
  const tag = `v${version}`;
  if (eventName === 'push') {
    if (!ref.startsWith('refs/tags/')) return { version, tag, shouldRelease: false, reason: 'Only version tags publish releases.' };
    if (ref !== `refs/tags/${tag}`) throw new Error('Release tag does not match the app version.');
  } else if (eventName === 'workflow_dispatch') {
    if (ref !== 'refs/heads/main') throw new Error('Manual releases must run from main.');
  } else {
    return { version, tag, shouldRelease: false, reason: 'This event does not publish releases.' };
  }
  const existing = releases.find(release => release.tag_name === tag || release.tag_name?.startsWith(`${tag}-build.`));
  if (existing) return { version, tag, shouldRelease: false, reason: `Version ${version} already has a release; build skipped.` };
  return { version, tag, shouldRelease: true, reason: `Preparing the first release for ${tag}.` };
}

export async function fetchReleases({ repository, token, apiUrl = 'https://api.github.com', fetcher = fetch }) {
  if (!/^[\w.-]+\/[\w.-]+$/.test(repository ?? '')) throw new Error('Invalid GitHub repository.');
  const headers = { Accept: 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28' };
  if (token) headers.Authorization = `Bearer ${token}`;
  const releases = [];
  for (let page = 1; ; page++) {
    const response = await fetcher(`${apiUrl}/repos/${repository}/releases?per_page=100&page=${page}`, { headers });
    if (!response.ok) throw new Error(`Cannot verify existing releases (HTTP ${response.status}); refusing to build.`);
    const batch = await response.json();
    if (!Array.isArray(batch)) throw new Error('Unexpected releases API response.');
    releases.push(...batch);
    if (batch.length < 100) return releases;
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = fileURLToPath(new URL('..', import.meta.url));
  const version = readSourceVersion(root);
  const releases = await fetchReleases({ repository: process.env.GITHUB_REPOSITORY, token: process.env.GH_TOKEN, apiUrl: process.env.GITHUB_API_URL });
  const plan = planRelease({ version, eventName: process.env.GITHUB_EVENT_NAME, ref: process.env.GITHUB_REF, releases });
  if (process.env.GITHUB_OUTPUT) fs.appendFileSync(process.env.GITHUB_OUTPUT, `version=${plan.version}\ntag=${plan.tag}\nshould_release=${plan.shouldRelease}\n`);
  console.log(plan.reason);
}

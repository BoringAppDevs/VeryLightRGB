// SPDX-License-Identifier: GPL-3.0-or-later
// Inspect the exact staged blobs before publishing; never print matched values.
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';

const all = process.argv.includes('--all');
const git = args => execFileSync('git', args, { maxBuffer: 32 * 1024 * 1024, windowsHide: true });
const files = git(all ? ['ls-files', '-z'] : ['diff', '--cached', '--name-only', '--diff-filter=ACMR', '-z'])
  .toString('utf8').split('\0').filter(Boolean);
const findings = [];
const rules = [
  ['access token', /\b(?:gh[pousr]_[A-Za-z0-9]{36,255}|github_pat_[A-Za-z0-9_]{50,255}|sk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{30,}|xox[baprs]-[A-Za-z0-9-]{20,}|AIza[A-Za-z0-9_-]{35}|(?:AKIA|ASIA)[A-Z0-9]{16})\b/],
  ['private key', /-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----/],
  ['personal Windows path', /[A-Z]:[\\/]+Users[\\/]+(?!Public\b|Default\b|All Users\b|USERNAME\b|USERPROFILE\b|<|\$|\{|%)[A-Za-z0-9_. -]+/i],
  ['Windows account SID', /S-1-5-21-\d+-\d+-\d+-\d+/],
  ['Windows computer name', /\b(?:DESKTOP|LAPTOP)-[A-Z0-9]{5,}\b/i],
];
for (const file of files) {
  if (/^(?:local|artifacts|\.vs|device-db\/upstream|third_party\/openrgb\/(?:dist|runtime))\//i.test(file) ||
      /(?:^|\/)(?:bin|obj)\//i.test(file) ||
      /(?:^|\/)(?:\.env(?:\..*)?|id_rsa|id_ed25519|settings\.json|startup-registration\.json)$/i.test(file) && !file.endsWith('.env.example') ||
      /\.(?:exe|dll|pdb|zip|gz|7z|pfx|p12|pem|key|kdbx|log)$/i.test(file)) {
    findings.push({ file, reason: 'private/generated file or downloaded archive' });
    continue;
  }
  const bytes = all ? fs.readFileSync(file) : git(['show', ':' + file]);
  if (/^native\/assets\/(?:app-icon\.png|app\.ico)$/.test(file)) continue;
  if (bytes.includes(0)) {
    findings.push({ file, reason: 'unexpected binary file' });
    continue;
  }
  const text = bytes.toString('utf8');
  for (const [reason, rule] of rules) {
    // Deliberately synthetic identifier used to verify scheduled-task XML.
    const inspected = file === 'tests/RgbTray.Tests/StartupTests.cs'
      ? text.replaceAll('S-1-5-21-' + [123, 456, 789, 1001].join('-'), '<test SID>') : text;
    if (rule.test(inspected)) findings.push({ file, reason });
  }
}
if (findings.length) {
  for (const { file, reason } of findings) console.error(`${file}: ${reason}`);
  process.exitCode = 1;
} else {
  console.log(`PASS: ${files.length} ${all ? 'tracked files' : 'staged blobs'} checked for private data and generated files.`);
}

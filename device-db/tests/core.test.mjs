import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import test from 'node:test';
import {spawn,execFileSync} from 'node:child_process';
const repo=path.resolve(import.meta.dirname,'../..');
const binary=path.join(repo,'artifacts/device-db-core/native-driver-core.exe');

test('compiled corpus enumerates without hardware detection or SDK',()=>{
  const data=JSON.parse(execFileSync(binary,['detectors'],{encoding:'utf8',maxBuffer:8*1024*1024,windowsHide:true}));
  assert.equal(data.serverRequired,false); assert.equal(data.hardwareDetectionStarted,false);
  assert.ok(data.detectors.length>2000);
  assert.ok(data.detectors.some(d=>d.name==='Corsair Lighting Node Pro'&&d.vid===0x1b1c&&d.pid===0x0c0b));
});

test('driver-core API applies and rejects frames on an in-memory controller',()=>{
  for(let i=0;i<10;i++) {
    const data=JSON.parse(execFileSync(binary,['--self-test'],{encoding:'utf8',timeout:10000,windowsHide:true}));
    assert.equal(data.ok,true); assert.ok(data.checks>=11);
    assert.equal(data.hardwareDetectionStarted,false); assert.equal(data.serverRequired,false);
  }
});

test('stdio API rejects invalid detector selection before any hardware access and opens no TCP port',async()=>{
  const directory=fs.mkdtempSync(path.join(repo,'local/device-db-sources/core-stdio-smoke-'));
  const child=spawn(binary,['stream',directory],{windowsHide:true,stdio:['pipe','pipe','pipe']});
  let output='',errors='';
  child.stdout.setEncoding('utf8'); child.stdout.on('data',d=>{output+=d;});
  child.stderr.setEncoding('utf8'); child.stderr.on('data',d=>{errors+=d;});
  const finished=new Promise((resolve,reject)=>{child.on('exit',(code,signal)=>resolve({code,signal}));child.on('error',reject);});
  try {
    child.stdin.write(JSON.stringify({op:'devices'})+'\n');
    child.stdin.write(JSON.stringify({op:'open',detectors:[]})+'\n');
    child.stdin.write(JSON.stringify({op:'open',detectors:['__NONEXISTENT_DETECTOR__']})+'\n');
    const deadline=Date.now()+5000;
    while(output.trim().split('\n').length<3&&Date.now()<deadline) await new Promise(r=>setTimeout(r,25));
    const lines=output.trim().split('\n').filter(Boolean).map(s=>JSON.parse(s));
    assert.equal(lines.length,3,errors);
    assert.deepEqual(lines[0].devices,[]);
    assert.equal(lines[1].ok,false); assert.equal(lines[2].ok,false);
    const connections=execFileSync('powershell.exe',['-NoProfile','-NonInteractive','-Command',
      `@(Get-NetTCPConnection -OwningProcess ${child.pid} -ErrorAction SilentlyContinue).Count`],{encoding:'utf8',windowsHide:true,timeout:15000}).trim();
    assert.equal(connections,'0');
  } finally {child.stdin.end();}
  const completion=await finished;
  assert.equal(completion.code,0,errors);
  // No ResourceManager was instantiated: only the explicitly selected empty smoke directory exists.
  assert.deepEqual(fs.readdirSync(directory),[]);
});

test('the EXE and bundled device transports import no Qt desktop libraries',()=>{
  const inspector=path.join(repo,'local/toolchain/llvm-mingw-20261020-ucrt-x86_64/bin/llvm-readobj.exe');
  for(const file of ['native-driver-core.exe','hidapi-hotplug.dll','libusb-1.0.dll','PawnIOLib.dll']) {
    const report=execFileSync(inspector,['--coff-imports',path.join(path.dirname(binary),file)],{encoding:'utf8',windowsHide:true});
    assert.doesNotMatch(report,/Name:\s*Qt\w*\.dll/i);
    assert.doesNotMatch(report,/Name:\s*(?:VCRUNTIME|MSVCP|libc\+\+|libunwind)\S*\.dll/i);
  }
});

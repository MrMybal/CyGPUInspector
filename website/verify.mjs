import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {copy,media} from './content.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const site=path.resolve(root,process.argv[2]||'build/showcase/site');
const inventory=JSON.parse(await fs.readFile(path.join(site,'site-manifest.json'),'utf8'));
const hash=b=>createHash('sha256').update(b).digest('hex');
assert.deepEqual(Object.keys(copy.en).sort(),Object.keys(copy.fr).sort());
for(const strings of Object.values(copy))for(const value of Object.values(strings))assert(value.trim());
for(const [name,digest] of Object.entries(inventory.files)){
  const target=path.resolve(site,name);assert(target.startsWith(site+path.sep));
  const bytes=await fs.readFile(target);assert.equal(hash(bytes),digest,name);
  const revision=/\.([a-f0-9]{12})\.(?:js|css|png|jpe?g)$/.exec(name);if(revision)assert.equal(revision[1],hash(bytes).slice(0,12),`Asset cache revision: ${name}`);
  assert(!/(?:node_modules|\.git\/|\.CyDocBuild|Database\/|Games\/|settings\.json|\.pdb$|\.exe$|\.dll$)/i.test(name),`Unexpected output: ${name}`);
}
async function walk(dir){let found=[];for(const e of await fs.readdir(dir,{withFileTypes:true})){const p=path.join(dir,e.name);found.push(...e.isDirectory()?await walk(p):[path.relative(site,p).replaceAll('\\','/')]);}return found;}
assert.deepEqual((await walk(site)).sort(),[...Object.keys(inventory.files),'site-manifest.json'].sort(),'Undeclared files in output');
const html=await fs.readFile(path.join(site,'index.html'),'utf8');
const ids=new Set([...html.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]));
for(const [,url] of html.matchAll(/(?:href|src)="([^"]+)"/g)){
  if(url.startsWith('#')){assert(ids.has(url.slice(1)),`Missing section: ${url}`);continue;}
  if(/^https?:/.test(url))continue;await fs.access(path.resolve(site,url.split('#')[0]));
}
const manifest=JSON.parse(await fs.readFile(path.join(site,'manual/DocData/manifest.json'),'utf8'));
for(const [,page] of html.matchAll(/data-doc="([^"]+)"/g))for(const locale of ['en','fr'])assert(manifest.pages.some(p=>p.id===page&&p.locale===locale));
for(const id of Object.keys(media))for(const locale of ['en','fr'])assert(copy[locale][id+'Alt']&&copy[locale][id+'Caption']);
const runtime=await fs.readFile(path.resolve(site,/script src="([^"]+)"/.exec(html)[1]),'utf8');
assert(!/\bfetch\s*\(|requestFullscreen|<script[^>]+type=["']module/.test(runtime));
assert(html.includes('lang="en"')&&html.includes('data-language="fr"'));
execFileSync(process.execPath,[path.join(root,'.CyDocBuild/check.mjs'),path.join(site,'manual')],{cwd:root,stdio:'inherit'});
console.log(`Verified showcase: ${Object.keys(inventory.files).length+1} files, local references, EN/FR routes, full manual, hashed assets.`);

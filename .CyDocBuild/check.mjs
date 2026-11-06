// Validate exactly the reader artifact that is uploaded to Pages or bundled with the application.
import fs from 'node:fs/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';
import assert from 'node:assert/strict';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const directory=path.resolve(root,process.argv[2]||'Documentation');
const urlIndex=process.argv.indexOf('--url');
const remote=urlIndex<0?null:new URL(process.argv[urlIndex+1]);
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const read=name=>fs.readFile(path.join(directory,name));
const receipt=JSON.parse(await read('DocData/delivery.json'));
assert.equal(receipt.schema,1);assert.deepEqual(receipt.modules,['image']);
const expected=new Set([...Object.keys(receipt.files),'DocData/delivery.json']);
async function inventory(folder) {
 const items=await fs.readdir(folder,{withFileTypes:true});
 const result=[];
 for(const item of items){assert(!item.isSymbolicLink(),'Symlinks must not enter the Pages artifact');const file=path.join(folder,item.name);result.push(...(item.isDirectory()?await inventory(file):[path.relative(directory,file).replaceAll('\\','/')]));}
 return result;
}
assert.deepEqual(new Set(await inventory(directory)),expected,'Artifact contains only declared documentation files');
for(const [name,digest] of Object.entries(receipt.files)) {
 assert(!name.startsWith('/')&&!name.split('/').includes('..')&&!name.includes('\\')&&!name.includes(':'),name);
 assert(!/(?:^|\/)(?:node_modules|\.git|Tools|TestWithoutAGame)(?:\/|$)|\.(?:exe|dll|addon64|pdb|zip|tgz|log)$/i.test(name),'Non-documentation payload: '+name);
 assert.equal(hash(await read(name)),digest,name+' export checksum');
}
const data=JSON.parse(await read('DocData/manifest.json'));
assert.equal(data.schemaVersion,1);assert.equal(data.defaultLocale,'en');
assert.deepEqual(new Set(data.locales.map(x=>x.code)),new Set(['en','fr']));
const translations=JSON.parse(await read('DocData/translation-report.json'));
for(const status of ['missing','outdated','untracked'])assert.deepEqual(translations[status],[],status+' translations');
const {createDocumentation,parseLink,hasAnchor}=await import(pathToFileURL(path.join(root,'.CyDocBuild/node_modules/cydoc/dist/index.js')));
const core=createDocumentation(data);
assert.deepEqual(core.availableLocales().map(l=>l.code),['en','fr']);
assert(data.pages.some(p=>p.parent),'Hierarchical navigation');
assert(data.pages.filter(p=>p.kind==='api').length===16,'Bilingual public API references');
assert(!data.pages.some(p=>p.kind==='source'||/^(?:source-|help-maintenance|translation-report|readme$)/.test(p.id)),'No raw source or internal pages');
assert((await read('DocAI.md')).length>100,'Separate assistant sidecar');
assert([...expected].every(name=>['Documentation.html','DocAI.md','index.html'].includes(name)||name.startsWith('DocData/')),'Generated-only distribution layout');

const decode=value=>value.replace(/&quot;/g,'"').replace(/&#39;/g,"'").replace(/&lt;/g,'<').replace(/&gt;/g,'>').replace(/&amp;/g,'&');
let links=0,resources=0,diagrams=0;
function asset(value){
 if(/^(?:data:|blob:)/.test(value))return;
 assert(!/^(?:https?:|file:|\/\/)/.test(value),'Reader resources must belong to the export: '+value);
 const logical=decodeURIComponent(value.split(/[?#]/)[0]); const name=logical.startsWith('assets/')?'DocData/'+logical:logical;assert(expected.has(name),'Missing resource: '+name);resources++;
}
for(const page of data.pages){
 const counterpart=core.resolve(page.id,page.locale==='en'?'fr':'en');assert(counterpart&&!counterpart.fallback,page.id+' translation');
 assert.deepEqual(new Set(page.headings.map(h=>h.id)),new Set(counterpart.page.headings.map(h=>h.id)),page.id+' shared section IDs');
 for(const [,raw] of page.html.matchAll(/href="([^"]*)"/g)){
  const href=decode(raw);if(!href||/^(?:https?:|mailto:)/.test(href))continue;
  if(href.startsWith('#/')){
   const route=parseLink(href,data.defaultLocale,'welcome');const target=core.resolve(route.pageId,route.locale);
   assert(target,'Broken page link: '+href);if(route.anchor)assert(hasAnchor(target.page,route.anchor),'Broken section: '+href);
   if(route.state?.media)assert(target.page.html.includes(`data-cydoc-id="${route.state.media}"`),'Broken media: '+href);
   links++;
  }else if(href.startsWith('#'))assert(hasAnchor(page,href.slice(1)),href);else asset(href);
 }
 for(const [,src] of page.html.matchAll(/\bsrc="([^"]+)"/g))asset(decode(src));
 for(const [,type] of page.html.matchAll(/data-cydoc-module="([^"]+)"/g))assert.equal(type,'image');
 diagrams+=(page.html.match(/<svg role="img"/g)||[]).length;
}
const index=(await read('Documentation.html')).toString();
for(const [,src] of index.matchAll(/(?:src|href)="([^"]+)"/g))asset(decode(src));
for(const [,src] of (await read('DocData/viewer.css')).toString().matchAll(/url\(["']?([^"')]+)["']?\)/g))asset(src);
assert(expected.has('DocData/media-image.js'));assert(![...expected].some(n=>/^DocData\/media-(?:mesh|graph|gif|video|gallery)\.js$/.test(n)));
if(remote){
 assert(remote.protocol==='http:'||remote.protocol==='https:');
 const entries=Object.entries(receipt.files);
 for(let start=0;start<entries.length;start+=8)await Promise.all(entries.slice(start,start+8).map(async([name,digest])=>{
  const response=await fetch(new URL(name,remote));assert(response.ok,`${response.status} ${name}`);
  assert.equal(hash(Buffer.from(await response.arrayBuffer())),digest,'Published bytes differ: '+name);
 }));
}
console.log(`${data.pages.length} pages; ${links} internal links; ${resources} resource references; ${diagrams} SVG diagrams; ${expected.size} documentation files verified${remote?' on '+remote:''}`);

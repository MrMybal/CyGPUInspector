import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {Script} from 'node:vm';
import assert from 'node:assert/strict';
import {product,media,copy} from './content.mjs';
const source=path.dirname(fileURLToPath(import.meta.url)),root=path.resolve(source,'..');
const output=path.join(root,'build/showcase/site');
const work=path.join(root,'build/showcase');
await fs.mkdir(work,{recursive:true});
const stage=path.join(work,`generation-${Date.now()}-${process.pid}`);
await fs.mkdir(path.join(stage,'assets'),{recursive:true});
const hash=b=>createHash('sha256').update(b).digest('hex');
const escape=v=>String(v).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
assert.deepEqual(Object.keys(copy.en).sort(),Object.keys(copy.fr).sort(),'Translation keys must match');
for(const [locale,strings] of Object.entries(copy))for(const [key,value] of Object.entries(strings))assert(typeof value==='string'&&value.trim(),`${locale}.${key} is empty`);
assert.equal(product.version,/CYGI_VERSION_STRING "([^"]+)"/.exec(await fs.readFile(path.join(root,'CyGPUInspectorCore/Include/CyGPUInspectorCore/Version.hpp'),'utf8'))[1]);
execFileSync(process.execPath,[path.join(root,'.CyDocBuild/build.mjs')],{cwd:root,stdio:'inherit'});
const manual=path.join(root,'Documentation'),receipt=JSON.parse(await fs.readFile(path.join(manual,'DocData/delivery.json'),'utf8'));
for(const [name,digest] of Object.entries(receipt.files)){
  const resolved=path.resolve(manual,name);assert(resolved.startsWith(manual+path.sep));
  const bytes=await fs.readFile(resolved);assert.equal(hash(bytes),digest,`Stale manual file: ${name}`);
  const target=path.join(stage,'manual',name);await fs.mkdir(path.dirname(target),{recursive:true});await fs.writeFile(target,bytes);
}
await fs.copyFile(path.join(manual,'DocData/delivery.json'),path.join(stage,'manual/DocData/delivery.json'));
await fs.copyFile(path.join(root,'LICENSE'),path.join(stage,'LICENSE.txt'));
const asset=async(name,bytes)=>{const filename=`assets/${name}.${hash(bytes).slice(0,12)}${path.extname(name)}`.replace(path.extname(name)+'.','.');await fs.writeFile(path.join(stage,filename),bytes);return './'+filename;};
const logo=await asset('logo.png',await fs.readFile(path.join(root,'Assets/cygpuinspector-logo-256.png')));
const mapped={};
for(const [id,entries] of Object.entries(media)){
  mapped[id]={};for(const locale of ['en','fr'])mapped[id][locale]=await asset(`${id}-${locale}${path.extname(entries[locale])}`,await fs.readFile(path.resolve(source,entries[locale])));
}
const css=await asset('site.css',await fs.readFile(path.join(source,'styles.css')));
const runtime='window.CYGI_SHOWCASE='+JSON.stringify({copy,media:mapped}).replaceAll('<','\\u003c')+';\n'+await fs.readFile(path.join(source,'main.js'),'utf8');
new Script(runtime);
const js=await asset('site.js',Buffer.from(runtime));
const t=copy.en;
const text=(key,tag='p',cls='')=>`<${tag}${cls?` class="${cls}"`:''} data-i18n="${key}">${escape(t[key])}</${tag}>`;
const doc=(key,page,cls='text-link')=>`<a class="${cls}" data-doc="${page}" href="./manual/Documentation.html#/en/${page}" data-i18n="${key}">${escape(t[key])}</a>`;
const link=(key,url,cls='text-link')=>`<a class="${cls}" href="${escape(url)}" data-i18n="${key}">${escape(t[key])}</a>`;
const shot=(id,eager=false)=>`<figure class="shot"><button type="button" data-enlarge data-caption="${id}Caption" aria-label="${escape(t.enlarge+t[id+'Caption'])}"><img data-media="${id}" data-alt="${id}Alt" src="${mapped[id].en}" alt="${escape(t[id+'Alt'])}" width="${media[id].width}" height="${media[id].height}" loading="${eager?'eager':'lazy'}"${eager?' fetchpriority="high"':''}></button><figcaption data-i18n="${id}Caption">${escape(t[id+'Caption'])}</figcaption></figure>`;
const head=(prefix)=>`<div class="section-head">${text(prefix+'Eyebrow','p','eyebrow')}${text(prefix+'Title','h2')}${text(prefix+'Body')}</div>`;
const chapters=['inspect','capture','shaders','integrations','documentation','availability'];
const diagram=`<figure class="diagram"><svg viewBox="0 0 1120 130" role="img" data-label="diagramLabel" aria-label="${escape(t.diagramLabel)}"><path d="M240 58H285 M525 58H570 M810 58H855"/><path d="m275 51 10 7-10 7m285-14 10 7-10 7m275-14 10 7-10 7"/>${['diagramGame','diagramTransport','diagramApp','diagramFiles'].map((key,i)=>`<rect x="${i*285}" y="20" width="240" height="76" rx="7"/><text data-i18n="${key}" x="${i*285+120}" y="64" text-anchor="middle">${escape(t[key])}</text>`).join('')}</svg><ol class="diagram-mobile">${['diagramGame','diagramTransport','diagramApp','diagramFiles'].map(key=>`<li data-i18n="${key}">${escape(t[key])}</li>`).join('')}</ol><figcaption data-i18n="diagramCaption">${escape(t.diagramCaption)}</figcaption></figure>`;
const html=`<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="description" content="${escape(t.description)}"><title>${escape(t.title)}</title><link rel="icon" href="${logo}"><link rel="stylesheet" href="${css}"><script src="${js}" defer></script></head>
<body><a class="skip" href="#main" data-i18n="skip">${t.skip}</a>
<header><div class="wrap header-row" data-menu-open="false"><a class="brand" href="#main"><img src="${logo}" width="30" height="30" alt="">CyGPUInspector</a><button class="menu-toggle" type="button" aria-expanded="false" aria-controls="primary-nav" data-i18n="menu">${t.menu}</button><nav id="primary-nav" class="primary-nav" data-label="navigation" aria-label="${t.navigation}">${['inspect','capture','documentation'].map(key=>`<a href="#${key}" data-i18n="${key}">${t[key]}</a>`).join('')}</nav><div class="languages" role="group" data-label="language" aria-label="Language"><button type="button" data-language="en" aria-pressed="true">EN</button><button type="button" data-language="fr" aria-pressed="false">FR</button></div><a class="github" href="${product.repository}">GitHub ↗</a></div></header>
<main id="main" class="wrap"><section class="hero" aria-labelledby="hero-name"><div class="hero-copy">${text('status','p','status')}<div class="identity"><img src="${logo}" width="52" height="52" alt=""><h1 id="hero-name">CyGPUInspector</h1></div>${text('headline','h2')}${text('intro','p','intro')}<div class="actions">${link('download',product.download,'button primary')}${doc('readManual','welcome','button')}</div>${text('platform','p','small')}</div>${shot('overview',true)}</section>
<div class="flow-strip">${['flow1','flow2','flow3'].map(k=>`<span data-i18n="${k}">${t[k]}</span>`).join('')}</div><nav class="chapters" data-label="chapters" aria-label="${t.chapters}">${chapters.map(key=>`<a href="#${key}" data-i18n="${key}">${t[key]}</a>`).join('')}</nav>${text('evidence','p','evidence')}
<section id="inspect" class="chapter">${head('inspect')}<div class="split">${shot('timeline')}<div class="copy-column">${text('inspectDetail')}${doc('timelineLink','timeline')}</div></div></section>
<section id="capture" class="chapter">${head('capture')}<div class="mode-grid">${['live','offline','deep'].map((prefix,i)=>`<article class="mode"><span class="number">0${i+1}</span>${text(prefix+'Title','h3')}${text(prefix+'Body')}</article>`).join('')}</div>${diagram}${doc('captureLink','capture')}</section>
<section id="shaders" class="chapter"><div class="split reverse"><div class="copy-column">${text('shaderEyebrow','p','eyebrow')}${text('shaderTitle','h2')}${text('shaderBody')}${text('shaderDetail')}${doc('shaderLink','hlsl')}</div>${shot('hlsl')}</div><div class="callout">${text('modTitle','h3')}${text('modBody')}${doc('modLink','mods')}</div></section>
<section id="integrations" class="chapter"><div class="section-head">${text('integrationsEyebrow','p','eyebrow')}${text('integrationsTitle','h2')}</div><div class="integration-grid"><article>${text('unrealTitle','h3')}${text('unrealBody')}${doc('unrealLink','unreal')}</article><article>${text('aiTitle','h3')}${text('aiBody')}${text('aiDetail')}<div class="actions">${doc('aiLink','ai')}${doc('mcpLink','mcp')}</div></article></div></section>
<section id="documentation" class="chapter">${head('docs')}<div class="split">${shot('reader')}<div class="manual-card">${text('docsDetail')}${doc('readManual','welcome','button primary')}${doc('limits','troubleshooting')}${doc('api','api-overview')}</div></div><div class="faq">${text('faqTitle','h2')}${[1,2,3,4].map(i=>`<details><summary data-i18n="faq${i}">${escape(t['faq'+i])}</summary>${text('answer'+i)}</details>`).join('')}</div></section>
<section id="availability" class="chapter availability"><div class="copy-column">${text('availableEyebrow','p','eyebrow')}${text('availableTitle','h2')}${text('availableBody')}${text('update')}</div><div class="download-box">${text('status','p','status')}${link('download',product.download,'button primary')}${doc('install','installation')}<div class="actions">${link('release',product.release)}${link('checksum',product.download+'.sha256')}</div>${text('platform','p','small')}</div></section>
</main><footer class="wrap footer-row"><div class="footer-brand"><a class="brand" href="#main"><img src="${logo}" width="30" height="30" alt="">CyGPUInspector</a>${text('footer')}</div><nav class="footer-links">${doc('documentation','welcome')}${link('license','./LICENSE.txt')}${doc('notices','third-party')}<a href="${product.repository}">GitHub ↗</a></nav></footer>
<dialog id="image-viewer" aria-labelledby="viewer-caption" data-label="viewerLabel" aria-label="${t.viewerLabel}"><div class="viewer-top"><p id="viewer-caption"></p><button class="viewer-close" type="button" data-i18n="close">${t.close}</button></div><img alt=""></dialog></body></html>`;
await fs.writeFile(path.join(stage,'index.html'),html);
await fs.writeFile(path.join(stage,'Documentation.html'),'<!doctype html><html lang="en"><meta charset="utf-8"><title>CyGPUInspector documentation</title><script>location.replace("./manual/Documentation.html"+location.search+location.hash)</script><a href="./manual/Documentation.html">Documentation</a></html>');
execFileSync(process.execPath,[path.join(root,'.CyDocBuild/check.mjs'),path.join(stage,'manual')],{cwd:root,stdio:'inherit'});
const manifest=JSON.parse(await fs.readFile(path.join(stage,'manual/DocData/manifest.json'),'utf8'));
for(const [,page] of html.matchAll(/data-doc="([^"]+)"/g))for(const locale of ['en','fr'])assert(manifest.pages.some(p=>p.id===page&&p.locale===locale),`Missing manual route: ${locale}/${page}`);
for(const [,url] of html.matchAll(/(?:src|href)="([^"]+)"/g)){
  if(/^(https?:|#)/.test(url))continue;const target=path.resolve(stage,url.split('#')[0]);assert(target.startsWith(stage+path.sep));await fs.access(target);
}
async function walk(dir){let files=[];for(const entry of await fs.readdir(dir,{withFileTypes:true})){const p=path.join(dir,entry.name);files.push(...entry.isDirectory()?await walk(p):[p]);}return files;}
const files={};
for(const file of await walk(stage))files[path.relative(stage,file).replaceAll('\\','/')]=hash(await fs.readFile(file));
await fs.writeFile(path.join(stage,'site-manifest.json'),JSON.stringify({schema:1,version:product.version,locales:['en','fr'],files},null,2));
assert.equal(path.resolve(output),path.join(root,'build','showcase','site'));
try{await fs.rename(output,path.join(work,`retired-${Date.now()}`));}catch(e){if(e.code!=='ENOENT')throw e;}
await fs.rename(stage,output);
console.log(`Showcase ${product.version}: ${Object.keys(files).length+1} files, EN/FR, ${output}`);

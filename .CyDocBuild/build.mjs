// Generate the complete local reader from application-owned, bilingual sources.
import * as fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import { spawnSync } from 'node:child_process';
const workspace = path.dirname(fileURLToPath(import.meta.url));
const root = path.dirname(workspace);
const args = process.argv.slice(2);
const argument = (key, fallback) => args.includes(key) ? args[args.indexOf(key)+1] : fallback;
const destination = path.resolve(root, argument('--output', 'Documentation'));
if (destination===workspace || destination===path.join(workspace,'internal') || !destination.startsWith(root+path.sep) || destination.startsWith(workspace+path.sep) && !destination.startsWith(path.join(workspace,'internal')+path.sep)) throw new Error('Output must remain inside the application and outside authoring inputs');
if (Number(process.versions.node.split('.')[0]) < 22) throw new Error('Documentation generation requires Node.js 22+');
const sha = data => createHash('sha256').update(data).digest('hex');
const read = file => fs.readFile(path.join(workspace,file),'utf8');
const origin = JSON.parse(await read('vendor/ORIGIN.json'));
if (sha(await fs.readFile(path.join(workspace,'vendor/cydoc-0.1.0.tgz'))) !== origin.sha256) throw new Error('CyDoc archive differs from its pinned provenance');
const config = JSON.parse(await read('cydoc.config.json'));
const versionHeader = 'CyGPUInspectorCore/Include/CyGPUInspectorCore/Version.hpp';
const version = (await fs.readFile(path.join(root,versionHeader),'utf8')).match(/CYGI_VERSION_STRING "([^"]+)"/)[1];
async function walk(directory) {
 const out=[];
 for (const entry of await fs.readdir(directory,{withFileTypes:true})) {
  if (['internal','node_modules','__pycache__'].includes(entry.name)) continue;
  const file=path.join(directory,entry.name);
  if(entry.isSymbolicLink()) throw new Error('Documentation input cannot be a symlink');
  out.push(...(entry.isDirectory()?await walk(file):[file]));
 }
 return out.sort();
}
const inputs = [...await walk(workspace),...['CyGPUInspectorCore/Include/CyGPUInspectorCore/InProcessApi.h','CyGPUInspectorCore/Include/CyGPUInspectorCore/ModPackage.hpp',versionHeader].map(p=>path.join(root,p))];
const inputHashes = Object.fromEntries(await Promise.all(inputs.map(async p=>[path.relative(root,p).replaceAll('\\','/'),sha(await fs.readFile(p))])));
const fingerprint = sha(JSON.stringify({inputs:inputHashes,pages:args.includes('--pages')}));
const old = await fs.readFile(path.join(destination,'DocData/delivery.json'),'utf8').then(JSON.parse).catch(()=>null);
if (!args.includes('--check') && old?.inputFingerprint===fingerprint && (await Promise.all(Object.entries(old.files).map(async([name,digest])=>sha(await fs.readFile(path.join(destination,name)).catch(()=>Buffer.alloc(0)))===digest))).every(Boolean)) {
 console.log('CyDoc: documentation is current');process.exit(0);
}
const require = createRequire(path.join(workspace,'package.json'));
const packageRoot = path.join(workspace,'node_modules/cydoc');
const cli = path.join(packageRoot,JSON.parse(await fs.readFile(path.join(packageRoot,'package.json'),'utf8')).bin.cydoc);
const { importApi } = await import('cydoc/api-import');
const { readFrontmatter } = await import('cydoc/compiler');
for(const filename of await fs.readdir(path.join(workspace,'manual/en'))) {
 const original=(await read('manual/en/'+filename)).replace(/\r\n?/g,'\n');
 const translated=(await read('manual/fr/'+filename)).replace(/\r\n?/g,'\n');
 const en=readFrontmatter(original),fr=readFrontmatter(translated);
 const revision=sha(original.split('---\n').slice(2).join('---\n')).slice(0,12);
 if(en.metadata.revision!==revision || fr.metadata.sourceRevision!==revision)
  throw new Error(`Review the English edit and its complete French translation, then update revision/sourceRevision: ${filename}`);
 if(!en.body.trim()||!fr.body.trim()||en.body.trim()===fr.body.trim())throw new Error(`Empty or copied translation: ${filename}`);
}

const stage = path.join(workspace,'internal',`generation-${Date.now()}-${process.pid}`);
const prepared = path.join(stage,'.CyDocBuild');
await fs.mkdir(prepared,{recursive:true});
await fs.cp(path.join(workspace,'manual'),path.join(prepared,'manual'),{recursive:true});
// The public neutral importer supplies signatures/parameters/relationships, never source bodies.
const imported = await importApi(config.apiImports,{base:workspace,locales:config.locales});
for (const page of imported) {
 const original = imported.find(p=>p.id===page.id&&p.locale==='en');
 const revision=sha(original.markdown).slice(0,12);
 const front=`---\nid: ${page.id}\ntitle: ${page.title}\ndescription: ${page.locale==='fr'?'Paramètres, résultat et appels associés.':'Parameters, result and related calls.'}\nsection: ${page.locale==='fr'?'Référence développeur':'Developer reference'}\nparent: api-overview\nkind: api\norder: 110\n${page.locale==='fr'?'sourceRevision':'revision'}: ${revision}\n---\n\n`;
 await fs.writeFile(path.join(prepared,'manual',page.locale,page.id+'.md'),front+page.markdown);
}
const preparedConfig={...config,version,apiImports:[],content:'manual',assets:path.join(workspace,'assets'),out:'../Documentation'};
await fs.writeFile(path.join(prepared,'cydoc.config.json'),JSON.stringify(preparedConfig,null,2));
const result=spawnSync(process.execPath,[cli,args.includes('--check')?'check':'build',path.join(prepared,'cydoc.config.json')],{stdio:'inherit',cwd:root});
if(result.status) throw new Error('CyDoc generation failed');
const exported=path.join(stage,'Documentation');
// Source templates provide application branding, Nord and the existing reading-data namespace.
await require('esbuild').build({entryPoints:[path.join(workspace,'reader.mjs')],bundle:true,format:'iife',minify:true,target:'es2020',outfile:path.join(exported,'DocData/viewer.js'),legalComments:'eof'});
await fs.copyFile(path.join(workspace,'reader.html'),path.join(exported,'Documentation.html'));
if (args.includes('--pages')) {
 await fs.copyFile(path.join(workspace,'index.html'),path.join(exported,'index.html'));
 await fs.copyFile(path.join(workspace,'compatibility.js'),path.join(exported,'DocData/compatibility.js'));
}
await fs.copyFile(path.join(workspace,'legacy-routes.json'),path.join(exported,'DocData/legacy-routes.json'));
await fs.mkdir(path.join(exported,'DocData/licenses'),{recursive:true});
await fs.copyFile(path.join(workspace,'vendor/LICENSE'),path.join(exported,'DocData/licenses/CyDoc-MIT.txt'));
await fs.copyFile(path.join(workspace,'vendor/ORIGIN.json'),path.join(exported,'DocData/licenses/CyDoc-ORIGIN.json'));
for(const name of ['marked','prismjs','esbuild']) {
 const directory=path.join(workspace,'node_modules',name);
 const notice=(await fs.readdir(directory)).find(p=>/^licen[sc]e(?:\.md|\.txt)?$/i.test(p));
 if(!notice)throw new Error(`Missing ${name} notice`);
 await fs.copyFile(path.join(directory,notice),path.join(exported,'DocData/licenses',name+'-'+notice));
}
const files=Object.fromEntries(await Promise.all((await walk(exported)).map(async file=>[path.relative(exported,file).replaceAll('\\','/'),sha(await fs.readFile(file))])));
await fs.writeFile(path.join(exported,'DocData/delivery.json'),JSON.stringify({schema:1,inputFingerprint:fingerprint,cydocRevision:origin.revision,modules:config.modules,inputs:inputHashes,files},null,2));
if(await fs.stat(destination).catch(()=>null)) {
 if(!await fs.stat(path.join(destination,'DocData/delivery.json')).catch(()=>null)) throw new Error('Refusing to replace an unrecognized directory; choose a fresh documentation output');
 const retired=path.join(workspace,'internal',`retired-${Date.now()}-${process.pid}`);
 await fs.rename(destination,retired);
}
await fs.mkdir(path.dirname(destination),{recursive:true});
await fs.cp(exported,destination,{recursive:true});
console.log(`CyDoc ${origin.version} (${origin.revision.slice(0,12)}): ${destination}/Documentation.html`);

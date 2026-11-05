// Compile application-owned sources with the pinned public CyDoc CLI. Node >= 22, generation only.
import * as fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createRequire } from 'node:module';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(path.join(root, 'Documentation/package.json'));
const args = process.argv.slice(2);
const argument = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
const destination = path.resolve(root, argument('--output', 'bin/Release/Help'));
if (!destination.startsWith(root + path.sep) || destination === root) throw new Error('Help output must stay inside this application workspace');
if (Number(process.versions.node.split('.')[0]) < 22) throw new Error('CyDoc generation requires Node.js 22 or later');
const sha = data => createHash('sha256').update(data).digest('hex');
const slug = text => text.normalize('NFD').replace(/\p{Diacritic}/gu, '').toLowerCase().replace(/[`*]/g, '').replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
const read = file => fs.readFile(path.join(root, file), 'utf8');
const catalog = JSON.parse(await read('Help/catalog.json'));
const config = JSON.parse(await read('Help/cydoc.config.json'));
const origin = JSON.parse(await read('ThirdParty/CyDoc/ORIGIN.json'));
if (sha(await fs.readFile(path.join(root,'ThirdParty/CyDoc/cydoc-0.1.0.tgz'))) !== origin.sha256) throw new Error('The pinned CyDoc package checksum differs from its provenance');
const diagrams = JSON.parse(await read('Help/diagrams.json'));
const sources = [
  'CyGPUInspectorCore/Include/CyGPUInspectorCore/Localization.hpp',
  'CyGPUInspectorCore/Include/CyGPUInspectorCore/ModPackage.hpp',
  'CyGPUInspectorCore/Include/CyGPUInspectorCore/Version.hpp',
  'CyGPUInspectorCore/Include/CyGPUInspectorCore/InProcessApi.h',
  'CyGPUInspectorApp/Source/Help/HelpLauncher.hpp'
];
const walk = async dir => (await Promise.all((await fs.readdir(dir, { withFileTypes: true })).filter(x => x.name !== 'node_modules' && !x.name.startsWith('.')).map(async x => x.isDirectory() ? walk(path.join(dir,x.name)) : [path.join(dir,x.name)]))).flat();
const inputFiles = [...await walk(path.join(root,'Help')), ...await walk(path.join(root,'Docs')), ...await walk(path.join(root,'LICENCES')), ...await walk(path.join(root,'Assets')),
  ...sources.map(x=>path.join(root,x)), ...['README.md','README.fr.md','THIRD-PARTY.md','LICENSE','Tools/build_help.mjs','Documentation/package-lock.json','ThirdParty/CyDoc/ORIGIN.json','ThirdParty/CyDoc/cydoc-0.1.0.tgz','ThirdParty/CyDoc/cydoc_link.hpp'].map(x=>path.join(root,x))];
const computeFingerprint = async () => sha(Buffer.concat(await Promise.all(inputFiles.sort().map(async x => Buffer.concat([Buffer.from(path.relative(root,x)), await fs.readFile(x)])))));
let fingerprint = await computeFingerprint();
const receipt = path.join(destination,'delivery.json');
if (!args.includes('--check') && !args.includes('--accept-translations')) {
  const old = await fs.readFile(receipt,'utf8').then(JSON.parse).catch(()=>null);
  if (old?.inputFingerprint === fingerprint && (await Promise.all(Object.entries(old.files).map(async ([name,hash]) => sha(await fs.readFile(path.join(destination,name)).catch(()=>Buffer.alloc(0))) === hash))).every(Boolean)) {
    console.log('CyDoc: exported documentation is current'); process.exit(0);
  }
}
let cli;
try {
  const packageRoot = path.join(root, 'Documentation/node_modules/cydoc');
  const metadata = JSON.parse(await fs.readFile(path.join(packageRoot, 'package.json'), 'utf8'));
  cli = path.join(packageRoot, metadata.bin.cydoc);
}
catch { throw new Error('Run npm --prefix Documentation ci before building documentation'); }
const { createLink } = await import(pathToFileURL(path.join(root,'Documentation/node_modules/cydoc/dist/index.js')));
const stage = path.join(root, 'build', `cydoc-generation-${Date.now()}-${process.pid}`);
await fs.mkdir(path.join(stage,'content/en'),{recursive:true}); await fs.mkdir(path.join(stage,'content/fr'),{recursive:true});
await fs.mkdir(path.join(stage,'assets'),{recursive:true});
await fs.cp(path.join(root,'Help/Media'),path.join(stage,'assets/media'),{recursive:true});
await fs.cp(path.join(root,'Assets'),path.join(stage,'assets/product'),{recursive:true});
await fs.cp(path.join(root,'LICENCES'),path.join(stage,'assets/notices'),{recursive:true});
const pageEntries = [], fileIndex = new Map(), aliases = {}, legacy = {}, issues = [];
function add(id, titles, files, section, figures = []) {
  const entry = { id, titles, files, section, figures, bodies:{}, headings:{} };
  pageEntries.push(entry);
  for (const locale of ['en','fr']) fileIndex.set(path.resolve(root,files[locale]),{entry,locale});
  return entry;
}
function original(file) { return file.startsWith('Reference/') ? file.slice(10) : 'Help/'+file; }
for (const chapter of catalog.chapters) {
  const id = slug(chapter.id);
  if (id !== chapter.id) aliases[chapter.id] = id;
  add(id,chapter.title,Object.fromEntries(['en','fr'].map(l=>[l,original(chapter.file[l])])),chapter.category,chapter.figures);
}
add('readme',{en:'Product overview',fr:'Présentation du produit'},{en:'README.md',fr:'README.fr.md'},'Technical reference');
add('third-party',{en:'Third-party components',fr:'Composants tiers'},{en:'THIRD-PARTY.md',fr:'THIRD-PARTY.md'},'Licenses');
add('legal-product',{en:'Product license (original text)',fr:'Licence du produit (texte original)'},{en:'LICENSE',fr:'LICENSE'},'Licenses');
add('help-maintenance',{en:'Maintaining this documentation',fr:'Maintenance de cette documentation'},{en:'Help/README.md',fr:'Help/README.fr.md'},'Maintenance');
for (const absolute of await walk(path.join(root,'LICENCES'))) {
  const file = path.relative(root,absolute).replaceAll('\\','/');
  const id = 'legal-'+slug(file.slice('LICENCES/'.length));
  add(id,{en:file,fr:file},{en:file,fr:file},'Licenses');
}
function headings(text) {
  let code=false; const out=[];
  for (const line of text.split(/\r?\n/)) {
    if (/^(```|~~~)/.test(line)) code=!code;
    if (!code && /^#{1,6} /.test(line)) out.push(line.replace(/^#+ /,''));
  }
  return out;
}
for (const entry of pageEntries) {
  for (const locale of ['en','fr']) {
    entry.bodies[locale] = (await read(entry.files[locale])).replace(/\r\n?/g,'\n');
    entry.headings[locale] = headings(entry.bodies[locale]);
  }
  if (entry.headings.en.length !== entry.headings.fr.length) throw new Error(`Align bilingual headings before exporting: ${entry.id} (${entry.headings.en.length}/${entry.headings.fr.length})`);
  legacy[entry.id]={en:{},fr:{}};
  const used=new Set();
  entry.anchorIds=entry.headings.en.map((h,i)=>{ let id=slug(h)||`section-${i}`; while(used.has(id))id+='-section'; used.add(id); return id; });
  const readmeFrench={
    'cygpuinspector':'cygpuinspector','captures-d-ecran':'screenshots','composants':'components',
    'ce-qui-fonctionne-aujourd-hui':'what-works-today','l-assistant-ia':'the-ai-assistant',
    'langue':'language','brancher-un-agent-ia-externe':'connecting-an-external-ai-agent',
    'construire':'building','installation':'installing','essayer-sans-jeu':'try-without-a-game',
    'tests':'tests','compatibilite':'honesty-about-what-this-is-not','documentation':'documentation','tiers':'third-party'
  };
  for (const locale of ['en','fr']) entry.headings[locale].forEach((h,i)=>{
    const key=slug(h); legacy[entry.id][locale][key]=entry.id==='readme'&&locale==='fr'?(readmeFrench[key]||entry.anchorIds[i]):entry.anchorIds[i];
    const old=h.normalize('NFD').replace(/\p{Diacritic}/gu,'').toLowerCase().replace(/[^a-z0-9 -]/g,'').replace(/ +/g,'-');
    legacy[entry.id][locale][old]=legacy[entry.id][locale][key];
  });
}
function targetLink(entry,locale,href) {
  if (/^(https?:|mailto:|doc:)/.test(href)) return href;
  if (href.startsWith('figure:')) {
    const i=Number(href.slice(7)); if (!entry.figures[i]) throw new Error(`Missing figure: ${entry.id}/${href}`);
    const name=path.basename(entry.figures[i].image[locale]).replace(/-(en|fr)\.png$/,'');
    return diagrams[name] ? `doc:${entry.id}#figure-${i}` : `doc:${entry.id}?media=figure-${i}`;
  }
  let [file,anchor] = href.split('#'), target=entry, targetLocale=locale;
  if (file.startsWith('help:')) target=pageEntries.find(x=>x.id===slug(file.slice(5)));
  else if (file) {
    let absolute=path.resolve(path.dirname(path.join(root,entry.files[locale])),decodeURIComponent(file));
    const oldReference=path.join(root,'Help/Reference');
    if (absolute.startsWith(oldReference+path.sep)) absolute=path.join(root,path.relative(oldReference,absolute));
    if ([path.join(root,'Docs'),path.join(root,'Docs/fr')].includes(absolute)) return 'doc:ref-architecture';
    const sourceRelative=path.relative(root,absolute).replaceAll('\\','/');
    if (sources.includes(sourceRelative)) return 'doc:source-'+slug('sources/'+sourceRelative);
    const destination=fileIndex.get(absolute);
    target=destination?.entry;
    if (target && target.files.en !== target.files.fr) targetLocale=destination.locale;
    if (!target) { issues.push({page:entry.id,locale,target:href,reason:'Source or non-page resource outside distributed page scope'}); return 'doc:source-scope'; }
  }
  if (!target) throw new Error(`Unknown page link: ${entry.id}: ${href}`);
  const mapped=anchor ? (legacy[target.id][targetLocale][slug(decodeURIComponent(anchor))] || legacy[target.id][targetLocale][decodeURIComponent(anchor)]) : '';
  if (anchor && !mapped) { issues.push({page:entry.id,locale,target:href,reason:'Old section not found; preserved page destination'}); }
  return targetLocale === locale ? `doc:${target.id}${mapped?'#'+mapped:''}` : createLink({locale:targetLocale,pageId:target.id,anchor:mapped||undefined});
}
const trackingFile=path.join(root,'Help/translations.json');
const tracking=await fs.readFile(trackingFile,'utf8').then(JSON.parse).catch(()=>({}));
for (const [order,entry] of pageEntries.entries()) {
  const revision=sha(entry.bodies.en).slice(0,12);
  if (args.includes('--accept-translations')) tracking[entry.id]={fr:revision};
  for (const locale of ['en','fr']) {
    let body=entry.bodies[locale], number=0, code=false;
    if (entry.section !== 'Licenses' || entry.id === 'third-party') body=body.split(/\r?\n/).map(line=>{
      if (/^(```|~~~)/.test(line)) code=!code;
      if (!code && /^#{1,6} /.test(line)) {
        const old=slug(line.replace(/^#+ /,'')); number++;
        return line+` {#${legacy[entry.id][locale][old]}}`;
      }
      return line;
    }).join('\n');
    // Existing original legal text is not translated or rewritten.
    if (entry.section === 'Licenses' && entry.id !== 'third-party') body='```text\n'+body+'\n```';
    else {
      body=body.replace(/<p[^>]*><img src="([^"]+)" alt="([^"]*)"[^>]*><\/p>/g,(_,src,alt)=>`![${alt}](${src})`);
      let imageNumber=0;
      body=body.replace(/!\[([^\]]*)\]\(([^)]+)\)/g,(_,alt,src)=>{
        const absolute=path.resolve(path.dirname(path.join(root,entry.files[locale])),src);
        let asset;
        if (absolute.startsWith(path.join(root,'Assets')+path.sep)) asset='assets/product/'+path.relative(path.join(root,'Assets'),absolute).replaceAll('\\','/');
        else throw new Error(`Unexpected Markdown image: ${entry.id}: ${src}`);
        return '\n```image\n'+JSON.stringify({id:'inline-'+imageNumber++,src:asset,alt,controls:'minimal'})+'\n```\n';
      });
      // Keep code samples verbatim: only rewrite links outside fenced blocks.
      let fence=false;
      body=body.split('\n').map(line=>{if(/^(```|~~~)/.test(line)){fence=!fence;return line;}return fence?line:line.replace(/\[([^\]]+)\]\(([^)]+)\)/g,(_,label,href)=>`[${label}](${targetLink(entry,locale,href)})`);}).join('\n');
    }
    if (entry.figures.length) body+=`\n\n## ${locale==='fr'?'Illustrations':'Illustrations'} {#illustrations}\n`;
    for (const [i,figure] of entry.figures.entries()) {
      const name=path.basename(figure.image[locale]).replace(/-(en|fr)\.png$/,'');
      body+=`\n### ${figure.title[locale]} {#figure-${i}}\n\n${figure.caption[locale]}\n\n`;
      if (diagrams[name]) {
        const spec=diagrams[name];
        const nodes=spec.nodes.map(([id,en,fr,x,y])=>({id,label:locale==='fr'?fr:en,x,y,width:280}));
        body+='```diagram\n'+JSON.stringify({type:'flow',title:figure.title[locale],nodes,edges:spec.edges.map(([from,to])=>({from,to}))})+'\n```\n';
      } else body+='```image\n'+JSON.stringify({id:`figure-${i}`,src:'assets/media/'+path.basename(figure.image[locale]),alt:figure.title[locale],controls:'minimal'})+'\n```\n';
    }
    const section=locale==='fr'?({'User guide':'Guide utilisateur','Technical reference':'Référence technique','Licenses':'Licences','Maintenance':'Maintenance'}[entry.section]||entry.section):entry.section;
    const metadata=`---\nid: ${entry.id}\ntitle: ${entry.titles[locale].replaceAll('\n',' ')}\ndescription: ${entry.titles[locale].replaceAll('\n',' ')}\nsection: ${section}\norder: ${order}\n${locale==='en'?'revision: '+revision:'sourceRevision: '+(tracking[entry.id]?.fr||'unreviewed')}\n---\n\n`;
    await fs.writeFile(path.join(stage,'content',locale,entry.id+'.md'),metadata+body);
  }
}
if (args.includes('--accept-translations')) {
  await fs.writeFile(trackingFile,JSON.stringify(tracking,null,2)+'\n');
  fingerprint = await computeFingerprint();
}
for (const locale of ['en','fr']) {
  const fr=locale==='fr';
  await fs.writeFile(path.join(stage,'content',locale,'source-scope.md'),`---\nid: source-scope\ntitle: ${fr?'Périmètre des sources et état de migration':'Source scope and migration status'}\nsection: ${fr?'Maintenance':'Maintenance'}\norder: 280\n${fr?'sourceRevision':'revision'}: 1\n---\n\n## ${fr?'Sources distribuées':'Distributed sources'} {#sources}\n\n${fr?'Index lexical C++ uniquement : commentaires et dépendances reconnues, sans analyse de compilateur ni résolution des macros. Les fichiers privés, configurations, outils fournisseurs et dépendances tierces ne sont pas indexés.':'C++ lexical source index only: comments and recognized dependencies, without compiler analysis or macro resolution. Private files, configurations, provider tools and third-party sources are not indexed.'}\n\n${sources.map(file=>'- `'+file+'`').join('\n')}\n\n## ${fr?'Réserves du contenu existant':'Existing content caveats'} {#caveats}\n\n${fr?'Les mentions de milestones et anciens nombres de tests décrivent des vérifications historiques, pas une certification actuelle. Les limites techniques restent explicites ; aucun replay GPU, source HLSL originale ou compatibilité supplémentaire n’est annoncé.':'Milestone labels and old test counts describe historical checks, not a current certification. Technical limits remain explicit; no GPU replay, original HLSL recovery or additional compatibility is claimed.'}\n\n${fr?'Certains anciens liens de code hors périmètre arrivent ici ; leur libellé reste conservé dans l’article.':'Some old code links outside the distributed scope arrive here; their labels are preserved in the article.'}\n`);
}
for (const file of sources) { const target=path.join(stage,'sources',file); await fs.mkdir(path.dirname(target),{recursive:true}); await fs.copyFile(path.join(root,file),target); }
config.aliases=aliases; config.version=(await read('CyGPUInspectorCore/Include/CyGPUInspectorCore/Version.hpp')).match(/CYGI_VERSION_STRING "([^"]+)"/)[1];
await fs.writeFile(path.join(stage,'cydoc.config.json'),JSON.stringify(config,null,2));
const result=spawnSync(process.execPath,[cli,args.includes('--check')?'check':'build',path.join(stage,'cydoc.config.json')],{stdio:'inherit',cwd:root});
if (result.status) throw new Error('CyDoc compilation/validation failed');
const exported=path.join(stage,'export');
const esbuild=require('esbuild');
await esbuild.build({entryPoints:[path.join(root,'Help/reader.mjs')],bundle:true,format:'iife',minify:true,target:'es2020',outfile:path.join(exported,'application-reader.js'),nodePaths:[path.join(root,'Documentation/node_modules')],legalComments:'eof'});
await fs.copyFile(path.join(root,'Help/reader.html'),path.join(exported,'index.html'));
await fs.writeFile(path.join(exported,'legacy-routes.json'),JSON.stringify(legacy));
await fs.mkdir(path.join(exported,'licenses'),{recursive:true});
await fs.copyFile(path.join(root,'ThirdParty/CyDoc/LICENSE'),path.join(exported,'licenses/CyDoc-MIT.txt'));
for (const name of ['marked','prismjs','esbuild']) {
  const dir=path.join(root,'Documentation/node_modules',name);
  const file=(await fs.readdir(dir)).find(x=>/^licen[sc]e(?:\.md|\.txt)?$/i.test(x));
  if (!file) throw new Error(`Missing ${name} license`);
  await fs.copyFile(path.join(dir,file),path.join(exported,'licenses',name+'-'+file));
}
await fs.copyFile(path.join(root,'ThirdParty/CyDoc/ORIGIN.json'),path.join(exported,'licenses/CyDoc-ORIGIN.json'));
await fs.writeFile(path.join(stage,'migration-findings.json'),JSON.stringify(issues,null,2));
const exportedFiles=Object.fromEntries(await Promise.all((await walk(exported)).map(async file=>[path.relative(exported,file).replaceAll('\\','/'),sha(await fs.readFile(file))])));
const inputHashes=Object.fromEntries(await Promise.all(inputFiles.map(async file=>[path.relative(root,file).replaceAll('\\','/'),sha(await fs.readFile(file))])));
await fs.writeFile(path.join(exported,'delivery.json'),JSON.stringify({schema:1,inputFingerprint:fingerprint,cydocRevision:origin.revision,modules:['image'],inputs:inputHashes,files:exportedFiles},null,2));
await fs.mkdir(path.dirname(destination),{recursive:true});
if (await fs.stat(destination).catch(()=>null)) {
  const retired=path.join(root,'build/cydoc-migration',`retired-help-${Date.now()}`);
  await fs.mkdir(path.dirname(retired),{recursive:true}); await fs.rename(destination,retired);
}
await fs.cp(exported,destination,{recursive:true});
console.log(`CyDoc: complete local reader → ${destination}; ${issues.length} legacy-link notes in ${path.relative(root,stage)}`);

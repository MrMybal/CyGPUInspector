// Exercise the same CyDoc export through file://, a project subpath, or the deployed Pages URL.
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import http from 'node:http';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const require=createRequire(path.join(root,'.CyDocBuild/package.json'));
const {chromium}=require('@playwright/test');
const exported=path.resolve(root,process.argv[2]||'Documentation');
const output=path.resolve(root,process.argv[3]||'.CyDocBuild/internal/browser');
await fs.mkdir(output,{recursive:true});
const data=JSON.parse(await fs.readFile(path.join(exported,'DocData/manifest.json'),'utf8'));
assert.equal(data.schemaVersion,1); assert.equal(data.defaultLocale,'en');
for(const page of data.pages.filter(p=>p.locale==='en')) {
 const fr=data.pages.find(p=>p.id===page.id&&p.locale==='fr'); assert(fr,page.id+' French page');
 assert.deepEqual(new Set(fr.headings.map(h=>h.id)),new Set(page.headings.map(h=>h.id)),page.id+' shared anchors');
}
const { createDocumentation } = await import(pathToFileURL(path.join(root,'.CyDocBuild/node_modules/cydoc/dist/index.js')));
const fallback = createDocumentation({...data,pages:data.pages.filter(p=>!(p.id==='welcome'&&p.locale==='fr'))}).resolve('welcome','fr');
assert(fallback.fallback && fallback.page.locale==='en' && fallback.requestedLocale==='fr');
const report=JSON.parse(await fs.readFile(path.join(exported,'DocData/translation-report.json'),'utf8'));
assert.deepEqual(report.missing,[]); assert.deepEqual(report.outdated,[]); assert.deepEqual(report.untracked,[]);
const names=await fs.readdir(path.join(exported,'DocData'));
assert.deepEqual(names.filter(n=>/^media-.*\.js$/.test(n)),['media-image.js']);
assert(!names.some(n=>/three|mesh|video|gallery|gif|graph/i.test(n)));
assert.equal(data.pages.filter(p=>p.kind==='api').length,16);
assert(!data.pages.some(p=>p.kind==='source'));
assert(!data.pages.some(p=>p.source&&/AssistantProvider|AppData|node_modules/.test(p.source)));
const browser=await chromium.launch({headless:true,executablePath:process.env.CYGI_TEST_BROWSER||(process.platform==='win32'?'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe':undefined),args:['--disable-background-networking','--disable-extensions']});
const errors=[],requests=[],failures=[],localRequests=[];
let checks=0;
const check=(condition,message)=>{assert(condition,message);checks++;console.log('PASS: '+message);};
let server;
if (process.env.CYGI_DOCS_TEST_TRANSPORT === 'http') {
 const prefix='/CyGPUInspector/';
 server=http.createServer(async(req,res)=>{
  try {
   const url=new URL(req.url,'http://localhost');
   if(!url.pathname.startsWith(prefix))throw new Error('Outside documentation subpath');
   const name=decodeURIComponent(url.pathname.slice(prefix.length))||'Documentation.html';
   const file=path.resolve(exported,name);
   if(!file.startsWith(exported+path.sep))throw new Error('Outside export');
   const types={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.json':'application/json','.png':'image/png','.svg':'image/svg+xml','.ico':'image/x-icon'};
   res.setHeader('Content-Type',types[path.extname(file)]||'application/octet-stream');res.end(await fs.readFile(file));
  } catch {res.writeHead(404);res.end('Not found');}
 });
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
}
const base=process.env.CYGI_DOCS_URL||(server?`http://127.0.0.1:${server.address().port}/CyGPUInspector/index.html`:pathToFileURL(path.join(exported,'Documentation.html')).href);
const offline=base.startsWith('file:');
const scope=new URL('.',base);
const requestPolicy=route=>{
 const url=new URL(route.request().url());
 if(!offline&&url.origin===scope.origin&&url.pathname.startsWith(scope.pathname)){localRequests.push(url.href);return route.continue();}
 requests.push(url.href);return route.abort();
};
const context=await browser.newContext({viewport:{width:1500,height:1000}});
await context.route(/^https?:/,requestPolicy);
const page=await context.newPage();
page.on('pageerror',error=>errors.push(error.message));
page.on('requestfailed',request=>failures.push(request.url()));
page.on('response',response=>{if(response.status()>=400)failures.push(`${response.status()} ${response.url()}`);});
async function go(route){
 const [locale,id]=route.split('?')[0].split('/');
 const pageId=data.aliases?.[id]||id;
 const target=data.pages.find(p=>p.id===pageId&&p.locale===locale)||data.pages.find(p=>p.id===pageId&&p.locale==='en');
 assert(target,'Known route: '+route);
 await page.goto(base+'#/'+route);await page.waitForSelector('.cydoc-viewer');
 await page.waitForFunction(({title,locale,contentLocale})=>document.title.startsWith(title+' — ')&&document.documentElement.lang===locale&&document.querySelector('article')?.lang===contentLocale,{title:target.title,locale,contentLocale:target.locale});
}
try {
 await go('en/welcome');
 check((await page.title()).startsWith('Start here'),'English landing page');
 check(await page.locator('#app').getAttribute('data-theme')==='nord','Navy-compatible Nord theme');
 await page.screenshot({path:path.join(output,'welcome-en.png')});
 check(await page.locator('.sidebar').evaluate(e=>Math.round(e.getBoundingClientRect().width))===330,'sidebar widened by 25 percent');
 check(await page.locator('.side-nav details .nav-children a[data-page]').count()>10,'nested navigation groups and child pages');
 check(await page.locator('.sidebar .aside-note,.sidebar .sidebar-footer,.sidebar .personal-trigger').count()===0,'sidebar has no decorative status or license cards');
 const headerSearch=page.locator('[data-header-search]');
 await headerSearch.fill('shader');
 await page.locator('.search-popover[open] .search-result').first().waitFor();
 check(await page.locator('.search-popover').isVisible(),'header search returns live results');
 await headerSearch.fill(''); await page.keyboard.press('Escape');
 await page.locator('.search-dialog').waitFor({state:'hidden'});
 await page.locator('.brand').focus();
 // A real keyboard round trip clears a tooltip dismissed by Escape, across browser engines.
 await page.keyboard.press('Tab'); await page.keyboard.press('Shift+Tab');
 await page.waitForFunction(()=>document.activeElement?.classList.contains('brand'));
 const brandLabel=await page.locator('.brand').getAttribute('aria-label');
 check(brandLabel.includes(data.name)&&brandLabel.includes(data.version),'keyboard focus exposes the full application name and version');
 await page.keyboard.press('Escape');
 await page.locator('.theme-picker summary').click();
 await page.locator('[data-set-theme="contrast"]').click();
 check(await page.locator('#app').getAttribute('data-theme')==='contrast','available themes switch in the reader');
 await page.locator('.theme-picker summary').click(); await page.locator('[data-set-theme="nord"]').click();

 await page.getByRole('link',{name:'the no-game demo',exact:true}).click();
 await page.waitForURL(/installation/); check(page.url().includes('/en/installation'),'internal article link');
 await page.locator('select[aria-label="Documentation language"]').selectOption('fr');
 await page.waitForURL(/\/fr\/installation/); check((await page.title()).startsWith('Installation'),'French switch keeps page ID');
 await go('fr/capture/choose-your-output');
 check(await page.locator('#choose-your-output').isVisible(),'translated explicit anchor');
 await page.screenshot({path:path.join(output,'capture-fr.png')});
 await go('en/welcome?media=figure-0');
 const image=page.locator('[data-cydoc-id="figure-0"]'); await image.scrollIntoViewIfNeeded();
 await image.locator('[data-action="expand"]').waitFor();
 check(await image.locator('img').first().evaluate(img=>img.complete&&img.naturalWidth>100),'image loads directly from local resource');
 await image.locator('[data-action="expand"]').focus(); await page.keyboard.press('Enter');
 await page.waitForSelector('.media-expanded');
 check(await page.evaluate(()=>document.fullscreenElement===null),'enlargement stays inside window');
 await page.screenshot({path:path.join(output,'image-enlarged.png')});
 await page.keyboard.press('Escape');
 check(await page.locator('.media-expanded').count()===0,'Escape restores inline image');
 await go('fr/capture/figure-0');
 const svg=page.locator('article svg[role="img"]').first(); await svg.scrollIntoViewIfNeeded();
 check(await svg.count()===1,'static SVG diagram without graph module');
 await go('en/ref-MCP');
 await page.waitForFunction(()=>globalThis.cygiManual.resolve('ref-MCP','en')?.page.id==='ref-mcp');
 check((await page.locator('article').innerText()).includes('CyGPUInspectorMCP'),'legacy uppercase page alias');
 await page.locator('select[aria-label="Documentation language"]').selectOption('fr'); await page.waitForURL(/\/fr\/ref-mcp/i);
 await page.locator('article[lang="fr"]').waitFor();
 check(page.url().toLowerCase().includes('/fr/ref-mcp'),'language selection preserves legacy page routes');
 await page.keyboard.press('Control+k');
 const search=page.locator('.search-dialog[open] input').first(); await search.waitFor({state:'visible'}); await search.fill('shader');
 await page.waitForTimeout(200);
 check((await page.locator('.search-dialog').innerText()).toLowerCase().includes('shader'),'keyboard search returns content');
 await page.keyboard.press('Escape');
 await go('en/api-cygi-modpackage-load-load');
 check((await page.locator('article').innerText()).includes('ModPackage'),'public C++ API without implementation bodies');
 await go('en/welcome');
 await page.evaluate(()=>document.fonts.ready);
 await page.evaluate(()=>window.scrollTo({top:700,behavior:'instant'}));
 await page.waitForFunction(()=>window.scrollY>=690);
 const position=await page.evaluate(()=>window.scrollY);
 // Use fixed navigation: an article link above the viewport would scroll before clicking.
 await page.locator('.side-nav a[data-page="installation"]').first().click(); await page.waitForURL(/installation/);
 await page.goBack(); await page.waitForURL(/welcome/);
 await page.waitForFunction(position=>Math.abs(window.scrollY-position)<180,position,{timeout:5000});
 check(Math.abs((await page.evaluate(()=>window.scrollY))-position)<180,'reading position restored on return');
 const mobile=await browser.newContext({viewport:{width:390,height:844},isMobile:true,hasTouch:true});
 await mobile.route(/^https?:/,requestPolicy);
 const touch=await mobile.newPage(); touch.on('pageerror',error=>errors.push(error.message)); touch.on('requestfailed',request=>failures.push(request.url()));
 touch.on('response',response=>{if(response.status()>=400)failures.push(`${response.status()} ${response.url()}`);});
 await touch.goto(base+'#/fr/welcome?media=figure-0'); await touch.waitForSelector('.cydoc-viewer');
 const media=touch.locator('[data-cydoc-id="figure-0"]'); await media.scrollIntoViewIfNeeded();
 await media.locator('[data-action="expand"]').waitFor();
 check(await media.locator('[data-action="expand"]').isVisible(),'touch controls stay accessible');
 await touch.waitForFunction(()=>{const image=document.querySelector('[data-cydoc-id="figure-0"] img');return image?.complete&&image.naturalWidth>100;});
 await touch.screenshot({path:path.join(output,'touch-fr.png')});
 check(await touch.locator('.header-search').isVisible(),'global search remains visible on touch screens');
 check(await touch.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),'narrow reader has no page overflow');
 await mobile.close();
 check(errors.length===0,'no browser runtime errors'); check(requests.length===0,offline?'no HTTP or CDN requests':`no requests outside the documentation site: ${requests.join(', ')}`);
 check(failures.length===0,'no missing local resources');
 await fs.writeFile(path.join(output,'results.json'),JSON.stringify({passed:true,checks,pages:data.pages.length,errors,requests,failures,localRequests:localRequests.length,url:base},null,2));
 console.log(`${checks} browser checks passed, ${data.pages.length} compiled pages, ${offline?'file:// only':'same-origin HTTP(S)'}`);
} finally { await browser.close(); if(server){server.closeAllConnections();await new Promise(resolve=>server.close(resolve));} }

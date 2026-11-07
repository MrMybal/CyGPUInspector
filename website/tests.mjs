// Software regression tests for the generated static consumer, including file:// delivery.
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';
import assert from 'node:assert/strict';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const require=createRequire(path.join(root,'.CyDocBuild/package.json'));
const {chromium}=require('@playwright/test');
const site=path.join(root,'build/showcase/site');
const output=path.join(root,'.CyDocBuild/internal/showcase-20261107/checks');await fs.mkdir(output,{recursive:true});
const base=process.env.CYGI_SHOWCASE_URL||pathToFileURL(path.join(site,'index.html')).href;
const browser=await chromium.launch({headless:true,executablePath:process.env.CYGI_TEST_BROWSER||(process.platform==='win32'?'C:/Program Files/Google/Chrome/Application/chrome.exe':undefined)});
const results=[],errors=[],requests=[],environmentRequests=[],environmentErrors=[];
// Optional, explicit test-environment instrumentation. Empty by default: all foreign requests fail.
// Record it separately rather than disabling host/browser protection or hiding project requests.
const environmentOrigins=new Set((process.env.CYGI_TEST_ENVIRONMENT_ORIGINS||'').split(',').filter(Boolean).map(u=>new URL(u).origin));
const isEnvironment=url=>/^https?:/.test(url)&&environmentOrigins.has(new URL(url).origin);
const pass=name=>{results.push(name);console.log('PASS: '+name);};
try{
 const context=await browser.newContext({viewport:{width:1280,height:720}});
 const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
 page.on('requestfailed',r=>{if(r.failure()?.errorText!=='net::ERR_ABORTED')(isEnvironment(r.url())?environmentErrors:errors).push(r.failure()?.errorText+': '+new URL(r.url()).origin);});
 page.on('response',r=>{if(r.status()>=400)(isEnvironment(r.url())?environmentErrors:errors).push('HTTP '+r.status()+': '+new URL(r.url()).origin);});
 page.on('request',r=>{if(/^https?:/.test(r.url())&&new URL(r.url()).origin!==new URL(base).origin)(isEnvironment(r.url())?environmentRequests:requests).push(new URL(r.url()).origin);});
 const open=async(locale,extra='')=>{await page.goto(base+'?lang='+locale+extra);await page.waitForFunction(l=>document.documentElement.lang===l,locale);await page.locator('.hero img[data-media]').waitFor();await page.waitForFunction(()=>[...document.querySelectorAll('.hero img')].every(i=>i.complete&&i.naturalWidth));};
 for(const [width,height] of [[1280,720],[1366,768],[1920,1080]])for(const locale of ['en','fr']){
  await page.setViewportSize({width,height});await open(locale);
  const box=await page.locator('.hero .shot').boundingBox();assert(box.y>=60&&box.y+box.height<=height+1,`${locale} ${width}: screenshot ends at ${box.y+box.height}`);
  assert(await page.locator('.hero .shot img').evaluate(i=>Math.abs(i.getBoundingClientRect().width/i.getBoundingClientRect().height-i.naturalWidth/i.naturalHeight)<.01),'Hero preserves aspect ratio');
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Desktop overflow');
  await page.screenshot({path:path.join(output,`desktop-${locale}-${width}.png`)});pass(`complete first screen ${locale} ${width}x${height}`);
 }
 for(const width of [360,390,820])for(const locale of ['en','fr']){
  await page.setViewportSize({width,height:844});await open(locale);
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Mobile overflow');
  assert(await page.locator('header .github').isVisible());
  await page.locator('.menu-toggle').click();assert.equal(await page.locator('.menu-toggle').getAttribute('aria-expanded'),'true');
  await page.keyboard.press('Escape');assert.equal(await page.locator('.menu-toggle').getAttribute('aria-expanded'),'false');
  await page.screenshot({path:path.join(output,`mobile-${locale}-${width}.png`)});pass(`mobile navigation and layout ${locale} ${width}`);
 }
 await page.setViewportSize({width:1280,height:720});await open('en','&campaign=local#capture');
 await page.locator('[data-language=fr]').click();assert(page.url().includes('campaign=local')&&page.url().endsWith('#capture'));
 assert.equal(await page.title(),'CyGPUInspector — Frames GPU, shaders et timings');
 assert((await page.locator('[data-doc=welcome]').first().getAttribute('href')).includes('#/fr/welcome'));pass('language preserves section, extra parameters, title and manual locale');
 await page.goto(base+'?lang=unsupported&campaign=local');assert.equal(await page.locator('html').getAttribute('lang'),'en');assert(page.url().includes('lang=en')&&page.url().includes('campaign=local'));pass('unsupported language normalizes to English');
 await page.goto(base);assert.equal(await page.locator('html').getAttribute('lang'),'en');pass('English default has no retained French preference');
 for(const width of [1280,390]){
  await page.setViewportSize({width,height:800});await open('fr');
  const opener=page.locator('[data-enlarge]').nth(width===1280?1:0);await opener.scrollIntoViewIfNeeded();const before=await page.evaluate(()=>scrollY);
  await opener.focus();await page.keyboard.press('Enter');assert(await page.locator('dialog').evaluate(e=>e.open));
  await page.waitForFunction(()=>{const i=document.querySelector('dialog img');return i.complete&&i.naturalWidth>0;});
  await page.keyboard.press('Tab');assert(await page.locator('dialog').evaluate(e=>e.contains(document.activeElement)));
  await page.screenshot({path:path.join(output,`viewer-${width}.png`)});
  await page.keyboard.press('Escape');assert(await opener.evaluate(e=>document.activeElement===e));assert(Math.abs(before-await page.evaluate(()=>scrollY))<2);assert.equal(await page.evaluate(()=>document.body.style.overflow),'');
  await opener.click();await page.locator('.viewer-close').click();assert(!(await page.locator('dialog').evaluate(e=>e.open)));assert.equal(await page.evaluate(()=>document.body.style.overflow),'');
  // Reopen immediately: a queued native close event must not unlock the new dialog.
  await opener.click();assert.equal(await page.evaluate(()=>document.body.style.overflow),'hidden');await page.keyboard.press('Escape');assert.equal(await page.evaluate(()=>document.body.style.overflow),'');
  pass(`viewer keyboard, close button, immediate reopen, focus and scroll restoration ${width}`);
 }
 for(const locale of ['en','fr']){
  // Effective CSS viewport for a 1280x800 window at 200% browser zoom.
  // This exercises reflow; it does not claim an actual browser/OS zoom setting.
  await page.setViewportSize({width:640,height:400});await open(locale);
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Zoom-equivalent reflow overflow');
  await page.locator('[data-doc=welcome]').first().scrollIntoViewIfNeeded();assert(await page.locator('[data-doc=welcome]').first().isVisible());
  await page.screenshot({path:path.join(output,`zoom-${locale}.png`)});pass(`200 percent equivalent viewport reflow ${locale}`);
 }
 await page.setViewportSize({width:1280,height:800});await open('fr');
 await page.locator('.faq details').first().locator('summary').focus();await page.keyboard.press('Enter');assert(await page.locator('.faq details').first().evaluate(e=>e.open));pass('FAQ keyboard interaction');
 for(const locale of ['en','fr']){
  await open(locale);
  for(const image of await page.locator('main img[data-media]').all()){
   await image.scrollIntoViewIfNeeded();await image.evaluate(i=>i.decode());
   assert(await image.evaluate(i=>i.naturalWidth>0&&i.alt.trim().length>10));
  }
  pass(`all four genuine screenshots decode with translated alternative text ${locale}`);
 }
 for(const locale of ['en','fr']){
  await open(locale);await page.locator('[data-doc=welcome]').first().click();await page.locator(`article[lang="${locale}"]`).waitFor();assert(await page.locator('.side-nav a[data-page]').count()>10);pass(`complete local CyDoc reader ${locale}`);
 }
 await page.goto(base+'#/fr/welcome');await page.locator('article[lang=fr]').waitFor();assert(page.url().includes('manual/Documentation.html#/fr/welcome'));pass('legacy root manual fragment');
 await page.goto(new URL('./Documentation.html#/en/welcome',base).href);await page.locator('article[lang=en]').waitFor();pass('root Documentation.html alias');
 if(base.startsWith('file:')){
  const moved=path.join(output,`Moved showcase é ${Date.now()}`);await fs.cp(site,moved,{recursive:true});
  await page.goto(pathToFileURL(path.join(moved,'index.html')).href+'?lang=fr');
  await page.waitForFunction(()=>document.documentElement.lang==='fr'&&document.querySelector('.hero [data-media]').complete);
  await page.locator('[data-doc=welcome]').first().click();await page.locator('article[lang=fr]').waitFor();
  pass('relocated complete output with spaces and Unicode in its file path');
 }
 assert.deepEqual(requests,[],'Unexpected external runtime requests');assert.deepEqual(errors,[],'Runtime/resource errors');pass(environmentRequests.length?`no project resource/runtime errors; ${environmentRequests.length} explicitly configured environment requests recorded separately`:'no missing resources, external runtime requests or browser errors');
 await context.close();
 await fs.writeFile(path.join(output,base.startsWith('file:')?'file-results.json':'http-results.json'),JSON.stringify({base,passed:true,checks:results,environmentOrigins:[...environmentOrigins],environmentRequests:environmentRequests.length,environmentErrors},null,2));
}finally{await browser.close();}
console.log(`${results.length} showcase checks passed (${base.startsWith('file:')?'file URLs':'HTTP subpath'}).`);

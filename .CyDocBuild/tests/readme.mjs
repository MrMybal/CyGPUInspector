// Follow the rendered public GitHub READMEs into the deployed CyDoc reader.
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(path.join(root, '.CyDocBuild/package.json'));
const { chromium } = require('@playwright/test');
const output = path.resolve(root, process.argv[2] || '.CyDocBuild/internal/pages/checks/readme');
await fs.mkdir(output, { recursive: true });
const browser = await chromium.launch({
  headless: true,
  executablePath: process.env.CYGI_TEST_BROWSER || (process.platform === 'win32'
    ? 'C:/Program Files/Google/Chrome/Application/chrome.exe' : undefined),
});
const results = [];
try {
  for (const locale of ['en', 'fr']) {
    const context = await browser.newContext({ viewport: { width: 1500, height: 1000 } });
    try {
      const page = await context.newPage();
      const readme = `https://github.com/MrMybal/CyGPUInspector/blob/main/README${locale === 'fr' ? '.fr' : ''}.md`;
      const destination = `https://mrmybal.github.io/CyGPUInspector/#/${locale}/welcome`;
      await page.goto(readme, { waitUntil: 'domcontentloaded' });
      const link = page.getByRole('link', { name: 'Documentation', exact: true })
        .and(page.locator(`a[href="${destination}"]`)).first();
      await link.waitFor();
      await page.screenshot({ path: path.join(output, `readme-${locale}.png`) });
      // Click GitHub's rendered link, rather than navigating directly to Pages.
      await link.click();
      await page.waitForURL(`https://mrmybal.github.io/CyGPUInspector/Documentation.html#/${locale}/welcome`);
      await page.locator(`.cydoc-viewer article[lang="${locale}"]`).waitFor();
      await page.waitForFunction(locale => document.documentElement.lang === locale, locale);
      assert((await page.title()).startsWith(locale === 'fr' ? 'Commencer ici' : 'Start here'));
      assert(await page.locator('.side-nav a[data-page]').count() > 10, 'reader navigation');
      await page.screenshot({ path: path.join(output, `reader-${locale}.png`) });
      const figure = page.locator('[data-cydoc-id="figure-0"]');
      await figure.scrollIntoViewIfNeeded();
      await page.waitForFunction(() => {
        const image = document.querySelector('[data-cydoc-id="figure-0"] img');
        return image?.complete && image.naturalWidth > 100;
      });
      assert(await page.locator('article svg[role="img"]').count() > 0, 'static diagram');
      await page.keyboard.press('Control+k');
      const search = page.locator('.search-dialog[open] input').first();
      await search.waitFor();
      await search.fill('shader');
      await page.locator('.search-result').first().waitFor();
      await page.keyboard.press('Escape');
      results.push({ locale, readme, destination: page.url(), passed: true });
      console.log(`PASS: published ${locale} README -> CyDoc reader, navigation, image, diagram and search`);
    } finally {
      await context.close();
    }
  }
  await fs.writeFile(path.join(output, 'results.json'), JSON.stringify({ passed: true, results }, null, 2));
} finally {
  await browser.close();
}

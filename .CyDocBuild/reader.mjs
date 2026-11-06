// Application-owned embedding of CyDoc's public viewer and exported classic-script assets.
import { mountViewer } from 'cydoc/viewer';
const options = globalThis.CYDOC_OPTIONS;
const pending = new Map();
function loadAsset(src) {
  if (pending.has(src)) return pending.get(src);
  const path = options.assets?.[src];
  if (!path) return Promise.reject(new Error('Local documentation asset is not packaged'));
  const result = new Promise((resolve, reject) => {
    const script = document.createElement('script'); script.src = `DocData/${path}`;
    script.onload = () => {
      const asset = globalThis.CYDOC_ASSETS?.[src];
      if (globalThis.CYDOC_ASSETS) delete globalThis.CYDOC_ASSETS[src];
      script.remove(); pending.delete(src);
      asset ? resolve(asset) : reject(new Error('Invalid local documentation asset'));
    };
    script.onerror = () => { script.remove(); pending.delete(src); reject(new Error('Local asset unavailable')); };
    document.head.append(script);
  });
  pending.set(src, result); return result;
}
const modules = Object.fromEntries(options.modules.map(name => [name, () => new Promise((resolve, reject) => {
  const script = document.createElement('script'); script.src = `DocData/media-${name}.js`;
  script.onload = () => resolve(globalThis[`CyDocMedia_${name}`].default);
  script.onerror = () => reject(new Error('Local media reader unavailable'));
  document.head.append(script);
})]));
let initialTheme = 'nord';
try { initialTheme = localStorage.getItem('cydoc:theme') || initialTheme; } catch { /* CyDoc reports storage availability. */ }
globalThis.cygiManual = mountViewer(document.querySelector('#app'), globalThis.CYDOC_DATA, {
  homePage: 'welcome', initialLocale: 'en', initialTheme,
  storageId: 'cygpuinspector-manual', assetBase: 'DocData/', modules, loadAsset
});
addEventListener('pagehide', event => {
  // Preserve the live viewer when the browser caches this document for back/forward navigation.
  if (!event.persisted) globalThis.cygiManual.destroy();
});

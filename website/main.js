(() => {
  'use strict';
  const {copy, media}=window.CYGI_SHOWCASE;
  const requested=new URL(location.href).searchParams.get('lang');
  let language=requested==='fr'?'fr':'en';
  const dialog=document.querySelector('#image-viewer');
  let opener=null, oldOverflow='', savedY=0;
  function applyLanguage(next,updateUrl=false){
    language=next==='fr'?'fr':'en';const t=copy[language];
    document.documentElement.lang=language;document.title=t.title;
    document.querySelector('meta[name=description]').content=t.description;
    document.querySelectorAll('[data-i18n]').forEach(e=>e.textContent=t[e.dataset.i18n]);
    document.querySelectorAll('[data-label]').forEach(e=>e.setAttribute('aria-label',t[e.dataset.label]));
    document.querySelectorAll('[data-language]').forEach(e=>e.setAttribute('aria-pressed',String(e.dataset.language===language)));
    document.querySelectorAll('[data-doc]').forEach(e=>e.href=`./manual/Documentation.html#/${language}/${e.dataset.doc}`);
    document.querySelectorAll('[data-media]').forEach(image=>{image.src=media[image.dataset.media][language];image.alt=t[image.dataset.alt];});
    document.querySelectorAll('[data-enlarge]').forEach(e=>e.setAttribute('aria-label',t.enlarge+t[e.dataset.caption]));
    if(updateUrl){const u=new URL(location.href);u.searchParams.set('lang',language);history.replaceState(null,'',u);}
    if(dialog.open&&opener){dialog.querySelector('img').src=opener.querySelector('img').src;dialog.querySelector('img').alt=opener.querySelector('img').alt;document.querySelector('#viewer-caption').textContent=t[opener.dataset.caption];}
  }
  if(/^#\/(en|fr)\//.test(location.hash)){location.replace('./manual/Documentation.html'+location.hash);return;}
  applyLanguage(language,requested!==null&&requested!=='en'&&requested!=='fr');
  document.querySelectorAll('[data-language]').forEach(e=>e.addEventListener('click',()=>applyLanguage(e.dataset.language,true)));
  const row=document.querySelector('.header-row'),menu=document.querySelector('.menu-toggle');
  function closeMenu(){row.dataset.menuOpen='false';menu.setAttribute('aria-expanded','false');}
  menu.addEventListener('click',()=>{const open=row.dataset.menuOpen!=='true';row.dataset.menuOpen=String(open);menu.setAttribute('aria-expanded',String(open));});
  document.querySelectorAll('.primary-nav a').forEach(e=>e.addEventListener('click',closeMenu));
  document.addEventListener('keydown',e=>{if(e.key==='Escape'&&row.dataset.menuOpen==='true'){closeMenu();menu.focus();}});
  document.querySelectorAll('[data-enlarge]').forEach(button=>button.addEventListener('click',()=>{
    opener=button;savedY=scrollY;oldOverflow=document.body.style.overflow;
    dialog.querySelector('img').src=button.querySelector('img').src;dialog.querySelector('img').alt=button.querySelector('img').alt;
    document.querySelector('#viewer-caption').textContent=copy[language][button.dataset.caption];
    dialog.showModal();document.body.style.overflow='hidden';dialog.querySelector('button').focus();
  }));
  function restoreViewer(){
    if(!opener)return;const target=opener;opener=null;
    document.body.style.overflow=oldOverflow;target.focus({preventScroll:true});window.scrollTo({top:savedY,behavior:'instant'});
  }
  function closeViewer(){dialog.close();restoreViewer();}
  dialog.querySelector('button').addEventListener('click',closeViewer);
  dialog.addEventListener('cancel',e=>{e.preventDefault();closeViewer();});
  dialog.addEventListener('keydown',e=>{if(e.key==='Tab'){e.preventDefault();dialog.querySelector('button').focus();}});
  dialog.addEventListener('click',e=>{if(e.target!==dialog)return;const r=dialog.getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)closeViewer();});
  dialog.addEventListener('close',()=>{if(!dialog.open)restoreViewer();});
})();

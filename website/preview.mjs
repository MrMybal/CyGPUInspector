import {createServer} from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../build/showcase/site');
const port=Number(process.argv.find(a=>a.startsWith('--port='))?.slice(7)||43763);
const base=process.argv.find(a=>a.startsWith('--base='))?.slice(7)||'/';
if(!Number.isInteger(port)||port<0||port>65535||!/^\/(?:[a-zA-Z0-9_-]+\/)*$/.test(base))throw new Error('Use --port=0..65535 and --base=/optional/subpath/');
await fs.access(path.join(root,'index.html'));
const types={'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.js':'text/javascript; charset=utf-8','.json':'application/json','.png':'image/png','.jpg':'image/jpeg','.svg':'image/svg+xml','.txt':'text/plain; charset=utf-8','.md':'text/plain; charset=utf-8'};
const server=createServer(async(req,res)=>{try{
  if(!['GET','HEAD'].includes(req.method)){res.writeHead(405).end();return;}
  const u=new URL(req.url,'http://localhost');if(!u.pathname.startsWith(base)){res.writeHead(404).end();return;}
  const name=decodeURIComponent(u.pathname.slice(base.length))||'index.html';
  let file=path.resolve(root,name);if(!file.startsWith(root+path.sep)){res.writeHead(403).end();return;}
  if((await fs.stat(file)).isDirectory())file=path.join(file,'index.html');
  const bytes=await fs.readFile(file);res.writeHead(200,{'Content-Type':types[path.extname(file)]||'application/octet-stream','Content-Length':bytes.length,'Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});res.end(req.method==='HEAD'?undefined:bytes);
}catch{res.writeHead(404).end('Not found');}});
await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(port,'127.0.0.1',resolve);});
console.log(`CyGPUInspector showcase: http://127.0.0.1:${server.address().port}${base}`);
for(const signal of ['SIGINT','SIGTERM'])process.on(signal,()=>server.close(()=>process.exit(0)));

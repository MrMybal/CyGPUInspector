"""Local-only acceptance test against isolated copies of the real application; no GitHub workflow."""
import argparse,json,pathlib,shutil,socket,subprocess,time,urllib.request,urllib.error
ROOT=pathlib.Path(__file__).resolve().parents[1]

def run(runtime):
    runtime=runtime.resolve();runtime.mkdir(parents=True,exist_ok=False);checks=[]
    def check(value,label):
        if not value:raise AssertionError(label)
        checks.append(label);print('PASS:',label,flush=True)
    for profile in [None,'free','local_controlled','token_only','protected','unknown']:
        folder=runtime/(profile or 'migrated');folder.mkdir()
        for name in ['CyGPUInspectorApp.exe','CyGPUInspectorMCP.exe','CyGPUInspectorLocalMcpTests.exe']:
            shutil.copy2(ROOT/'bin/Release'/name,folder/name)
        shutil.copytree(ROOT/'Lang',folder/'Lang')
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        prefs={'language':'fr','unrelated':{'keep':'user data'},'mcp':{'enabled':True,'port':port,'custom':'preserve'}}
        if profile:prefs['mcp'].update(access_profile=profile,permission='read-only')
        settings=folder/'CyGPUInspectorApp.settings.json';settings.write_text(json.dumps(prefs),encoding='utf-8')
        app=subprocess.Popen([str(folder/'CyGPUInspectorApp.exe')],cwd=folder)
        cli=folder/'CyGPUInspectorMCP.exe';url=f'http://127.0.0.1:{port}/mcp';session=None;seq=0
        def rpc(method,params):
            nonlocal session,seq
            seq+=1;headers={'Content-Type':'application/json','Accept':'application/json, text/event-stream','MCP-Protocol-Version':'2025-11-25'}
            if session:headers['Mcp-Session-Id']=session
            req=urllib.request.Request(url,data=json.dumps({'jsonrpc':'2.0','id':seq,'method':method,'params':params}).encode(),headers=headers)
            with urllib.request.urlopen(req,timeout=8) as response:
                session=response.headers.get('Mcp-Session-Id',session);return json.load(response)
        def invoke(name,args=None):return rpc('tools/call',{'name':name,'arguments':args or {}})['result']
        try:
            for _ in range(100):
                state=subprocess.run([str(cli),'invoke','get_application_state','{}'],capture_output=True,timeout=10)
                if state.returncode==0:break
                if app.poll() is not None:raise AssertionError('Application exited before startup')
                time.sleep(.1)
            check(state.returncode==0, f'{profile or "migrated"}: real application started')
            state=json.loads(state.stdout)['result']
            if profile=='unknown':
                check(not state['mcp']['http_enabled'] and json.loads(settings.read_text())['mcp']['access_profile']=='unknown','unrecognized explicit profile preserved and HTTP disabled')
                continue
            if profile in ('token_only','protected'):
                try:rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'local-test','version':'1'}})
                except urllib.error.HTTPError as error:check(error.code==401,profile+': real HTTP requires a token')
                else:raise AssertionError('Missing token was accepted')
                continue
            rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'local-test','version':'1'}})
            check(invoke('get_application_state')['structuredContent']['ok'],'HTTP reads without a token')
            reply=invoke('set_filters',{'shaders':'http-url-only'})
            if profile=='local_controlled':
                check(reply['isError'],'controlled read-only profile refuses modification')
                denied=subprocess.run([str(cli),'invoke','set_filters','{"shaders":"refused"}'],capture_output=True)
                check(denied.returncode==1,'CLI preserves controlled permission refusal')
            else:
                check(not reply['isError'] and invoke('get_application_state')['structuredContent']['result']['shader_filter']=='http-url-only','real HTTP mutation succeeds without token or confirmation')
                bad=invoke('set_filters',{'unexpected':True})
                check(bad['isError'] and invoke('get_application_state')['structuredContent']['result']['shader_filter']=='http-url-only','Free still rejects invalid arguments before mutation')
                sdk=subprocess.run([str(folder/'CyGPUInspectorLocalMcpTests.exe'),url],capture_output=True,timeout=30)
                check(sdk.returncode==0,'native SDK client connects with URL alone and reads/modifies real app: '+sdk.stderr.decode(errors='replace'))
                cli_result=subprocess.run([str(cli),'invoke','set_filters','{"shaders":"cli-free"}'],capture_output=True,timeout=10)
                check(cli_result.returncode==0 and invoke('get_application_state')['structuredContent']['result']['shader_filter']=='cli-free','CLI mutates same application in Free mode')
            after=json.loads(settings.read_text(encoding='utf-8'))
            check(after['unrelated']==prefs['unrelated'] and after['mcp']['custom']=='preserve','migration preserves unrelated configuration')
            check(after['mcp']['access_profile']==(profile or 'free'),'default migration and explicit profile retained')
        finally:
            if app.poll() is None:app.terminate()
            app.wait(timeout=25)
    (runtime/'results.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2),encoding='utf-8')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runtime',type=pathlib.Path,required=True);run(parser.parse_args().runtime)

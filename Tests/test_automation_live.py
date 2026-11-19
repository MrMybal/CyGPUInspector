"""Explicit local end-to-end test; never invoked by a GitHub workflow.
Run: python Tests/test_automation_live.py --runtime build/automation/live
Uses only isolated copies of the real binaries and the included synthetic producer.
"""
import argparse,contextlib,http.server,json,pathlib,shutil,subprocess,threading,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
checks=[]
def check(value,label):
    if not value:raise AssertionError(label)
    checks.append(label);print('PASS:',label,flush=True)
def run(runtime):
    runtime=runtime.resolve();runtime.mkdir(parents=True,exist_ok=False)
    for name in ['CyGPUInspectorApp.exe','CyGPUInspectorMCP.exe','CyGPUInspectorFakeSession.exe']:
        shutil.copy2(ROOT/'bin/Release'/name,runtime/name)
    shutil.copytree(ROOT/'Lang',runtime/'Lang')
    exe=runtime/'CyGPUInspectorMCP.exe'
    def call(name,args=None,expected=0):
        proc=subprocess.run([str(exe),'invoke',name,json.dumps(args or {})],capture_output=True,timeout=12)
        data=json.loads(proc.stdout)
        if proc.returncode!=expected:raise AssertionError((name,proc.returncode,data,proc.stderr))
        return data.get('result') if expected==0 else data
    fake=subprocess.Popen([str(runtime/'CyGPUInspectorFakeSession.exe')],cwd=runtime,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    app=None
    def launch(level='read-only'):
        nonlocal app
        app=subprocess.Popen([str(runtime/'CyGPUInspectorApp.exe'),'--mcp-permission='+level],cwd=runtime)
        for _ in range(40):
            result=subprocess.run([str(exe),'invoke','get_application_state','{}'],capture_output=True)
            if result.returncode==0:return
            time.sleep(.1)
        raise AssertionError('Application failed to open its real control pipe')
    def stop():
        nonlocal app
        if app:
            if app.poll() is None:app.terminate()
            app.wait(timeout=8);app=None
    try:
        launch()
        check(not call('get_application_state')['connected'],'state available without a session')
        call('configure_assistant',{'key':'fixture-not-a-secret'},2);call('set_mcp_permission',{'level':2},2)
        check(call('get_application_state')['external_permission']==0,'schema and registry prevent credential injection and self-granted permissions')
        sessions=call('list_sessions')['sessions'];producer=next(s for s in sessions if s['process_id']==fake.pid)
        check(call('connect_session',{'process_id':fake.pid},1)['ok']==False,'read-only cannot connect or mutate workspace')
        check(call('get_editor_state',{},1)['ok']==False,'read-only cannot disclose editor source')
        stop();launch('shader-modification')
        call('connect_session',{'process_id':producer['process_id'],'device_index':producer['device_index']})
        for _ in range(30):
            state=call('get_session')
            if state['shader_count']>=9:break
            time.sleep(.1)
        check(state['shader_count']>=9,'exact process/device connected through real host')
        for name,args in [('set_workspace',{'workspace':'preview'}),('focus_panel',{'panel':'Shaders'}),('select_shader',{'shader_id':2}),('select_resource',{'resource_id':7}),('select_event',{'event_index':0}),('set_filters',{'shaders':'stage:ps','resources':'hdr','draws_only':True}),('set_appearance',{'theme':'navy','comfortable':True,'scale':1.1}),('set_language',{'language':'fr'})]:call(name,args)
        state=call('get_application_state');check(state['language']=='fr' and state['shader_id']==2 and state['shader_filter']=='stage:ps','navigation and persisted preferences affect the real application')
        call('set_language',{'language':'en'})
        layout=call('get_panel_layout');check(len(layout['panels'])>=15,'all main panels discoverable')
        backup=call('dock_panel',{'panel':'Resources','target':'Shaders','side':'tab'})['backup']
        call('restore_layout',{'file':pathlib.Path(backup).name});call('reset_layout');check(pathlib.Path(backup).exists(),'docking layout changes are recoverable')
        call('set_preview_settings',{'mode':1,'min':0.,'max':2.,'fit':False,'zoom':1.2})
        check(call('get_preview_settings')['mode']==1,'preview controls update real display settings')
        call('set_preview_settings',{'min':10.,'max':2.},1)
        call('start_preview',{'resource_id':999999},1)
        call('set_workspace',{'workspace':'preview'});call('start_preview',{'resource_id':0})
        for _ in range(25):
            proc=subprocess.run([str(exe),'invoke','export_preview','{}'],capture_output=True)
            if proc.returncode==0:break
            time.sleep(.1)
        check(proc.returncode==0,'preview renderer exports a real PNG from the synthetic producer')
        call('set_preview_frozen',{'frozen':True});call('set_preview_frozen',{'frozen':False});call('stop_preview')
        check(call('get_preview_settings')['min']==0,'invalid preview ranges do not mutate display settings')
        call('select_shader',{'shader_id':999999},1)
        call('set_shader_name',{'shader_id':2,'name':'Automation pixel shader'})
        call('set_shader_tag',{'shader_id':2,'tag':'Bloom','enabled':True})
        call('add_shader_annotation',{'shader_id':2,'text':'Synthetic AI annotation','confidence':75})
        notes=call('get_shader_notes',{'shader_id':2});check(notes['name']=='Automation pixel shader' and 'Bloom' in notes['tags'] and notes['annotations'][-1]['source']=='AI','persistent notes keep name, tag and AI provenance')
        index=len(notes['annotations'])-1
        saved=call('remove_shader_annotation',{'shader_id':2,'index':index,'expected_text':'Synthetic AI annotation'})['backup']
        call('restore_shader_notes',{'shader_id':2,'file':pathlib.Path(saved).name})
        check(call('get_shader_notes',{'shader_id':2})['annotations'][-1]['text']=='Synthetic AI annotation','annotation deletion can be restored from its signature-bound backup')
        before=call('get_editor_state');source='float4 main() : SV_Target { return float4(1,0,0,1); }'
        saved=call('set_editor_text',{'hlsl':source,'profile':'ps_5_0','expected_sha256':before['sha256']})
        call('editor_undo');check(call('get_editor_state')['hlsl']==before['hlsl'],'AI document changes participate in native editor undo');call('editor_redo')
        check(call('compile_editor')['compiled'],'editor compiles real HLSL with actual Windows compiler')
        call('set_editor_text',{'hlsl':'invalid','expected_sha256':before['sha256']},1)
        check(call('get_editor_state')['hlsl']==source,'optimistic edit revision prevents overwriting changed work')
        exported=call('export_editor')['file'];call('import_editor',{'file':pathlib.Path(saved['backup']).name,'expected_sha256':saved['sha256']})
        call('import_editor',{'file':pathlib.Path(exported).name,'expected_sha256':before['sha256']})
        call('import_editor',{'file':'../outside.hlsl','expected_sha256':saved['sha256']},1)
        check(call('get_editor_state')['hlsl']==source and pathlib.Path(exported).exists(),'managed HLSL imports, exports and draft recovery work; traversal refused')
        call('set_editor_view',{'wrap':True,'line':1,'minimap':True})
        call('disable_shader',{'shader_id':2});check(call('get_mod_package')['included']>=1,'real game modification appears in export recorder')
        call('set_mod_included',{'shader_id':2,'included':True});call('save_game_profile');call('set_profile_reapply',{'enabled':False});call('reapply_game_profile');call('clear_saved_profile_state')
        package=call('export_mod_package',{'name':'Synthetic automation mod'})['directory']
        check(pathlib.Path(package).is_dir(),'real selected modifications exported without overwrite')
        recovery=call('clear_mod_recording')['backup'];check(pathlib.Path(recovery).is_dir(),'clearing mod selection preserves a full recovery package')
        call('enable_shader',{'shader_id':2})
        call('set_tracking_level',{'level':'pass_timing'});call('request_full_sync');call('refresh_frame_graph')
        passes=call('get_passes');first=passes['passes'][0]['id'] if isinstance(passes,dict) else passes[0]['id']
        call('rename_pass',{'pass_id':first,'name':'Synthetic pass'});call('select_pass',{'pass_id':first})
        call('set_timeline_view',{'continuous':True,'follow':False,'start':0,'span':20})
        call('set_follow_frame',{'follow':True});call('get_diagnostics');call('export_analysis_snapshot');history=call('get_timeline_history')['frames']
        if history:call('select_history_frame',{'frame':history[-1]['frame']})
        call('set_profile_filter',{'query':'Synthetic'})
        call('capture_frame');captures=call('list_captures');rows=captures['captures'] if isinstance(captures,dict) else captures
        name=rows[-1]['name'];call('open_capture',{'name':name});check(call('get_application_state')['viewing_capture'],'saved capture opened via same application loader')
        call('disable_shader',{'shader_id':2},1);call('close_capture')
        check(not call('get_application_state')['viewing_capture'],'offline capture remains read-only and returns to live connection')
        # A loopback provider streams a real native tool call into the integrated assistant.
        stage={'mode':'tool','calls':0}
        class Api(http.server.BaseHTTPRequestHandler):
            def log_message(self,*args):pass
            def do_GET(self):
                data=b'{"data":[{"id":"fixture-model"}]}'
                self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
            def do_POST(self):
                payload=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                stage['calls']+=1
                if stage['mode']=='tool' and not any(m['role']=='tool' for m in payload['messages']):
                    delta={'tool_calls':[{'index':0,'id':'native-1','type':'function','function':{'name':'set_filters','arguments':json.dumps({'shaders':'native-chat-filter'})}}]};finish='tool_calls'
                else:delta={'content':'The requested filter is applied.'};finish='stop'
                self.send_response(200);self.send_header('Content-Type','text/event-stream');self.end_headers()
                try:
                    event={'choices':[{'index':0,'delta':delta,'finish_reason':None}]};self.wfile.write(('data: '+json.dumps(event)+'\n\n').encode());self.wfile.flush()
                    if stage['mode']=='slow':time.sleep(1)
                    event={'choices':[{'index':0,'delta':{},'finish_reason':finish}]};self.wfile.write(('data: '+json.dumps(event)+'\n\ndata: [DONE]\n\n').encode());self.wfile.flush()
                except (BrokenPipeError,ConnectionResetError):pass
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Api);server.daemon_threads=True;server.block_on_close=False;threading.Thread(target=server.serve_forever,daemon=True).start()
        try:
            call('configure_assistant',{'provider':3,'model':'fixture-model','endpoint':f'http://127.0.0.1:{server.server_port}/v1/chat/completions','deferred_tools':False})
            call('connect_assistant')
            for _ in range(50):
                if call('get_assistant_status')['ready']:break
                time.sleep(.05)
            call('send_assistant_message',{'text':'Set the shader filter to native-chat-filter.'})
            for _ in range(80):
                chat=call('get_assistant_conversation')
                if not chat['pending']:break
                time.sleep(.05)
            check(call('get_application_state')['shader_filter']=='native-chat-filter' and stage['calls']>=2,'integrated SDK chat discovers and invokes new semantic host operation')
            stage['mode']='slow';call('send_assistant_message',{'text':'Give a short explanation.'});time.sleep(.15);call('cancel_assistant_request')
            check(not call('get_assistant_conversation')['pending'],'integrated streaming generation can be cancelled through real application')
            backup=call('export_conversation')['file'];time.sleep(.4);call('new_conversation');check(pathlib.Path(backup).exists() and not call('get_assistant_conversation')['messages'],'new conversation keeps recoverable transcript backup')
            call('disconnect_assistant')
        finally:server.shutdown();server.server_close()
        call('open_about');call('disconnect_session')
        check(not call('get_application_state')['connected'],'disconnection keeps application available')
        check(call('request_exit')['closing'],'application exit backs up work and acknowledges the request');app.wait(timeout=8)
        (runtime/'results.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2),encoding='utf-8')
    finally:
        stop();fake.terminate();fake.wait(timeout=8)
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runtime',type=pathlib.Path,required=True);run(parser.parse_args().runtime)

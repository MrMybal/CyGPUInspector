// CyGPUInspectorMCP — the MCP server an agent launches.
//
// It speaks MCP (JSON-RPC over stdio, one message per line) and forwards every tool call to
// CyGPUInspectorApp over a named pipe. It can retain bounded result snapshots; the standalone owns
// the session and the permission level, because this process runs wherever an agent started it.
//
// Usage: point an MCP client at this executable. Nothing to configure; it finds the standalone by
// its pipe and says so plainly when the standalone is not running.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include <CyGPUInspectorCore/ControlPipe.hpp>
#include <CyGPUInspectorCore/Json.hpp>
#include <CyGPUInspectorCore/McpTools.hpp>
#include <CyGPUInspectorCore/Protocol.hpp>
#include <CyGPUInspectorCore/Version.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "Operations.hpp"
#include <cymcp/mcp.hpp>
#include <cymcp/performance.hpp>
#include <fstream>
#include <mutex>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	class AppLink
	{
	public:
		bool EnsureConnected()
		{
			if (m_pipe.IsConnected())
				return true;
			return m_pipe.Connect(cygi::kAppPipeName, 500);
		}

		// Returns the raw JSON answer, or an explanation of why there is none.
		bool Call(const std::string &tool, const cygi::Json &arguments, std::string &out)
		{
			if (!EnsureConnected())
			{
				out = "CyGPUInspectorApp is not running, or its MCP server is not listening. "
				      "Start the standalone and try again.";
				return false;
			}

			cygi::Json request = cygi::Json::Object();
			request["tool"] = cygi::Json(tool);
			request["arguments"] = arguments;

			const std::string text = request.Write(-1);
			const uint64_t id = m_pipe.NextRequestId();
			if (!m_pipe.Send(cygi::ControlType::mcp_request, id, text.data(),
			                 static_cast<uint32_t>(text.size())))
			{
				m_pipe.Disconnect();
				out = "the request could not be sent to CyGPUInspectorApp";
				return false;
			}

			cygi::ControlMessage reply;
			for (int attempt = 0; attempt < 4; ++attempt)
			{
				// The standalone answers on its UI thread, so one frame of latency is normal.
				if (!m_pipe.Receive(reply, 5000))
				{
					m_pipe.Disconnect();
					out = "CyGPUInspectorApp did not answer within five seconds";
					return false;
				}
				if (reply.request_id == id)
					break;
			}

			out.assign(reinterpret_cast<const char *>(reply.payload.data()), reply.payload.size());
			return true;
		}

	private:
		cygi::ControlPipeClient m_pipe;
	};
}

int main(int argc, char** argv)
{
 using namespace cymcp;
 AppLink link; std::mutex link_mutex;
 auto operations=cygi::CreateOperations([&](const std::string& name,const Json& args,const CallContext& ctx){
  ctx.cancellation.check();std::lock_guard<std::mutex> guard(link_mutex);
  cygi::Json old_args; cygi::Json::Parse(args.dump(),old_args);std::string answer;
  if(!link.Call(name,old_args,answer))throw Error("ConnectionError",answer);
  auto result=parse_json(answer,32*1024*1024);
  if(!result.value("ok",false))throw Error("OperationFailed",result.value("error","Operation failed"));
  return result;
 });
 try {
  const char* metrics=std::getenv("CYGI_MCP_METRICS_FILE");
  if(metrics&&*metrics){cymcp::PerformanceOptions o;o.jsonl_file=std::filesystem::u8path(metrics);operations->set_performance_monitor(std::make_shared<cymcp::PerformanceMonitor>(o));}
  auto setting=[](const char* key,size_t fallback,size_t min,size_t max){const char* value=std::getenv(key);if(!value||!*value)return fallback;std::string text=value;size_t used=0;auto result=std::stoull(text,&used);if(used!=text.size()||result<min||result>max)throw Error("InvalidOptions",std::string("Invalid host setting: ")+key);return static_cast<size_t>(result);};
  McpServerOptions server_options;server_options.cache_ttl=std::chrono::milliseconds(setting("CYGI_MCP_CACHE_TTL_MS",30000,0,86400000));
  const std::string command=argc>1?argv[1]:"mcp";
  if(command=="--help"||command=="help") {
   std::cout<<"CyGPUInspectorMCP [mcp | mcp-compact | list | describe NAME | invoke NAME JSON | invoke-file NAME FILE]\n"
    <<"No arguments preserves MCP stdio mode. Calls address the running standalone's fixed local pipe.\n"
    <<"JSON stdout; diagnostics stderr. Exit: 0 success, 1 application failure, 2 usage/schema/connection error.\n";return 0;
  }
  if(command=="list"&&argc==2){std::cout<<operations->describe().dump()<<'\n';return 0;}
  if(command=="describe"&&argc==3){std::cout<<operations->describe(argv[2]).dump()<<'\n';return 0;}
  if((command=="invoke"||command=="invoke-file")&&argc==4){
   std::string input=argv[3];if(command=="invoke-file"){std::ifstream f(std::filesystem::u8path(input));if(!f)throw Error("FileError","Cannot read arguments file");input.assign(std::istreambuf_iterator<char>(f),{});}
   const auto result=operations->invoke(argv[2],parse_json(input));std::cout<<result.dump()<<'\n';
   if(!result.value("ok",false)){std::cerr<<result.value("error","Operation failed")<<'\n';return 1;}return 0;
  }
  if(command=="mcp-compact") {
   ToolGatewayOptions gateway_options;
   gateway_options.resources.chunk_bytes=setting("CYGI_MCP_CHUNK_BYTES",32768,256,1048576);
   gateway_options.resources.max_stored_bytes=setting("CYGI_MCP_RESULT_BYTES",64*1024*1024,1024,256*1024*1024);
   gateway_options.resources.result_ttl=std::chrono::milliseconds(setting("CYGI_MCP_RESULT_TTL_MS",300000,1,86400000));
   ToolGateway gateway(operations,gateway_options);McpServerOptions options=server_options;options.resources=gateway.resources();
   McpServer server(*gateway.tools(),"CyGPUInspectorMCP",options);server.serve_stdio(std::cin,std::cout);return 0;
  }
  if(command!="mcp"||argc>2)throw Error("UsageError","Use --help for command syntax");
  McpServer server(*operations,"CyGPUInspectorMCP",server_options);server.serve_stdio(std::cin,std::cout);return 0;
 } catch(const cymcp::Error& e){const auto message=cymcp::redact_error_message(e.what());if(argc>1&&std::string(argv[1]).rfind("invoke",0)==0)std::cout<<cymcp::Json{{"ok",false},{"error",message},{"code",e.code}}.dump()<<'\n';std::cerr<<message<<'\n';return e.code=="OperationFailed"?1:2;
 } catch(const std::exception& e){std::cerr<<cymcp::redact_error_message(e.what())<<'\n';return 2;}
}

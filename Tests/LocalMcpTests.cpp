#include "McpAccess.hpp"
#include "Mcp/McpHttpHost.hpp"
#include <cymcp/mcp.hpp>
#include <atomic>
#include <iostream>
#include <thread>

using namespace cymcp;
void check(bool value) { if (!value) throw std::runtime_error("Local MCP assertion failed"); }
int main(int argc, char** argv) {
 try {
    if (argc == 2) {
        // Real application acceptance path: URL only, no token and no callbacks/confirmation.
        McpClient client(argv[1]); client.connect();
        check(client.list_tools().size() == 111);
        check(client.call_tool("get_application_state",Json::object())["structuredContent"]["ok"] == true);
        check(client.call_tool("set_filters",{{"shaders","sdk-url-only"}})["structuredContent"]["ok"] == true);
        check(client.call_tool("get_application_state",Json::object())["structuredContent"]["result"]["shader_filter"] == "sdk-url-only");
        std::cout << "Real application: SDK URL-only client discovers all 111 operations, reads and mutates\n";
        return 0;
    }
    Json root={{"language","fr"},{"appearance",{{"theme","navy"}}},{"mcp",{{"port",41824},{"custom","keep"}}}};
    auto config=cygi::ReadMcpAccessConfig(root);
    check(config.profile==LocalAccessProfile::free && !config.explicit_profile);
    auto saved=cygi::WriteMcpAccessConfig(root,config);
    check(saved["language"]=="fr" && saved["appearance"]==root["appearance"] && saved["mcp"]["custom"]=="keep");
    check(cygi::ReadMcpAccessConfig(saved).profile==LocalAccessProfile::free);
    for (const auto& option:cygi::kMcpAccessOptions) {
        auto explicit_root=root;explicit_root["mcp"]["access_profile"]=option.id;
        auto parsed=cygi::ReadMcpAccessConfig(explicit_root);
        check(parsed.profile==option.profile && parsed.explicit_profile);
    }
    auto legacy=cygi::ReadMcpAccessConfig({{"mcp_permission","read-only"}});
    check(legacy.profile==LocalAccessProfile::local_controlled && legacy.explicit_profile);
    bool rejected=false;try{cygi::ReadMcpAccessConfig({{"mcp",{{"access_profile","unknown"}}}});}catch(...){rejected=true;}check(rejected);
    for (const auto& option:cygi::kMcpAccessOptions) {
        cygi::McpHttpHost host;config.profile=option.profile;config.port=0;
        const auto access=local_access_settings(config.profile);
        const auto token=random_token(32);
        check(host.Start(config,token,[](const auto& name){return name=="get_application_state";}));
        const auto url="http://127.0.0.1:"+std::to_string(host.Port())+"/mcp";
        std::atomic_bool done=false;std::exception_ptr failure;int writes=0;
        std::thread worker([&]{try{
            bool no_token_failed=false;try{McpClient open(url);open.connect();}catch(const Error&){no_token_failed=true;}
            check(no_token_failed==access.require_token);
            if(access.require_token){bool bad=false;try{McpClient wrong(url,"incorrect-test-token");wrong.connect();}catch(const Error&){bad=true;}check(bad);}
            McpClient client(url,access.require_token?token:std::string());client.connect();
            check(client.list_tools().size()==111);
            check(client.call_tool("get_application_state",Json::object())["structuredContent"]["ok"]==true);
            bool denied=false;try{denied=client.call_tool("set_filters",{{"shaders","test"}}).value("isError",false);}catch(const Error&){denied=true;}
            check(denied==access.control_operations);
            bool invalid=false;try{invalid=client.call_tool("get_application_state",{{"unexpected",true}}).value("isError",false);}catch(const Error&){invalid=true;}check(invalid);
        }catch(...){failure=std::current_exception();}done=true;});
        auto deadline=Clock::now()+std::chrono::seconds(20);
        while(!done && Clock::now()<deadline){host.Pump([&](const auto& name,const auto&,const auto&){if(name=="set_filters")++writes;return Json{{"ok",true},{"result",Json::object()}};});std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        if(!done)host.Stop();worker.join();host.Stop();if(failure)std::rethrow_exception(failure);
        check(done && writes==(access.control_operations?0:1));
        std::cout << "PASS profile " << option.id << ": authentication, discovery, policy and argument validation\n";
    }
    std::cout << "Configuration migration preserves explicit profiles and unrelated preferences\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

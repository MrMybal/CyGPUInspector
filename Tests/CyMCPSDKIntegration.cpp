#include "Operations.hpp"
#include <cymcp/resources.hpp>
#include <cassert>
#include <iostream>
#include <thread>
int main(){
 try{
 int calls=0;const std::string payload(100000,'x');
 auto registry=cygi::CreateOperations([&](const auto&,const auto&,const auto&){++calls;return cymcp::Json{{"ok",true},{"result",{{"text",payload}}}};});
 auto check=[](bool ok){if(!ok)throw std::runtime_error("Integration assertion failed");};
 bool invalid=false;try{registry->invoke("inspect_shader",cymcp::Json::object());}catch(const cymcp::Error&){invalid=true;}check(invalid&&calls==0);
 cymcp::ToolGatewayOptions opts;opts.inline_bytes=512;opts.resources.chunk_bytes=4096;opts.resources.result_ttl=std::chrono::milliseconds(100);
 cymcp::ToolGateway gateway(registry,opts);auto tools=gateway.tools();
 const auto response=tools->invoke("cy_tools_call",{{"name","get_session"}});
 const auto uri=response.at("resource").at("uri").get<std::string>();
 const auto manifest=tools->invoke("cy_resources_read",{{"uri",uri}});
 auto next=cymcp::parse_json(manifest["contents"][0]["text"]).at("firstUri").get<std::string>();std::string all;
 while(!next.empty()){auto page=tools->invoke("cy_resources_read",{{"uri",next}});all+=page["contents"][0]["text"].get<std::string>();next=page["_meta"]["com.cymcp/chunk"].value("nextUri","");}
 check(cymcp::parse_json(all)["result"]["text"]==payload&&calls==1);
 std::this_thread::sleep_for(std::chrono::milliseconds(150));bool expired=false;
 try{tools->invoke("cy_resources_read",{{"uri",uri}});}catch(const cymcp::Error&){expired=true;}check(expired&&calls==1);
 cymcp::Cancellation stop;stop.cancel();cymcp::CallContext ctx;ctx.cancellation=stop;bool cancelled=false;
 try{registry->invoke("get_session",cymcp::Json::object(),ctx);}catch(const cymcp::Error&){cancelled=true;}check(cancelled&&calls==1);
 std::cout<<"Schema rejection, complete chunk recovery, expiry without replay and cancellation passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

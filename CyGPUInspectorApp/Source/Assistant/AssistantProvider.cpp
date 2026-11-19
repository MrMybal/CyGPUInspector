// CyGPUInspectorApp — the model the assistant talks to.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "AssistantProvider.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ShlObj.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

namespace cygi::assistant
{
	namespace
	{
		constexpr size_t kMaxResponse = 2 * 1024 * 1024;
		constexpr size_t kMaxCliOutput = 16 * 1024 * 1024;

		std::wstring Widen(const std::string &text)
		{
			if (text.empty())
				return {};
			const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring out(static_cast<size_t>(length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
			return out;
		}

		std::wstring Environment(const wchar_t *name)
		{
			wchar_t buffer[32768];
			const DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
			return length > 0 && length < std::size(buffer) ? std::wstring(buffer, length) : std::wstring();
		}

		Text Cancelled() { return "Request cancelled."; }

		// ---- URLs --------------------------------------------------------------------------------

		struct Url
		{
			bool https = false;
			bool local = false;
			std::wstring host;
			INTERNET_PORT port = 0;
			std::wstring path;   // path and query
		};

		Url Crack(const std::string &text)
		{
			if (text.empty())
				throw Error("Set the full /chat/completions URL and the model.");

			const std::wstring wide = Widen(text);
			std::vector<wchar_t> host(1024), user(256), password(256), path(8192), extra(8192);
			URL_COMPONENTS parts{};
			parts.dwStructSize = sizeof(parts);
			parts.lpszHostName = host.data();
			parts.dwHostNameLength = static_cast<DWORD>(host.size());
			parts.lpszUserName = user.data();
			parts.dwUserNameLength = static_cast<DWORD>(user.size());
			parts.lpszPassword = password.data();
			parts.dwPasswordLength = static_cast<DWORD>(password.size());
			parts.lpszUrlPath = path.data();
			parts.dwUrlPathLength = static_cast<DWORD>(path.size());
			parts.lpszExtraInfo = extra.data();
			parts.dwExtraInfoLength = static_cast<DWORD>(extra.size());
			if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts))
				throw Error("The URL is not valid. Give the full /chat/completions URL.");
			if (parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0)
				throw Error("Put the key in the API key field, not in the URL.");

			Url url;
			url.https = parts.nScheme == INTERNET_SCHEME_HTTPS;
			url.host.assign(parts.lpszHostName, parts.dwHostNameLength);
			url.port = parts.nPort;
			url.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
			url.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
			if (url.path.empty())
				url.path = L"/";

			std::wstring lower = url.host;
			std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
			url.local = lower == L"localhost" || lower == L"127.0.0.1" || lower == L"::1" || lower == L"[::1]";

			// Plain HTTP would send the key and the shaders in clear over the network. On the
			// loopback it never leaves the machine, which is where a local model runs.
			if (!url.https && !(parts.nScheme == INTERNET_SCHEME_HTTP && url.local))
				throw Error("Use HTTPS, or HTTP on localhost for a local model.");
			return url;
		}

		struct Internet
		{
			HINTERNET handle = nullptr;
			explicit Internet(HINTERNET value) : handle(value) {}
			~Internet() { if (handle != nullptr) WinHttpCloseHandle(handle); }
			Internet(const Internet &) = delete;
			Internet &operator=(const Internet &) = delete;
		};

		// ---- processes ---------------------------------------------------------------------------

		struct Handle
		{
			HANDLE value = nullptr;
			Handle() = default;
			explicit Handle(HANDLE handle) : value(handle) {}
			~Handle() { Close(); }
			void Close()
			{
				if (value != nullptr && value != INVALID_HANDLE_VALUE)
					CloseHandle(value);
				value = nullptr;
			}
			Handle(const Handle &) = delete;
			Handle &operator=(const Handle &) = delete;
		};

		// A folder of its own for each request, removed afterwards: the CLIs are started in it,
		// so they find no project, no CLAUDE.md and no git repository of the user's around them.
		class TempFolder
		{
		public:
			TempFolder()
			{
				static std::atomic<uint32_t> counter{ 0 };
				wchar_t base[MAX_PATH + 1] = {};
				GetTempPathW(MAX_PATH, base);
				m_path = std::filesystem::path(base) / (L"CyGPUInspector-assistant-" +
					std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter) + L"-" +
					std::to_wstring(GetTickCount64()));
				std::error_code code;
				std::filesystem::create_directories(m_path, code);
			}
			~TempFolder()
			{
				std::error_code code;
				std::filesystem::remove_all(m_path, code);
			}
			const std::filesystem::path &Path() const { return m_path; }

		private:
			std::filesystem::path m_path;
		};

		struct ProcessResult
		{
			DWORD exit_code = 0;
			std::string output;
		};

		// Runs a CLI with `input` on its standard input and returns its standard output. Standard
		// error goes nowhere: it can hold account details, and it is never shown. Every process the
		// CLI starts is in a job object, so a cancellation or a timeout ends all of them.
		ProcessResult RunProcess(const std::wstring &executable, const std::vector<std::wstring> &arguments,
		                         const std::filesystem::path &directory, const std::string &input,
		                         DWORD timeout_ms, const char *name, Cancellation &cancel)
		{
			if (cancel.IsCancelled())
				throw Error(Cancelled());

			std::wstring command = QuoteArgument(executable);
			for (const std::wstring &argument : arguments)
				command += L" " + QuoteArgument(argument);

			SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
			Handle stdin_read, stdin_write, stdout_read, stdout_write;
			if (!CreatePipe(&stdin_read.value, &stdin_write.value, &inherit, 0) ||
			    !CreatePipe(&stdout_read.value, &stdout_write.value, &inherit, 0))
				throw Error(Text::Format("Cannot start %s (Windows %s).", { Text::Literal(name), Text::Literal(std::to_string(GetLastError())) }));
			SetHandleInformation(stdin_write.value, HANDLE_FLAG_INHERIT, 0);
			SetHandleInformation(stdout_read.value, HANDLE_FLAG_INHERIT, 0);
			Handle null_output(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
				OPEN_EXISTING, 0, nullptr));

			Handle job(CreateJobObjectW(nullptr, nullptr));
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
			limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			if (job.value != nullptr)
				SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			startup.dwFlags = STARTF_USESTDHANDLES;
			startup.hStdInput = stdin_read.value;
			startup.hStdOutput = stdout_write.value;
			startup.hStdError = null_output.value;

			PROCESS_INFORMATION process{};
			if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
				CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &process))
				throw Error(Text::Format("Cannot start %s (Windows %s).", { Text::Literal(name), Text::Literal(std::to_string(GetLastError())) }));
			Handle process_handle(process.hProcess);
			if (job.value != nullptr)
				AssignProcessToJobObject(job.value, process.hProcess);
			ResumeThread(process.hThread);
			CloseHandle(process.hThread);

			// Only the child keeps these ends now: its exit is what closes the pipes.
			stdin_read.Close();
			stdout_write.Close();
			null_output.Close();

			cancel.Attach(job.value);

			// Written and read on threads of their own: a CLI that starts answering before it has
			// read all of a long prompt would otherwise block against a full pipe.
			std::thread writer([&stdin_write, &input]() {
				size_t offset = 0;
				while (offset < input.size())
				{
					DWORD written = 0;
					const DWORD chunk = static_cast<DWORD>(std::min<size_t>(input.size() - offset, 65536));
					if (!WriteFile(stdin_write.value, input.data() + offset, chunk, &written, nullptr) || written == 0)
						break;
					offset += written;
				}
				stdin_write.Close();
			});

			ProcessResult result;
			std::thread reader([&stdout_read, &result]() {
				std::vector<char> buffer(65536);
				DWORD read = 0;
				while (ReadFile(stdout_read.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0)
					if (result.output.size() < kMaxCliOutput)
						result.output.append(buffer.data(), read);
			});

			bool cancelled = false;
			bool timed_out = false;
			const ULONGLONG started = GetTickCount64();
			for (;;)
			{
				if (WaitForSingleObject(process.hProcess, 100) == WAIT_OBJECT_0)
					break;
				if (cancel.IsCancelled())
				{
					cancelled = true;
					break;
				}
				if (GetTickCount64() - started > timeout_ms)
				{
					timed_out = true;
					break;
				}
			}

			// Also ends anything the CLI left running, which would otherwise keep the pipe open.
			if (job.value != nullptr)
				TerminateJobObject(job.value, 1);
			else if (cancelled || timed_out)
				TerminateProcess(process.hProcess, 1);
			WaitForSingleObject(process.hProcess, 5000);
			cancel.Detach();

			writer.join();
			reader.join();
			GetExitCodeProcess(process.hProcess, &result.exit_code);

			if (cancelled)
				throw Error(Cancelled());
			if (timed_out)
				throw Error(Text::Format("%s did not answer within %s seconds.",
					{ Text::Literal(name), Text::Literal(std::to_string(timeout_ms / 1000)) }));
			return result;
		}

		// ---- providers ---------------------------------------------------------------------------

	}

	const char *ProviderName(ProviderKind kind)
	{
		switch (kind)
		{
		case ProviderKind::codex_cli: return "Codex (CLI session)";
		case ProviderKind::claude_cli: return "Claude Code (CLI session)";
		case ProviderKind::openai_api: return "OpenAI-compatible API";
		case ProviderKind::local_api: return "Local model (API)";
		default: return "?";
		}
	}

	Cancellation::~Cancellation() = default;

	void Cancellation::Cancel()
	{
        sdk.cancel();
		std::lock_guard<std::mutex> lock(m_mutex);
		m_cancelled.store(true);
		if (m_job != nullptr)
			TerminateJobObject(static_cast<HANDLE>(m_job), 1);
	}

	void Cancellation::Attach(void *job)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_job = job;
		if (m_cancelled.load() && m_job != nullptr)
			TerminateJobObject(static_cast<HANDLE>(m_job), 1);
	}

	void Cancellation::Detach()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_job = nullptr;
	}

    // The host retains its CyUI 1.2 panel and approval queue. SDK transports run only on workers.
    class SdkProvider final : public Provider {
        ProviderConfig config;
        cymcp::Json checkpoint;
        std::mutex chat_mutex;
        std::filesystem::path HistoryDirectory() const {
            if(!config.history_directory.empty())return config.history_directory;
            PWSTR folder=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder)))throw Error("Cannot locate private application storage.");
            auto result=std::filesystem::path(folder)/L"CyGPUInspector"/L"AssistantHistory";CoTaskMemFree(folder);return result;
        }
        std::string HistoryId() const {return cymcp::sha256(std::to_string(static_cast<int>(config.kind))+config.model+config.endpoint);}
        std::shared_ptr<cymcp::Provider> Make(std::shared_ptr<const cymcp::Registry> tools={}) {
            cymcp::ProviderConfig c;c.tools=std::move(tools);c.api_key=config.key;
            std::string id;
            if(IsCli(config.kind)) {
                id=config.kind==ProviderKind::codex_cli?"codex":"claude-code";
                c.process.executable=FindCli(config.kind==ProviderKind::codex_cli?"codex":"claude");
                if(config.kind==ProviderKind::codex_cli)c.process.arguments={"-c","features.shell_tool=false","-c","mcp_servers={}","-c","web_search=\"disabled\""};
                c.settings={{"deferred_tools",config.deferred_tools},{"sandbox","read-only"},{"approval_policy","never"},{"allowed_tools",cymcp::Json::array()},{"setting_sources",""}};
                // Claude's CLI cannot use dynamic SDK tools. It keeps the host JSON tool loop.
            } else {
                ValidateEndpoint(config.endpoint);id="openai-compatible";
                std::string base=config.endpoint;const std::string suffix="/chat/completions";
                if(base.size()>=suffix.size()&&base.compare(base.size()-suffix.size(),suffix.size(),suffix)==0)base.resize(base.size()-suffix.size());
                c.settings={{"base_url",base},{"deferred_tools",config.deferred_tools},{"max_tool_rounds",16}};
            }
            return cymcp::ProviderRegistry().create(id,c);
        }
    public:
        explicit SdkProvider(ProviderConfig c):config(std::move(c)){
            if(config.remember_history){cymcp::ConversationStore store(HistoryDirectory());const auto file=HistoryDirectory()/(HistoryId()+".json");if(std::filesystem::exists(file))checkpoint=store.load(HistoryId());}
        }
        void ResetHistory() override {std::lock_guard<std::mutex> guard(chat_mutex);checkpoint=nullptr;if(config.remember_history)cymcp::ConversationStore(HistoryDirectory()).erase(HistoryId());}
        Json RestoredHistory() const override {
            Json out=Json::Array();if(!checkpoint.is_object())return out;
            for(const auto& msg:checkpoint.value("messages",cymcp::Json::array())){
                const auto role=msg.value("role","");if((role!="user"&&role!="assistant")||!msg.value("content",cymcp::Json()).is_string())continue;
                auto text=msg["content"].get<std::string>();
                if(role=="user"){auto parsed=cymcp::Json::parse(text,nullptr,false);if(parsed.is_object()&&parsed.contains("request"))text=parsed["request"].get<std::string>();}
                Json row=Json::Object();row["role"]=Json(role);row["content"]=Json(text);out.Push(row);
            }return out;
        }
        Text Check(Cancellation& cancel) override {
            try {auto p=Make();cymcp::CallContext ctx;ctx.cancellation=cancel.sdk;p->connect(ctx);p->disconnect();return "Connected.";}
            catch(const std::exception&){throw Error("Provider connection failed. Check the executable, endpoint and authentication.");}
        }
        std::string Describe() const override {return std::string(ProviderName(config.kind))+" (CyMCPSDK; "+(config.model.empty()?"default model":config.model)+")";}
        bool UsesNativeTools() const override {return config.kind!=ProviderKind::claude_cli;}
        std::string NativeChat(std::shared_ptr<const cymcp::Registry> tools,const std::string& system,const std::string& input,Cancellation& cancel,const std::function<void(const std::string&)>& progress) override {
            const bool chat=static_cast<bool>(tools);std::unique_lock<std::mutex> guard(chat_mutex,std::defer_lock);if(chat)guard.lock();
            try {
                auto p=Make(std::move(tools));cymcp::Conversation conversation(p,config.model,system);
                if(p->uses_host_history()&&!p->uses_native_turn_context()){
                    cymcp::ContextOptions window;window.max_bytes=static_cast<size_t>(std::max(0,config.context_bytes));conversation.configure_context(window);
                }
                cymcp::CallContext ctx;ctx.cancellation=cancel.sdk;std::string partial;
                ctx.events=[&](const cymcp::Event& e){if(e.type=="TextDelta"){partial+=e.data.value("text","");if(progress)progress(partial);}else if(e.type=="ToolStarted"&&progress)progress(e.data.value("name",""));};
                if(chat&&checkpoint.is_object())conversation.restore(checkpoint,ctx);else conversation.connect({},ctx);
                auto save=[&](){if(!chat)return;checkpoint=conversation.checkpoint();if(config.remember_history)cymcp::ConversationStore(HistoryDirectory()).save(HistoryId(),conversation);};
                try{auto result=conversation.send(input,ctx);save();conversation.disconnect();return result.text;}
                catch(...){try{save();}catch(...){}throw;}
            }catch(const std::exception& e){if(cancel.IsCancelled())throw Error("Request cancelled.");throw Error(Text::Literal(cymcp::redact_error_message(e.what(),{config.key})));}
        }
        Json Complete(const std::string& system,const std::string& input,Cancellation& cancel) override {
            Json result;if(!ParseJsonReply(NativeChat({},system,input,cancel,{}),result))throw Error("The provider response is invalid. You can try again.");return result;
        }
    };
    std::unique_ptr<Provider> CreateProvider(const ProviderConfig &config) {return std::make_unique<SdkProvider>(config);}

	bool ParseJsonReply(const std::string &raw, Json &out)
	{
		const size_t first = raw.find_first_not_of(" \t\r\n");
		if (first == std::string::npos)
			return false;
		std::string text = raw.substr(first, raw.find_last_not_of(" \t\r\n") - first + 1);

		// Same tolerance as CyRSAssistant: a reply in a ``` fence is still a reply.
		if (text.size() >= 6 && text.compare(0, 3, "```") == 0 && text.compare(text.size() - 3, 3, "```") == 0)
		{
			const size_t newline = text.find('\n');
			text = newline == std::string::npos ? std::string() : text.substr(newline + 1, text.size() - 3 - newline - 1);
		}
		if (Json::Parse(text, out) && out.IsObject())
			return true;

		// A sentence before or after the object.
		const size_t open = text.find('{');
		const size_t close = text.rfind('}');
		if (open == std::string::npos || close == std::string::npos || close <= open)
			return false;
		return Json::Parse(text.substr(open, close - open + 1), out) && out.IsObject();
	}

	void ValidateEndpoint(const std::string &url)
	{
		Crack(url);
	}

	std::wstring QuoteArgument(const std::wstring &argument)
	{
		// The rules of CommandLineToArgvW and of the C runtime: backslashes are literal except
		// before a quote, where they have to be doubled, and the quote itself escaped.
		if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
			return argument;

		std::wstring out = L"\"";
		for (size_t i = 0;; ++i)
		{
			size_t backslashes = 0;
			while (i < argument.size() && argument[i] == L'\\')
			{
				++i;
				++backslashes;
			}
			if (i == argument.size())
			{
				out.append(backslashes * 2, L'\\');
				break;
			}
			if (argument[i] == L'"')
			{
				out.append(backslashes * 2 + 1, L'\\');
				out += L'"';
			}
			else
			{
				out.append(backslashes, L'\\');
				out += argument[i];
			}
		}
		out += L'"';
		return out;
	}

	std::wstring FindCli(const char *name)
	{
		const std::wstring executable = Widen(name) + L".exe";
		std::error_code code;

		// The PATH, one folder at a time. Not SearchPath: it also looks in the current folder,
		// and a codex.exe dropped wherever the application was started from is not the CLI.
		const std::wstring path = Environment(L"PATH");
		size_t start = 0;
		while (start <= path.size())
		{
			size_t end = path.find(L';', start);
			if (end == std::wstring::npos)
				end = path.size();
			std::wstring folder = path.substr(start, end - start);
			folder.erase(std::remove(folder.begin(), folder.end(), L'"'), folder.end());
			if (!folder.empty())
			{
				const std::filesystem::path candidate = std::filesystem::path(folder) / executable;
				if (std::filesystem::is_regular_file(candidate, code))
					return candidate.wstring();
			}
			start = end + 1;
		}

		// npm puts a script on the PATH and the executable deep in its tree.
		const std::filesystem::path npm = std::filesystem::path(Environment(L"APPDATA")) / L"npm" / L"node_modules";
		if (std::string(name) == "codex")
		{
			const std::filesystem::path vendors = npm / L"@openai" / L"codex" / L"node_modules" / L"@openai";
			for (const auto &platform : std::filesystem::directory_iterator(vendors, code))
			{
				if (platform.path().filename().wstring().rfind(L"codex-win32-", 0) != 0)
					continue;
				for (const auto &target : std::filesystem::directory_iterator(platform.path() / L"vendor", code))
				{
					const std::filesystem::path candidate = target.path() / L"bin" / L"codex.exe";
					if (std::filesystem::is_regular_file(candidate, code))
						return candidate.wstring();
				}
			}
		}
		else
		{
			const std::filesystem::path candidates[] = {
				npm / L"@anthropic-ai" / L"claude-code" / L"bin" / L"claude.exe",
				std::filesystem::path(Environment(L"USERPROFILE")) / L".local" / L"bin" / L"claude.exe",
			};
			for (const std::filesystem::path &candidate : candidates)
				if (std::filesystem::is_regular_file(candidate, code))
					return candidate.wstring();
		}

		throw Error(Text::Format("%s CLI was not found. Install it and sign in before retrying.", { Text::Literal(name) }));
	}

	HttpResponse HttpPostJson(const std::string &url_text, const std::string &body, const std::string &key,
	                          unsigned long timeout_ms, Cancellation &cancel)
	{
		const Url url = Crack(url_text);
		if (cancel.IsCancelled())
			throw Error(Cancelled());

		// A proxy never applies to the loopback, and asking for one there only adds a way to fail.
		Internet session(WinHttpOpen(L"CyGPUInspector",
			url.local ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
		if (session.handle == nullptr)
			throw Error("Provider unavailable or TLS connection failed.");
		WinHttpSetTimeouts(session.handle, 15000, 15000, 60000, static_cast<int>(timeout_ms));

		Internet connection(WinHttpConnect(session.handle, url.host.c_str(), url.port, 0));
		Internet request(connection.handle != nullptr
			? WinHttpOpenRequest(connection.handle, L"POST", url.path.c_str(), nullptr, WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES, url.https ? WINHTTP_FLAG_SECURE : 0)
			: nullptr);
		if (request.handle == nullptr)
			throw Error("Provider unavailable or TLS connection failed.");

		DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
		WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

		std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
		if (!key.empty())
			headers += L"Authorization: Bearer " + Widen(key) + L"\r\n";

		const BOOL sent = WinHttpSendRequest(request.handle, headers.c_str(), static_cast<DWORD>(-1),
			const_cast<char *>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
		if (!sent || !WinHttpReceiveResponse(request.handle, nullptr))
		{
			if (cancel.IsCancelled())
				throw Error(Cancelled());
			throw Error("Provider unavailable or TLS connection failed.");
		}

		HttpResponse response;
		DWORD status = 0;
		DWORD size = sizeof(status);
		WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
		response.status = status;

		std::vector<char> buffer(65536);
		for (;;)
		{
			if (cancel.IsCancelled())
				throw Error(Cancelled());
			DWORD read = 0;
			if (!WinHttpReadData(request.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read))
				throw Error("Provider unavailable or TLS connection failed.");
			if (read == 0)
				break;
			response.body.append(buffer.data(), read);
			if (response.body.size() > kMaxResponse)
				throw Error("Provider response too large.");
		}
		return response;
	}
}

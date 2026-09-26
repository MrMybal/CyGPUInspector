// CyGPUInspectorApp — the assistant: connection, conversation, approvals and shader jobs.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Assistant.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace cygi::assistant
{
	namespace
	{
		constexpr size_t kMaxConversation = 32;   // messages kept, as in CyRSAssistant
		constexpr auto kApprovalTimeout = std::chrono::minutes(10);
		constexpr auto kAnalysisPatience = std::chrono::seconds(20);

		using Clock = std::chrono::steady_clock;

		Json Failure(const std::string &message)
		{
			Json answer = Json::Object();
			answer["ok"] = Json(false);
			answer["error"] = Json(message);
			return answer;
		}

		bool IsBlank(const std::string &text)
		{
			return text.find_first_not_of(" \t\r\n") == std::string::npos;
		}

		struct ToolRequest
		{
			std::string tool;
			Json arguments;
			uint64_t prompt_id = 0;
			std::promise<Json> result;
			bool retrying = false;
			Clock::time_point deadline;
			Clock::time_point next_try;
		};
	}

	struct Assistant::Shared
	{
		mutable std::mutex mutex;
		std::condition_variable idle;
		int workers = 0;

		// Connection.
		bool wanted = false;
		uint64_t epoch = 0;
		Phase phase = Phase::disconnected;
		Text message = "Choose a provider, then click Connect.";
		std::shared_ptr<Provider> provider;
		std::shared_ptr<Cancellation> connect_cancel;

		// Chat.
		PermissionMode mode = PermissionMode::automatic;
		std::vector<ChatMessage> conversation;
		uint64_t version = 0;
		bool pending = false;
		uint64_t prompt_id = 0;
		ULONGLONG started = 0;
		std::string progress;
		std::shared_ptr<Cancellation> cancel;

		// The approval the loop is waiting for, in "ask each time".
		bool approval_active = false;
		int approval_status = 0;   // 0 waiting, 1 allowed, 2 denied
		Approval approval;
		uint64_t next_approval = 0;

		// Tool calls waiting for the UI thread.
		std::deque<std::shared_ptr<ToolRequest>> tools;

		// Shader jobs.
		bool job_running = false;
		uint64_t job_id = 0;
		uint32_t job_shader = 0;
		JobKind job_kind = JobKind::explanation;
		ULONGLONG job_started = 0;
		std::shared_ptr<Cancellation> job_cancel;
		std::deque<JobOutcome> outcomes;

		void Add(ChatMessage entry)
		{
			conversation.push_back(std::move(entry));
			while (conversation.size() > kMaxConversation)
				conversation.erase(conversation.begin());
			++version;
		}

		// With the mutex held.
		void StopRequest(const Text &notice)
		{
			if (!pending)
				return;
			pending = false;
			progress.clear();
			approval_active = false;
			if (cancel)
				cancel->Cancel();
			if (!notice.Empty())
			{
				ChatMessage entry;
				entry.role = "information";
				entry.notice = notice;
				Add(std::move(entry));
			}
		}

		void StopJob()
		{
			if (!job_running)
				return;
			job_running = false;
			if (job_cancel)
				job_cancel->Cancel();
		}
	};

	using SharedState = Assistant::Shared;

	namespace
	{
		// Every worker is counted, so the destructor can give them a moment to stop.
		template <typename Work>
		void StartWorker(const std::shared_ptr<SharedState> &shared, Work work)
		{
			{
				std::lock_guard<std::mutex> lock(shared->mutex);
				++shared->workers;
			}
			std::thread([shared, work]() mutable {
				work();
				std::lock_guard<std::mutex> lock(shared->mutex);
				--shared->workers;
				shared->idle.notify_all();
			}).detach();
		}

		// Worker side of a tool call: queued for the UI thread, then waited for.
		Json CallTool(const std::shared_ptr<SharedState> &shared, uint64_t prompt_id, Cancellation &cancel,
		              const std::string &tool, const Json &arguments)
		{
			auto request = std::make_shared<ToolRequest>();
			request->tool = tool;
			request->arguments = arguments;
			request->prompt_id = prompt_id;
			std::future<Json> answer = request->result.get_future();
			{
				std::lock_guard<std::mutex> lock(shared->mutex);
				shared->tools.push_back(request);
			}
			while (answer.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready)
				if (cancel.IsCancelled())
					return Failure("the request was cancelled");
			return answer.get();
		}

		bool RequestApproval(const std::shared_ptr<SharedState> &shared, uint64_t prompt_id, Cancellation &cancel,
		                     const Json &arguments, const Text &summary)
		{
			uint64_t id = 0;
			{
				std::lock_guard<std::mutex> lock(shared->mutex);
				if (!shared->pending || shared->prompt_id != prompt_id)
					return false;
				id = ++shared->next_approval;
				shared->approval.id = id;
				shared->approval.summary = summary;
				shared->approval.payload = arguments["hlsl"].GetType() == Json::Type::string
					? arguments["hlsl"].AsString() : arguments.Write(2);
				shared->approval_active = true;
				shared->approval_status = 0;
			}

			const Clock::time_point deadline = Clock::now() + kApprovalTimeout;
			for (;;)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				std::lock_guard<std::mutex> lock(shared->mutex);
				// Cleared by a cancellation or a change of mode: the answer is no.
				if (!shared->approval_active || shared->approval.id != id)
					return false;
				if (shared->approval_status != 0 || cancel.IsCancelled() || Clock::now() > deadline)
				{
					const bool allowed = shared->approval_status == 1 && !cancel.IsCancelled();
					shared->approval_active = false;
					return allowed;
				}
			}
		}
	}

	Assistant::Assistant() : m_shared(std::make_shared<Shared>())
	{
	}

	Assistant::~Assistant()
	{
		std::unique_lock<std::mutex> lock(m_shared->mutex);
		m_shared->wanted = false;
		++m_shared->epoch;
		if (m_shared->connect_cancel)
			m_shared->connect_cancel->Cancel();
		m_shared->StopRequest({});
		m_shared->StopJob();
		for (const auto &request : m_shared->tools)
			request->result.set_value(Failure("CyGPUInspector is closing"));
		m_shared->tools.clear();

		// A CLI stops at once; an HTTP request cannot be interrupted and is left to finish on its
		// own, against the shared state it holds.
		m_shared->idle.wait_for(lock, std::chrono::seconds(2), [this]() { return m_shared->workers == 0; });
	}

	// ---- connection ---------------------------------------------------------------------------------

	bool Assistant::Wanted() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->wanted;
	}

	void Assistant::Connect()
	{
		const ProviderConfig config = m_config;
		auto cancel = std::make_shared<Cancellation>();
		uint64_t epoch = 0;
		{
			std::lock_guard<std::mutex> lock(m_shared->mutex);
			if (m_shared->connect_cancel)
				m_shared->connect_cancel->Cancel();
			m_shared->wanted = true;
			epoch = ++m_shared->epoch;
			m_shared->phase = Phase::connecting;
			m_shared->message = "Checking the provider...";
			m_shared->provider.reset();
			m_shared->connect_cancel = cancel;
		}

		std::shared_ptr<Shared> shared = m_shared;
		StartWorker(shared, [shared, config, cancel, epoch]() {
			std::shared_ptr<Provider> provider;
			Text message;
			Phase phase = Phase::error;
			try
			{
				provider = CreateProvider(config);
				message = provider->Check(*cancel);
				phase = Phase::ready;
			}
			catch (const Error &error)
			{
				message = error.Message();
			}
			catch (const std::exception &error)
			{
				message = Text::Format("Connection failed: %s", { Text::Literal(error.what()) });
			}

			std::lock_guard<std::mutex> lock(shared->mutex);
			if (shared->epoch != epoch || !shared->wanted)
				return;
			shared->provider = phase == Phase::ready ? provider : nullptr;
			shared->phase = phase;
			shared->message = message;
			shared->connect_cancel.reset();
		});
	}

	void Assistant::Disconnect()
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		m_shared->wanted = false;
		++m_shared->epoch;
		if (m_shared->connect_cancel)
			m_shared->connect_cancel->Cancel();
		m_shared->connect_cancel.reset();
		m_shared->provider.reset();
		m_shared->phase = Phase::disconnected;
		m_shared->message = "Disconnected.";
		m_shared->StopRequest("Request cancelled. Operations already done stay done.");
		m_shared->StopJob();
	}

	Phase Assistant::GetPhase() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->phase;
	}

	Text Assistant::StatusMessage() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->message;
	}

	bool Assistant::IsReady() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->wanted && m_shared->phase == Phase::ready && m_shared->provider != nullptr;
	}

	std::string Assistant::ProviderDescription() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->provider ? m_shared->provider->Describe() : std::string();
	}

	// ---- chat ---------------------------------------------------------------------------------------

	PermissionMode Assistant::Mode() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->mode;
	}

	void Assistant::SetMode(PermissionMode mode)
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		if (mode == m_shared->mode)
			return;
		m_shared->StopRequest("The permission mode changed: the request in progress was cancelled.");
		m_shared->mode = mode;
	}

	std::vector<ChatMessage> Assistant::Conversation() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->conversation;
	}

	uint64_t Assistant::ConversationVersion() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->version;
	}

	bool Assistant::IsPending() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->pending;
	}

	uint64_t Assistant::PendingSeconds() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->pending ? (GetTickCount64() - m_shared->started) / 1000 : 0;
	}

	std::string Assistant::Progress() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->progress;
	}

	bool Assistant::Send(const std::string &text, const Json &selection, const std::string &language_name,
	                     const std::string &language_code)
	{
		AgentRequest request;
		std::shared_ptr<Provider> provider;
		std::shared_ptr<Cancellation> cancel;
		uint64_t prompt_id = 0;
		{
			std::lock_guard<std::mutex> lock(m_shared->mutex);
			const bool ready = m_shared->wanted && m_shared->phase == Phase::ready && m_shared->provider != nullptr;
			if (!ready || m_shared->pending || IsBlank(text))
				return false;

			request.text = text;
			request.conversation = m_shared->conversation;
			request.mode = m_shared->mode;
			request.language_name = language_name;
			request.language_code = language_code;

			ChatMessage message;
			message.role = "user";
			message.content = text;
			m_shared->Add(std::move(message));

			m_shared->pending = true;
			prompt_id = ++m_shared->prompt_id;
			m_shared->started = GetTickCount64();
			m_shared->progress.clear();
			m_shared->cancel = std::make_shared<Cancellation>();
			cancel = m_shared->cancel;
			provider = m_shared->provider;
		}

		std::shared_ptr<Shared> shared = m_shared;
		StartWorker(shared, [shared, provider, request, selection, cancel, prompt_id]() {
			AgentHost host;
			host.call_tool = [&](const std::string &tool, const Json &arguments) {
				return CallTool(shared, prompt_id, *cancel, tool, arguments);
			};
			host.approve = [&](const std::string &, const Json &arguments, const Text &summary) {
				return RequestApproval(shared, prompt_id, *cancel, arguments, summary);
			};
			host.progress = [&](const std::string &progress) {
				std::lock_guard<std::mutex> lock(shared->mutex);
				if (shared->pending && shared->prompt_id == prompt_id)
					shared->progress = progress;
			};
			host.application_state = [&]() {
				Json state = Json::Object();
				const Json session = CallTool(shared, prompt_id, *cancel, "get_session", Json::Object());
				state["session"] = session["ok"].AsBool() ? session["result"] : Json(session["error"].AsString());
				const Json frame = CallTool(shared, prompt_id, *cancel, "get_current_frame", Json::Object());
				state["frame"] = frame["ok"].AsBool() ? frame["result"] : Json(frame["error"].AsString());
				state["selection"] = selection;
				return state;
			};

			ChatMessage reply;
			try
			{
				const AgentReply answer = RunChat(*provider, request, host, *cancel);
				if (answer.notice.Empty())
				{
					reply.role = "assistant";
					reply.content = answer.text;
				}
				else
				{
					reply.role = "information";
					reply.notice = answer.notice;
				}
			}
			catch (const Error &error)
			{
				reply.role = "information";
				reply.notice = Text::Format("Request failed: %s", { error.Message() });
			}
			catch (const std::exception &error)
			{
				reply.role = "information";
				reply.notice = Text::Format("Request failed: %s", { Text::Literal(error.what()) });
			}

			std::lock_guard<std::mutex> lock(shared->mutex);
			// Cancelled or superseded: whatever came back is dropped.
			if (!shared->pending || shared->prompt_id != prompt_id)
				return;
			shared->pending = false;
			shared->progress.clear();
			shared->approval_active = false;
			shared->Add(std::move(reply));
		});
		return true;
	}

	void Assistant::CancelRequest()
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		m_shared->StopRequest("Request cancelled. Operations already done stay done.");
	}

	void Assistant::NewConversation()
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		if (m_shared->pending)
			return;
		m_shared->conversation.clear();
		++m_shared->version;
	}

	bool Assistant::PendingApproval(Approval &out) const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		if (!m_shared->approval_active || m_shared->approval_status != 0)
			return false;
		out = m_shared->approval;
		return true;
	}

	void Assistant::Decide(uint64_t approval_id, bool allow)
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		if (m_shared->approval_active && m_shared->approval.id == approval_id && m_shared->approval_status == 0)
			m_shared->approval_status = allow ? 1 : 2;
	}

	// ---- jobs ---------------------------------------------------------------------------------------

	bool Assistant::IsJobRunning() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->job_running;
	}

	uint32_t Assistant::JobShader() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->job_running ? m_shared->job_shader : 0;
	}

	JobKind Assistant::RunningJob() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->job_kind;
	}

	uint64_t Assistant::JobSeconds() const
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		return m_shared->job_running ? (GetTickCount64() - m_shared->job_started) / 1000 : 0;
	}

	bool Assistant::StartJob(JobKind kind, uint32_t shader_id, const Sha256Digest &signature, Json shader,
	                         const std::string &language_name, const std::string &language_code)
	{
		std::shared_ptr<Provider> provider;
		std::shared_ptr<Cancellation> cancel;
		uint64_t job_id = 0;
		{
			std::lock_guard<std::mutex> lock(m_shared->mutex);
			const bool ready = m_shared->wanted && m_shared->phase == Phase::ready && m_shared->provider != nullptr;
			if (!ready || m_shared->job_running)
				return false;
			m_shared->job_running = true;
			job_id = ++m_shared->job_id;
			m_shared->job_shader = shader_id;
			m_shared->job_kind = kind;
			m_shared->job_started = GetTickCount64();
			m_shared->job_cancel = std::make_shared<Cancellation>();
			cancel = m_shared->job_cancel;
			provider = m_shared->provider;
		}

		std::shared_ptr<Shared> shared = m_shared;
		StartWorker(shared, [shared, provider, cancel, job_id, kind, shader_id, signature, shader,
		                     language_name, language_code]() {
			JobOutcome outcome;
			outcome.shader_id = shader_id;
			outcome.signature = signature;
			outcome.result.kind = kind;
			try
			{
				outcome.result = RunJob(*provider, kind, shader, language_name, language_code, *cancel);
				outcome.ok = true;
			}
			catch (const Error &error)
			{
				outcome.error = error.Message();
			}
			catch (const std::exception &error)
			{
				outcome.error = Text::Literal(error.what());
			}

			std::lock_guard<std::mutex> lock(shared->mutex);
			if (!shared->job_running || shared->job_id != job_id)
				return;
			shared->job_running = false;
			shared->outcomes.push_back(std::move(outcome));
		});
		return true;
	}

	void Assistant::CancelJob()
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		m_shared->StopJob();
	}

	bool Assistant::TakeJobOutcome(JobOutcome &out)
	{
		std::lock_guard<std::mutex> lock(m_shared->mutex);
		if (m_shared->outcomes.empty())
			return false;
		out = std::move(m_shared->outcomes.front());
		m_shared->outcomes.pop_front();
		return true;
	}

	// ---- UI thread ----------------------------------------------------------------------------------

	void Assistant::Pump(const ToolHandler &handler)
	{
		std::shared_ptr<ToolRequest> request;
		{
			std::lock_guard<std::mutex> lock(m_shared->mutex);
			if (m_shared->tools.empty())
				return;
			request = m_shared->tools.front();
			// A tool of a request that has been cancelled never runs.
			if (!m_shared->pending || m_shared->prompt_id != request->prompt_id)
			{
				m_shared->tools.pop_front();
				request->result.set_value(Failure("the request was cancelled"));
				return;
			}
			if (request->retrying && Clock::now() < request->next_try)
				return;
		}

		// Outside the lock: a tool can take a while, and the workers must still be able to report.
		const Json answer = handler(request->tool, request->arguments);

		// The disassembler and the decompilers work in the background, and a first call only
		// starts them. Asking again a few times per second, for a while, saves the model a step.
		if (!answer["ok"].AsBool() && answer["error"].AsString().find("ask again in a moment") != std::string::npos)
		{
			const Clock::time_point now = Clock::now();
			if (!request->retrying)
			{
				request->retrying = true;
				request->deadline = now + kAnalysisPatience;
			}
			if (now < request->deadline)
			{
				request->next_try = now + std::chrono::milliseconds(250);
				return;
			}
		}

		std::lock_guard<std::mutex> lock(m_shared->mutex);
		if (!m_shared->tools.empty() && m_shared->tools.front() == request)
			m_shared->tools.pop_front();
		request->result.set_value(answer);
	}
}

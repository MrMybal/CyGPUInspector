// CyGPUInspectorCoreTests — self checks for the pieces the whole system depends on.
//
// These run without a game and without a GPU: hashing, the shared memory ring buffer, the DXBC
// container parser and the session directory. Run bin/<Config>/CyGPUInspectorCoreTests.exe.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include <CyGPUInspectorCore/ControlPipe.hpp>
#include <CyGPUInspectorCore/Json.hpp>
#include <CyGPUInspectorCore/Localization.hpp>
#include <CyGPUInspectorCore/Protocol.hpp>
#include <CyGPUInspectorCore/RingBuffer.hpp>
#include <CyGPUInspectorCore/SessionDirectory.hpp>
#include <CyGPUInspectorCore/Sha256.hpp>
#include <CyGPUInspectorCore/ShaderBlob.hpp>
#include <CyGPUInspectorCore/SharedMemory.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;
	int g_checks = 0;

	void Check(bool condition, const char *what)
	{
		++g_checks;
		if (!condition)
		{
			++g_failures;
			std::printf("  FAIL  %s\n", what);
		}
	}

	void CheckEqual(const std::string &actual, const std::string &expected, const char *what)
	{
		++g_checks;
		if (actual != expected)
		{
			++g_failures;
			std::printf("  FAIL  %s\n        expected %s\n        actual   %s\n", what, expected.c_str(),
				actual.c_str());
		}
	}

	void TestSha256()
	{
		std::printf("SHA-256\n");

		// FIPS 180-4 test vectors.
		CheckEqual(cygi::Sha256::Hash("", 0).ToHex(),
			"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty string");
		CheckEqual(cygi::Sha256::Hash("abc", 3).ToHex(),
			"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");

		const char *long_input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
		CheckEqual(cygi::Sha256::Hash(long_input, std::strlen(long_input)).ToHex(),
			"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "56 byte input");

		// A one million character message exercises the block loop and the length encoding.
		cygi::Sha256 streaming;
		const std::string chunk(1000, 'a');
		for (int i = 0; i < 1000; ++i)
			streaming.Update(chunk.data(), chunk.size());
		CheckEqual(streaming.Finish().ToHex(),
			"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "one million 'a'");

		cygi::Sha256Digest parsed;
		Check(cygi::Sha256Digest::FromHex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
			parsed), "FromHex accepts a valid digest");
		Check(parsed == cygi::Sha256::Hash("abc", 3), "FromHex round trips");
		Check(!cygi::Sha256Digest::FromHex("nothex", parsed), "FromHex rejects garbage");
	}

	void TestRingBuffer()
	{
		std::printf("Ring buffer\n");

		constexpr size_t kCapacity = 64 * 1024;
		std::vector<uint8_t> memory(cygi::RingMappingSize(kCapacity));

		cygi::RingWriter writer;
		Check(writer.Initialize(memory.data(), kCapacity), "writer initializes");

		cygi::RingReader reader;
		Check(reader.Attach(memory.data(), kCapacity), "reader attaches");

		// Round trip of a fixed part plus a trailing blob.
		cygi::FrameBeginRecord begin = {};
		begin.frame_index = 42;
		const std::string blob = "hello";
		Check(writer.Write(cygi::RecordType::frame_begin, &begin, sizeof(begin), blob.data(),
			static_cast<uint32_t>(blob.size())), "write succeeds");

		const cygi::RecordHeader *header = nullptr;
		const uint8_t *payload = nullptr;
		uint32_t payload_size = 0;
		Check(reader.Peek(header, payload, payload_size), "reader sees the record");
		Check(header->type == cygi::RecordType::frame_begin, "record type survives");
		Check(payload_size >= sizeof(begin) + blob.size(), "payload is complete");
		Check(reinterpret_cast<const cygi::FrameBeginRecord *>(payload)->frame_index == 42, "fixed part survives");
		Check(std::memcmp(payload + sizeof(begin), blob.data(), blob.size()) == 0, "blob survives");
		reader.Pop();
		Check(!reader.Peek(header, payload, payload_size), "ring is empty after Pop");

		// Wrap around: many records of a size that does not divide the capacity.
		std::vector<uint8_t> filler(300, 0xAB);
		uint64_t written = 0;
		uint64_t read_back = 0;
		for (int round = 0; round < 2000; ++round)
		{
			if (writer.Write(cygi::RecordType::log_message, filler.data(), static_cast<uint32_t>(filler.size())))
				++written;

			while (reader.Peek(header, payload, payload_size))
			{
				Check(payload_size >= filler.size(), "wrapped payload is complete");
				Check(payload[0] == 0xAB && payload[filler.size() - 1] == 0xAB, "wrapped payload is intact");
				reader.Pop();
				++read_back;
			}
		}
		Check(written == read_back, "every written record was read back");
		Check(written > 1500, "the ring kept accepting records while being drained");

		// Overflow: stop draining and make sure the writer refuses instead of corrupting.
		uint64_t refused = 0;
		for (int round = 0; round < 1000; ++round)
		{
			if (!writer.Write(cygi::RecordType::log_message, filler.data(), static_cast<uint32_t>(filler.size())))
				++refused;
		}
		Check(refused > 0, "a full ring refuses writes");
		Check(writer.DroppedRecords() == refused, "refused writes are accounted as drops");

		// The reader must still find valid records in what was accepted.
		uint64_t survivors = 0;
		while (reader.Peek(header, payload, payload_size))
		{
			reader.Pop();
			++survivors;
		}
		Check(survivors > 0, "records written before the overflow are still readable");

		// A record larger than the whole ring is rejected, not truncated.
		std::vector<uint8_t> huge(kCapacity + 16, 0);
		Check(!writer.Write(cygi::RecordType::shader_code, huge.data(), static_cast<uint32_t>(huge.size())),
			"an oversized record is rejected");
	}

	// Builds a minimal but structurally valid DXBC container with one code chunk.
	std::vector<uint8_t> MakeContainer(const char *code_fourcc, uint32_t version_token)
	{
		const uint32_t header_size = 4 + 16 + 4 + 4 + 4 + 4; // + one chunk offset
		const uint32_t chunk_payload = 16;
		std::vector<uint8_t> blob(header_size + 8 + chunk_payload, 0);

		std::memcpy(blob.data(), "DXBC", 4);
		for (int i = 0; i < 16; ++i)
			blob[4 + i] = static_cast<uint8_t>(0x10 + i); // checksum, must not affect the signature

		const uint32_t one = 1;
		const uint32_t total_size = static_cast<uint32_t>(blob.size());
		const uint32_t chunk_count = 1;
		const uint32_t chunk_offset = header_size;
		std::memcpy(blob.data() + 20, &one, 4);
		std::memcpy(blob.data() + 24, &total_size, 4);
		std::memcpy(blob.data() + 28, &chunk_count, 4);
		std::memcpy(blob.data() + 32, &chunk_offset, 4);

		std::memcpy(blob.data() + chunk_offset, code_fourcc, 4);
		std::memcpy(blob.data() + chunk_offset + 4, &chunk_payload, 4);
		std::memcpy(blob.data() + chunk_offset + 8, &version_token, 4);
		return blob;
	}

	void TestShaderBlob()
	{
		std::printf("Shader blob\n");

		// Program kind 0 (pixel), version 5.0.
		const std::vector<uint8_t> dxbc = MakeContainer("SHEX", (0u << 16) | (5u << 4) | 0u);
		cygi::ShaderBlobInfo info;
		Check(cygi::ParseShaderBlob(dxbc.data(), dxbc.size(), info), "a DXBC container parses");
		Check(info.format == cygi::ShaderFormat::dxbc, "DXBC is recognized");
		Check(info.stage == cygi::ShaderStage::pixel, "the program kind gives the stage");
		Check(info.shader_model == 0x50, "the shader model is read");
		CheckEqual(cygi::ShaderModelName(info.shader_model), "5_0", "shader model name");

		// Program kind 5 (compute), version 6.6, DXIL.
		const std::vector<uint8_t> dxil = MakeContainer("DXIL", (5u << 16) | (6u << 4) | 6u);
		Check(cygi::ParseShaderBlob(dxil.data(), dxil.size(), info), "a DXIL container parses");
		Check(info.format == cygi::ShaderFormat::dxil, "DXIL is recognized");
		Check(info.stage == cygi::ShaderStage::compute, "compute stage is recognized");
		CheckEqual(cygi::ShaderModelName(info.shader_model), "6_6", "SM 6.6 name");

		// The container checksum must not take part in the signature.
		std::vector<uint8_t> same_shader = dxbc;
		same_shader[6] ^= 0xFF;
		Check(cygi::ComputeShaderSignature(dxbc.data(), dxbc.size()) ==
			cygi::ComputeShaderSignature(same_shader.data(), same_shader.size()),
			"the signature ignores the container checksum");

		// Changing the code must change the signature.
		std::vector<uint8_t> other_shader = dxbc;
		other_shader.back() ^= 0xFF;
		Check(!(cygi::ComputeShaderSignature(dxbc.data(), dxbc.size()) ==
			cygi::ComputeShaderSignature(other_shader.data(), other_shader.size())),
			"the signature follows the code");

		// The semantic hash only covers the code chunk.
		Check(cygi::ComputeShaderSemanticHash(dxbc.data(), dxbc.size()) ==
			cygi::ComputeShaderSemanticHash(same_shader.data(), same_shader.size()),
			"the semantic hash ignores the checksum too");

		// Garbage must not crash the parser.
		const uint8_t garbage[] = { 'D', 'X', 'B', 'C', 0, 0, 0 };
		Check(!cygi::ParseShaderBlob(garbage, sizeof(garbage), info), "a truncated container is rejected");
		Check(!cygi::ParseShaderBlob(nullptr, 0, info), "a null blob is rejected");
	}

	void TestSessionDirectory()
	{
		std::printf("Session directory\n");

		cygi::SessionPublisher publisher;
		Check(publisher.Claim(GetCurrentProcessId(), 7, cygi::GraphicsApi::d3d12, "TestGame.exe",
			"Local\\CyGPUInspectorTest.ring", "Local\\CyGPUInspectorTest.signal",
			"\\\\.\\pipe\\CyGPUInspectorTest", 1024, cygi::kCapSharedResource), "a session slot is claimed");

		cygi::SessionDirectory directory;
		Check(directory.Open(), "the directory opens");

		const std::vector<cygi::SessionEntry> entries = directory.List();
		bool found = false;
		for (const cygi::SessionEntry &entry : entries)
		{
			if (entry.process_id == GetCurrentProcessId() && entry.device_index == 7)
			{
				found = true;
				CheckEqual(entry.process_name, "TestGame.exe", "process name survives");
				Check(entry.api == cygi::GraphicsApi::d3d12, "graphics API survives");
				CheckEqual(entry.ring_name, "Local\\CyGPUInspectorTest.ring", "ring name survives");
				Check(entry.IsFresh(), "a session that just published is fresh");
			}
		}
		Check(found, "the published session is listed");

		publisher.Release();
		bool still_there = false;
		for (const cygi::SessionEntry &entry : directory.List())
			if (entry.process_id == GetCurrentProcessId() && entry.device_index == 7)
				still_there = true;
		Check(!still_there, "a released session disappears");
	}

	void TestJson()
	{
		std::printf("JSON\n");

		// Round trip: whatever is written must read back identical.
		cygi::Json root = cygi::Json::Object();
		root["name"] = cygi::Json("CyGPUInspector");
		root["version"] = cygi::Json(1);
		root["ratio"] = cygi::Json(0.25);
		root["enabled"] = cygi::Json(true);
		root["missing"] = cygi::Json();

		cygi::Json list = cygi::Json::Array();
		list.Push(cygi::Json(10u));
		list.Push(cygi::Json(20u));
		list.Push(cygi::Json("thirty"));
		root["values"] = list;

		cygi::Json nested = cygi::Json::Object();
		nested["deep"] = cygi::Json(42u);
		root["nested"] = nested;

		const std::string text = root.Write(2);
		Check(!text.empty(), "an object writes to text");

		cygi::Json parsed;
		std::string error;
		Check(cygi::Json::Parse(text, parsed, &error), "the text parses back");
		if (!error.empty())
			std::printf("        %s\n", error.c_str());

		CheckEqual(parsed["name"].AsString(), "CyGPUInspector", "strings survive");
		Check(parsed["version"].AsUInt() == 1, "integers survive");
		Check(parsed["ratio"].AsNumber() > 0.249 && parsed["ratio"].AsNumber() < 0.251,
			"fractions survive");
		Check(parsed["enabled"].AsBool(), "booleans survive");
		Check(parsed["missing"].IsNull(), "null survives");
		Check(parsed["values"].Size() == 3, "arrays keep their length");
		Check(parsed["values"].Items()[1].AsUInt() == 20, "array items survive");
		CheckEqual(parsed["values"].Items()[2].AsString(), "thirty", "mixed arrays survive");
		Check(parsed["nested"]["deep"].AsUInt() == 42, "nesting survives");

		// An integer must not gain a decimal tail: captures are read by people.
		Check(text.find("\"version\": 1") != std::string::npos, "integers are written as integers");

		// Escapes.
		cygi::Json escapes = cygi::Json::Object();
		escapes["text"] = cygi::Json(std::string("a ") + '"' + " b" + '\n' + "c" + '\t' + "d" + '\\' + "e");
		cygi::Json escaped_back;
		Check(cygi::Json::Parse(escapes.Write(0), escaped_back), "escaped text parses");
		CheckEqual(escaped_back["text"].AsString(), escapes["text"].AsString(), "escapes round trip");

		// Missing keys and wrong types must give the fallback, not a crash.
		Check(parsed["nothing"].IsNull(), "a missing key reads as null");
		Check(parsed["nothing"]["deeper"].IsNull(), "a missing key can be walked through");
		Check(parsed["name"].AsUInt(7) == 7, "a wrong type falls back");
		Check(parsed["values"].AsString().empty(), "an array read as a string is empty");

		// Malformed input must be refused rather than half accepted.
		cygi::Json rejected;
		Check(!cygi::Json::Parse("{\"a\":}", rejected), "a truncated object is refused");
		Check(!cygi::Json::Parse("[1, 2", rejected), "an unterminated array is refused");
		Check(!cygi::Json::Parse("{} trailing", rejected), "trailing characters are refused");
		Check(cygi::Json::Parse("  {\"a\" : [ 1 , 2 ] }  ", rejected), "whitespace is tolerated");
		Check(rejected["a"].Size() == 2, "the tolerated form still parses correctly");
	}

	void TestControlPipe()
	{
		std::printf("Control pipe\n");

		const char *pipe_name = "\\\\.\\pipe\\CyGPUInspectorTest.control";

		cygi::ControlPipeServer server;
		Check(server.Start(pipe_name), "the server starts");

		cygi::ControlPipeClient client;
		bool connected = false;
		for (int attempt = 0; attempt < 50 && !connected; ++attempt)
		{
			connected = client.Connect(pipe_name, 200);
			if (!connected)
				Sleep(20);
		}
		Check(connected, "the client connects");

		cygi::HelloRequest hello = {};
		hello.protocol_version = cygi::kProtocolVersion;
		hello.app_process_id = GetCurrentProcessId();
		Check(client.Send(cygi::ControlType::hello, 1234, &hello, sizeof(hello)), "the client sends a request");

		cygi::ControlMessage received;
		bool got_request = false;
		for (int attempt = 0; attempt < 100 && !got_request; ++attempt)
		{
			got_request = server.PopMessage(received);
			if (!got_request)
				Sleep(10);
		}
		Check(got_request, "the server receives the request");
		Check(received.type == cygi::ControlType::hello, "the request type survives");
		Check(received.request_id == 1234, "the request id survives");

		const cygi::HelloRequest *payload = received.As<cygi::HelloRequest>();
		Check(payload != nullptr && payload->app_process_id == GetCurrentProcessId(),
			"the request payload survives");

		Check(server.SendAck(received.request_id, true, "welcome"), "the server answers");

		cygi::ControlMessage reply;
		Check(client.Receive(reply, 2000), "the client receives the answer");
		Check(reply.type == cygi::ControlType::ack, "the answer type survives");
		Check(reply.request_id == 1234, "the answer carries the request id");

		const cygi::ControlAck *ack = reply.As<cygi::ControlAck>();
		Check(ack != nullptr && ack->succeeded != 0, "the answer says it succeeded");
		Check(ack != nullptr && ack->message_length == 7, "the trailing text length survives");
		Check(ack != nullptr && std::memcmp(reply.Blob(sizeof(cygi::ControlAck)), "welcome", 7) == 0,
			"the trailing text survives");

		client.Disconnect();
		server.Stop();
		Check(!server.IsRunning(), "the server stops");
	}

	void TestSharedMemory()
	{
		std::printf("Shared memory\n");

		cygi::SharedMemory creator;
		bool created = false;
		Check(creator.Create("Local\\CyGPUInspectorTest.shm", 4096, &created), "a mapping is created");
		Check(created, "the first Create reports a new mapping");
		std::memset(creator.Data(), 0x5A, 4096);

		cygi::SharedMemory opener;
		Check(opener.Open("Local\\CyGPUInspectorTest.shm", 4096), "the mapping can be opened again");
		Check(static_cast<const uint8_t *>(opener.Data())[100] == 0x5A, "both views see the same bytes");

		cygi::SharedMemory second;
		bool created_again = true;
		Check(second.Create("Local\\CyGPUInspectorTest.shm", 4096, &created_again), "Create opens an existing mapping");
		Check(!created_again, "Create reports that the mapping already existed");
	}
}

	// Translation. What is checked is the property the whole design rests on: a string with no
	// translation comes back as the English it was keyed on, so a half finished catalogue can
	// never produce a blank label or a raw key.
	void TestLocalization()
	{
		std::printf("Localization\n");

		const std::filesystem::path root =
			std::filesystem::temp_directory_path() / "CyGPUInspectorLangTest";
		std::error_code code;
		std::filesystem::remove_all(root, code);
		std::filesystem::create_directories(root, code);

		{
			// A deliberately partial catalogue: one translated, one left empty, one absent.
			cygi::Json strings = cygi::Json::Object();
			strings["Shaders"] = cygi::Json(std::string("Nuanceurs"));
			strings["Resources"] = cygi::Json(std::string());
			strings["%u draws"] = cygi::Json(std::string("%u dessins"));

			cygi::Json root_json = cygi::Json::Object();
			root_json["language"] = cygi::Json(std::string("xt"));
			root_json["name"] = cygi::Json(std::string("Test language"));
			root_json["strings"] = strings;

			const std::string text = root_json.Write(1);
			std::ofstream file(root / "xt.json", std::ios::binary | std::ios::trunc);
			file.write(text.data(), static_cast<std::streamsize>(text.size()));
		}

		cygi::i18n::SetLanguageRoot(root.string());
		cygi::i18n::ScanLanguages();

		bool found = false;
		for (const cygi::i18n::LanguageInfo &language : cygi::i18n::Languages())
			if (language.code == "xt" && language.name == "Test language")
				found = true;
		Check(found, "a catalogue dropped in the folder is discovered");
		Check(cygi::i18n::Languages().size() == 2, "English is listed beside it");
		Check(cygi::i18n::Languages()[0].code == "en", "and English comes first");
		Check(cygi::i18n::CurrentLanguage() == "en", "English is what is active by default");
		Check(std::string(cygi::i18n::Tr("Shaders")) == "Shaders", "English returns the source");

		Check(cygi::i18n::SetLanguage("xt"), "the language can be switched");
		Check(cygi::i18n::IsTranslated(), "and it reports that it is no longer English");
		Check(std::string(cygi::i18n::Tr("Shaders")) == "Nuanceurs", "a translated string is translated");
		Check(std::string(cygi::i18n::Tr("%u draws")) == "%u dessins",
			"a format string keeps its conversions");
		Check(std::string(cygi::i18n::Tr("Resources")) == "Resources",
			"an empty translation falls back to the English, not to a blank");
		Check(std::string(cygi::i18n::Tr("Never translated anywhere")) == "Never translated anywhere",
			"a string absent from the catalogue falls back to the English");

		// The identity ImGui hashes is the part after ###, so a window keeps its layout when the
		// language changes. That is why TrId exists at all.
		const std::string id = cygi::i18n::TrId("Shaders");
		Check(id == "Nuanceurs###Shaders", "TrId keeps the English as the widget identity");
		Check(id.find("###Shaders") != std::string::npos, "so a saved layout survives a translation");

		Check(!cygi::i18n::SetLanguage("zz"), "an unknown language is refused");
		Check(cygi::i18n::CurrentLanguage() == "xt", "and the current one is left alone");

		const std::string template_path = (root / "written.json").string();
		std::string error;
		Check(cygi::i18n::WriteTemplate(template_path, error), "a template can be written from a run");
		Check(cygi::i18n::SeenCount() >= 4, "it covers every string the run asked for");
		Check(cygi::i18n::MissingCount() >= 2, "including the ones with no translation yet");

		// Back to English so the rest of the suite reads normally.
		cygi::i18n::SetLanguage("en");
		std::filesystem::remove_all(root, code);
	}

int main()
{
	std::printf("CyGPUInspectorCoreTests (protocol version %u)\n\n", cygi::kProtocolVersion);

	TestSha256();
	TestRingBuffer();
	TestShaderBlob();
	TestSessionDirectory();
	TestSharedMemory();
	TestControlPipe();
	TestJson();
	TestLocalization();

	std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

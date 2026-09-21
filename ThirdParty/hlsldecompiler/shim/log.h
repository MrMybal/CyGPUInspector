// Shim for the 3Dmigoto decompiler's logging.
//
// The vendored sources call LogInfo, LogDebug and LogTime, which in 3Dmigoto write into that
// tool's own log file inside a game process. Here the decompiler runs in CyGPUInspectorApp, so
// the messages are collected in memory and attached to the decompilation result: a warning that
// the shader could not be fully translated belongs with the shader, not in a log file nobody
// opens.
//
// This file is NOT from 3Dmigoto. It exists only so the 3Dmigoto sources can stay unmodified.
//
// Copyright (C) 2026 CyberAlien. Licensed under the GNU GPL v3 or later.
#pragma once

#include <string>

namespace cygi::hlsldecompiler
{
	// Set by the backend around a decompilation, read back afterwards.
	void BeginCapture();
	std::string EndCapture();

	void Write(const char *format, ...);
	void WriteDebug(const char *format, ...);
}

// 3Dmigoto guards its debug logging with this global.
extern bool gLogDebug;

#define LogInfo  ::cygi::hlsldecompiler::Write
#define LogDebug ::cygi::hlsldecompiler::WriteDebug

// 3Dmigoto stamps the generated HLSL with the time it was produced.
std::string LogTime();

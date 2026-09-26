// CyGPUInspectorApp — an HLSL text editor: colours, line numbers, undo, find, compiler errors.
//
// A thin layer over ImGuiColorTextEdit (MIT, ThirdParty/ImGuiColorTextEdit) with its HLSL language
// definition. It is used for the HLSL tab, the HLSL editor window and the read-only views of
// reconstructed code, so all of them colour code the same way.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "CompilerMessages.hpp"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class TextEditor;

namespace cygi
{
	class HlslEditor
	{
	public:
		HlslEditor();
		~HlslEditor();
		HlslEditor(const HlslEditor &) = delete;
		HlslEditor &operator=(const HlslEditor &) = delete;

		// Replaces the document, its undo history and its marks.
		void SetText(const std::string &text);
		std::string GetText() const;
		bool IsEmpty() const;

		// Draws the editor in a child of `size` (0 = all the room left).
		void Render(const char *id, const ImVec2 &size = ImVec2(0.0f, 0.0f));

		void SetReadOnly(bool read_only);
		void SetWordWrap(bool wrap);
		bool IsWordWrap() const;
		void SetShowWhitespace(bool show);
		bool IsShowWhitespace() const;
		void SetShowMiniMap(bool show);
		bool IsShowMiniMap() const;

		// Marks every line a compiler complained about, with its message as a tooltip. Returns the
		// messages, so the caller can list them.
		std::vector<CompilerMessage> MarkCompilerMessages(const std::string &messages);
		void ClearMarks();

		void GoToLine(size_t line);   // one-based
		void Focus();
		void OpenFind();
		void Undo();
		void Redo();
		bool CanUndo() const;
		bool CanRedo() const;
		void SelectAll();

		size_t CursorLine() const;     // one-based
		size_t CursorColumn() const;   // one-based
		size_t LineCount() const;
		// True once the text differs from what SetText put there: an edit not undone since.
		bool IsModified() const;

	private:
		std::unique_ptr<TextEditor> m_editor;
		size_t m_loaded_undo = 0;   // the undo position right after SetText
	};
}

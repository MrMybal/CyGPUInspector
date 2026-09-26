// CyGPUInspectorApp — an HLSL text editor: colours, line numbers, undo, find, compiler errors.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "HlslEditor.hpp"

#include <TextEditor.h>

#include <algorithm>

namespace cygi
{
	HlslEditor::HlslEditor() : m_editor(std::make_unique<TextEditor>())
	{
		m_editor->SetLanguage(TextEditor::Language::Hlsl());
		m_editor->SetPalette(TextEditor::GetDarkPalette());
		m_editor->SetTabSize(4);
		m_editor->SetShowLineNumbersEnabled(true);
		m_editor->SetShowMatchingBrackets(true);
		m_editor->SetShowScrollbarMiniMapEnabled(true);
	}

	HlslEditor::~HlslEditor() = default;

	void HlslEditor::SetText(const std::string &text)
	{
		m_editor->SetText(std::string_view(text));
		// Not a change callback: the widget reports loading a text as a change too. The undo
		// history does not, and undoing every edit brings the document back to "not modified".
		m_loaded_undo = m_editor->GetUndoIndex();
	}

	bool HlslEditor::IsModified() const
	{
		return m_editor->GetUndoIndex() != m_loaded_undo;
	}

	std::string HlslEditor::GetText() const { return m_editor->GetText(); }
	bool HlslEditor::IsEmpty() const { return m_editor->IsEmpty(); }

	void HlslEditor::Render(const char *id, const ImVec2 &size)
	{
		m_editor->Render(id, size);
	}

	void HlslEditor::SetReadOnly(bool read_only) { m_editor->SetReadOnlyEnabled(read_only); }
	void HlslEditor::SetWordWrap(bool wrap) { m_editor->SetWordWrapEnabled(wrap); }
	bool HlslEditor::IsWordWrap() const { return m_editor->IsWordWrapEnabled(); }
	void HlslEditor::SetShowWhitespace(bool show) { m_editor->SetShowWhitespacesEnabled(show); }
	bool HlslEditor::IsShowWhitespace() const { return m_editor->IsShowWhitespacesEnabled(); }
	void HlslEditor::SetShowMiniMap(bool show) { m_editor->SetShowMiniMapEnabled(show); }
	bool HlslEditor::IsShowMiniMap() const { return m_editor->IsShowMiniMapEnabled(); }

	std::vector<CompilerMessage> HlslEditor::MarkCompilerMessages(const std::string &messages)
	{
		m_editor->ClearMarkers();
		std::vector<CompilerMessage> parsed = ParseCompilerMessages(messages);
		for (const CompilerMessage &message : parsed)
		{
			if (message.line == 0 || message.line > m_editor->GetLineCount())
				continue;
			const ImU32 number = message.error ? IM_COL32(230, 80, 70, 255) : IM_COL32(230, 180, 60, 255);
			const ImU32 text = message.error ? IM_COL32(230, 80, 70, 60) : IM_COL32(230, 180, 60, 45);
			m_editor->AddMarker(message.line - 1, number, text, message.text, message.text);
		}
		return parsed;
	}

	void HlslEditor::ClearMarks() { m_editor->ClearMarkers(); }

	void HlslEditor::GoToLine(size_t line)
	{
		if (line == 0 || m_editor->GetLineCount() == 0)
			return;
		const size_t target = std::min(line, m_editor->GetLineCount()) - 1;
		m_editor->SetCursor(TextEditor::DocPos(target, 0));
		m_editor->ScrollToLine(target);
		m_editor->SetFocus();
	}

	void HlslEditor::Focus() { m_editor->SetFocus(); }
	void HlslEditor::OpenFind() { m_editor->OpenFindReplaceWindow(); }
	void HlslEditor::Undo() { m_editor->Undo(); }
	void HlslEditor::Redo() { m_editor->Redo(); }
	bool HlslEditor::CanUndo() const { return m_editor->CanUndo(); }
	bool HlslEditor::CanRedo() const { return m_editor->CanRedo(); }
	void HlslEditor::SelectAll() { m_editor->SelectAll(); }

	size_t HlslEditor::CursorLine() const { return m_editor->GetMainCursorPosition().line + 1; }
	size_t HlslEditor::CursorColumn() const { return m_editor->GetMainCursorPosition().index + 1; }
	size_t HlslEditor::LineCount() const { return m_editor->GetLineCount(); }
}

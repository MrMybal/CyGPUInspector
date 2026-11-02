// CyGPUInspectorApp — an HLSL text editor: colours, line numbers, undo, find, compiler errors.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "HlslEditor.hpp"

#include <TextEditor.h>
#include <cyui/modules/CodeEditor.h>

#include <algorithm>
#include <CyGPUInspectorCore/Localization.hpp>

namespace cygi
{
	HlslEditor::HlslEditor() : m_editor(std::make_unique<cyui::CodeEditor>())
	{
		m_editor->setLanguage(cyui::CodeLanguage::hlsl);
		m_editor->native().SetTabSize(4);
		m_editor->native().SetShowLineNumbersEnabled(true);
		m_editor->native().SetShowMatchingBrackets(true);
		m_editor->native().SetShowScrollbarMiniMapEnabled(true);
		// Dots for every space make code harder to read, not easier; View > Show whitespace.
		m_editor->native().SetShowWhitespacesEnabled(false);
	}

	HlslEditor::~HlslEditor() = default;

	void HlslEditor::SetText(const std::string &text)
	{
		m_editor->native().SetText(std::string_view(text));
		m_editor->setText(text);
		// Not a change callback: the widget reports loading a text as a change too. The undo
		// history does not, and undoing every edit brings the document back to "not modified".
		m_loaded_undo = m_editor->native().GetUndoIndex();
	}

	bool HlslEditor::IsModified() const
	{
		return m_editor->native().GetUndoIndex() != m_loaded_undo;
	}

	std::string HlslEditor::GetText() const { return m_editor->text(); }
	bool HlslEditor::IsEmpty() const { return m_editor->native().IsEmpty(); }

	void HlslEditor::Render(const char *id, const ImVec2 &size)
	{
		// Code's lexical preparation is bounded. Keep the complete editable document when
		// it exceeds that bound, with an explicit notice rather than truncation or an exception.
		const auto revision = m_editor->native().GetTextRevision();
		if (revision != m_checked_revision)
		{
			const bool available = m_editor->text().size() <= 32u * 1024u * 1024u;
			if (available != m_syntax_available) m_editor->setSyntaxEnabled(available);
			m_syntax_available = available;
			m_checked_revision = revision;
		}
		ImVec2 available = size;
		if (!m_syntax_available)
		{
			cyui::textDim("%s", i18n::Tr("Syntax highlighting is limited to 32 MiB; the full document remains editable."));
			if (available.y > 0) available.y = std::max(ImGui::GetFrameHeight(), available.y - ImGui::GetTextLineHeightWithSpacing());
		}
		m_editor->draw(id, available);
	}

	void HlslEditor::SetReadOnly(bool read_only) { m_editor->native().SetReadOnlyEnabled(read_only); }
	void HlslEditor::SetWordWrap(bool wrap) { m_editor->native().SetWordWrapEnabled(wrap); }
	bool HlslEditor::IsWordWrap() const { return m_editor->native().IsWordWrapEnabled(); }
	void HlslEditor::SetShowWhitespace(bool show) { m_editor->native().SetShowWhitespacesEnabled(show); }
	bool HlslEditor::IsShowWhitespace() const { return m_editor->native().IsShowWhitespacesEnabled(); }
	void HlslEditor::SetShowMiniMap(bool show) { m_editor->native().SetShowMiniMapEnabled(show); }
	bool HlslEditor::IsShowMiniMap() const { return m_editor->native().IsShowMiniMapEnabled(); }

	std::vector<CompilerMessage> HlslEditor::MarkCompilerMessages(const std::string &messages)
	{
		m_editor->native().ClearMarkers();
		std::vector<CompilerMessage> parsed = ParseCompilerMessages(messages);
		for (const CompilerMessage &message : parsed)
		{
			if (message.line == 0 || message.line > m_editor->native().GetLineCount())
				continue;
			const ImU32 number = message.error ? cyui::palette().error : cyui::palette().warning;
			const ImU32 text = cyui::alpha(number, 0.2f);
			m_editor->native().AddMarker(message.line - 1, number, text, message.text, message.text);
		}
		return parsed;
	}

	void HlslEditor::ClearMarks() { m_editor->native().ClearMarkers(); }

	void HlslEditor::GoToLine(size_t line)
	{
		if (line == 0 || m_editor->native().GetLineCount() == 0)
			return;
		const size_t target = std::min(line, m_editor->native().GetLineCount()) - 1;
		m_editor->native().SetCursor(TextEditor::DocPos(target, 0));
		m_editor->native().ScrollToLine(target);
		m_editor->native().SetFocus();
	}

	void HlslEditor::Focus() { m_editor->native().SetFocus(); }
	void HlslEditor::OpenFind() { m_editor->native().OpenFindReplaceWindow(); }
	void HlslEditor::Undo() { m_editor->native().Undo(); }
	void HlslEditor::Redo() { m_editor->native().Redo(); }
	bool HlslEditor::CanUndo() const { return m_editor->native().CanUndo(); }
	bool HlslEditor::CanRedo() const { return m_editor->native().CanRedo(); }
	void HlslEditor::SelectAll() { m_editor->native().SelectAll(); }

	size_t HlslEditor::CursorLine() const { return m_editor->native().GetMainCursorPosition().line + 1; }
	size_t HlslEditor::CursorColumn() const { return m_editor->native().GetMainCursorPosition().index + 1; }
	size_t HlslEditor::LineCount() const { return m_editor->native().GetLineCount(); }
}

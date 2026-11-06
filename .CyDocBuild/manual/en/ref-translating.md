---
id: ref-translating
title: Translating the interface
description: Technical detail: Find panels, follow selections and adjust appearance or language.
section: Technical reference
order: 36
parent: navigation
revision: 6c5e7d61f6e6
---

# Translating CyGPUInspector {#translating-cygpuinspector}



English is the base language. The code, the comments, these documents and every string in the
interface are written in English first, and that is what is right when a translation disagrees
with it.

---

## 1. The interface {#1-the-interface}

### The idea {#the-idea}

A key is **the English sentence itself**, not an invented identifier like `ui.shaders.title`. That
one decision is what makes the rest work:

* a missing or half-finished translation shows English — never a blank label, never a raw key;
* nothing has to be named, so wrapping a new string costs one call and no bookkeeping;
* a translator reads the English and writes the other language beside it, with the context right
  there instead of in a separate document.

```cpp
ImGui::TextUnformatted(Tr("Applications running CyGPUInspectorRS"));
if (ImGui::Button(TrId("Capture now")))
    StartDeepCapture();
```

`Tr` is for text. `TrId` is for anything ImGui also uses as an identifier — a window title, a
button, a table column. It appends the English as a hidden `###` suffix, and ImGui hashes only the
part after `###`, so **a saved layout survives a language change**: an English user and a French
user share one `CyGPUInspectorApp.ini`.

### Adding a language {#adding-a-language}

1. Copy `Lang/template.json` to `Lang/<code>.json`, where `<code>` is something like `de` or `ja`.
2. Set `language` to that code and `name` to what the language calls itself — `Deutsch`, not
   `German`. That is what the menu shows.
3. Fill in the values. Leave one empty to keep the English for that string.
4. Restart the application. No rebuild: the folder is scanned at start-up, and the language appears
   under **View → Language**.

Editing a catalogue that lives in the repository's `Lang/` needs a build, because that is what
copies it next to the executable — a plain `build.cmd` is enough, and it copies even when nothing
else changed. Editing the copy beside the executable directly needs nothing but a restart.

The choice is remembered in `CyGPUInspectorApp.settings.json`, next to the executable.

### Rules a translation has to respect {#rules-a-translation-has-to-respect}

* **Keep every `%s`, `%u`, `%llu`, `%.2f` and so on, in the same order.** They are where numbers
  and names are inserted. Dropping one, or swapping two, makes the interface print nonsense or read
  past the arguments it was given.
* **Do not translate anything after `###`.** That is the widget's identity, not text.
* `\n` is a line break. Keep it where the English has it.
* Text is allowed to get longer. The panels reflow; nothing is laid out to a fixed width.

### Keeping a catalogue up to date {#keeping-a-catalogue-up-to-date}

The application writes a template from a **real run**: **View → Language → Write translation
template…** dumps every string that has actually been asked for since it started, into
`Lang/template.json`, with the current language's translations already filled in. Open the panels
you care about first — a string the interface never displayed is a string the run never saw.

The same menu shows how many strings have been displayed so far and how many of them had no
translation, which is how you tell "finished" from "looks finished".

### The add-on overlays {#the-add-on-overlays}

The two ReShade overlays — the capture agent's and CyGPUInjector's — go through the same `Tr`, and
through the same catalogues. They are small: a few dozen strings each.

---

## 2. The documentation {#2-the-documentation}

The application language and documentation language are independent. Use View > Language for application widgets and the selector in the documentation header for the manual. English is the original and default documentation language. Both English and French editions are complete; choosing French preserves the page and section.

## 3. What is deliberately not translated {#3-what-is-deliberately-not-translated}

* **The code, and every comment in it.** One language for the source, and it is English.
* **Log lines and error strings from the add-ons.** They end up in `ReShade.log`, which is what
  gets pasted into a bug report; an English log can be read by whoever is helping.
* **The MCP tool names and their descriptions.** They are an API surface that agents match on.
* **Format-only strings** such as `%s`, `RT%u` or `%.3f ms`. There is nothing in them to translate,
  and the catalogue leaves them out rather than repeating them.

---

## 4. Where this lives in the code {#4-where-this-lives-in-the-code}

Language catalogues are editable JSON files under `Lang/` beside the executable. Keep the English keys stable and use the template action to discover new labels.

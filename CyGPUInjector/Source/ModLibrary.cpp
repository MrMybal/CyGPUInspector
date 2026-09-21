// CyGPUInjector — loading mod packages and resolving them into a single lookup.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "ModLibrary.hpp"

#include "Log.hpp"

#include <algorithm>
#include <filesystem>

namespace cygi
{
	ModLibrary &Library()
	{
		static ModLibrary library;
		return library;
	}

	uint32_t ModLibrary::LoadFrom(const std::string &root)
	{
		m_root = root;
		m_packages.clear();

		std::error_code code;
		if (!std::filesystem::is_directory(root, code))
		{
			log::Info("No mod folder at %s, nothing to apply", root.c_str());
			Rebuild();
			return 0;
		}

		std::vector<std::filesystem::path> candidates;
		for (const std::filesystem::directory_entry &entry :
		     std::filesystem::directory_iterator(root, code))
		{
			if (!entry.is_directory(code))
				continue;
			if (entry.path().extension() != ModPackage::kExtension)
				continue;
			candidates.push_back(entry.path());
		}

		// Alphabetical, so that two packages touching the same shader resolve the same way on
		// every machine. Load order decides who wins, and the overlay says who did.
		std::sort(candidates.begin(), candidates.end());

		uint32_t loaded = 0;
		for (const std::filesystem::path &path : candidates)
		{
			LoadedPackage entry;
			entry.directory = path.string();

			std::string error;
			if (ModPackage::Load(entry.directory, entry.package, error))
			{
				entry.loaded = true;
				++loaded;
				log::Info("Loaded \"%s\" by %s: %u replacement(s), %u disable(s)",
					entry.package.name.c_str(), entry.package.author.c_str(),
					entry.package.CountOf(ModAction::replace),
					entry.package.CountOf(ModAction::disable));
			}
			else
			{
				// A broken package must not take the others down with it, and the reason has to
				// reach the user rather than only the log.
				entry.error = error;
				log::Warning("Could not load %s: %s", entry.directory.c_str(), error.c_str());
			}

			m_packages.push_back(std::move(entry));
		}

		Rebuild();
		return loaded;
	}

	void ModLibrary::Rebuild()
	{
		m_rules.clear();
		m_any_disable = false;

		for (size_t package_index = 0; package_index < m_packages.size(); ++package_index)
		{
			LoadedPackage &package = m_packages[package_index];
			package.matched_shaders = 0;
			if (!package.loaded || !package.enabled)
				continue;

			for (size_t entry_index = 0; entry_index < package.package.entries.size(); ++entry_index)
			{
				const ModEntry &entry = package.package.entries[entry_index];

				ModRule rule;
				rule.action = entry.action;
				rule.package_index = package_index;
				rule.entry_index = entry_index;
				if (entry.action == ModAction::replace)
				{
					rule.byte_code = entry.byte_code.data();
					rule.byte_code_size = entry.byte_code.size();
				}
				else
				{
					m_any_disable = true;
				}

				// Two packages asking for the same shader: the first one alphabetically keeps it.
				// Silently letting the second overwrite the first would make the result depend on
				// something nobody can see.
				const auto existing = m_rules.find(entry.semantic_hash);
				if (existing != m_rules.end())
				{
					log::Warning("\"%s\" also modifies %s, which \"%s\" already claims: ignored",
						package.package.name.c_str(), entry.semantic_hash.ToShortHex(8).c_str(),
						m_packages[existing->second.package_index].package.name.c_str());
					continue;
				}

				m_rules.emplace(entry.semantic_hash, rule);
			}
		}
	}

	const ModRule *ModLibrary::Find(const Sha256Digest &semantic_hash) const
	{
		const auto found = m_rules.find(semantic_hash);
		return found != m_rules.end() ? &found->second : nullptr;
	}

	void ModLibrary::CountReplacement(size_t package_index, size_t entry_index)
	{
		m_replacements.fetch_add(1, std::memory_order_relaxed);
		(void)entry_index;
		if (package_index < m_packages.size())
			++m_packages[package_index].matched_shaders;
	}

	void ModLibrary::ResetCounters()
	{
		m_replacements.store(0, std::memory_order_relaxed);
		m_draws_skipped.store(0, std::memory_order_relaxed);
		for (LoadedPackage &package : m_packages)
			package.matched_shaders = 0;
	}
}

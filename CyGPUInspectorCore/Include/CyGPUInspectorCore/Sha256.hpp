// CyGPUInspector — SHA-256, used for persistent shader signatures.
//
// Self contained (no OpenSSL / bcrypt dependency) because this code also runs inside the
// add-on, which is loaded into arbitrary game processes and must stay dependency free.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace cygi
{
	// A shader signature: the raw 32 bytes of a SHA-256 digest.
	struct Sha256Digest
	{
		std::array<uint8_t, 32> bytes{};

		bool operator==(const Sha256Digest &other) const { return bytes == other.bytes; }
		bool operator!=(const Sha256Digest &other) const { return bytes != other.bytes; }

		// Lowercase hex, 64 characters.
		std::string ToHex() const;
		// First `chars` hex characters, for display ("A84DF290...").
		std::string ToShortHex(size_t chars = 8) const;

		bool IsZero() const;
		static bool FromHex(const std::string &hex, Sha256Digest &out);
	};

	class Sha256
	{
	public:
		Sha256();
		void Update(const void *data, size_t size);
		Sha256Digest Finish();

		static Sha256Digest Hash(const void *data, size_t size);

	private:
		void Transform(const uint8_t block[64]);

		uint32_t m_state[8];
		uint64_t m_bit_count;
		uint8_t m_buffer[64];
		size_t m_buffer_size;
	};

	// Hash helper for std::unordered_map<Sha256Digest, ...>.
	struct Sha256DigestHasher
	{
		size_t operator()(const Sha256Digest &digest) const noexcept
		{
			size_t value = 0;
			// The digest is already uniformly distributed: just fold the first 8 bytes.
			for (int i = 0; i < 8; ++i)
				value = (value << 8) | digest.bytes[i];
			return value;
		}
	};
}

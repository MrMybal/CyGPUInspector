// CyGPUInspector — SHA-256 implementation (FIPS 180-4).
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorCore/Sha256.hpp"

#include <cstring>

namespace cygi
{
	namespace
	{
		constexpr uint32_t kK[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

		inline uint32_t Ror(uint32_t value, uint32_t bits) { return (value >> bits) | (value << (32 - bits)); }
	}

	Sha256::Sha256()
	{
		m_state[0] = 0x6a09e667; m_state[1] = 0xbb67ae85; m_state[2] = 0x3c6ef372; m_state[3] = 0xa54ff53a;
		m_state[4] = 0x510e527f; m_state[5] = 0x9b05688c; m_state[6] = 0x1f83d9ab; m_state[7] = 0x5be0cd19;
		m_bit_count = 0;
		m_buffer_size = 0;
		std::memset(m_buffer, 0, sizeof(m_buffer));
	}

	void Sha256::Transform(const uint8_t block[64])
	{
		uint32_t w[64];
		for (int i = 0; i < 16; ++i)
			w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
			       (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
		for (int i = 16; i < 64; ++i)
		{
			const uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}

		uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
		uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];

		for (int i = 0; i < 64; ++i)
		{
			const uint32_t s1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
			const uint32_t ch = (e & f) ^ (~e & g);
			const uint32_t temp1 = h + s1 + ch + kK[i] + w[i];
			const uint32_t s0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
			const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
			const uint32_t temp2 = s0 + maj;

			h = g; g = f; f = e; e = d + temp1;
			d = c; c = b; b = a; a = temp1 + temp2;
		}

		m_state[0] += a; m_state[1] += b; m_state[2] += c; m_state[3] += d;
		m_state[4] += e; m_state[5] += f; m_state[6] += g; m_state[7] += h;
	}

	void Sha256::Update(const void *data, size_t size)
	{
		const uint8_t *bytes = static_cast<const uint8_t *>(data);
		m_bit_count += static_cast<uint64_t>(size) * 8;

		while (size > 0)
		{
			const size_t take = (size < 64 - m_buffer_size) ? size : 64 - m_buffer_size;
			std::memcpy(m_buffer + m_buffer_size, bytes, take);
			m_buffer_size += take;
			bytes += take;
			size -= take;

			if (m_buffer_size == 64)
			{
				Transform(m_buffer);
				m_buffer_size = 0;
			}
		}
	}

	Sha256Digest Sha256::Finish()
	{
		const uint64_t bit_count = m_bit_count;

		uint8_t padding = 0x80;
		Update(&padding, 1);
		m_bit_count = bit_count; // Update() above must not count the padding.

		padding = 0x00;
		while (m_buffer_size != 56)
		{
			Update(&padding, 1);
			m_bit_count = bit_count;
		}

		uint8_t length[8];
		for (int i = 0; i < 8; ++i)
			length[i] = static_cast<uint8_t>((bit_count >> (56 - i * 8)) & 0xFF);
		std::memcpy(m_buffer + 56, length, 8);
		Transform(m_buffer);
		m_buffer_size = 0;

		Sha256Digest digest;
		for (int i = 0; i < 8; ++i)
		{
			digest.bytes[i * 4 + 0] = static_cast<uint8_t>((m_state[i] >> 24) & 0xFF);
			digest.bytes[i * 4 + 1] = static_cast<uint8_t>((m_state[i] >> 16) & 0xFF);
			digest.bytes[i * 4 + 2] = static_cast<uint8_t>((m_state[i] >> 8) & 0xFF);
			digest.bytes[i * 4 + 3] = static_cast<uint8_t>(m_state[i] & 0xFF);
		}
		return digest;
	}

	Sha256Digest Sha256::Hash(const void *data, size_t size)
	{
		Sha256 sha;
		sha.Update(data, size);
		return sha.Finish();
	}

	std::string Sha256Digest::ToHex() const
	{
		static const char *kHex = "0123456789abcdef";
		std::string out(64, '0');
		for (size_t i = 0; i < bytes.size(); ++i)
		{
			out[i * 2 + 0] = kHex[(bytes[i] >> 4) & 0xF];
			out[i * 2 + 1] = kHex[bytes[i] & 0xF];
		}
		return out;
	}

	std::string Sha256Digest::ToShortHex(size_t chars) const
	{
		const std::string hex = ToHex();
		return hex.substr(0, chars < hex.size() ? chars : hex.size());
	}

	bool Sha256Digest::IsZero() const
	{
		for (uint8_t byte : bytes)
			if (byte != 0)
				return false;
		return true;
	}

	bool Sha256Digest::FromHex(const std::string &hex, Sha256Digest &out)
	{
		if (hex.size() != 64)
			return false;

		auto nibble = [](char c, uint8_t &value) {
			if (c >= '0' && c <= '9') { value = static_cast<uint8_t>(c - '0'); return true; }
			if (c >= 'a' && c <= 'f') { value = static_cast<uint8_t>(c - 'a' + 10); return true; }
			if (c >= 'A' && c <= 'F') { value = static_cast<uint8_t>(c - 'A' + 10); return true; }
			return false;
		};

		for (size_t i = 0; i < 32; ++i)
		{
			uint8_t high = 0, low = 0;
			if (!nibble(hex[i * 2], high) || !nibble(hex[i * 2 + 1], low))
				return false;
			out.bytes[i] = static_cast<uint8_t>((high << 4) | low);
		}
		return true;
	}
}

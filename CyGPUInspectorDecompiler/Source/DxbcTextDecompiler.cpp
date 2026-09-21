// CyGPUInspectorDecompiler — "cygi-dxbc": register level DXBC reconstruction.
//
// The first backend of the multi-backend architecture. It translates the DXBC assembly produced by
// D3DDisassemble instruction by instruction into HLSL that keeps the original register structure,
// and rebuilds the declarations from the shader reflection so the result compiles.
//
// What it does NOT do, on purpose, and what other backends are meant to do better:
//   * it does not reconstruct expressions: every instruction stays one statement;
//   * it does not recover variable names, structures or algorithms;
//   * it keeps the original control flow shape rather than restructuring it.
//
// That is why the output is honest to read and useful to diff against the disassembly, and why
// nothing here ever claims to be the original source.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorDecompiler/Decompiler.hpp"

#include <CyGPUInspectorCore/ShaderBlob.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <unordered_map>

namespace cygi
{
	namespace
	{
		std::string Trim(const std::string &text)
		{
			size_t begin = 0;
			size_t end = text.size();
			while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
				++begin;
			while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
				--end;
			return text.substr(begin, end - begin);
		}

		bool StartsWith(const std::string &text, const char *prefix)
		{
			return text.compare(0, std::strlen(prefix), prefix) == 0;
		}

		// Splits "a, b, c" respecting brackets and parentheses.
		std::vector<std::string> SplitOperands(const std::string &text)
		{
			std::vector<std::string> operands;
			int depth = 0;
			std::string current;
			for (char c : text)
			{
				if (c == '(' || c == '[')
					++depth;
				else if (c == ')' || c == ']')
					--depth;

				if (c == ',' && depth == 0)
				{
					operands.push_back(Trim(current));
					current.clear();
					continue;
				}
				current += c;
			}
			if (!Trim(current).empty())
				operands.push_back(Trim(current));
			return operands;
		}

		// D3DDisassemble prefixes every line with an instruction number and a byte offset:
		//   "3  0x000000C4: div r1.xyz, r0.xyzx, cb0[0].yyyy"
		// Both are useful in the viewer and noise here.
		std::string StripLinePrefix(const std::string &line)
		{
			auto skip_spaces = [&line](size_t cursor) {
				while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])))
					++cursor;
				return cursor;
			};

			// A byte offset may come first, on the declaration lines. It has to be tested before
			// the instruction number, because "0x..." also starts with a digit.
			if (line.compare(0, 2, "0x") == 0)
			{
				const size_t colon = line.find(':');
				return colon != std::string::npos ? line.substr(skip_spaces(colon + 1)) : line;
			}

			size_t cursor = 0;
			while (cursor < line.size() && std::isdigit(static_cast<unsigned char>(line[cursor])))
				++cursor;
			if (cursor == 0)
				return line;   // no prefix at all

			cursor = skip_spaces(cursor);
			if (line.compare(cursor, 2, "0x") == 0)
			{
				const size_t colon = line.find(':', cursor);
				if (colon == std::string::npos)
					return line;
				return line.substr(skip_spaces(colon + 1));
			}
			if (cursor < line.size() && line[cursor] == ':')
				return line.substr(skip_spaces(cursor + 1));

			return line;
		}

		// "v1.xy" -> "v[1].xy", "o0.xyzw" -> "o[0].xyzw". Registers keep their names otherwise.
		std::string TranslateOperand(const std::string &raw)
		{
			std::string operand = Trim(raw);
			if (operand.empty())
				return operand;

			// Negation and absolute value modifiers.
			std::string prefix;
			std::string suffix;
			if (StartsWith(operand, "-"))
			{
				prefix = "-";
				operand = operand.substr(1);
			}
			if (StartsWith(operand, "abs("))
			{
				const size_t close = operand.rfind(')');
				if (close != std::string::npos)
				{
					prefix += "abs(";
					suffix = ")";
					operand = operand.substr(4, close - 4);
				}
			}

			// Immediate values: "l(1.000000, 1.000000, 1.000000, 0.000000)" -> "float4(1.0, ...)".
			if (StartsWith(operand, "l(") && operand.back() == ')')
			{
				const std::vector<std::string> values = SplitOperands(operand.substr(2, operand.size() - 3));
				std::string literal;
				if (values.size() == 1)
				{
					literal = values[0];
				}
				else
				{
					literal = "float" + std::to_string(values.size()) + "(";
					for (size_t i = 0; i < values.size(); ++i)
						literal += (i != 0 ? ", " : "") + values[i];
					literal += ")";
				}
				return prefix + literal + suffix;
			}

			// Indexed input and output registers become arrays, which is what they are.
			if ((operand[0] == 'v' || operand[0] == 'o') && operand.size() > 1 &&
			    std::isdigit(static_cast<unsigned char>(operand[1])))
			{
				size_t index_end = 1;
				while (index_end < operand.size() && std::isdigit(static_cast<unsigned char>(operand[index_end])))
					++index_end;
				operand = std::string(1, operand[0]) + "[" + operand.substr(1, index_end - 1) + "]" +
					operand.substr(index_end);
			}

			return prefix + operand + suffix;
		}

		// Rewrites the swizzle of an operand so it yields exactly `components` channels.
		std::string Truncate(const std::string &operand, int components)
		{
			static const char *const kSwizzle = "xyzw";
			const size_t dot = operand.rfind('.');

			std::string base = operand;
			std::string existing;
			if (dot != std::string::npos && dot + 1 < operand.size())
			{
				// Only treat it as a swizzle when it is made of channel letters.
				const std::string candidate = operand.substr(dot + 1);
				bool is_swizzle = !candidate.empty() && candidate.size() <= 4;
				for (char c : candidate)
					if (c != 'x' && c != 'y' && c != 'z' && c != 'w')
						is_swizzle = false;
				if (is_swizzle)
				{
					base = operand.substr(0, dot);
					existing = candidate;
				}
			}

			std::string swizzle;
			for (int i = 0; i < components; ++i)
				swizzle += (i < static_cast<int>(existing.size())) ? existing[i] : kSwizzle[i];
			return base + "." + swizzle;
		}

		int SwizzleWidth(const std::string &operand)
		{
			const size_t dot = operand.rfind('.');
			if (dot == std::string::npos || dot + 1 >= operand.size())
				return 4;

			int width = 0;
			for (size_t i = dot + 1; i < operand.size(); ++i)
			{
				const char c = operand[i];
				if (c != 'x' && c != 'y' && c != 'z' && c != 'w')
					return 4;
				++width;
			}
			return width != 0 ? width : 4;
		}

		// Everything that maps to "dst = <intrinsic>(sources...)" or a simple operator.
		struct SimpleOp
		{
			const char *pattern;   // $0, $1, $2 are the translated sources
			int sources;
		};

		const std::unordered_map<std::string, SimpleOp> &SimpleOps()
		{
			static const std::unordered_map<std::string, SimpleOp> kOps = {
				{ "mov",      { "$0", 1 } },
				{ "add",      { "$0 + $1", 2 } },
				{ "mul",      { "$0 * $1", 2 } },
				{ "div",      { "$0 / $1", 2 } },
				{ "mad",      { "mad($0, $1, $2)", 3 } },
				{ "min",      { "min($0, $1)", 2 } },
				{ "max",      { "max($0, $1)", 2 } },
				{ "rcp",      { "rcp($0)", 1 } },
				{ "rsq",      { "rsqrt($0)", 1 } },
				{ "sqrt",     { "sqrt($0)", 1 } },
				{ "exp",      { "exp2($0)", 1 } },
				{ "log",      { "log2($0)", 1 } },
				{ "frc",      { "frac($0)", 1 } },
				{ "round_ni", { "floor($0)", 1 } },
				{ "round_pi", { "ceil($0)", 1 } },
				{ "round_z",  { "trunc($0)", 1 } },
				{ "round_ne", { "round($0)", 1 } },
				{ "sincos",   { "sin($0)", 1 } },   // handled specially, see below
				{ "deriv_rtx", { "ddx($0)", 1 } },
				{ "deriv_rty", { "ddy($0)", 1 } },
				{ "deriv_rtx_coarse", { "ddx_coarse($0)", 1 } },
				{ "deriv_rty_coarse", { "ddy_coarse($0)", 1 } },
				{ "deriv_rtx_fine", { "ddx_fine($0)", 1 } },
				{ "deriv_rty_fine", { "ddy_fine($0)", 1 } },
				{ "iadd",     { "asfloat(asint($0) + asint($1))", 2 } },
				{ "imul",     { "asfloat(asint($0) * asint($1))", 2 } },
				{ "imax",     { "asfloat(max(asint($0), asint($1)))", 2 } },
				{ "imin",     { "asfloat(min(asint($0), asint($1)))", 2 } },
				{ "umax",     { "asfloat(max(asuint($0), asuint($1)))", 2 } },
				{ "umin",     { "asfloat(min(asuint($0), asuint($1)))", 2 } },
				{ "ineg",     { "asfloat(-asint($0))", 1 } },
				{ "and",      { "asfloat(asuint($0) & asuint($1))", 2 } },
				{ "or",       { "asfloat(asuint($0) | asuint($1))", 2 } },
				{ "xor",      { "asfloat(asuint($0) ^ asuint($1))", 2 } },
				{ "not",      { "asfloat(~asuint($0))", 1 } },
				{ "ishl",     { "asfloat(asint($0) << asint($1))", 2 } },
				{ "ishr",     { "asfloat(asint($0) >> asint($1))", 2 } },
				{ "ushr",     { "asfloat(asuint($0) >> asuint($1))", 2 } },
				{ "ftoi",     { "asfloat((int)$0)", 1 } },
				{ "ftou",     { "asfloat((uint)$0)", 1 } },
				{ "itof",     { "(float)asint($0)", 1 } },
				{ "utof",     { "(float)asuint($0)", 1 } },
			};
			return kOps;
		}

		// Comparisons write a bit mask in DXBC. We write 1.0 / 0.0, which behaves the same through
		// movc and if_nz, and say so in the notes rather than pretending it is identical.
		const std::unordered_map<std::string, const char *> &CompareOps()
		{
			static const std::unordered_map<std::string, const char *> kOps = {
				{ "lt",  "$0 < $1" },
				{ "ge",  "$0 >= $1" },
				{ "eq",  "$0 == $1" },
				{ "ne",  "$0 != $1" },
				{ "ilt", "asint($0) < asint($1)" },
				{ "ige", "asint($0) >= asint($1)" },
				{ "ieq", "asint($0) == asint($1)" },
				{ "ine", "asint($0) != asint($1)" },
				{ "ult", "asuint($0) < asuint($1)" },
				{ "uge", "asuint($0) >= asuint($1)" },
			};
			return kOps;
		}

		std::string Substitute(const char *pattern, const std::vector<std::string> &sources)
		{
			std::string out;
			for (const char *c = pattern; *c != '\0'; ++c)
			{
				if (*c == '$' && c[1] >= '0' && c[1] <= '9')
				{
					const size_t index = static_cast<size_t>(c[1] - '0');
					out += index < sources.size() ? sources[index] : "0";
					++c;
					continue;
				}
				out += *c;
			}
			return out;
		}

		std::string HlslTypeOf(const SignatureElement &element, int &components)
		{
			components = 0;
			for (int i = 0; i < 4; ++i)
				if (element.mask & (1u << i))
					++components;
			if (components == 0)
				components = 1;

			const std::string base = element.component_type == "float" ? "float"
				: element.component_type == "int" ? "int" : "uint";
			return components > 1 ? base + std::to_string(components) : base;
		}

		class DxbcTextDecompiler final : public DecompilerBackend
		{
		public:
			const DecompilerInfo &Info() const override
			{
				static const DecompilerInfo kInfo = {
					"cygi-dxbc",
					"CyGPUInspector register level DXBC",
					"0.1",
					"GPL-3.0-or-later",
					"Translates the DXBC assembly one instruction at a time and rebuilds the "
					"declarations from reflection. Faithful to the register structure and usually "
					"compilable, but it recovers no names, no expressions and no algorithms.",
				};
				return kInfo;
			}

			bool Supports(ShaderFormat format) const override { return format == ShaderFormat::dxbc; }

			DecompilationResult Decompile(const void *code, size_t size) const override;
		};

		DecompilationResult DxbcTextDecompiler::Decompile(const void *code, size_t size) const
		{
			const auto start = std::chrono::steady_clock::now();

			DecompilationResult result;
			result.backend_id = Info().id;
			result.backend_version = Info().version;

			const DisassemblyResult disassembly = Disassemble(code, size);
			if (!disassembly.ok)
			{
				result.error = "disassembly failed: " + disassembly.error;
				return result;
			}

			const ReflectionResult reflection = Reflect(code, size);
			ShaderBlobInfo blob;
			ParseShaderBlob(code, size, blob);

			std::ostringstream out;
			out << "// Reconstructed by " << Info().name << " " << Info().version << ".\n";
			out << "// This is NOT the original source: that does not exist in the compiled shader.\n";
			out << "// Registers, order and control flow follow the disassembly exactly.\n\n";

			// --- declarations, taken from reflection rather than guessed from the assembly ---
			std::unordered_map<std::string, std::string> sampler_names;
			for (const ShaderBinding &binding : reflection.bindings)
			{
				switch (binding.kind)
				{
				case BindingKind::constant_buffer:
				{
					// The assembly addresses constant buffers as arrays of float4, so that is what
					// is declared: it matches the instructions and it compiles.
					const uint32_t vectors = binding.size != 0 ? (binding.size + 15) / 16 : 16;
					out << "cbuffer " << binding.name << " : register(b" << binding.bind_point << ")\n{\n";
					out << "    float4 cb" << binding.bind_point << "[" << vectors << "];\n};\n";
					break;
				}
				case BindingKind::texture:
					out << "Texture2D<float4> t" << binding.bind_point << " : register(t"
					    << binding.bind_point << ");   // " << binding.name << "\n";
					break;
				case BindingKind::sampler:
					out << "SamplerState s" << binding.bind_point << " : register(s"
					    << binding.bind_point << ");   // " << binding.name << "\n";
					break;
				case BindingKind::unordered_access:
					out << "RWTexture2D<float4> u" << binding.bind_point << " : register(u"
					    << binding.bind_point << ");   // " << binding.name << "\n";
					break;
				case BindingKind::structured_buffer:
					out << "StructuredBuffer<float4> t" << binding.bind_point << " : register(t"
					    << binding.bind_point << ");   // " << binding.name << "\n";
					break;
				default:
					out << "// unhandled binding: " << BindingKindName(binding.kind) << " "
					    << binding.name << "\n";
					break;
				}
			}
			out << "\n";

			// --- input and output structures, from the signatures ---
			uint32_t max_input_register = 0;
			uint32_t max_output_register = 0;

			out << "struct Input\n{\n";
			for (const SignatureElement &element : reflection.inputs)
			{
				int components = 0;
				const std::string type = HlslTypeOf(element, components);
				out << "    " << type << " " << element.semantic_name << element.semantic_index << " : "
				    << element.semantic_name << element.semantic_index << ";\n";
				max_input_register = (std::max)(max_input_register, element.register_index + 1);
			}
			if (reflection.inputs.empty())
				out << "    uint dummy : SV_VertexID;\n";
			out << "};\n\n";

			out << "struct Output\n{\n";
			for (const SignatureElement &element : reflection.outputs)
			{
				int components = 0;
				const std::string type = HlslTypeOf(element, components);
				out << "    " << type << " " << element.semantic_name << element.semantic_index << " : "
				    << element.semantic_name << element.semantic_index << ";\n";
				max_output_register = (std::max)(max_output_register, element.register_index + 1);
			}
			out << "};\n\n";

			// --- the body ---
			if (reflection.thread_group[0] != 0)
				out << "[numthreads(" << reflection.thread_group[0] << ", " << reflection.thread_group[1]
				    << ", " << reflection.thread_group[2] << ")]\n";

			out << "Output main(Input input)\n{\n";

			uint32_t temp_count = 0;
			std::vector<std::string> body;
			int indent = 1;
			uint32_t untranslated = 0;

			std::istringstream lines(disassembly.text);
			std::string line;
			bool saw_profile = false;

			auto emit = [&](const std::string &statement) {
				body.push_back(std::string(static_cast<size_t>(indent) * 4, ' ') + statement);
			};

			while (std::getline(lines, line))
			{
				std::string text = Trim(line);
				if (text.empty() || StartsWith(text, "//"))
					continue;

				text = StripLinePrefix(text);
				if (text.empty())
					continue;

				const size_t space = text.find(' ');
				std::string opcode = space == std::string::npos ? text : text.substr(0, space);
				const std::string operand_text = space == std::string::npos ? "" : Trim(text.substr(space + 1));

				// Strip the parenthesised qualifiers: sample_indexable(texture2d)(float,...).
				const size_t parenthesis = opcode.find('(');
				if (parenthesis != std::string::npos)
					opcode = opcode.substr(0, parenthesis);

				if (!saw_profile)
				{
					// The first line is the profile, e.g. "ps_5_0".
					if (opcode.size() > 3 && opcode[2] == '_')
					{
						result.suggested_profile = opcode;
						saw_profile = true;
						continue;
					}
				}

				if (StartsWith(opcode, "dcl_temps"))
				{
					temp_count = static_cast<uint32_t>(std::atoi(operand_text.c_str()));
					continue;
				}
				if (StartsWith(opcode, "dcl_") || opcode == "ret")
					continue;   // declarations come from reflection; the return is written below

				std::vector<std::string> raw = SplitOperands(operand_text);
				std::vector<std::string> operands;
				operands.reserve(raw.size());
				for (const std::string &operand : raw)
					operands.push_back(TranslateOperand(operand));

				// --- control flow ---
				if (opcode == "if_nz" || opcode == "if_z")
				{
					emit("if (" + (operands.empty() ? std::string("0") : operands[0]) +
						(opcode == "if_nz" ? " != 0)" : " == 0)"));
					emit("{");
					++indent;
					continue;
				}
				if (opcode == "else")
				{
					--indent;
					emit("}");
					emit("else");
					emit("{");
					++indent;
					continue;
				}
				if (opcode == "endif" || opcode == "endloop" || opcode == "endswitch")
				{
					--indent;
					emit("}");
					continue;
				}
				if (opcode == "loop")
				{
					emit("[loop] while (true)");
					emit("{");
					++indent;
					continue;
				}
				if (opcode == "break")
				{
					emit("break;");
					continue;
				}
				if (opcode == "breakc_nz" || opcode == "breakc_z")
				{
					emit("if (" + (operands.empty() ? std::string("0") : operands[0]) +
						(opcode == "breakc_nz" ? " != 0) break;" : " == 0) break;"));
					continue;
				}
				if (opcode == "discard_nz" || opcode == "discard_z")
				{
					emit("if (" + (operands.empty() ? std::string("0") : operands[0]) +
						(opcode == "discard_nz" ? " != 0) discard;" : " == 0) discard;"));
					continue;
				}

				if (operands.empty())
				{
					body.push_back("    // [cygi-dxbc] untranslated: " + text);
					++untranslated;
					continue;
				}

				const std::string &destination = operands[0];
				const int width = SwizzleWidth(destination);
				std::vector<std::string> sources(operands.begin() + 1, operands.end());

				// --- texture sampling ---
				if (StartsWith(opcode, "sample") || opcode == "ld" || opcode == "ld_ms" ||
				    StartsWith(opcode, "gather4"))
				{
					if (sources.size() >= 3)
					{
						const std::string &coordinates = sources[0];
						const std::string &texture = sources[1];
						const std::string &sampler = sources[2];

						// The texture operand carries a swizzle that reorders the result; keeping it
						// would be wrong here, so it is dropped and the destination decides.
						const std::string texture_name = texture.substr(0, texture.find('.'));
						const std::string sampler_name = sampler.substr(0, sampler.find('.'));

						if (opcode == "sample_l")
							emit(destination + " = " + texture_name + ".SampleLevel(" + sampler_name + ", " +
								Truncate(coordinates, 2) + ", " +
								(sources.size() > 3 ? sources[3] : "0") + ")." +
								std::string("xyzw").substr(0, static_cast<size_t>(width)) + ";");
						else if (StartsWith(opcode, "gather4"))
							emit(destination + " = " + texture_name + ".Gather(" + sampler_name + ", " +
								Truncate(coordinates, 2) + ");");
						else
							emit(destination + " = " + texture_name + ".Sample(" + sampler_name + ", " +
								Truncate(coordinates, 2) + ")." +
								std::string("xyzw").substr(0, static_cast<size_t>(width)) + ";");
						continue;
					}
					if (opcode == "ld" && sources.size() >= 2)
					{
						const std::string texture_name = sources[1].substr(0, sources[1].find('.'));
						emit(destination + " = " + texture_name + ".Load(asint(" +
							Truncate(sources[0], 3) + "));");
						continue;
					}
				}

				// --- dot products, which need an explicit component count ---
				if (opcode == "dp2" || opcode == "dp3" || opcode == "dp4")
				{
					const int components = opcode == "dp2" ? 2 : (opcode == "dp3" ? 3 : 4);
					if (sources.size() >= 2)
					{
						emit(destination + " = dot(" + Truncate(sources[0], components) + ", " +
							Truncate(sources[1], components) + ");");
						continue;
					}
				}

				// --- comparisons ---
				const auto compare = CompareOps().find(opcode);
				if (compare != CompareOps().end() && sources.size() >= 2)
				{
					emit(destination + " = (" + Substitute(compare->second, sources) + ") ? 1.0f : 0.0f;");
					continue;
				}

				// --- conditional move ---
				if (opcode == "movc" && sources.size() >= 3)
				{
					emit(destination + " = (" + sources[0] + " != 0) ? " + sources[1] + " : " +
						sources[2] + ";");
					continue;
				}

				// --- sin and cos share one instruction with two destinations ---
				if (opcode == "sincos" && operands.size() >= 3)
				{
					if (operands[0] != "null")
						emit(operands[0] + " = sin(" + operands[2] + ");");
					if (operands[1] != "null")
						emit(operands[1] + " = cos(" + operands[2] + ");");
					continue;
				}

				// --- everything that is a plain expression ---
				const auto simple = SimpleOps().find(opcode);
				if (simple != SimpleOps().end() && static_cast<int>(sources.size()) >= simple->second.sources)
				{
					emit(destination + " = " + Substitute(simple->second.pattern, sources) + ";");
					continue;
				}

				body.push_back("    // [cygi-dxbc] untranslated: " + text);
				++untranslated;
			}

			// Registers, declared once the assembly told us how many there are.
			if (temp_count != 0)
			{
				out << "    float4";
				for (uint32_t i = 0; i < temp_count; ++i)
					out << (i != 0 ? ", r" : " r") << i;
				out << ";\n";
			}
			if (max_input_register != 0)
			{
				out << "    float4 v[" << max_input_register << "];\n";
				for (const SignatureElement &element : reflection.inputs)
				{
					int components = 0;
					HlslTypeOf(element, components);
					out << "    v[" << element.register_index << "] = float4(input."
					    << element.semantic_name << element.semantic_index;
					for (int i = components; i < 4; ++i)
						out << ", 0";
					out << ");\n";
				}
			}
			if (max_output_register != 0)
				out << "    float4 o[" << max_output_register << "];\n";
			out << "\n";

			for (const std::string &statement : body)
				out << statement << "\n";

			out << "\n    Output output;\n";
			for (const SignatureElement &element : reflection.outputs)
			{
				int components = 0;
				HlslTypeOf(element, components);
				out << "    output." << element.semantic_name << element.semantic_index << " = o["
				    << element.register_index << "]."
				    << std::string("xyzw").substr(0, static_cast<size_t>(components)) << ";\n";
			}
			out << "    return output;\n}\n";

			result.hlsl = out.str();
			result.untranslated_lines = untranslated;
			result.ok = true;
			result.milliseconds =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

			result.notes = "Register level reconstruction: one statement per instruction, no name "
			               "recovery, no expression folding. Comparisons write 1.0 / 0.0 where the "
			               "hardware writes a bit mask, which behaves the same through movc and "
			               "conditionals.";
			if (untranslated != 0)
				result.notes += " " + std::to_string(untranslated) + " instruction(s) could not be "
				                "translated and are left as comments.";
			if (!reflection.ok)
				result.notes += " Reflection was unavailable, so the declarations may be incomplete.";
			if (blob.format != ShaderFormat::dxbc)
				result.notes += " The container was not DXBC.";

			return result;
		}
	}

	std::unique_ptr<DecompilerBackend> MakeDxbcTextDecompiler()
	{
		return std::make_unique<DxbcTextDecompiler>();
	}
}

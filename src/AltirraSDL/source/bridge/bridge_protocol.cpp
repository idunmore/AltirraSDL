// AltirraBridge - line-delimited JSON protocol helpers (impl)

#include <stdafx.h>

#include "bridge_protocol.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ATBridge {

std::string JsonEscape(const std::string& s) {
	std::string out;
	out.reserve(s.size() + 8);
	for (unsigned char c : s) {
		switch (c) {
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b";  break;
		case '\f': out += "\\f";  break;
		case '\n': out += "\\n";  break;
		case '\r': out += "\\r";  break;
		case '\t': out += "\\t";  break;
		default:
			if (c < 0x20) {
				char buf[8];
				std::snprintf(buf, sizeof buf, "\\u%04x", (unsigned)c);
				out += buf;
			} else {
				out += (char)c;
			}
			break;
		}
	}
	return out;
}

std::string JsonOk() {
	return std::string("{\"ok\":true}\n");
}

std::string JsonOk(const std::string& extraPayload) {
	if (extraPayload.empty())
		return JsonOk();
	std::string out = "{\"ok\":true,";
	out += extraPayload;
	out += "}\n";
	return out;
}

std::string JsonError(const std::string& msg) {
	std::string out = "{\"ok\":false,\"error\":\"";
	out += JsonEscape(msg);
	out += "\"}\n";
	return out;
}

std::vector<std::string> TokenizeCommand(const std::string& line) {
	std::vector<std::string> tokens;
	std::string cur;
	for (char c : line) {
		if (c == ' ' || c == '\t' || c == '\r') {
			if (!cur.empty()) {
				tokens.push_back(cur);
				cur.clear();
			}
		} else {
			cur += c;
		}
	}
	if (!cur.empty())
		tokens.push_back(cur);
	return tokens;
}

// ---------------------------------------------------------------------------
// Base64
// ---------------------------------------------------------------------------

namespace {
constexpr char kBase64Alphabet[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 0..63 = valid alphabet index, 64 = padding '=', 0xFF = invalid.
constexpr int8_t Base64Decode1(unsigned char c) {
	if (c >= 'A' && c <= 'Z') return (int8_t)(c - 'A');
	if (c >= 'a' && c <= 'z') return (int8_t)(c - 'a' + 26);
	if (c >= '0' && c <= '9') return (int8_t)(c - '0' + 52);
	if (c == '+') return 62;
	if (c == '/') return 63;
	if (c == '=') return 64;
	return -1;
}
}  // namespace

std::string Base64Encode(const uint8_t* data, size_t len) {
	std::string out;
	out.reserve(((len + 2) / 3) * 4);
	size_t i = 0;
	while (i + 3 <= len) {
		uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i+1] << 8) | data[i+2];
		out += kBase64Alphabet[(v >> 18) & 0x3F];
		out += kBase64Alphabet[(v >> 12) & 0x3F];
		out += kBase64Alphabet[(v >>  6) & 0x3F];
		out += kBase64Alphabet[ v        & 0x3F];
		i += 3;
	}
	if (i < len) {
		uint32_t v = (uint32_t)data[i] << 16;
		if (i + 1 < len) v |= (uint32_t)data[i+1] << 8;
		out += kBase64Alphabet[(v >> 18) & 0x3F];
		out += kBase64Alphabet[(v >> 12) & 0x3F];
		out += (i + 1 < len) ? kBase64Alphabet[(v >> 6) & 0x3F] : '=';
		out += '=';
	}
	return out;
}

bool Base64Decode(const std::string& s, std::vector<uint8_t>& out) {
	out.clear();
	out.reserve((s.size() / 4) * 3);

	uint32_t accum = 0;
	int bits = 0;
	int padding = 0;
	for (unsigned char c : s) {
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
			continue;
		int v = Base64Decode1(c);
		if (v < 0) return false;
		if (v == 64) {        // '=' padding
			++padding;
			continue;
		}
		if (padding > 0) return false;  // data after padding
		accum = (accum << 6) | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back((uint8_t)((accum >> bits) & 0xFF));
		}
	}
	// Remaining bits must be zero (well-formed base64 leaves at most
	// 4 bits of padding-context that aren't a full byte).
	if (bits > 0 && (accum & ((1u << bits) - 1)) != 0)
		return false;
	return true;
}

bool ParseUint(const std::string& s, uint32_t& out) {
	if (s.empty())
		return false;

	size_t i = 0;
	int base = 10;

	// Accept "$" (Atari hex), "0x"/"0X" (C hex)
	if (s[0] == '$') {
		base = 16;
		i = 1;
	} else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		i = 2;
	}

	if (i >= s.size())
		return false;

	uint64_t val = 0;
	for (; i < s.size(); ++i) {
		char c = s[i];
		int digit;
		if (c >= '0' && c <= '9')      digit = c - '0';
		else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
		else                           return false;
		if (digit >= base)
			return false;
		val = val * (uint64_t)base + (uint64_t)digit;
		if (val > 0xFFFFFFFFu)
			return false;
	}
	out = (uint32_t)val;
	return true;
}

}  // namespace ATBridge

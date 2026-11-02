#pragma once
#include <string>
#include <string_view>

namespace vlr {
// Only application text is translated; hardware names, IDs and user data stay intact.
std::wstring localize(std::string_view language, std::wstring_view text);
std::wstring localizeMessage(std::string_view language, std::wstring_view text);
} // namespace vlr

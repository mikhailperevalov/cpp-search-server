#pragma once

#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

template <typename StringContainer>
std::set<std::string, std::less<>> MakeUniqueNonEmptyStrings(const StringContainer& strings) {
    std::set<std::string, std::less<>> non_empty_strings;
    for (std::string_view str : strings) {
        if (!str.empty()) {
            non_empty_strings.emplace(str);
        }
    }
    return non_empty_strings;
}

std::vector<std::string_view> SplitIntoWords(std::string_view text);
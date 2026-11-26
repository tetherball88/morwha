#pragma once

// Core precompiled header shared strictly by pure STL modules (Layers 0-2) and morwha_tests.
// Must NEVER include RE/*, SKSE/*, Windows.h, nlohmann/*, yaml-cpp/*, or spdlog/*.
// Must NEVER contain global or namespace-scope `using namespace` directives.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "morwha/foundation/StrongIds.h"

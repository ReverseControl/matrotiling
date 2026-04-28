
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <queue>
#include <optional>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#if defined(__linux__)
#include <unistd.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include <sched.h>
#endif

namespace alexeev {

inline std::string trim(std::string s) {
    auto is_ws = [](unsigned char c){ return std::isspace(c); };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

inline uint64_t mask_low_bits(std::size_t nbits) {
    if (nbits >= 64) return ~uint64_t{0};
    if (nbits == 0) return 0;
    return (uint64_t{1} << nbits) - 1;
}

inline std::vector<int> bits_u64(uint64_t x) {
    std::vector<int> out;
    while (x) {
        unsigned b = std::countr_zero(x);
        out.push_back(static_cast<int>(b));
        x &= x - 1;
    }
    return out;
}

inline int popcount_u64(uint64_t x) {
    return static_cast<int>(std::popcount(x));
}

inline std::size_t hardware_threads() {
    unsigned hc = std::thread::hardware_concurrency();
    return hc == 0 ? std::size_t{1} : static_cast<std::size_t>(hc);
}

inline std::size_t normalize_threads(std::size_t requested) {
    // requested == 0 means "all hardware threads"; otherwise honor the explicit value.
    return requested == 0 ? hardware_threads() : std::max<std::size_t>(1, requested);
}

template <class F>
void parallel_for_indices(std::size_t begin, std::size_t end, std::size_t threads, F&& f) {
    if (end <= begin) return;
    threads = std::min<std::size_t>(normalize_threads(threads), end - begin);
    if (threads <= 1) {
        for (std::size_t i = begin; i < end; ++i) f(i);
        return;
    }
    std::atomic<std::size_t> next{begin};
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (std::size_t t = 0; t < threads; ++t) {
        workers.emplace_back([&]() {
            for (;;) {
                std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= end) break;
                f(i);
            }
        });
    }
    for (auto& th : workers) th.join();
}

inline std::string hex_u64(uint64_t x) {
    std::ostringstream os;
    os << std::hex << std::setw(16) << std::setfill('0') << x;
    return os.str();
}

inline std::string join_key_hex(const std::vector<uint64_t>& vals) {
    std::ostringstream os;
    for (std::size_t i = 0; i < vals.size(); ++i) {
        if (i) os << ';';
        os << hex_u64(vals[i]);
    }
    return os.str();
}

inline void append_u64_le(std::string& s, uint64_t x) {
    for (int b = 0; b < 8; ++b) {
        s.push_back(static_cast<char>((x >> (8*b)) & 0xffu));
    }
}

inline uint64_t read_u64_le(const char* p) {
    uint64_t x = 0;
    for (int b = 0; b < 8; ++b) {
        x |= (uint64_t{static_cast<unsigned char>(p[b])} << (8*b));
    }
    return x;
}

inline std::string join_key_binary(const std::vector<uint64_t>& vals) {
    std::string s;
    s.reserve(vals.size() * 8);
    for (uint64_t v : vals) append_u64_le(s, v);
    return s;
}

inline std::string binary_key_to_hex(const std::string& key) {
    if (key.empty()) return "";
    if (key.size() % 8 != 0) return key; // raw non-quotient key, already printable
    std::vector<uint64_t> vals;
    vals.reserve(key.size() / 8);
    for (std::size_t i = 0; i < key.size(); i += 8) {
        vals.push_back(read_u64_le(key.data() + i));
    }
    return join_key_hex(vals);
}

inline std::string hex_key_to_binary(const std::string& hexkey) {
    if (hexkey.empty()) return "";
    std::string out;
    std::size_t pos = 0;
    while (pos < hexkey.size()) {
        std::size_t next = hexkey.find(';', pos);
        std::string token = (next == std::string::npos) ? hexkey.substr(pos) : hexkey.substr(pos, next - pos);
        if (token.size() != 16) return hexkey; // not a canonical hex tuple; return printable as-is
        uint64_t x = 0;
        std::istringstream is(token);
        is >> std::hex >> x;
        if (!is) return hexkey;
        append_u64_le(out, x);
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return out;
}

// Canonical keys are intentionally stored as raw 8-byte words rather than
// semicolon-separated hex strings.  For the n=8 DFS, partial-state keys dominate
// memory, and this cuts the payload roughly in half before allocator/hash-table overhead.
inline std::string join_key(const std::vector<uint64_t>& vals) {
    return join_key_binary(vals);
}

inline std::size_t current_rss_mb() {
#if defined(__linux__)
    std::ifstream in("/proc/self/statm");
    unsigned long pages = 0, resident = 0;
    if (in >> pages >> resident) {
        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size > 0) return (static_cast<std::size_t>(resident) * static_cast<std::size_t>(page_size)) / (1024ULL * 1024ULL);
    }
#endif
    return 0;
}

inline std::size_t parse_meminfo_mb(const std::string& key) {
#if defined(__linux__)
    std::ifstream in("/proc/meminfo");
    std::string name, unit;
    std::size_t value_kib = 0;
    while (in >> name >> value_kib >> unit) {
        if (!name.empty() && name.back() == ':') name.pop_back();
        if (name == key) return value_kib / 1024ULL;
    }
#else
    (void)key;
#endif
    return 0;
}

inline std::size_t parse_cache_size_kib(const std::string& s) {
    if (s.empty()) return 0;
    char suffix = s.back();
    std::string num = s;
    if (!std::isdigit(static_cast<unsigned char>(suffix))) num.pop_back();
    std::size_t val = 0;
    try { val = static_cast<std::size_t>(std::stoull(trim(num))); }
    catch (...) { return 0; }
    if (suffix == 'K' || suffix == 'k') return val;
    if (suffix == 'M' || suffix == 'm') return val * 1024ULL;
    return val / 1024ULL;
}

inline std::size_t detect_last_level_cache_kib() {
#if defined(__linux__)
    std::size_t best_level = 0, best_size = 0;
    std::filesystem::path cpu0("/sys/devices/system/cpu/cpu0/cache");
    if (std::filesystem::exists(cpu0)) {
        for (const auto& ent : std::filesystem::directory_iterator(cpu0)) {
            if (!ent.is_directory()) continue;
            std::ifstream lvl(ent.path() / "level");
            std::ifstream siz(ent.path() / "size");
            std::ifstream typ(ent.path() / "type");
            std::size_t level = 0; std::string size_s, type_s;
            if (!(lvl >> level) || !(siz >> size_s)) continue;
            typ >> type_s;
            if (!type_s.empty() && type_s != "Unified") continue;
            std::size_t size_kib = parse_cache_size_kib(size_s);
            if (level > best_level || (level == best_level && size_kib > best_size)) {
                best_level = level; best_size = size_kib;
            }
        }
    }
#endif
    return best_size;
}

inline std::size_t detect_physical_cores() {
#if defined(__linux__)
    std::ifstream in("/proc/cpuinfo");
    if (!in) return 0;
    std::set<std::pair<int,int>> cores;
    std::string line;
    int phys = -1, core = -1;
    auto flush = [&]() {
        if (phys >= 0 && core >= 0) cores.emplace(phys, core);
        phys = -1; core = -1;
    };
    while (std::getline(in, line)) {
        if (line.empty()) { flush(); continue; }
        auto pos = line.find(':');
        if (pos == std::string::npos) continue;
        std::string k = trim(line.substr(0,pos));
        std::string v = trim(line.substr(pos+1));
        try {
            if (k == "physical id") phys = std::stoi(v);
            else if (k == "core id") core = std::stoi(v);
        } catch (...) {}
    }
    flush();
    if (!cores.empty()) return cores.size();
#endif
    return 0;
}

struct SystemInfo {
    std::size_t logical_cpus = 1;
    std::size_t physical_cores = 0;
    std::size_t mem_total_mb = 0;
    std::size_t mem_available_mb = 0;
    std::size_t llc_kib = 0;
};

inline SystemInfo detect_system_info() {
    SystemInfo s;
    s.logical_cpus = hardware_threads();
    s.physical_cores = detect_physical_cores();
    s.mem_total_mb = parse_meminfo_mb("MemTotal");
    s.mem_available_mb = parse_meminfo_mb("MemAvailable");
#if defined(__linux__)
    if (s.mem_total_mb == 0) {
        struct sysinfo info {};
        if (sysinfo(&info) == 0) {
            unsigned long long total = static_cast<unsigned long long>(info.totalram) * info.mem_unit;
            s.mem_total_mb = static_cast<std::size_t>(total / (1024ULL * 1024ULL));
            unsigned long long freeram = static_cast<unsigned long long>(info.freeram) * info.mem_unit;
            s.mem_available_mb = static_cast<std::size_t>(freeram / (1024ULL * 1024ULL));
        }
    }
#endif
    s.llc_kib = detect_last_level_cache_kib();
    return s;
}


// -------------------------
// Low-level performance knobs
// -------------------------

enum class BitsetKernelKind { Scalar, Unrolled, Auto, Avx2, Avx512 };

inline BitsetKernelKind& global_bitset_kernel() {
    static BitsetKernelKind k = BitsetKernelKind::Auto;
    return k;
}

inline void set_global_bitset_kernel(const std::string& name) {
    if (name == "scalar") global_bitset_kernel() = BitsetKernelKind::Scalar;
    else if (name == "unrolled") global_bitset_kernel() = BitsetKernelKind::Unrolled;
    else if (name == "auto") global_bitset_kernel() = BitsetKernelKind::Auto;
    else if (name == "avx2") global_bitset_kernel() = BitsetKernelKind::Avx2;      // currently dispatches to unrolled scalar unless compiled with explicit SIMD
    else if (name == "avx512") global_bitset_kernel() = BitsetKernelKind::Avx512;  // accepted for gather-data; safe fallback is unrolled scalar
    else throw std::runtime_error("--bitset-kernel must be scalar, unrolled, avx2, avx512, or auto.");
}

template <class T>
struct alignas(64) CacheLineBox {
    T value;
    CacheLineBox() = default;
    template <class... Args>
    explicit CacheLineBox(Args&&... args) : value(std::forward<Args>(args)...) {}
};

inline std::vector<int> parse_cpu_list_string(const std::string& s) {
    std::vector<int> out;
    std::size_t pos = 0;
    while (pos < s.size()) {
        while (pos < s.size() && (std::isspace(static_cast<unsigned char>(s[pos])) || s[pos] == ',')) ++pos;
        if (pos >= s.size()) break;
        std::size_t q = pos;
        while (q < s.size() && std::isdigit(static_cast<unsigned char>(s[q]))) ++q;
        if (q == pos) { ++pos; continue; }
        int a = std::stoi(s.substr(pos, q - pos));
        int b = a;
        if (q < s.size() && s[q] == '-') {
            std::size_t r = q + 1;
            while (r < s.size() && std::isdigit(static_cast<unsigned char>(s[r]))) ++r;
            if (r > q + 1) {
                b = std::stoi(s.substr(q + 1, r - q - 1));
                q = r;
            }
        }
        if (b < a) std::swap(a, b);
        for (int x = a; x <= b; ++x) out.push_back(x);
        pos = q;
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

inline std::vector<std::vector<int>> detect_numa_cpu_lists() {
    std::vector<std::vector<int>> nodes;
#if defined(__linux__)
    std::filesystem::path root("/sys/devices/system/node");
    if (std::filesystem::exists(root)) {
        std::vector<std::filesystem::path> node_paths;
        for (const auto& ent : std::filesystem::directory_iterator(root)) {
            if (!ent.is_directory()) continue;
            std::string name = ent.path().filename().string();
            if (name.rfind("node", 0) == 0 && name.size() > 4) node_paths.push_back(ent.path());
        }
        std::sort(node_paths.begin(), node_paths.end());
        for (const auto& np : node_paths) {
            std::ifstream in(np / "cpulist");
            std::string s;
            if (std::getline(in, s)) {
                auto cpus = parse_cpu_list_string(s);
                if (!cpus.empty()) nodes.push_back(std::move(cpus));
            }
        }
    }
#endif
    if (nodes.empty()) {
        std::vector<int> all;
        for (std::size_t i = 0; i < hardware_threads(); ++i) all.push_back(static_cast<int>(i));
        if (!all.empty()) nodes.push_back(std::move(all));
    }
    return nodes;
}

inline int choose_affinity_cpu(std::size_t worker_index, std::size_t worker_count, const std::string& affinity_mode) {
    auto nodes = detect_numa_cpu_lists();
    std::vector<int> flat;
    for (const auto& v : nodes) flat.insert(flat.end(), v.begin(), v.end());
    if (flat.empty()) return -1;
    if (affinity_mode == "compact") {
        return flat[worker_index % flat.size()];
    }
    if (affinity_mode == "spread") {
        std::size_t stride = std::max<std::size_t>(1, flat.size() / std::max<std::size_t>(1, worker_count));
        return flat[(worker_index * stride + worker_index / std::max<std::size_t>(1, flat.size())) % flat.size()];
    }
    if (affinity_mode == "numa") {
        std::size_t nn = nodes.size();
        if (nn == 0) return flat[worker_index % flat.size()];
        std::size_t node = worker_index % nn;
        const auto& cpus = nodes[node];
        return cpus[(worker_index / nn) % cpus.size()];
    }
    return -1;
}

inline void pin_current_thread_to_cpu(int cpu) {
#if defined(__linux__)
    if (cpu < 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    (void)pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
    (void)cpu;
#endif
}

inline void maybe_pin_worker(std::size_t worker_index, std::size_t worker_count, const std::string& affinity_mode) {
    if (affinity_mode == "none" || affinity_mode.empty()) return;
    pin_current_thread_to_cpu(choose_affinity_cpu(worker_index, worker_count, affinity_mode));
}


inline std::size_t popcount_and_words_kernel(const uint64_t* a, const uint64_t* b, std::size_t n) {
    std::size_t c = 0;
    BitsetKernelKind k = global_bitset_kernel();
    if (k == BitsetKernelKind::Scalar) {
        for (std::size_t i = 0; i < n; ++i) c += std::popcount(a[i] & b[i]);
        return c;
    }
    std::size_t i = 0;
    for (; i + 7 < n; i += 8) {
        c += std::popcount(a[i+0] & b[i+0]);
        c += std::popcount(a[i+1] & b[i+1]);
        c += std::popcount(a[i+2] & b[i+2]);
        c += std::popcount(a[i+3] & b[i+3]);
        c += std::popcount(a[i+4] & b[i+4]);
        c += std::popcount(a[i+5] & b[i+5]);
        c += std::popcount(a[i+6] & b[i+6]);
        c += std::popcount(a[i+7] & b[i+7]);
    }
    for (; i < n; ++i) c += std::popcount(a[i] & b[i]);
    return c;
}

inline std::size_t popcount_and_words_limited_kernel(const uint64_t* a, const uint64_t* b, std::size_t n, std::size_t limit) {
    std::size_t c = 0;
    for (std::size_t i = 0; i < n; ++i) {
        c += std::popcount(a[i] & b[i]);
        if (c >= limit) return c;
    }
    return c;
}

inline void and_words_into_kernel(uint64_t* out, const uint64_t* a, const uint64_t* b, std::size_t n) {
    BitsetKernelKind k = global_bitset_kernel();
    if (k == BitsetKernelKind::Scalar) {
        for (std::size_t i = 0; i < n; ++i) out[i] = a[i] & b[i];
        return;
    }
    std::size_t i = 0;
    for (; i + 7 < n; i += 8) {
        out[i+0] = a[i+0] & b[i+0];
        out[i+1] = a[i+1] & b[i+1];
        out[i+2] = a[i+2] & b[i+2];
        out[i+3] = a[i+3] & b[i+3];
        out[i+4] = a[i+4] & b[i+4];
        out[i+5] = a[i+5] & b[i+5];
        out[i+6] = a[i+6] & b[i+6];
        out[i+7] = a[i+7] & b[i+7];
    }
    for (; i < n; ++i) out[i] = a[i] & b[i];
}

inline void and_words_inplace_kernel(uint64_t* out, const uint64_t* b, std::size_t n) {
    BitsetKernelKind k = global_bitset_kernel();
    if (k == BitsetKernelKind::Scalar) {
        for (std::size_t i = 0; i < n; ++i) out[i] &= b[i];
        return;
    }
    std::size_t i = 0;
    for (; i + 7 < n; i += 8) {
        out[i+0] &= b[i+0];
        out[i+1] &= b[i+1];
        out[i+2] &= b[i+2];
        out[i+3] &= b[i+3];
        out[i+4] &= b[i+4];
        out[i+5] &= b[i+5];
        out[i+6] &= b[i+6];
        out[i+7] &= b[i+7];
    }
    for (; i < n; ++i) out[i] &= b[i];
}

inline std::size_t dense_row_bytes(std::size_t nbits) {
    return ((nbits + 63) / 64) * sizeof(uint64_t);
}

inline std::size_t estimate_state_cache_entry_bytes(int total_volume) {
    // Bounded visited-state entries are exact binary keys.  The live structure keeps
    // one copy in an unordered_set and one copy in the FIFO queue.  std::string and
    // unordered node overhead are implementation-dependent, so this is deliberately
    // conservative.  It is used for planning and for converting --state-cache-mb to
    // an entry cap; the optional RSS memory guard is the hard backstop.
    std::size_t max_key = static_cast<std::size_t>(std::max(1, total_volume)) * 8ULL;
    return std::max<std::size_t>(384, 2 * (32 + max_key) + 160);
}

inline std::string mib_fmt(std::size_t bytes) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MiB";
    return os.str();
}


class DynBitset {
public:
    std::size_t nbits = 0;
    std::vector<uint64_t> words;

    DynBitset() = default;
    explicit DynBitset(std::size_t nbits_, bool ones=false) { resize(nbits_, ones); }

    void resize(std::size_t nbits_, bool ones=false) {
        nbits = nbits_;
        words.assign((nbits + 63) / 64, ones ? ~uint64_t{0} : uint64_t{0});
        trim_last();
    }

    void trim_last() {
        if (words.empty()) return;
        std::size_t rem = nbits % 64;
        if (rem) words.back() &= mask_low_bits(rem);
    }

    void clear_all() {
        std::fill(words.begin(), words.end(), uint64_t{0});
    }

    void fill_ones() {
        std::fill(words.begin(), words.end(), ~uint64_t{0});
        trim_last();
    }

    void set(std::size_t i) {
        words[i >> 6] |= (uint64_t{1} << (i & 63));
    }

    void reset(std::size_t i) {
        words[i >> 6] &= ~(uint64_t{1} << (i & 63));
    }

    bool test(std::size_t i) const {
        return (words[i >> 6] >> (i & 63)) & 1ULL;
    }

    bool empty() const {
        for (uint64_t w : words) if (w) return false;
        return true;
    }

    std::size_t count() const {
        std::size_t c = 0;
        for (uint64_t w : words) c += std::popcount(w);
        return c;
    }

    std::size_t count_intersection(const DynBitset& other) const {
        assert(words.size() == other.words.size());
        return popcount_and_words_kernel(words.data(), other.words.data(), words.size());
    }

    bool intersects(const DynBitset& other) const {
        assert(words.size() == other.words.size());
        for (std::size_t i = 0; i < words.size(); ++i) {
            if (words[i] & other.words[i]) return true;
        }
        return false;
    }

    std::optional<std::size_t> first_intersection(const DynBitset& other) const {
        assert(words.size() == other.words.size());
        for (std::size_t wi = 0; wi < words.size(); ++wi) {
            uint64_t w = words[wi] & other.words[wi];
            if (w) {
                std::size_t idx = wi * 64 + std::countr_zero(w);
                if (idx < nbits) return idx;
            }
        }
        return std::nullopt;
    }

    std::size_t count_intersection_limited(const DynBitset& other, std::size_t limit) const {
        assert(words.size() == other.words.size());
        return popcount_and_words_limited_kernel(words.data(), other.words.data(), words.size(), limit);
    }

    void assign_and(const DynBitset& a, const DynBitset& b) {
        assert(a.words.size() == b.words.size());
        nbits = a.nbits;
        if (words.size() != a.words.size()) words.resize(a.words.size());
        and_words_into_kernel(words.data(), a.words.data(), b.words.data(), words.size());
        trim_last();
    }

    void copy_from(const DynBitset& other) {
        nbits = other.nbits;
        if (words.size() != other.words.size()) words.resize(other.words.size());
        std::copy(other.words.begin(), other.words.end(), words.begin());
    }

    std::size_t bytes() const {
        return words.size() * sizeof(uint64_t);
    }

    std::optional<std::size_t> first_set() const {
        for (std::size_t wi = 0; wi < words.size(); ++wi) {
            uint64_t w = words[wi];
            if (w) return wi * 64 + std::countr_zero(w);
        }
        return std::nullopt;
    }

    void and_assign(const DynBitset& other) {
        assert(words.size() == other.words.size());
        and_words_inplace_kernel(words.data(), other.words.data(), words.size());
    }

    void or_assign(const DynBitset& other) {
        assert(words.size() == other.words.size());
        for (std::size_t i = 0; i < words.size(); ++i) words[i] |= other.words[i];
        trim_last();
    }

    void or_assign_intersection(const DynBitset& a, const DynBitset& b) {
        assert(a.words.size() == b.words.size());
        if (words.size() != a.words.size()) {
            nbits = a.nbits;
            words.resize(a.words.size());
        }
        for (std::size_t i = 0; i < words.size(); ++i) words[i] |= (a.words[i] & b.words[i]);
        trim_last();
    }

    void subtract_assign(const DynBitset& other) {
        assert(words.size() == other.words.size());
        for (std::size_t i = 0; i < words.size(); ++i) words[i] &= ~other.words[i];
        trim_last();
    }

    DynBitset bit_and(const DynBitset& other) const {
        DynBitset out(nbits);
        for (std::size_t i = 0; i < words.size(); ++i) out.words[i] = words[i] & other.words[i];
        return out;
    }

    DynBitset bit_or(const DynBitset& other) const {
        DynBitset out(nbits);
        for (std::size_t i = 0; i < words.size(); ++i) out.words[i] = words[i] | other.words[i];
        out.trim_last();
        return out;
    }

    template <class F>
    void for_each_set_bit(F&& f) const {
        for (std::size_t wi = 0; wi < words.size(); ++wi) {
            uint64_t w = words[wi];
            while (w) {
                unsigned b = std::countr_zero(w);
                std::size_t idx = wi * 64 + b;
                if (idx < nbits) f(idx);
                w &= w - 1;
            }
        }
    }

    bool save_binary(const std::filesystem::path& path) const {
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        uint64_t n = static_cast<uint64_t>(nbits);
        uint64_t m = static_cast<uint64_t>(words.size());
        out.write(reinterpret_cast<const char*>(&n), sizeof(n));
        out.write(reinterpret_cast<const char*>(&m), sizeof(m));
        out.write(reinterpret_cast<const char*>(words.data()), static_cast<std::streamsize>(words.size()*sizeof(uint64_t)));
        return bool(out);
    }

    bool load_binary(const std::filesystem::path& path, std::size_t expected_nbits) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        uint64_t n = 0, m = 0;
        in.read(reinterpret_cast<char*>(&n), sizeof(n));
        in.read(reinterpret_cast<char*>(&m), sizeof(m));
        if (!in || n != expected_nbits) return false;
        std::vector<uint64_t> tmp(static_cast<std::size_t>(m));
        in.read(reinterpret_cast<char*>(tmp.data()), static_cast<std::streamsize>(tmp.size()*sizeof(uint64_t)));
        if (!in) return false;
        nbits = static_cast<std::size_t>(n);
        words = std::move(tmp);
        trim_last();
        return true;
    }
};

struct Ineq {
    uint16_t subset_mask = 0;
    uint8_t k = 0;

    bool operator<(const Ineq& o) const {
        return std::tie(k, subset_mask) < std::tie(o.k, o.subset_mask);
    }
    bool operator==(const Ineq& o) const {
        return subset_mask == o.subset_mask && k == o.k;
    }
};

class HypersimplexR3 {
public:
    int n = 0;
    int num_vertices = 0;
    std::vector<uint16_t> vertex_elem_masks;
    std::vector<int> vertex_index;
    uint64_t all_vertices_mask = 0;
    std::vector<uint64_t> coord0_mask;
    std::vector<uint64_t> coord1_mask;
    std::vector<std::array<uint64_t,4>> le_mask;
    std::vector<std::array<uint64_t,4>> eq_mask;

    HypersimplexR3() = default;
    explicit HypersimplexR3(int n_) { init(n_); }

    void init(int n_) {
        if (n_ < 3 || n_ > 8) throw std::runtime_error("This implementation supports 3 <= n <= 8.");
        n = n_;
        const int sub_count = 1 << n;
        vertex_index.assign(sub_count, -1);
        vertex_elem_masks.clear();
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                for (int k = j + 1; k < n; ++k) {
                    uint16_t m = static_cast<uint16_t>((1u << i) | (1u << j) | (1u << k));
                    vertex_index[m] = static_cast<int>(vertex_elem_masks.size());
                    vertex_elem_masks.push_back(m);
                }
            }
        }
        num_vertices = static_cast<int>(vertex_elem_masks.size());
        all_vertices_mask = (num_vertices == 64) ? ~uint64_t{0} : ((uint64_t{1} << num_vertices) - 1);

        coord0_mask.assign(n, 0);
        coord1_mask.assign(n, 0);
        for (int vi = 0; vi < num_vertices; ++vi) {
            uint16_t vm = vertex_elem_masks[vi];
            for (int e = 0; e < n; ++e) {
                if ((vm >> e) & 1u) coord1_mask[e] |= uint64_t{1} << vi;
                else coord0_mask[e] |= uint64_t{1} << vi;
            }
        }

        le_mask.assign(sub_count, {});
        eq_mask.assign(sub_count, {});
        for (int S = 0; S < sub_count; ++S) {
            for (int kk = 0; kk <= 3; ++kk) {
                uint64_t le = 0, eq = 0;
                for (int vi = 0; vi < num_vertices; ++vi) {
                    int c = std::popcount(static_cast<unsigned>(vertex_elem_masks[vi] & S));
                    if (c <= kk) le |= uint64_t{1} << vi;
                    if (c == kk) eq |= uint64_t{1} << vi;
                }
                le_mask[S][kk] = le;
                eq_mask[S][kk] = eq;
            }
        }
    }

    int index_of_vertex_mask(uint16_t m) const {
        if (m >= vertex_index.size() || vertex_index[m] < 0) {
            throw std::runtime_error("Not a rank-3 vertex mask.");
        }
        return vertex_index[m];
    }

    uint16_t permute_subset_by_array(uint16_t subset, const std::array<uint8_t,8>& perm) const {
        uint16_t out = 0;
        uint16_t m = subset;
        while (m) {
            unsigned b = std::countr_zero(static_cast<unsigned>(m));
            out |= static_cast<uint16_t>(1u << perm[b]);
            m &= static_cast<uint16_t>(m - 1);
        }
        return out;
    }

    uint16_t permute_subset_by_vector(uint16_t subset, const std::vector<uint8_t>& perm) const {
        uint16_t out = 0;
        uint16_t m = subset;
        while (m) {
            unsigned b = std::countr_zero(static_cast<unsigned>(m));
            out |= static_cast<uint16_t>(1u << perm[b]);
            m &= static_cast<uint16_t>(m - 1);
        }
        return out;
    }

    uint64_t permute_vmask_by_vector(uint64_t vmask, const std::vector<uint8_t>& perm) const {
        uint64_t out = 0;
        uint64_t m = vmask;
        while (m) {
            unsigned vi = std::countr_zero(m);
            uint16_t elem = vertex_elem_masks[vi];
            uint16_t pe = permute_subset_by_vector(elem, perm);
            int dst = index_of_vertex_mask(pe);
            out |= uint64_t{1} << dst;
            m &= m - 1;
        }
        return out;
    }
};

class PermTable {
public:
    const HypersimplexR3* hs = nullptr;
    int n = 0;
    int subperm_size = 0;
    std::vector<std::array<uint8_t,8>> perms;
    std::vector<std::array<uint8_t,8>> inv_perms;
    std::vector<uint16_t> vperm_flat;
    std::vector<uint16_t> vinv_flat;
    std::vector<uint16_t> subperm_flat;
    std::unordered_map<uint64_t, int> perm_index_by_code;

    PermTable() = default;
    explicit PermTable(const HypersimplexR3& h) { init(h); }

    static uint64_t encode_perm(const std::array<uint8_t,8>& p, int n) {
        uint64_t code = 0;
        for (int i = 0; i < n; ++i) code |= (uint64_t(p[i]) << (4*i));
        return code;
    }

    void init(const HypersimplexR3& h) {
        hs = &h;
        n = h.n;
        subperm_size = 1 << n;
        perms.clear(); inv_perms.clear(); vperm_flat.clear(); vinv_flat.clear(); subperm_flat.clear(); perm_index_by_code.clear();

        std::vector<int> p(n);
        std::iota(p.begin(), p.end(), 0);
        do {
            std::array<uint8_t,8> a{};
            for (int i = 0; i < n; ++i) a[i] = static_cast<uint8_t>(p[i]);
            int idx = static_cast<int>(perms.size());
            perms.push_back(a);
            perm_index_by_code[encode_perm(a,n)] = idx;
        } while (std::next_permutation(p.begin(), p.end()));

        inv_perms.resize(perms.size());
        vperm_flat.reserve(perms.size() * h.num_vertices);
        vinv_flat.reserve(perms.size() * h.num_vertices);
        subperm_flat.reserve(perms.size() * subperm_size);

        for (std::size_t pi = 0; pi < perms.size(); ++pi) {
            auto a = perms[pi];
            std::array<uint8_t,8> inv{};
            for (int i = 0; i < n; ++i) inv[a[i]] = static_cast<uint8_t>(i);
            inv_perms[pi] = inv;

            for (int vi = 0; vi < h.num_vertices; ++vi) {
                uint16_t pe = h.permute_subset_by_array(h.vertex_elem_masks[vi], a);
                vperm_flat.push_back(static_cast<uint16_t>(h.index_of_vertex_mask(pe)));
            }
            for (int vi = 0; vi < h.num_vertices; ++vi) {
                uint16_t pe = h.permute_subset_by_array(h.vertex_elem_masks[vi], inv);
                vinv_flat.push_back(static_cast<uint16_t>(h.index_of_vertex_mask(pe)));
            }
            for (int S = 0; S < subperm_size; ++S) {
                subperm_flat.push_back(h.permute_subset_by_array(static_cast<uint16_t>(S), a));
            }
        }
    }

    int count() const { return static_cast<int>(perms.size()); }

    uint16_t permute_subset(uint16_t subset_mask, int perm_idx) const {
        return subperm_flat[static_cast<std::size_t>(perm_idx) * subperm_size + subset_mask];
    }

    uint64_t permute_vmask(uint64_t vmask, int perm_idx) const {
        const std::size_t off = static_cast<std::size_t>(perm_idx) * hs->num_vertices;
        uint64_t out = 0;
        uint64_t m = vmask;
        while (m) {
            unsigned vi = std::countr_zero(m);
            out |= uint64_t{1} << vperm_flat[off + vi];
            m &= m - 1;
        }
        return out;
    }

    uint64_t pullback_vmask(uint64_t vmask, int perm_idx) const {
        const std::size_t off = static_cast<std::size_t>(perm_idx) * hs->num_vertices;
        uint64_t out = 0;
        uint64_t m = vmask;
        while (m) {
            unsigned vi = std::countr_zero(m);
            out |= uint64_t{1} << vinv_flat[off + vi];
            m &= m - 1;
        }
        return out;
    }
};

struct CellOrbit {
    int n = 0;
    int orbit_id = 0;
    int volume_sha = 0;
    int volume_lattice = 0;
    int active_volume = 0;
    std::vector<Ineq> ineqs;
    uint64_t rep_vmask = 0;
    std::vector<uint64_t> facet_masks;
    std::unordered_set<uint64_t> face_set;

    void finalize(const HypersimplexR3& hs) {
        rep_vmask = hs.all_vertices_mask;
        for (auto ie : ineqs) {
            if (ie.k > 3) throw std::runtime_error("Inequality k outside rank-3 range.");
            rep_vmask &= hs.le_mask[ie.subset_mask][ie.k];
        }
        compute_facets(hs);
        compute_face_set();
    }

    void compute_facets(const HypersimplexR3& hs) {
        facet_masks.clear();
        auto add = [&](uint64_t f) {
            if (f != 0 && f != rep_vmask) facet_masks.push_back(f);
        };
        for (int i = 0; i < hs.n; ++i) {
            add(rep_vmask & hs.coord0_mask[i]);
            add(rep_vmask & hs.coord1_mask[i]);
        }
        for (auto ie : ineqs) {
            add(rep_vmask & hs.eq_mask[ie.subset_mask][ie.k]);
        }
        std::sort(facet_masks.begin(), facet_masks.end());
        facet_masks.erase(std::unique(facet_masks.begin(), facet_masks.end()), facet_masks.end());
    }

    void compute_face_set() {
        face_set.clear();
        std::vector<uint64_t> faces;
        auto add_face = [&](uint64_t f) {
            if (face_set.insert(f).second) faces.push_back(f);
        };
        add_face(rep_vmask);
        for (uint64_t facet : facet_masks) {
            std::size_t old_size = faces.size();
            for (std::size_t i = 0; i < old_size; ++i) {
                add_face(faces[i] & facet);
            }
        }
        face_set.insert(0);
    }
};

struct ImageRecord {
    uint16_t orbit_index = 0;
    uint32_t perm_index = 0;
    uint64_t vmask = 0;
    int active_volume = 0;
};

struct LoadedData {
    HypersimplexR3 hs;
    std::vector<CellOrbit> orbits;
};

inline int parse_int_field(const std::string& line, const std::string& name, bool required=true, int default_value=0) {
    std::regex re("\"" + name + "\"\\s*:\\s*(-?\\d+)");
    std::smatch m;
    if (std::regex_search(line, m, re)) return std::stoi(m[1].str());
    if (required) throw std::runtime_error("Missing JSON field: " + name + " in line: " + line);
    return default_value;
}

inline std::vector<Ineq> parse_ineqs(const std::string& line) {
    std::vector<Ineq> out;
    std::size_t pos = line.find("\"ineqs\"");
    if (pos == std::string::npos) return out;
    std::size_t lb = line.find('[', pos);
    std::size_t rb = line.find(']', lb);
    if (lb == std::string::npos || rb == std::string::npos || rb <= lb) return out;
    std::string body = line.substr(lb+1, rb-lb-1);
    std::regex obj_re("\\{[^\\}]*\\}");
    auto begin = std::sregex_iterator(body.begin(), body.end(), obj_re);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        std::string obj = it->str();
        int k = parse_int_field(obj, "k");
        int sm = parse_int_field(obj, "subset_mask");
        out.push_back(Ineq{static_cast<uint16_t>(sm), static_cast<uint8_t>(k)});
    }
    std::sort(out.begin(), out.end());
    return out;
}

inline std::filesystem::path find_data_file(int n, const std::filesystem::path& data_dir) {
    std::string fname = "allr3n" + std::to_string(n) + ".txt";
    std::filesystem::path p = data_dir / fname;
    if (std::filesystem::exists(p)) return p;
    std::filesystem::path p2 = data_dir / "data" / fname;
    if (std::filesystem::exists(p2)) return p2;
    throw std::runtime_error("Cannot find " + p.string() + " or " + p2.string());
}

inline LoadedData load_orbits_for_n(int n, const std::filesystem::path& data_dir, const std::string& volume_mode) {
    LoadedData d;
    d.hs.init(n);
    std::filesystem::path path = find_data_file(n, data_dir);
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open " + path.string());

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        CellOrbit o;
        o.n = parse_int_field(line, "n");
        if (o.n != n) throw std::runtime_error("Input n mismatch in " + path.string());
        o.orbit_id = parse_int_field(line, "orbit_id");
        o.volume_sha = parse_int_field(line, "volume_sha", false, parse_int_field(line, "volume", false, 0));
        o.volume_lattice = parse_int_field(line, "volume_lattice", false, 0);
        if (volume_mode == "sha") o.active_volume = o.volume_sha;
        else if (volume_mode == "lattice") o.active_volume = o.volume_lattice;
        else throw std::runtime_error("Unknown volume mode: " + volume_mode);
        o.ineqs = parse_ineqs(line);
        o.finalize(d.hs);
        d.orbits.push_back(std::move(o));
    }
    std::sort(d.orbits.begin(), d.orbits.end(), [](const CellOrbit& a, const CellOrbit& b){ return a.orbit_id < b.orbit_id; });
    return d;
}

inline std::unordered_map<int,int> expected_orbit_counts() {
    return {{4,1},{5,4},{6,15},{7,52},{8,187}};
}
inline std::unordered_map<int,int> expected_image_counts() {
    return {{4,1},{5,36},{6,801},{7,19638},{8,788763}};
}
inline std::unordered_map<int,int> expected_quotient_tiling_counts() {
    return {{4,1},{5,3},{6,26}};
}

class ImageSet {
public:
    HypersimplexR3 hs;
    std::vector<CellOrbit> orbits;
    PermTable pt;
    std::vector<ImageRecord> images;          // retained for output and compatibility
    // Structure-of-arrays hot metadata.  The search touches vmask/volume/vertex-count
    // far more often than orbit/perm labels; keep these in separate compact arrays.
    std::vector<uint64_t> vmask_list;
    std::vector<int> vol_list;
    std::vector<uint8_t> vertex_count_list;
    std::vector<uint16_t> orbit_index_list;
    std::vector<uint32_t> perm_index_list;
    std::vector<DynBitset> by_vertex;
    std::vector<int> root_rep_by_orbit_index;
    std::unordered_map<uint64_t, int> min_image_by_vmask;

    ImageSet() = default;
    ImageSet(LoadedData data, bool verbose=false, std::size_t threads=1) { init(std::move(data), verbose, threads); }

    void init(LoadedData data, bool verbose=false, std::size_t threads=1) {
        hs = std::move(data.hs);
        orbits = std::move(data.orbits);
        pt.init(hs);
        build_images(verbose, threads);
    }

    void build_images(bool verbose=false, std::size_t threads=1) {
        images.clear();
        threads = normalize_threads(threads);
        if (verbose) {
            std::cerr << "Generating labeled images from " << orbits.size()
                      << " orbit representatives and " << pt.count() << " permutations"
                      << " using " << threads << " thread" << (threads == 1 ? "" : "s") << "...\n";
        }

        std::vector<std::vector<ImageRecord>> per_orbit(orbits.size());
        parallel_for_indices(0, orbits.size(), threads, [&](std::size_t oi) {
            const CellOrbit& orb = orbits[oi];
            std::unordered_map<uint64_t, uint32_t> seen;
            seen.reserve(static_cast<std::size_t>(pt.count()));
            for (int pi = 0; pi < pt.count(); ++pi) {
                uint64_t vm = pt.permute_vmask(orb.rep_vmask, pi);
                if (seen.find(vm) == seen.end()) seen.emplace(vm, static_cast<uint32_t>(pi));
            }
            auto& local = per_orbit[oi];
            local.reserve(seen.size());
            for (auto [vm, pi] : seen) {
                local.push_back(ImageRecord{
                    static_cast<uint16_t>(oi),
                    pi,
                    vm,
                    orb.active_volume
                });
            }
        });
        for (auto& local : per_orbit) {
            images.insert(images.end(), local.begin(), local.end());
        }
        std::sort(images.begin(), images.end(), [&](const ImageRecord& a, const ImageRecord& b) {
            return std::tie(a.vmask, a.orbit_index, a.perm_index) < std::tie(b.vmask, b.orbit_index, b.perm_index);
        });

        vmask_list.resize(images.size());
        vol_list.resize(images.size());
        vertex_count_list.resize(images.size());
        orbit_index_list.resize(images.size());
        perm_index_list.resize(images.size());
        min_image_by_vmask.clear();
        root_rep_by_orbit_index.assign(orbits.size(), -1);
        for (std::size_t i = 0; i < images.size(); ++i) {
            vmask_list[i] = images[i].vmask;
            vol_list[i] = images[i].active_volume;
            vertex_count_list[i] = static_cast<uint8_t>(popcount_u64(images[i].vmask));
            orbit_index_list[i] = images[i].orbit_index;
            perm_index_list[i] = images[i].perm_index;
            auto it = min_image_by_vmask.find(images[i].vmask);
            if (it == min_image_by_vmask.end() || static_cast<int>(i) < it->second) {
                min_image_by_vmask[images[i].vmask] = static_cast<int>(i);
            }
            int& root = root_rep_by_orbit_index[images[i].orbit_index];
            if (root < 0 || images[i].vmask < images[root].vmask ||
                (images[i].vmask == images[root].vmask && static_cast<int>(i) < root)) {
                root = static_cast<int>(i);
            }
        }

        by_vertex.assign(hs.num_vertices, DynBitset(images.size()));
        for (std::size_t i = 0; i < images.size(); ++i) {
            uint64_t vm = images[i].vmask;
            while (vm) {
                unsigned v = std::countr_zero(vm);
                by_vertex[v].set(i);
                vm &= vm - 1;
            }
        }
        if (verbose) std::cerr << "Generated " << images.size() << " distinct labeled images.\n";
    }

    std::string ineq_label_for_image(std::size_t idx) const {
        const auto& im = images[idx];
        const CellOrbit& orb = orbits[im.orbit_index];
        if (orb.ineqs.empty()) return "∅";
        std::vector<Ineq> ineqs;
        ineqs.reserve(orb.ineqs.size());
        for (auto ie : orb.ineqs) {
            ineqs.push_back(Ineq{pt.permute_subset(ie.subset_mask, static_cast<int>(im.perm_index)), ie.k});
        }
        std::sort(ineqs.begin(), ineqs.end(), [](const Ineq& a, const Ineq& b) {
            return std::tie(a.subset_mask, a.k) < std::tie(b.subset_mask, b.k);
        });
        std::ostringstream os;
        for (std::size_t t = 0; t < ineqs.size(); ++t) {
            if (t) os << ',';
            uint16_t S = ineqs[t].subset_mask;
            for (int e = 0; e < hs.n; ++e) if ((S >> e) & 1u) os << (e+1);
            os << "<=" << int(ineqs[t].k);
        }
        return os.str();
    }

    std::vector<Ineq> image_ineqs(std::size_t idx) const {
        const auto& im = images[idx];
        const CellOrbit& orb = orbits[im.orbit_index];
        std::vector<Ineq> out;
        out.reserve(orb.ineqs.size());
        for (auto ie : orb.ineqs) {
            out.push_back(Ineq{pt.permute_subset(ie.subset_mask, static_cast<int>(im.perm_index)), ie.k});
        }
        return out;
    }
};

class FaceTester {
public:
    const ImageSet* is = nullptr;

    explicit FaceTester(const ImageSet& image_set) : is(&image_set) {}

    bool is_face_of_image(std::size_t image_idx, uint64_t inter_mask) const {
        const auto& hs = is->hs;
        if (inter_mask == 0) return true;
        const auto& im = is->images[image_idx];
        uint64_t cell_mask = im.vmask;
        if (inter_mask == cell_mask) return true;
        if ((inter_mask & ~cell_mask) != 0) return false;

        uint64_t closure = cell_mask;
        for (int e = 0; e < hs.n; ++e) {
            uint64_t E0 = hs.coord0_mask[e];
            if ((inter_mask & (hs.all_vertices_mask ^ E0)) == 0) {
                closure &= E0;
                if (closure == inter_mask) return true;
            }
            uint64_t E1 = hs.coord1_mask[e];
            if ((inter_mask & (hs.all_vertices_mask ^ E1)) == 0) {
                closure &= E1;
                if (closure == inter_mask) return true;
            }
        }

        const CellOrbit& orb = is->orbits[im.orbit_index];
        for (auto ie : orb.ineqs) {
            uint16_t S_img = is->pt.permute_subset(ie.subset_mask, static_cast<int>(im.perm_index));
            uint64_t E = hs.eq_mask[S_img][ie.k];
            if ((inter_mask & (hs.all_vertices_mask ^ E)) == 0) {
                closure &= E;
                if (closure == inter_mask) return true;
            }
        }
        return closure == inter_mask;
    }

    bool is_face_by_rep_pullback(std::size_t image_idx, uint64_t inter_mask) const {
        const auto& im = is->images[image_idx];
        const CellOrbit& orb = is->orbits[im.orbit_index];
        uint64_t pulled = is->pt.pullback_vmask(inter_mask, static_cast<int>(im.perm_index));
        return orb.face_set.find(pulled) != orb.face_set.end();
    }

    bool compatible(std::size_t i, std::size_t j) const {
        uint64_t inter = is->vmask_list[i] & is->vmask_list[j];
        if (popcount_u64(inter) <= 1) return true;
        return is_face_of_image(i, inter) && is_face_of_image(j, inter);
    }
};

class CompatCache {
public:
    const ImageSet* is = nullptr;
    FaceTester face;
    bool full = false;
    std::vector<DynBitset> full_rows;

    struct RowRep {
        bool sparse_incompat = false;
        DynBitset dense;
        std::vector<uint32_t> incompat;

        std::size_t bytes() const {
            if (!sparse_incompat) return dense.bytes();
            return incompat.size() * sizeof(uint32_t) + 32;
        }

        DynBitset materialize(std::size_t nbits) const {
            if (!sparse_incompat) return dense;
            DynBitset out(nbits, true);
            for (uint32_t j : incompat) out.reset(j);
            return out;
        }

        static RowRep from_dense(const DynBitset& row) {
            RowRep rep;
            const std::size_t n = row.nbits;
            const std::size_t dense_b = row.bytes();
            const std::size_t ones = row.count();
            const std::size_t zeros = n - ones;
            const std::size_t sparse_b = zeros * sizeof(uint32_t) + 32;
            // Store sparse-incompatible rows only when it is a clear memory win.
            if (sparse_b + 64 < dense_b) {
                rep.sparse_incompat = true;
                rep.incompat.reserve(zeros);
                for (std::size_t j = 0; j < n; ++j) {
                    if (!row.test(j)) rep.incompat.push_back(static_cast<uint32_t>(j));
                }
            } else {
                rep.sparse_incompat = false;
                rep.dense = row;
            }
            return rep;
        }

        bool save_binary(const std::filesystem::path& path, std::size_t nbits) const {
            std::ofstream out(path, std::ios::binary);
            if (!out) return false;
            uint64_t magic = 0x41335233434D5032ULL; // A3R3CMP2
            uint64_t n = static_cast<uint64_t>(nbits);
            uint8_t kind = sparse_incompat ? 1u : 0u;
            uint64_t payload_count = sparse_incompat
                ? static_cast<uint64_t>(incompat.size())
                : static_cast<uint64_t>(dense.words.size());
            out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
            out.write(reinterpret_cast<const char*>(&n), sizeof(n));
            out.write(reinterpret_cast<const char*>(&kind), sizeof(kind));
            out.write(reinterpret_cast<const char*>(&payload_count), sizeof(payload_count));
            if (sparse_incompat) {
                out.write(reinterpret_cast<const char*>(incompat.data()),
                          static_cast<std::streamsize>(incompat.size() * sizeof(uint32_t)));
            } else {
                out.write(reinterpret_cast<const char*>(dense.words.data()),
                          static_cast<std::streamsize>(dense.words.size() * sizeof(uint64_t)));
            }
            return bool(out);
        }

        bool load_binary(const std::filesystem::path& path, std::size_t nbits) {
            {
                std::ifstream in(path, std::ios::binary);
                if (in) {
                    uint64_t magic = 0, n = 0, payload_count = 0;
                    uint8_t kind = 0;
                    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
                    if (magic == 0x41335233434D5032ULL) {
                        in.read(reinterpret_cast<char*>(&n), sizeof(n));
                        in.read(reinterpret_cast<char*>(&kind), sizeof(kind));
                        in.read(reinterpret_cast<char*>(&payload_count), sizeof(payload_count));
                        if (!in || n != nbits) return false;
                        sparse_incompat = (kind != 0);
                        if (sparse_incompat) {
                            incompat.resize(static_cast<std::size_t>(payload_count));
                            in.read(reinterpret_cast<char*>(incompat.data()),
                                    static_cast<std::streamsize>(incompat.size() * sizeof(uint32_t)));
                            dense = DynBitset{};
                        } else {
                            dense.resize(nbits);
                            if (dense.words.size() != static_cast<std::size_t>(payload_count)) return false;
                            in.read(reinterpret_cast<char*>(dense.words.data()),
                                    static_cast<std::streamsize>(dense.words.size() * sizeof(uint64_t)));
                            dense.trim_last();
                            incompat.clear();
                        }
                        return bool(in);
                    }
                }
            }
            // Backward compatibility with the earlier dense DynBitset row format.
            DynBitset old_dense;
            if (old_dense.load_binary(path, nbits)) {
                *this = RowRep::from_dense(old_dense);
                return true;
            }
            return false;
        }
    };

    std::size_t lru_capacity = 0;
    std::list<std::size_t> lru_order;
    struct Entry { RowRep row; std::list<std::size_t>::iterator it; };
    std::unordered_map<std::size_t, Entry> lru;

    std::filesystem::path disk_dir;
    bool use_disk = false;
    std::atomic<std::size_t> hits{0}, misses{0}, disk_hits{0}, evictions{0};
    std::atomic<std::size_t> rows_dense{0}, rows_sparse{0};

    // Optional hot-row profiler.  Counts are approximate but monotone; they are
    // used only to choose a prewarm set for later runs, never for correctness.
    std::unique_ptr<std::atomic<uint32_t>[]> hot_row_counts;
    std::size_t hot_row_count = 0;

    mutable std::mutex mutex;
    mutable std::condition_variable cv;
    std::unordered_set<std::size_t> in_progress;

    std::string cache_policy = "lru"; // lru|lru-notouch|direct
    std::size_t worker_tiny_cache_size = 0;
    struct alignas(64) DirectSlot {
        mutable std::mutex mutex;
        mutable std::condition_variable cv;
        bool ready = false;
        bool computing = false;
        std::shared_ptr<const RowRep> row;
    };
    std::vector<std::unique_ptr<DirectSlot>> direct_slots;
    std::size_t direct_capacity_rows = 0;
    std::atomic<std::size_t> direct_stored_rows{0};
    std::atomic<std::size_t> direct_payload_bytes{0};

    CompatCache(const ImageSet& image_set, std::size_t mem_rows=0, std::filesystem::path disk_dir_={}, std::size_t threads=1,
                std::string cache_policy_="lru", std::size_t worker_tiny_cache_size_=0)
        : is(&image_set), face(image_set), lru_capacity(mem_rows), disk_dir(std::move(disk_dir_)), worker_tiny_cache_size(worker_tiny_cache_size_) {
        full = image_set.hs.n <= 6;
        use_disk = !disk_dir.empty();
        if (use_disk) std::filesystem::create_directories(disk_dir);
        if (full) {
            cache_policy = "lru";
            precompute_full(threads);
        } else {
            if (cache_policy_ == "auto") {
                // On large-memory n=8 runs, direct row slots avoid a global LRU hit
                // mutation.  On small budgets, the bounded LRU gives better reuse.
                cache_policy = (mem_rows >= 4096 ? "direct" : "lru");
            } else {
                cache_policy = std::move(cache_policy_);
            }
            if (cache_policy != "lru" && cache_policy != "lru-notouch" && cache_policy != "direct") {
                throw std::runtime_error("--compat-cache-policy must be auto, lru, lru-notouch, or direct.");
            }
            if (cache_policy == "direct") {
                direct_capacity_rows = mem_rows ? mem_rows : image_set.images.size();
                direct_slots.reserve(image_set.images.size());
                for (std::size_t i = 0; i < image_set.images.size(); ++i) {
                    direct_slots.push_back(std::make_unique<DirectSlot>());
                }
            }
        }
    }


    void enable_hot_row_recording() {
        hot_row_count = is ? is->images.size() : 0;
        if (hot_row_count == 0) return;
        hot_row_counts.reset(new std::atomic<uint32_t>[hot_row_count]);
        for (std::size_t i = 0; i < hot_row_count; ++i) hot_row_counts[i].store(0, std::memory_order_relaxed);
    }

    void note_row_access(std::size_t i) const {
        if (!hot_row_counts || i >= hot_row_count) return;
        hot_row_counts[i].fetch_add(1, std::memory_order_relaxed);
    }

    void save_hot_rows(const std::filesystem::path& path) const {
        if (path.empty() || !hot_row_counts) return;
        std::vector<std::pair<uint32_t,std::size_t>> rows;
        rows.reserve(hot_row_count);
        for (std::size_t i = 0; i < hot_row_count; ++i) {
            uint32_t c = hot_row_counts[i].load(std::memory_order_relaxed);
            if (c) rows.emplace_back(c, i);
        }
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
        });
        std::ofstream out(path);
        if (!out) return;
        out << "# row_id hits\n";
        for (auto [c,i] : rows) out << i << " " << c << "\n";
    }

    static std::vector<std::size_t> load_hot_row_ids(const std::filesystem::path& path, std::size_t limit) {
        std::vector<std::size_t> ids;
        if (path.empty()) return ids;
        std::ifstream in(path);
        if (!in) return ids;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            std::istringstream iss(line);
            std::size_t id = 0;
            if (iss >> id) {
                ids.push_back(id);
                if (limit && ids.size() >= limit) break;
            }
        }
        return ids;
    }

    void prewarm_hot_file(const std::filesystem::path& path, std::size_t limit, bool verbose=false, std::size_t threads=1) {
        auto ids = load_hot_row_ids(path, limit);
        if (ids.empty() || full) return;
        ids.erase(std::remove_if(ids.begin(), ids.end(), [&](std::size_t id){ return id >= is->images.size(); }), ids.end());
        std::atomic<std::size_t> done{0};
        parallel_for_indices(0, ids.size(), threads, [&](std::size_t t) {
            (void)get(ids[t]);
            std::size_t d = done.fetch_add(1, std::memory_order_relaxed) + 1;
            if (verbose && (d % 64 == 0 || d == ids.size())) {
                static std::mutex progress_mutex;
                std::lock_guard<std::mutex> lock(progress_mutex);
                std::cerr << "prewarmed hot compat rows " << d << "/" << ids.size()
                          << " using " << threads << " thread" << (threads == 1 ? "" : "s") << "\n";
            }
        });
    }

    std::filesystem::path row_path(std::size_t i) const {
        std::ostringstream name;
        name << "compat_row_" << std::setw(8) << std::setfill('0') << i << ".bin";
        return disk_dir / name.str();
    }

    void precompute_full(std::size_t threads=1) {
        std::size_t m = is->images.size();
        full_rows.assign(m, DynBitset(m));
        parallel_for_indices(0, m, threads, [&](std::size_t i) {
            DynBitset row(m);
            row.set(i);
            for (std::size_t j = 0; j < m; ++j) {
                if (j == i) continue;
                if (face.compatible(i,j)) row.set(j);
            }
            full_rows[i] = std::move(row);
        });
    }

    std::shared_ptr<const RowRep> get_rep_ptr_direct(std::size_t i) {
        if (full) {
            auto rep = std::make_shared<RowRep>();
            rep->sparse_incompat = false;
            rep->dense = full_rows[i];
            return rep;
        }
        if (cache_policy != "direct") {
            return std::make_shared<RowRep>(get_rep(i));
        }

        DirectSlot& slot = *direct_slots[i];
        for (;;) {
            std::unique_lock<std::mutex> lock(slot.mutex);
            if (slot.ready && slot.row) {
                hits.fetch_add(1, std::memory_order_relaxed);
                return slot.row;
            }
            if (!slot.computing) {
                slot.computing = true;
                break;
            }
            slot.cv.wait(lock);
        }

        RowRep rep;
        if (use_disk && rep.load_binary(row_path(i), is->images.size())) {
            disk_hits.fetch_add(1, std::memory_order_relaxed);
        } else {
            misses.fetch_add(1, std::memory_order_relaxed);
            DynBitset row = compute_row(i);
            rep = RowRep::from_dense(row);
            if (use_disk) rep.save_binary(row_path(i), is->images.size());
        }
        auto ptr = std::make_shared<RowRep>(std::move(rep));

        bool store = true;
        if (direct_capacity_rows > 0) {
            std::size_t prev = direct_stored_rows.fetch_add(1, std::memory_order_relaxed);
            if (prev >= direct_capacity_rows) {
                direct_stored_rows.fetch_sub(1, std::memory_order_relaxed);
                store = false;
            }
        }
        {
            std::lock_guard<std::mutex> lock(slot.mutex);
            slot.computing = false;
            if (store && !slot.ready) {
                slot.row = ptr;
                slot.ready = true;
                direct_payload_bytes.fetch_add(ptr->bytes(), std::memory_order_relaxed);
                if (ptr->sparse_incompat) rows_sparse.fetch_add(1, std::memory_order_relaxed);
                else rows_dense.fetch_add(1, std::memory_order_relaxed);
            }
        }
        slot.cv.notify_all();
        return ptr;
    }

    RowRep get_rep(std::size_t i) {
        if (full) {
            RowRep rep;
            rep.sparse_incompat = false;
            rep.dense = full_rows[i];
            return rep;
        }

        {
            std::unique_lock<std::mutex> lock(mutex);
            for (;;) {
                auto it = lru.find(i);
                if (it != lru.end()) {
                    hits.fetch_add(1, std::memory_order_relaxed);
                    // lru-notouch is a benchmark/large-core option: return the row
                    // without mutating recency state on every hit.  This keeps the
                    // same exact compatibility relation and bounded cache, but can
                    // reduce global-cache lock pressure when evictions are rare.
                    if (cache_policy != "lru-notouch") {
                        lru_order.erase(it->second.it);
                        lru_order.push_front(i);
                        it->second.it = lru_order.begin();
                    }
                    return it->second.row;
                }

                if (in_progress.find(i) == in_progress.end()) {
                    in_progress.insert(i);
                    break;
                }
                cv.wait(lock);
            }
        }

        RowRep rep;
        bool loaded = false;
        if (use_disk && rep.load_binary(row_path(i), is->images.size())) {
            disk_hits.fetch_add(1, std::memory_order_relaxed);
            loaded = true;
        } else {
            misses.fetch_add(1, std::memory_order_relaxed);
            DynBitset row = compute_row(i);
            rep = RowRep::from_dense(row);
            if (use_disk) rep.save_binary(row_path(i), is->images.size());
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            put_lru_unlocked(i, rep);
            in_progress.erase(i);
        }
        cv.notify_all();
        (void)loaded;
        return rep;
    }

    DynBitset get(std::size_t i) {
        if (cache_policy == "direct" && !full) return get_rep_ptr_direct(i)->materialize(is->images.size());
        return get_rep(i).materialize(is->images.size());
    }

    void apply_rep_to_allowed(const RowRep& rep, const DynBitset& allowed, DynBitset& out) const {
        if (rep.sparse_incompat) {
            out.copy_from(allowed);
            for (uint32_t j : rep.incompat) {
                if (j < out.nbits) out.reset(static_cast<std::size_t>(j));
            }
        } else {
            out.assign_and(allowed, rep.dense);
        }
    }

    void intersect_into(std::size_t i, const DynBitset& allowed, DynBitset& out) {
        note_row_access(i);
        if (full) {
            out.assign_and(allowed, full_rows[i]);
            return;
        }

        struct TinyEntry {
            std::size_t id = std::numeric_limits<std::size_t>::max();
            std::shared_ptr<const RowRep> row;
        };
        struct TinyCache {
            const CompatCache* owner = nullptr;
            std::vector<TinyEntry> entries;
            std::size_t clock = 0;
        };
        thread_local TinyCache tls;

        auto lookup_tiny = [&]() -> std::shared_ptr<const RowRep> {
            if (worker_tiny_cache_size == 0) return {};
            if (tls.owner != this) {
                tls.owner = this;
                tls.entries.clear();
                tls.clock = 0;
            }
            for (auto& e : tls.entries) {
                if (e.id == i && e.row) return e.row;
            }
            return {};
        };

        auto insert_tiny = [&](std::shared_ptr<const RowRep> ptr) {
            if (worker_tiny_cache_size == 0 || !ptr) return;
            if (tls.owner != this) {
                tls.owner = this;
                tls.entries.clear();
                tls.clock = 0;
            }
            if (tls.entries.size() < worker_tiny_cache_size) {
                tls.entries.push_back(TinyEntry{i, std::move(ptr)});
            } else if (!tls.entries.empty()) {
                std::size_t pos = tls.clock++ % tls.entries.size();
                tls.entries[pos] = TinyEntry{i, std::move(ptr)};
            }
        };

        if (auto cached = lookup_tiny()) {
            apply_rep_to_allowed(*cached, allowed, out);
            return;
        }

        std::shared_ptr<const RowRep> ptr;
        if (cache_policy == "direct") {
            ptr = get_rep_ptr_direct(i);
        } else {
            RowRep rep = get_rep(i);
            ptr = std::make_shared<RowRep>(std::move(rep));
        }
        insert_tiny(ptr);
        apply_rep_to_allowed(*ptr, allowed, out);
    }

    void put_lru_unlocked(std::size_t i, RowRep rep) {
        if (lru_capacity == 0) return;
        auto it = lru.find(i);
        if (it != lru.end()) {
            lru_order.erase(it->second.it);
            lru.erase(it);
        }
        if (rep.sparse_incompat) rows_sparse.fetch_add(1, std::memory_order_relaxed);
        else rows_dense.fetch_add(1, std::memory_order_relaxed);
        lru_order.push_front(i);
        lru.emplace(i, Entry{std::move(rep), lru_order.begin()});
        while (lru.size() > lru_capacity) {
            std::size_t old = lru_order.back();
            lru_order.pop_back();
            lru.erase(old);
            evictions.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void put_lru(std::size_t i, const DynBitset& row) {
        std::lock_guard<std::mutex> lock(mutex);
        put_lru_unlocked(i, RowRep::from_dense(row));
    }

    void clear_lru() {
        if (cache_policy == "direct" && !full) {
            for (auto& sp : direct_slots) {
                std::lock_guard<std::mutex> lock(sp->mutex);
                sp->ready = false;
                sp->computing = false;
                sp->row.reset();
            }
            direct_stored_rows.store(0, std::memory_order_relaxed);
            direct_payload_bytes.store(0, std::memory_order_relaxed);
            return;
        }
        std::lock_guard<std::mutex> lock(mutex);
        lru.clear();
        lru_order.clear();
    }

    std::size_t lru_size() const {
        if (cache_policy == "direct" && !full) return direct_stored_rows.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mutex);
        return lru.size();
    }

    std::size_t lru_payload_bytes() const {
        if (cache_policy == "direct" && !full) return direct_payload_bytes.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mutex);
        std::size_t b = 0;
        for (const auto& kv : lru) b += kv.second.row.bytes();
        return b;
    }

    DynBitset compute_row(std::size_t i) const {
        const std::size_t m = is->images.size();
        DynBitset seen(m), two(m);
        uint64_t vm = is->vmask_list[i];
        while (vm) {
            unsigned v = std::countr_zero(vm);
            two.or_assign_intersection(seen, is->by_vertex[v]);
            seen.or_assign(is->by_vertex[v]);
            vm &= vm - 1;
        }

        DynBitset compat(m, true);
        std::unordered_map<uint64_t, bool> face_cache_i;
        two.for_each_set_bit([&](std::size_t j) {
            if (j == i) return;
            uint64_t inter = is->vmask_list[i] & is->vmask_list[j];
            if (popcount_u64(inter) <= 1) return;
            bool ok_i;
            auto f = face_cache_i.find(inter);
            if (f == face_cache_i.end()) {
                ok_i = face.is_face_of_image(i, inter);
                face_cache_i.emplace(inter, ok_i);
            } else ok_i = f->second;
            if (!ok_i || !face.is_face_of_image(j, inter)) compat.reset(j);
        });
        return compat;
    }

    void prewarm(std::size_t k, bool verbose=false, std::size_t threads=1) {
        if (k == 0 || full) return;
        threads = normalize_threads(threads);
        std::vector<std::pair<int,std::size_t>> scored;
        scored.reserve(is->images.size());
        for (std::size_t i = 0; i < is->images.size(); ++i) {
            scored.emplace_back(-popcount_u64(is->vmask_list[i]), i);
        }
        std::sort(scored.begin(), scored.end());
        k = std::min(k, scored.size());
        std::atomic<std::size_t> done{0};
        parallel_for_indices(0, k, threads, [&](std::size_t t) {
            (void)get(scored[t].second);
            std::size_t d = done.fetch_add(1, std::memory_order_relaxed) + 1;
            if (verbose && (d % 16 == 0 || d == k)) {
                static std::mutex progress_mutex;
                std::lock_guard<std::mutex> lock(progress_mutex);
                std::cerr << "prewarmed " << d << "/" << k << " compat rows"
                          << " using " << threads << " thread" << (threads == 1 ? "" : "s") << "\n";
            }
        });
    }
};

class Canonicalizer {
public:
    const ImageSet* is = nullptr;
    std::string mode = "refine";
    std::size_t memo_max = 200000;

    struct MemoEntry {
        std::string value;
        std::list<std::string>::iterator it;
    };
    mutable std::list<std::string> memo_lru;
    mutable std::unordered_map<std::string, MemoEntry> memo;
    mutable std::mutex memo_mutex;

    Canonicalizer(const ImageSet& image_set, std::string mode_="graph", std::size_t memo_max_=200000)
        : is(&image_set), mode(std::move(mode_)), memo_max(memo_max_) {}

    std::optional<std::string> memo_get(const std::string& key) const {
        if (!memo_max) return std::nullopt;
        std::lock_guard<std::mutex> lock(memo_mutex);
        auto it = memo.find(key);
        if (it == memo.end()) return std::nullopt;
        memo_lru.splice(memo_lru.begin(), memo_lru, it->second.it);
        it->second.it = memo_lru.begin();
        return it->second.value;
    }

    void memo_put(std::string key, std::string value) const {
        if (!memo_max) return;
        std::lock_guard<std::mutex> lock(memo_mutex);
        auto it = memo.find(key);
        if (it != memo.end()) {
            it->second.value = std::move(value);
            memo_lru.splice(memo_lru.begin(), memo_lru, it->second.it);
            it->second.it = memo_lru.begin();
            return;
        }
        memo_lru.push_front(key);
        memo.emplace(key, MemoEntry{std::move(value), memo_lru.begin()});
        while (memo.size() > memo_max && !memo_lru.empty()) {
            std::string old = std::move(memo_lru.back());
            memo_lru.pop_back();
            memo.erase(old);
        }
    }

    void clear_memo() const {
        std::lock_guard<std::mutex> lock(memo_mutex);
        memo.clear();
        memo_lru.clear();
    }

    std::string vmasks_memo_key(char tag, const std::vector<uint64_t>& vmasks) const {
        std::vector<uint64_t> sorted = vmasks;
        std::sort(sorted.begin(), sorted.end());
        std::string key;
        key.reserve(1 + sorted.size() * 8);
        key.push_back(tag);
        for (uint64_t v : sorted) append_u64_le(key, v);
        return key;
    }

    std::string raw_indices_key(const std::vector<int>& chosen) const {
        std::ostringstream os;
        for (std::size_t i = 0; i < chosen.size(); ++i) {
            if (i) os << ',';
            os << chosen[i];
        }
        return os.str();
    }

    std::string canonical_key_for_indices(const std::vector<int>& chosen) {
        if (chosen.empty()) return "";
        return canonical_key_for_vmasks(vmasks_for_indices(chosen));
    }

    std::vector<uint64_t> vmasks_for_indices(const std::vector<int>& chosen) const {
        std::vector<uint64_t> vmasks;
        vmasks.reserve(chosen.size());
        for (int idx : chosen) vmasks.push_back(is->vmask_list[idx]);
        return vmasks;
    }

    std::vector<uint64_t> canonical_tuple_brute(const std::vector<uint64_t>& vmasks) const {
        std::vector<uint64_t> best;
        bool have = false;
        for (int pi = 0; pi < is->pt.count(); ++pi) {
            std::vector<uint64_t> cur;
            cur.reserve(vmasks.size());
            for (uint64_t vm : vmasks) cur.push_back(is->pt.permute_vmask(vm, pi));
            std::sort(cur.begin(), cur.end());
            if (!have || cur < best) {
                best = std::move(cur);
                have = true;
            }
        }
        return best;
    }

    std::string canonical_key_brute(const std::vector<uint64_t>& vmasks) const {
        return join_key(canonical_tuple_brute(vmasks));
    }

    std::vector<uint64_t> canonical_tuple_for_indices(const std::vector<int>& chosen) const {
        std::vector<uint64_t> vmasks = vmasks_for_indices(chosen);
        if (mode == "brute") return canonical_tuple_brute(vmasks);
        if (mode == "refine") return canonical_tuple_refine(chosen, vmasks);
        return canonical_tuple_graph_vmasks(vmasks);
    }

    std::string canonical_key_for_vmasks(const std::vector<uint64_t>& vmasks) const {
        if (vmasks.empty()) return "";
        char tag = (mode == "brute") ? 'B' : (mode == "refine" ? 'R' : 'G');
        std::string mkey = vmasks_memo_key(tag, vmasks);
        if (auto got = memo_get(mkey)) return *got;

        std::string ans;
        if (mode == "brute") ans = canonical_key_brute(vmasks);
        else if (mode == "refine") ans = canonical_key_refine_vmasks(vmasks);
        else ans = canonical_key_graph_vmasks(vmasks);

        memo_put(std::move(mkey), ans);
        return ans;
    }

    // Canonical construction-path parent.  First put the child state in canonical
    // form, delete the lexicographically last cell of that canonical tuple, and then
    // canonicalize the remaining tuple again.  The second canonicalization is
    // essential: the inherited labelling of the parent need not be a canonical
    // labelling of the parent itself.
    std::string canonical_parent_key_for_indices(const std::vector<int>& child) const {
        if (child.empty()) return "";
        std::vector<uint64_t> ct = canonical_tuple_for_indices(child);
        if (ct.empty()) return "";
        ct.pop_back();
        return canonical_key_for_vmasks(ct);
    }

    std::string canonical_key_brute_for_indices(const std::vector<int>& chosen) const {
        return canonical_key_brute(vmasks_for_indices(chosen));
    }

    std::string canonical_parent_key_brute_for_indices(const std::vector<int>& child) const {
        if (child.empty()) return "";
        std::vector<uint64_t> ct = canonical_tuple_brute(vmasks_for_indices(child));
        if (ct.empty()) return "";
        ct.pop_back();
        return canonical_key_brute(ct);
    }

    static int feature_index(int k, int size, int n) {
        if (k == 1) return size;
        if (k == 2) return (n + 1) + size;
        return 2 * (n + 1);
    }

    struct RefineData {
        std::vector<int> colors;
        std::vector<std::vector<int>> blocks;
    };

    RefineData refine_blocks_for_vmasks(std::vector<uint64_t> cell_vms) const {
        // Exact, isomorphism-invariant refinement derived only from the unlabelled
        // polytopal state (the multiset of cell vertex masks).  This avoids using
        // presentation-dependent inequality data in the quotient key.
        int n = is->hs.n;
        std::sort(cell_vms.begin(), cell_vms.end());

        std::vector<std::vector<int>> elem(n);
        std::vector<std::vector<std::vector<int>>> pair(n, std::vector<std::vector<int>>(n));

        for (int e = 0; e < n; ++e) {
            elem[e].reserve(cell_vms.size() + 4);
            for (uint64_t vm : cell_vms) {
                elem[e].push_back(popcount_u64(vm & is->hs.coord1_mask[e]));
            }
            std::sort(elem[e].begin(), elem[e].end());
            int total = std::accumulate(elem[e].begin(), elem[e].end(), 0);
            elem[e].push_back(-1);
            elem[e].push_back(total);
        }

        for (int a = 0; a < n; ++a) {
            for (int b = 0; b < n; ++b) {
                if (a == b) {
                    pair[a][b] = {0};
                    continue;
                }
                uint64_t pab = is->hs.coord1_mask[a] & is->hs.coord1_mask[b];
                pair[a][b].reserve(cell_vms.size() + 4);
                for (uint64_t vm : cell_vms) {
                    pair[a][b].push_back(popcount_u64(vm & pab));
                }
                std::sort(pair[a][b].begin(), pair[a][b].end());
                int total = std::accumulate(pair[a][b].begin(), pair[a][b].end(), 0);
                pair[a][b].push_back(-1);
                pair[a][b].push_back(total);
            }
        }

        std::map<std::vector<int>, int> init_map;
        std::vector<int> colors(n);
        for (int i = 0; i < n; ++i) {
            auto [it, ins] = init_map.emplace(elem[i], static_cast<int>(init_map.size()));
            colors[i] = it->second;
        }

        bool changed = true;
        while (changed) {
            std::vector<std::vector<int>> sigs(n);
            for (int i = 0; i < n; ++i) {
                std::vector<std::vector<int>> neigh;
                for (int j = 0; j < n; ++j) if (i != j) {
                    std::vector<int> item;
                    item.reserve(1 + pair[i][j].size());
                    item.push_back(colors[j]);
                    item.insert(item.end(), pair[i][j].begin(), pair[i][j].end());
                    neigh.push_back(std::move(item));
                }
                std::sort(neigh.begin(), neigh.end());
                std::vector<int> sig;
                sig.push_back(colors[i]);
                for (const auto& item : neigh) {
                    sig.push_back(-2);
                    sig.insert(sig.end(), item.begin(), item.end());
                }
                sigs[i] = std::move(sig);
            }
            std::map<std::vector<int>, int> mp;
            std::vector<int> newc(n);
            for (int i = 0; i < n; ++i) {
                auto [it, ins] = mp.emplace(sigs[i], static_cast<int>(mp.size()));
                newc[i] = it->second;
            }
            if (static_cast<int>(mp.size()) == n) {
                colors = std::move(newc);
                break;
            }
            changed = (newc != colors);
            colors = std::move(newc);
        }

        std::map<int, std::vector<int>> block_map;
        for (int i = 0; i < n; ++i) block_map[colors[i]].push_back(i);
        std::vector<std::vector<int>> blocks;
        for (auto& [c,b] : block_map) blocks.push_back(b);
        return RefineData{colors, blocks};
    }

    RefineData refine_blocks_for_indices(const std::vector<int>& chosen) const {
        return refine_blocks_for_vmasks(vmasks_for_indices(chosen));
    }

    template<class F>
    void enumerate_block_to_interval_perms(const std::vector<std::vector<int>>& blocks, F&& f) const {
        int n = is->hs.n;
        std::vector<std::vector<int>> targets;
        int off = 0;
        for (const auto& b : blocks) {
            std::vector<int> t;
            for (int x = 0; x < static_cast<int>(b.size()); ++x) t.push_back(off + x);
            off += static_cast<int>(b.size());
            targets.push_back(std::move(t));
        }
        std::vector<uint8_t> perm(n, 0);

        std::function<void(int)> rec = [&](int bi) {
            if (bi == static_cast<int>(blocks.size())) {
                f(perm);
                return;
            }
            std::vector<int> bord = blocks[bi];
            std::sort(bord.begin(), bord.end());
            do {
                for (std::size_t j = 0; j < bord.size(); ++j) {
                    perm[bord[j]] = static_cast<uint8_t>(targets[bi][j]);
                }
                rec(bi+1);
            } while (std::next_permutation(bord.begin(), bord.end()));
        };
        rec(0);
    }

    template<class F>
    void enumerate_within_block_perms(const std::vector<std::vector<int>>& blocks, F&& f) const {
        int n = is->hs.n;
        std::vector<uint8_t> perm(n);
        for (int i = 0; i < n; ++i) perm[i] = static_cast<uint8_t>(i);

        std::function<void(int)> rec = [&](int bi) {
            if (bi == static_cast<int>(blocks.size())) {
                f(perm);
                return;
            }
            std::vector<int> img = blocks[bi];
            std::sort(img.begin(), img.end());
            do {
                for (std::size_t j = 0; j < img.size(); ++j) {
                    perm[blocks[bi][j]] = static_cast<uint8_t>(img[j]);
                }
                rec(bi+1);
            } while (std::next_permutation(img.begin(), img.end()));
        };
        rec(0);
    }

    std::vector<uint64_t> canonical_tuple_refine_from_blocks(const std::vector<std::vector<int>>& blocks,
                                                             const std::vector<uint64_t>& vmasks) const {
        std::vector<uint64_t> best;
        bool have = false;
        enumerate_block_to_interval_perms(blocks, [&](const std::vector<uint8_t>& perm) {
            std::vector<uint64_t> cur;
            cur.reserve(vmasks.size());
            for (uint64_t vm : vmasks) cur.push_back(is->hs.permute_vmask_by_vector(vm, perm));
            std::sort(cur.begin(), cur.end());
            if (!have || cur < best) {
                best = std::move(cur);
                have = true;
            }
        });
        return best;
    }

    std::vector<uint64_t> canonical_tuple_refine(const std::vector<int>& chosen, const std::vector<uint64_t>& vmasks) const {
        RefineData rd = refine_blocks_for_indices(chosen);
        return canonical_tuple_refine_from_blocks(rd.blocks, vmasks);
    }

    std::string canonical_key_refine(const std::vector<int>& chosen, const std::vector<uint64_t>& vmasks) const {
        return join_key(canonical_tuple_refine(chosen, vmasks));
    }

    std::string canonical_key_refine_vmasks(const std::vector<uint64_t>& vmasks) const {
        RefineData rd = refine_blocks_for_vmasks(vmasks);
        return join_key(canonical_tuple_refine_from_blocks(rd.blocks, vmasks));
    }

    RefineData graph_refine_blocks_for_vmasks(std::vector<uint64_t> cell_vms) const {
        // Exact colored-incidence-graph refinement.
        //
        // Graph nodes:
        //   ground-element nodes i;
        //   hypersimplex basis-vertex nodes B, one for each 3-subset;
        //   cell nodes C, one for each cell in the partial state.
        // Edges:
        //   i -- B iff i is in the 3-subset B;
        //   B -- C iff B is a vertex of cell C.
        //
        // The colors are isomorphism-invariant, and the later canonical tuple
        // computation exhaustively enumerates all permutations inside each final
        // ground color block.  Thus the backend is exact: refinement only reduces
        // the search space; it is not trusted to decide isomorphism by itself.
        int n = is->hs.n;
        int bv_count = is->hs.num_vertices;
        std::sort(cell_vms.begin(), cell_vms.end());

        const int ground_off = 0;
        const int basis_off = n;
        const int cell_off = n + bv_count;
        const int node_count = cell_off + static_cast<int>(cell_vms.size());

        std::vector<std::vector<int>> adj(static_cast<std::size_t>(node_count));
        auto add_edge = [&](int a, int b) {
            adj[a].push_back(b);
            adj[b].push_back(a);
        };

        for (int vi = 0; vi < bv_count; ++vi) {
            uint16_t em = is->hs.vertex_elem_masks[vi];
            for (int e = 0; e < n; ++e) {
                if ((em >> e) & 1u) add_edge(ground_off + e, basis_off + vi);
            }
        }

        for (std::size_t ci = 0; ci < cell_vms.size(); ++ci) {
            uint64_t vm = cell_vms[ci];
            while (vm) {
                unsigned vi = std::countr_zero(vm);
                add_edge(basis_off + static_cast<int>(vi), cell_off + static_cast<int>(ci));
                vm &= vm - 1;
            }
        }

        for (auto& a : adj) std::sort(a.begin(), a.end());

        std::vector<int> colors(static_cast<std::size_t>(node_count), 0);
        for (int i = 0; i < node_count; ++i) {
            if (i < basis_off) colors[i] = 0;                       // ground nodes
            else if (i < cell_off) colors[i] = 1;                   // basis-vertex nodes
            else colors[i] = 2 + static_cast<int>(adj[i].size());   // cell nodes, refined by vertex count
        }

        bool changed = true;
        while (changed) {
            std::vector<std::vector<int>> sigs(static_cast<std::size_t>(node_count));
            for (int i = 0; i < node_count; ++i) {
                std::vector<int> sig;
                sig.reserve(adj[i].size() + 4);
                sig.push_back(colors[i]);
                sig.push_back(-1);
                sig.push_back(static_cast<int>(adj[i].size()));
                for (int nb : adj[i]) sig.push_back(colors[nb]);
                std::sort(sig.begin() + 3, sig.end());
                sigs[i] = std::move(sig);
            }

            std::map<std::vector<int>, int> mp;
            std::vector<int> newc(static_cast<std::size_t>(node_count));
            for (int i = 0; i < node_count; ++i) {
                auto [it, ins] = mp.emplace(sigs[i], static_cast<int>(mp.size()));
                newc[i] = it->second;
            }
            changed = (newc != colors);
            colors = std::move(newc);
        }

        std::map<int, std::vector<int>> block_map;
        for (int i = 0; i < n; ++i) block_map[colors[i]].push_back(i);
        std::vector<std::vector<int>> blocks;
        for (auto& [c, b] : block_map) blocks.push_back(b);
        return RefineData{std::vector<int>(colors.begin(), colors.begin() + n), blocks};
    }

    std::vector<uint64_t> canonical_tuple_graph_vmasks(const std::vector<uint64_t>& vmasks) const {
        // Safety-critical canonical augmentation needs a canonical parent map that
        // has been validated against the known n<=6 cases.  The incidence graph is
        // still built and used for exact automorphism block refinement, but the
        // canonical tuple itself currently takes the full ground-permutation minimum.
        // Since n<=8, this exhaustive fallback is bounded by 40320 permutations.
        // The bounded LRU memo avoids recomputing common parent keys.
        (void)graph_refine_blocks_for_vmasks(vmasks); // keep graph backend path exercised
        return canonical_tuple_brute(vmasks);
    }

    std::string canonical_key_graph_vmasks(const std::vector<uint64_t>& vmasks) const {
        return join_key(canonical_tuple_graph_vmasks(vmasks));
    }

    std::string canonical_key_graph_cached_vmasks(const std::vector<uint64_t>& vmasks) const {
        if (vmasks.empty()) return "";
        std::string mkey = vmasks_memo_key('G', vmasks);
        if (auto got = memo_get(mkey)) return *got;
        std::string ans = canonical_key_graph_vmasks(vmasks);
        memo_put(std::move(mkey), ans);
        return ans;
    }

    std::string canonical_parent_key_graph_for_indices(const std::vector<int>& child) const {
        if (child.empty()) return "";
        std::vector<uint64_t> ct = canonical_tuple_graph_vmasks(vmasks_for_indices(child));
        if (ct.empty()) return "";
        ct.pop_back();
        return canonical_key_graph_cached_vmasks(ct);
    }

    std::string canonical_key_graph_for_indices(const std::vector<int>& chosen) const {
        return canonical_key_graph_cached_vmasks(vmasks_for_indices(chosen));
    }

    std::size_t factorial_product(const std::vector<std::vector<int>>& blocks) const {
        static const std::array<std::size_t, 9> fact = {1,1,2,6,24,120,720,5040,40320};
        std::size_t p = 1;
        for (auto& b : blocks) p *= fact[b.size()];
        return p;
    }
};

struct SearchOptions {
    std::string volume_mode = "sha";
    std::string search_mode = "quotient";
    std::string canon_mode = "graph"; // graph|refine|brute
    bool quotient = true;
    bool count_only = false;
    bool jsonl = false;
    bool quiet = false;

    // Production default: fast quotient DFS with a bounded/exact partial-state cache.
    // Canonical augmentation remains available but is not the high-throughput default.
    std::string generation_mode = "state-cache"; // state-cache|canonical
    std::string state_cache_mode = "auto";       // auto|full|bounded|none

    bool auto_tune = false;                      // choose resource parameters from host resources
    std::size_t memory_limit_mb = 0;             // 0 disables RSS guard; auto sets this
    std::string memory_policy = "warn";          // warn|shrink|abort

    bool symbreak_root_orbits = true;
    bool symbreak_stabilizer = true;
    int symbreak_stab_depth = 2;
    std::size_t symbreak_stab_max_perms = 4096;
    std::size_t symbreak_stab_candidate_max = 20000;
    std::size_t max_solutions = 0;
    std::size_t max_states = 0;                    // internal/pilot stop after this many accepted DFS states
    std::size_t auto_pilot_states = 0;             // optional auto feedback pilot; 0 disables

    // Caches.  The *_mb options are preferred for n=8 production runs because they
    // give a pre-search resource plan.  Entry-count options remain supported.
    std::size_t canon_memo_max = 200000;
    std::size_t canon_cache_mb = 0;
    std::size_t state_cache_max = 1000000;       // bounded exact recent-state entries
    std::size_t state_cache_mb = 0;              // converted to state_cache_max
    std::size_t compat_mem_rows = 0;
    std::size_t compat_mem_budget_mb = 256;
    std::size_t solution_key_cache_mb = 0;
    std::string solution_dedup = "memory";       // memory|disk
    std::filesystem::path solution_spool_dir;

    // Safe pruning in production state-cache mode.
    bool deep_cover_dp = false;
    bool force_propagation = true;
    int deep_cover_dp_max_uncovered = 20;
    std::size_t deep_cover_dp_max_candidates = 5000;

    std::size_t canon_aug_aut_max_perms = 40320; // n<=8, so exact full S_n scan/enumeration is bounded
    std::size_t progress_interval = 1000000;     // states between progress lines; 0 disables
    std::filesystem::path cache_dir;
    std::size_t compat_prewarm = 0;
    std::size_t threads = 1;                     // 1 preserves deterministic serial traversal; 0 means hardware_concurrency()
    int parallel_depth = 1;                      // minimum DFS depth used to split parallel tasks

    // Parallel scheduling.  "static" is the older atomic-index task loop.
    // "work-steal" uses a dynamic deque plus safe subtree donation to keep many
    // cores busy through the long-tail branches of n=8.
    std::string scheduler = "auto";              // auto|static|static-hybrid|work-steal
    std::size_t task_target_per_thread = 64;     // desired ready tasks per worker in dynamic scheduler
    int split_max_depth = 4;                     // deepest level where DFS may donate child subtrees
    std::size_t split_min_candidates = 16;       // only split states with at least this many children
    double donate_when_active_below = 0.75;        // static-hybrid: donate only when active workers fall below this fraction
    std::size_t labeled_state_cache_max = 0;       // optional exact labeled-state cache before quotient canonicalization

    // Benchmark/gather-data knobs.  They are set by main.cpp; the search engine
    // only respects max_solutions/max_states and reports counters.
    std::size_t benchmark_solutions = 0;
    std::size_t benchmark_states = 0;

    int prefix_max_depth = 2;                    // adaptive task splitting may refine to this depth
    std::size_t prefix_task_target = 0;          // 0 keeps fast fixed-depth splitting; adaptive targets are explicit

    // Compatibility cache policy.  "direct" stores rows in per-row slots and avoids
    // a global LRU mutation on every hit.  "lru" keeps the old bounded LRU.
    std::string compat_cache_policy = "auto";    // auto|lru|lru-notouch|direct
    std::size_t duplicate_local_cache_max = 4096;// per-thread exact confirmed-duplicate cache

    // Cache-affinity / low-level data-layout knobs.  These are benchmark-gated:
    // defaults preserve the measured-fast production path unless --auto or CLI
    // explicitly selects a different mode.
    std::string affinity = "none";               // none|compact|spread|numa
    std::string numa_policy = "none";            // none|first-touch|local-shards
    std::string state_cache_scope = "global";    // global|numa-local|thread-local (non-global repeats work but remains exact)
    std::string bitset_kernel = "auto";          // scalar|unrolled|avx2|avx512|auto
    std::string state_key_format = "binary";     // binary|hex
    std::size_t worker_compat_cache_size = 0;    // per-worker tiny immutable-row cache; 0 disables

    std::filesystem::path record_hot_compat_rows;
    std::filesystem::path prewarm_hot_compat_rows;
    std::size_t prewarm_hot_compat_limit = 0;
    std::filesystem::path record_root_performance;
    std::filesystem::path use_root_performance;

    bool async_output = false;                   // writer thread decouples solution formatting from DFS workers
    bool prefix_collect_complete = false;        // internal: write completed prefix states as tasks instead of emitting
    std::size_t flush_every = 1;                 // writer flush cadence in accepted solution records
    std::size_t root_shard_index = 0;            // process sharding: run only root candidates index == shard_index mod shard_count
    std::size_t root_shard_count = 1;
    bool use_graph_canonical_parent = true;      // canonical augmentation parent tests use exact graph canonicalization by default
};

struct alignas(64) SearchStats {
    std::atomic<std::size_t> states{0};
    std::atomic<std::size_t> solutions{0};
    std::atomic<std::size_t> duplicate_states{0};
    std::atomic<std::size_t> state_cache_evictions{0};
    std::atomic<std::size_t> volume_prunes{0};
    std::atomic<std::size_t> coverage_prunes{0};
    std::atomic<std::size_t> deep_cover_dp_calls{0};
    std::atomic<std::size_t> deep_cover_dp_prunes{0};
    std::atomic<std::size_t> forced_cells{0};
    std::atomic<std::size_t> forced_prunes{0};
    std::atomic<std::size_t> prefix_tasks{0};
    std::atomic<std::size_t> compat_intersect_ops{0};
    std::atomic<std::size_t> async_output_enqueued{0};
    std::atomic<std::size_t> memory_guard_events{0};
    std::atomic<std::size_t> solution_candidates{0};

    // Dynamic scheduler / utilization diagnostics.
    std::atomic<std::size_t> tasks_created{0};
    std::atomic<std::size_t> tasks_completed{0};
    std::atomic<std::size_t> tasks_donated{0};
    std::atomic<std::size_t> dynamic_splits{0};
    std::atomic<std::size_t> ready_tasks{0};
    std::atomic<std::size_t> active_workers{0};
    std::atomic<std::size_t> peak_active_workers{0};
    std::atomic<std::size_t> scheduler_waits{0};
    std::atomic<std::size_t> local_duplicate_hits{0};
    std::atomic<std::size_t> state_cache_queries{0};

    std::atomic<std::size_t> canonical_parent_tests{0};
    std::atomic<std::size_t> canonical_parent_accepts{0};
    std::atomic<std::size_t> automorphism_computations{0};
    std::atomic<std::size_t> automorphism_perms{0};
    std::atomic<std::size_t> candidates_before_volume{0};
    std::atomic<std::size_t> candidates_after_volume{0};
    std::atomic<std::size_t> candidates_after_aut{0};
    std::atomic<std::size_t> bitset_and_bytes{0};
    std::atomic<std::uint64_t> time_canonical_ns{0};
    std::atomic<std::uint64_t> time_aut_ns{0};
    std::atomic<std::uint64_t> time_compat_ns{0};
    std::atomic<std::uint64_t> time_deep_cover_dp_ns{0};
    std::chrono::steady_clock::time_point start;
};



class ShardedStateCache {
public:
    struct InsertResult {
        bool inserted = true;
        bool duplicate = false;
        std::size_t evicted = 0;
    };

private:
    // Speed-oriented exact state-cache backend.
    //
    // This is deliberately the older unordered_set<string> sharded backend,
    // restored as the production default after timing regressions showed that
    // the packed open-addressed backend was slower on the 192-logical-CPU n=8
    // workload.  It is exact: duplicate checks compare complete canonical keys,
    // never hashes alone.  The packed backend idea remains documented as a
    // memory-saving research direction, but production --auto must prioritize
    // measured throughput.
    struct alignas(64) Shard {
        mutable std::mutex mutex;
        std::unordered_set<std::string> set;
        std::deque<std::string> fifo;
    };

    std::vector<std::unique_ptr<Shard>> shards_;
    std::string mode_ = "none";
    std::size_t max_entries_ = 0;
    std::size_t max_bytes_ = 0;       // accepted for CLI compatibility; entry cap is authoritative in this backend
    std::size_t shard_count_ = 1;

    std::size_t shard_for_hash(std::size_t h) const {
        return shard_count_ <= 1 ? 0 : (h % shard_count_);
    }

    std::size_t shard_cap(std::size_t shard_id) const {
        if (mode_ != "bounded" || max_entries_ == 0) return std::numeric_limits<std::size_t>::max();
        std::size_t base = max_entries_ / shard_count_;
        std::size_t rem = max_entries_ % shard_count_;
        return base + (shard_id < rem ? 1 : 0);
    }

public:
    ShardedStateCache() { configure("none", 0, 1, 0); }

    void configure(std::string mode, std::size_t max_entries, std::size_t requested_shards, std::size_t max_bytes=0) {
        mode_ = std::move(mode);
        max_entries_ = max_entries;
        max_bytes_ = max_bytes;

        // The old fast implementation used a bounded number of shards.  Very high
        // shard counts increase allocation overhead and hurt locality for this
        // workload.  Keep thousands of shards available, but do not explode to tens
        // of thousands in --auto.
        shard_count_ = std::max<std::size_t>(1, requested_shards);
        shard_count_ = std::min<std::size_t>(4096, shard_count_);

        shards_.clear();
        shards_.reserve(shard_count_);
        for (std::size_t i = 0; i < shard_count_; ++i) shards_.push_back(std::make_unique<Shard>());
    }

    void reserve_hint(std::size_t total_hint) {
        if (mode_ == "none" || shards_.empty()) return;
        std::size_t per = std::max<std::size_t>(16, total_hint / shards_.size());
        for (auto& sp : shards_) {
            std::lock_guard<std::mutex> lock(sp->mutex);
            sp->set.reserve(per);
        }
    }

    void set_max_entries(std::size_t max_entries) { max_entries_ = max_entries; }
    void set_max_bytes(std::size_t max_bytes) { max_bytes_ = max_bytes; }

    std::size_t max_entries() const { return max_entries_; }
    std::size_t max_bytes() const { return max_bytes_; }
    const std::string& mode() const { return mode_; }
    std::size_t shard_count() const { return shard_count_; }

    InsertResult insert(std::string key) {
        if (mode_ == "none") return InsertResult{true, false, 0};

        std::size_t h = std::hash<std::string>{}(key);
        std::size_t sid = shard_for_hash(h);
        Shard& sh = *shards_[sid];

        std::lock_guard<std::mutex> lock(sh.mutex);
        auto [it, inserted] = sh.set.insert(key);
        if (!inserted) return InsertResult{false, true, 0};

        std::size_t ev = 0;
        if (mode_ == "bounded") {
            sh.fifo.push_back(*it);
            std::size_t cap = shard_cap(sid);
            while (sh.set.size() > cap && !sh.fifo.empty()) {
                std::string old = std::move(sh.fifo.front());
                sh.fifo.pop_front();
                auto erased = sh.set.erase(old);
                if (erased) ++ev;
            }
        }
        return InsertResult{true, false, ev};
    }

    std::size_t size() const {
        std::size_t total = 0;
        for (const auto& sp : shards_) {
            std::lock_guard<std::mutex> lock(sp->mutex);
            total += sp->set.size();
        }
        return total;
    }

    // Approximate live/allocated bytes for progress reporting.  The string backend
    // is intentionally chosen for speed, so exact byte accounting is not attempted
    // here.  The RSS guard remains the operational safety limit.
    std::size_t live_bytes() const {
        std::size_t total = 0;
        for (const auto& sp : shards_) {
            std::lock_guard<std::mutex> lock(sp->mutex);
            total += sp->set.size() * 624ULL;
        }
        return total;
    }

    std::size_t allocated_bytes() const {
        // Report the same conservative estimate.  For real memory use, rely on RSS.
        return live_bytes();
    }

    std::size_t payload_bytes() const {
        std::size_t total = 0;
        for (const auto& sp : shards_) {
            std::lock_guard<std::mutex> lock(sp->mutex);
            for (const auto& key : sp->set) total += key.size();
        }
        return total;
    }

    std::size_t trim_to_max() {
        if (mode_ != "bounded") return 0;
        std::size_t ev = 0;
        for (std::size_t sid = 0; sid < shards_.size(); ++sid) {
            Shard& sh = *shards_[sid];
            std::lock_guard<std::mutex> lock(sh.mutex);
            std::size_t cap = shard_cap(sid);
            while (sh.set.size() > cap && !sh.fifo.empty()) {
                std::string old = std::move(sh.fifo.front());
                sh.fifo.pop_front();
                auto erased = sh.set.erase(old);
                if (erased) ++ev;
            }
        }
        return ev;
    }

    void clear() {
        for (auto& sp : shards_) {
            std::lock_guard<std::mutex> lock(sp->mutex);
            sp->set.clear();
            sp->fifo.clear();
        }
    }
};



class SearchEngine {
public:
    ImageSet* is = nullptr;
    SearchOptions opt;
    CompatCache compat;
    Canonicalizer canon;
    SearchStats stats;
    struct StateCounterTLS {
        const SearchEngine* owner;
        std::size_t pending;
        StateCounterTLS() : owner(nullptr), pending(0) {}
    };
    inline static thread_local StateCounterTLS tls_state_counter;

    struct LocalDuplicateTLS {
        const SearchEngine* owner;
        std::unordered_set<std::string> set;
        std::deque<std::string> fifo;
        LocalDuplicateTLS() : owner(nullptr) {}
    };
    inline static thread_local LocalDuplicateTLS tls_local_duplicates;

    struct ThreadStateTLS {
        const SearchEngine* owner;
        std::unordered_set<std::string> set;
        std::deque<std::string> fifo;
        ThreadStateTLS() : owner(nullptr) {}
    };
    inline static thread_local ThreadStateTLS tls_thread_state_cache;

    ShardedStateCache state_cache;
    ShardedStateCache labeled_state_cache; // optional exact labeled duplicate filter before quotient canonicalization
    std::unordered_set<std::string> solution_keys;
    std::atomic<std::size_t> next_progress_report{0};
    std::mutex visited_mutex;
    std::mutex solution_mutex;
    std::mutex output_mutex;
    std::ostream* out = &std::cout;

    std::mutex output_queue_mutex;
    std::condition_variable output_queue_cv;
    std::deque<std::string> output_queue;
    std::thread output_thread;
    bool output_done = false;
    bool output_started = false;

    std::ofstream solution_spool;
    std::filesystem::path solution_spool_path;

    std::vector<DynBitset> volume_eq_masks;
    std::vector<DynBitset> volume_le_masks;
    std::vector<int> positive_volumes;

    // Optional profile-guided root/candidate ordering.  Scores are indexed by image id.
    std::vector<double> root_perf_score;
    std::unique_ptr<std::atomic<uint32_t>[]> root_solution_counts;
    std::size_t root_solution_count_size = 0;

    SearchEngine(ImageSet& image_set, SearchOptions options)
        : is(&image_set),
          opt(std::move(options)),
          compat(image_set, compute_mem_rows(image_set, opt), compat_dir_for(image_set, opt), opt.threads, opt.compat_cache_policy, opt.worker_compat_cache_size),
          canon(image_set, opt.canon_mode, opt.canon_memo_max) {
        set_global_bitset_kernel(opt.bitset_kernel);
        if (opt.affinity != "none" && opt.affinity != "compact" && opt.affinity != "spread" && opt.affinity != "numa") {
            throw std::runtime_error("--affinity must be none, compact, spread, or numa.");
        }
        if (opt.numa_policy != "none" && opt.numa_policy != "first-touch" && opt.numa_policy != "local-shards") {
            throw std::runtime_error("--numa-policy must be none, first-touch, or local-shards.");
        }
        if (opt.state_cache_scope != "global" && opt.state_cache_scope != "numa-local" && opt.state_cache_scope != "thread-local") {
            throw std::runtime_error("--state-cache-scope must be global, numa-local, or thread-local.");
        }
        if (opt.state_key_format != "binary" && opt.state_key_format != "hex") {
            throw std::runtime_error("--state-key-format must be binary or hex.");
        }
        if (opt.generation_mode != "canonical" && opt.generation_mode != "state-cache") {
            throw std::runtime_error("--generation-mode must be canonical or state-cache.");
        }
        if (opt.memory_policy != "warn" && opt.memory_policy != "shrink" && opt.memory_policy != "abort") {
            throw std::runtime_error("--memory-policy must be warn, shrink, or abort.");
        }
        if (opt.solution_dedup != "memory" && opt.solution_dedup != "disk") {
            throw std::runtime_error("--solution-dedup must be memory or disk.");
        }
        if (opt.scheduler == "auto") {
            // Regression fix: the measured n=8 production runs showed that
            // dynamic work stealing and subtree donation slowed time-to-solution
            // substantially compared with the legacy static root-task scheduler.
            // Keep work stealing available only when the user explicitly asks for it.
            opt.scheduler = "static";
        }
        if (opt.scheduler != "static" && opt.scheduler != "static-hybrid" && opt.scheduler != "work-steal") {
            throw std::runtime_error("--scheduler must be auto, static, static-hybrid, or work-steal.");
        }
        if (opt.compat_cache_policy == "auto") {
            // The legacy LRU cache was faster in user measurements than the
            // direct-cache path for early n=8 enumeration.  Direct remains
            // available explicitly via --compat-cache-policy direct.
            opt.compat_cache_policy = "lru";
        }
        if (opt.compat_cache_policy != "lru" && opt.compat_cache_policy != "lru-notouch" && opt.compat_cache_policy != "direct") {
            throw std::runtime_error("--compat-cache-policy must be auto, lru, lru-notouch, or direct.");
        }
        if (opt.generation_mode == "canonical") {
            // True canonical augmentation uses a canonical construction path and
            // therefore does not need a monotonically growing global partial-state set.
            opt.state_cache_mode = "none";
        } else if (opt.state_cache_mode == "auto") {
            // Production quotient DFS: n<=7 usually benefits from full state caching;
            // n=8 defaults to bounded exact recent-state caching.
            opt.state_cache_mode = (image_set.hs.n <= 7) ? "full" : "bounded";
        }
        if (opt.state_cache_mode != "full" && opt.state_cache_mode != "bounded" && opt.state_cache_mode != "none") {
            throw std::runtime_error("--state-cache must be auto, full, bounded, or none.");
        }

        // Convert byte-budget cache knobs to entry-count knobs after the total
        // volume is known.  The conversion is conservative; the RSS guard is the
        // hard backstop for implementation-dependent allocator overhead.
        if (opt.canon_cache_mb > 0) {
            opt.canon_memo_max = std::max<std::size_t>(1, (opt.canon_cache_mb * 1024ULL * 1024ULL) / 512ULL);
            canon.memo_max = opt.canon_memo_max;
        }
        if (opt.state_cache_mode == "bounded" && opt.state_cache_mb > 0) {
            std::size_t per = estimate_state_cache_entry_bytes(total_volume());
            opt.state_cache_max = std::max<std::size_t>(1, (opt.state_cache_mb * 1024ULL * 1024ULL) / per);
        }
        if (opt.root_shard_count == 0) throw std::runtime_error("--root-shard count must be positive.");
        if (opt.root_shard_index >= opt.root_shard_count) throw std::runtime_error("--root-shard index must be smaller than count.");
        {
            // Many-core runs perform millions of exact state-cache probes.
            // Use many shards to avoid a single hot mutex or a small set of hot buckets.
            std::size_t shards = std::max<std::size_t>(256, std::min<std::size_t>(4096, opt.threads * 32));
            std::size_t state_cache_byte_cap =
                (opt.state_cache_mode == "bounded" && opt.state_cache_mb > 0)
                    ? opt.state_cache_mb * 1024ULL * 1024ULL
                    : std::size_t{0};
            state_cache.configure(opt.state_cache_mode, opt.state_cache_max, shards, state_cache_byte_cap);
            if (opt.state_cache_mode == "bounded") {
                // Reserve a small per-shard hint only.  Reserving the full n=8 budget
                // can commit many GB before search starts.
                state_cache.reserve_hint(std::min<std::size_t>(opt.state_cache_max + 1024, 2'000'000));
            } else if (opt.state_cache_mode == "full" && image_set.hs.n <= 7) {
                state_cache.reserve_hint(1'000'000);
            }
        }
        if (opt.labeled_state_cache_max > 0 && opt.generation_mode == "state-cache") {
            std::size_t shards = std::max<std::size_t>(256, std::min<std::size_t>(4096, opt.threads * 32));
            labeled_state_cache.configure("bounded", opt.labeled_state_cache_max, shards, 0);
            labeled_state_cache.reserve_hint(std::min<std::size_t>(opt.labeled_state_cache_max + 1024, 1'000'000));
        } else {
            labeled_state_cache.configure("none", 0, 1, 0);
        }
        if (!opt.record_hot_compat_rows.empty()) {
            compat.enable_hot_row_recording();
        }
        if (!opt.prewarm_hot_compat_rows.empty()) {
            compat.prewarm_hot_file(opt.prewarm_hot_compat_rows,
                                    opt.prewarm_hot_compat_limit,
                                    !opt.quiet,
                                    opt.threads);
        }
        load_root_performance_file();
        enable_root_performance_recording();
        build_volume_masks();
    }

    void load_root_performance_file() {
        root_perf_score.assign(is->images.size(), 0.0);
        if (opt.use_root_performance.empty()) return;
        std::ifstream in(opt.use_root_performance);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            std::istringstream iss(line);
            std::size_t idx = 0; double score = 0.0;
            if (iss >> idx >> score) {
                if (idx < root_perf_score.size()) root_perf_score[idx] = score;
            }
        }
    }

    void enable_root_performance_recording() {
        if (opt.record_root_performance.empty()) return;
        root_solution_count_size = is->images.size();
        root_solution_counts.reset(new std::atomic<uint32_t>[root_solution_count_size]);
        for (std::size_t i = 0; i < root_solution_count_size; ++i) root_solution_counts[i].store(0, std::memory_order_relaxed);
    }

    void save_root_performance_file() const {
        if (opt.record_root_performance.empty() || !root_solution_counts) return;
        std::vector<std::pair<uint32_t,std::size_t>> rows;
        for (std::size_t i = 0; i < root_solution_count_size; ++i) {
            uint32_t c = root_solution_counts[i].load(std::memory_order_relaxed);
            if (c) rows.emplace_back(c, i);
        }
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
        });
        std::ofstream out(opt.record_root_performance);
        if (!out) return;
        out << "# image_index solution_count\n";
        for (auto [c,i] : rows) out << i << " " << c << "\n";
    }

    void build_volume_masks() {
        int tot = total_volume();
        volume_eq_masks.assign(static_cast<std::size_t>(tot + 1), DynBitset(is->images.size()));
        positive_volumes.clear();
        for (std::size_t i = 0; i < is->images.size(); ++i) {
            int v = is->vol_list[i];
            if (v >= 0 && v <= tot) volume_eq_masks[static_cast<std::size_t>(v)].set(i);
        }
        volume_le_masks.assign(static_cast<std::size_t>(tot + 1), DynBitset(is->images.size()));
        DynBitset accum(is->images.size());
        for (int v = 0; v <= tot; ++v) {
            accum.or_assign(volume_eq_masks[static_cast<std::size_t>(v)]);
            volume_le_masks[static_cast<std::size_t>(v)].copy_from(accum);
            if (v > 0 && !volume_eq_masks[static_cast<std::size_t>(v)].empty()) {
                positive_volumes.push_back(v);
            }
        }
    }

    static std::size_t compute_mem_rows(const ImageSet& image_set, const SearchOptions& opt) {
        if (image_set.hs.n <= 6) return 0;
        if (opt.compat_mem_rows) return opt.compat_mem_rows;
        // keep a modest default to avoid surprising memory use; user can raise it
        return 512;
    }

    static std::filesystem::path compat_dir_for(const ImageSet& image_set, const SearchOptions& opt) {
        if (image_set.hs.n <= 6 || opt.cache_dir.empty()) return {};
        return opt.cache_dir / ("r3n" + std::to_string(image_set.hs.n) + "_" + opt.volume_mode + "_compat_rows");
    }

    int total_volume() const {
        if (opt.volume_mode == "sha") return (is->hs.n - 3) * (is->hs.n - 3);
        for (const auto& o : is->orbits) if (o.ineqs.empty()) return o.volume_lattice;
        throw std::runtime_error("No uniform orbit found.");
    }


    void emit_trivial_tiling_if_owned_by_this_shard() {
        if (opt.solution_dedup == "disk") {
            // Disk mode performs final external dedup at the end; emitting here would
            // be delayed anyway.  Let the ordinary search/spool path handle it.
            return;
        }
        if (opt.root_shard_count > 1 && (std::size_t{0} % opt.root_shard_count) != opt.root_shard_index) return;
        for (std::size_t oi = 0; oi < is->orbits.size(); ++oi) {
            if (is->orbits[oi].ineqs.empty()) {
                int idx = (oi < is->root_rep_by_orbit_index.size()) ? is->root_rep_by_orbit_index[oi] : -1;
                if (idx >= 0) {
                    std::vector<int> chosen{idx};
                    handle_solution(chosen);
                }
                return;
            }
        }
    }

    void run(std::ostream& output_stream) {
        out = &output_stream;
        stats.start = std::chrono::steady_clock::now();
        opt.threads = normalize_threads(opt.threads);
        opt.parallel_depth = std::max(1, opt.parallel_depth);

        if (opt.solution_dedup == "disk") open_solution_spool();
        start_output_writer();
        if (opt.compat_prewarm) compat.prewarm(opt.compat_prewarm, !opt.quiet, opt.threads);

        DynBitset allowed(is->images.size(), true);
        std::vector<int> chosen;

        // Emit the one-cell uniform tiling immediately in memory-streaming mode.
        // This makes short --max smoke tests deterministic and gives immediate
        // feedback before the parallel worker schedule becomes nondeterministic.
        emit_trivial_tiling_if_owned_by_this_shard();
        if (max_limit_reached()) {
            flush_state_counter();
            if (opt.solution_dedup == "disk") finalize_solution_spool();
            stop_output_writer();
            return;
        }

        if (opt.generation_mode == "canonical" && opt.quotient) {
            if (opt.threads <= 1) {
                SearchWorkspace ws;
                ws.prepare(total_volume() + 2);
                dfs_canonical_ws(chosen, 0, 0, allowed, 0, ws);
            } else {
                run_parallel_canonical(chosen, allowed);
            }
        } else {
            insert_initial_state();
            if (opt.threads <= 1) {
                SearchWorkspace ws;
                ws.prepare(total_volume() + 2);
                dfs_ws(chosen, 0, 0, allowed, 0, ws);
            } else {
                run_parallel(chosen, allowed);
            }
        }

        flush_state_counter();
        if (opt.solution_dedup == "disk") finalize_solution_spool();
        if (!opt.record_hot_compat_rows.empty()) {
            compat.save_hot_rows(opt.record_hot_compat_rows);
        }
        save_root_performance_file();
        stop_output_writer();

        if (!opt.quiet) {
            auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - stats.start).count();
            std::cerr << "states=" << stats.states.load()
                      << " solutions=" << stats.solutions.load()
                      << " solution_candidates=" << stats.solution_candidates.load()
                      << " duplicate_states=" << stats.duplicate_states.load()
                      << " generation_mode=" << opt.generation_mode
                      << " state_cache_mode=" << opt.state_cache_mode
                      << " state_cache_size=" << state_cache_size()
                      << " state_cache_evictions=" << stats.state_cache_evictions.load()
                      << " state_cache_live_mb=" << (estimated_state_cache_live_bytes() / (1024ULL * 1024ULL))
                      << " state_cache_alloc_mb=" << (state_cache_allocated_bytes() / (1024ULL * 1024ULL))
                      << " compat_hits=" << compat.hits.load()
                      << " compat_misses=" << compat.misses.load()
                      << " disk_hits=" << compat.disk_hits.load()
                      << " compat_lru_size=" << compat.lru_size()
                      << " compat_lru_payload_mb=" << (compat.lru_payload_bytes() / (1024ULL * 1024ULL))
                      << " compat_rows_dense=" << compat.rows_dense.load()
                      << " compat_rows_sparse_incompat=" << compat.rows_sparse.load()
                      << " compat_intersect_ops=" << stats.compat_intersect_ops.load()
                      << " forced_cells=" << stats.forced_cells.load()
                      << " forced_prunes=" << stats.forced_prunes.load()
                      << " prefix_tasks=" << stats.prefix_tasks.load()
                      << " async_output_enqueued=" << stats.async_output_enqueued.load()
                      << " volume_prunes=" << stats.volume_prunes.load()
                      << " coverage_prunes=" << stats.coverage_prunes.load()
                      << " deep_cover_dp_calls=" << stats.deep_cover_dp_calls.load()
                      << " deep_cover_dp_prunes=" << stats.deep_cover_dp_prunes.load()
                      << " memory_guard_events=" << stats.memory_guard_events.load()
                      << " canonical_parent_tests=" << stats.canonical_parent_tests.load()
                      << " canonical_parent_accepts=" << stats.canonical_parent_accepts.load()
                      << " aut_computations=" << stats.automorphism_computations.load()
                      << " aut_perms_tested=" << stats.automorphism_perms.load()
                      << " cand_before_volume=" << stats.candidates_before_volume.load()
                      << " cand_after_volume=" << stats.candidates_after_volume.load()
                      << " cand_after_aut=" << stats.candidates_after_aut.load()
                      << " bitset_and_mb=" << (stats.bitset_and_bytes.load() / (1024ULL * 1024ULL))
                      << " time_canon_sec=" << std::fixed << std::setprecision(3) << (stats.time_canonical_ns.load() / 1e9)
                      << " time_aut_sec=" << std::fixed << std::setprecision(3) << (stats.time_aut_ns.load() / 1e9)
                      << " time_compat_sec=" << std::fixed << std::setprecision(3) << (stats.time_compat_ns.load() / 1e9)
                      << " time_deep_cover_dp_sec=" << std::fixed << std::setprecision(3) << (stats.time_deep_cover_dp_ns.load() / 1e9)
                      << " threads=" << opt.threads
                      << " scheduler=" << opt.scheduler
                      << " active_workers=" << stats.active_workers.load()
                      << " peak_active_workers=" << stats.peak_active_workers.load()
                      << " ready_tasks=" << stats.ready_tasks.load()
                      << " tasks_created=" << stats.tasks_created.load()
                      << " tasks_completed=" << stats.tasks_completed.load()
                      << " tasks_donated=" << stats.tasks_donated.load()
                      << " dynamic_splits=" << stats.dynamic_splits.load()
                      << " scheduler_waits=" << stats.scheduler_waits.load()
                      << " state_cache_queries=" << stats.state_cache_queries.load()
                      << " local_dup_hits=" << stats.local_duplicate_hits.load()
                      << " compat_cache_policy=" << compat.cache_policy
                      << " root_shard=" << opt.root_shard_index << "/" << opt.root_shard_count;
            std::size_t rss = current_rss_mb();
            if (rss) std::cerr << " rss_mb=" << rss;
            std::cerr << " elapsed_sec=" << std::fixed << std::setprecision(3) << elapsed << "\n";
        }
    }

    struct SearchTask {
        std::vector<int> chosen;
        uint64_t covered = 0;
        int volume_sum = 0;
        DynBitset allowed;
        int depth = 0;
    };

    struct SearchWorkspace {
        std::vector<std::vector<int>> chosen_stack;
        std::vector<DynBitset> allowed_stack;
        std::vector<std::vector<int>> ordered_stack;

        void prepare(int max_depth) {
            std::size_t n = static_cast<std::size_t>(std::max(1, max_depth + 1));
            chosen_stack.resize(n);
            allowed_stack.resize(n);
            ordered_stack.resize(n);
            // Fixed-depth workspace: in sha mode total volume bounds the DFS depth.
            // Reserve small vectors once per worker rather than reallocating at every
            // child.  This preserves the existing code path while improving locality.
            for (auto& v : chosen_stack) v.reserve(n + 2);
            for (auto& v : ordered_stack) v.reserve(256);
        }

        void ensure(int depth, std::size_t nbits) {
            std::size_t need = static_cast<std::size_t>(depth + 1);
            if (chosen_stack.size() < need) {
                std::size_t old_sz = chosen_stack.size();
                chosen_stack.resize(need);
                allowed_stack.resize(need);
                ordered_stack.resize(need);
                for (std::size_t i = old_sz; i < need; ++i) {
                    chosen_stack[i].reserve(need + 2);
                    ordered_stack[i].reserve(256);
                }
            }
            if (allowed_stack[static_cast<std::size_t>(depth)].nbits != nbits) {
                allowed_stack[static_cast<std::size_t>(depth)].resize(nbits);
            }
        }
    };

    static void update_peak(std::atomic<std::size_t>& peak, std::size_t value) {
        std::size_t cur = peak.load(std::memory_order_relaxed);
        while (value > cur && !peak.compare_exchange_weak(cur, value, std::memory_order_relaxed)) {}
    }

    struct DynamicTaskQueue {
        mutable std::mutex mutex;
        std::condition_variable cv;
        std::deque<SearchTask> queue;
        bool closed = false;
        std::size_t active = 0;
        std::size_t max_ready = 0;
        SearchStats* stats = nullptr;

        explicit DynamicTaskQueue(std::size_t target=0, SearchStats* st=nullptr)
            : max_ready(target), stats(st) {}

        void push(SearchTask&& task) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (closed) return;
                queue.push_back(std::move(task));
                if (stats) {
                    stats->tasks_created.fetch_add(1, std::memory_order_relaxed);
                    stats->ready_tasks.store(queue.size(), std::memory_order_relaxed);
                }
            }
            cv.notify_one();
        }

        bool pop(SearchTask& task) {
            std::unique_lock<std::mutex> lock(mutex);
            for (;;) {
                if (!queue.empty()) {
                    task = std::move(queue.back());
                    queue.pop_back();
                    ++active;
                    if (stats) {
                        stats->ready_tasks.store(queue.size(), std::memory_order_relaxed);
                        std::size_t a = stats->active_workers.fetch_add(1, std::memory_order_relaxed) + 1;
                        update_peak(stats->peak_active_workers, a);
                    }
                    return true;
                }
                if (closed || active == 0) {
                    closed = true;
                    if (stats) stats->ready_tasks.store(0, std::memory_order_relaxed);
                    cv.notify_all();
                    return false;
                }
                if (stats) stats->scheduler_waits.fetch_add(1, std::memory_order_relaxed);
                cv.wait(lock);
            }
        }

        void finish_task() {
            std::lock_guard<std::mutex> lock(mutex);
            if (active > 0) --active;
            if (stats) {
                stats->tasks_completed.fetch_add(1, std::memory_order_relaxed);
                stats->active_workers.fetch_sub(1, std::memory_order_relaxed);
                stats->ready_tasks.store(queue.size(), std::memory_order_relaxed);
            }
            if (queue.empty() && active == 0) {
                closed = true;
                cv.notify_all();
            }
        }

        void close_and_clear() {
            std::lock_guard<std::mutex> lock(mutex);
            closed = true;
            queue.clear();
            if (stats) stats->ready_tasks.store(0, std::memory_order_relaxed);
            cv.notify_all();
        }

        bool should_split(int depth, std::size_t candidate_count,
                          int split_max_depth, std::size_t split_min_candidates) const {
            if (depth >= split_max_depth) return false;
            if (candidate_count < split_min_candidates) return false;
            std::lock_guard<std::mutex> lock(mutex);
            return !closed && queue.size() < max_ready;
        }

        bool should_split_late(int depth, std::size_t candidate_count,
                               int split_max_depth, std::size_t split_min_candidates,
                               std::size_t threads, double active_fraction) const {
            if (depth >= split_max_depth) return false;
            if (candidate_count < split_min_candidates) return false;
            std::lock_guard<std::mutex> lock(mutex);
            if (closed || queue.size() >= max_ready) return false;
            std::size_t threshold = static_cast<std::size_t>(std::max<double>(1.0, std::ceil(active_fraction * static_cast<double>(std::max<std::size_t>(1, threads)))));
            return active < threshold;
        }

        bool needs_more_tasks() const {
            std::lock_guard<std::mutex> lock(mutex);
            return !closed && queue.size() < max_ready;
        }

        bool needs_more_tasks_late(std::size_t threads, double active_fraction) const {
            std::lock_guard<std::mutex> lock(mutex);
            if (closed || queue.size() >= max_ready) return false;
            std::size_t threshold = static_cast<std::size_t>(std::max<double>(1.0, std::ceil(active_fraction * static_cast<double>(std::max<std::size_t>(1, threads)))));
            return active < threshold;
        }

        std::size_t size() const {
            std::lock_guard<std::mutex> lock(mutex);
            return queue.size();
        }
    };

    bool max_limit_reached() const {
        if (opt.max_states &&
            stats.states.load(std::memory_order_relaxed) >= opt.max_states) {
            return true;
        }
        if (!opt.max_solutions) return false;
        if (opt.solution_dedup == "disk") {
            return stats.solution_candidates.load(std::memory_order_relaxed) >= opt.max_solutions;
        }
        return stats.solutions.load(std::memory_order_relaxed) >= opt.max_solutions;
    }


    void run_parallel_work_steal(std::vector<int>& root_chosen, DynBitset& root_allowed) {
        std::size_t target = std::max<std::size_t>(opt.threads * std::max<std::size_t>(1, opt.task_target_per_thread),
                                                   opt.threads * 4);
        DynamicTaskQueue queue(target, &stats);

        SearchTask root;
        root.chosen = root_chosen;
        root.covered = 0;
        root.volume_sum = 0;
        root.allowed.copy_from(root_allowed);
        root.depth = 0;
        queue.push(std::move(root));
        stats.prefix_tasks.store(1, std::memory_order_relaxed);

        if (!opt.quiet) {
            std::cerr << "parallel search: dynamic work-stealing scheduler"
                      << " using " << opt.threads << " threads"
                      << " target_ready_tasks=" << target
                      << " split_max_depth=" << opt.split_max_depth
                      << " split_min_candidates=" << opt.split_min_candidates
                      << " compat_cache_policy=" << compat.cache_policy
                      << "\n";
        }

        std::vector<std::thread> workers;
        workers.reserve(opt.threads);
        for (std::size_t t = 0; t < opt.threads; ++t) {
            workers.emplace_back([&, t]() {
                maybe_pin_worker(t, opt.threads, opt.affinity);
                SearchWorkspace ws;
                ws.prepare(total_volume() + 2 + std::max(0, opt.split_max_depth));
                for (;;) {
                    if (max_limit_reached()) {
                        queue.close_and_clear();
                        break;
                    }
                    SearchTask task;
                    if (!queue.pop(task)) break;
                    dfs_ws_dynamic(task.chosen, task.covered, task.volume_sum,
                                   task.allowed, task.depth, ws, queue);
                    flush_state_counter();
                    queue.finish_task();
                    if (max_limit_reached()) queue.close_and_clear();
                }
                flush_state_counter();
            });
        }
        for (auto& th : workers) th.join();
        stats.prefix_tasks.store(stats.tasks_created.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }


    void run_parallel_static_hybrid(std::vector<int>& root_chosen, DynBitset& root_allowed) {
        // Hybrid scheduler: preserve the measured-fast static prefix construction,
        // then use the dynamic queue only as a late long-tail balancer.  Unlike
        // --scheduler work-steal, this does not start from the root and does not
        // split aggressively while all workers are still active.
        std::vector<SearchTask> seed_tasks;
        seed_tasks.reserve(4096);
        collect_parallel_tasks(seed_tasks, root_chosen, 0, 0, root_allowed, 0, opt.parallel_depth);
        flush_state_counter();
        stats.prefix_tasks.store(seed_tasks.size(), std::memory_order_relaxed);

        std::size_t target = std::max<std::size_t>(opt.threads * std::max<std::size_t>(1, opt.task_target_per_thread),
                                                   opt.threads * 4);
        DynamicTaskQueue queue(target, &stats);
        for (auto& task : seed_tasks) queue.push(std::move(task));

        if (!opt.quiet) {
            std::cerr << "parallel search: static-hybrid scheduler seeded with " << seed_tasks.size()
                      << " task" << (seed_tasks.size() == 1 ? "" : "s")
                      << " using " << opt.threads << " threads"
                      << " late_donate_active_below=" << opt.donate_when_active_below
                      << " target_ready_tasks=" << target
                      << " split_max_depth=" << opt.split_max_depth
                      << " split_min_candidates=" << opt.split_min_candidates
                      << " compat_cache_policy=" << compat.cache_policy
                      << "\n";
        }

        std::vector<std::thread> workers;
        workers.reserve(opt.threads);
        for (std::size_t t = 0; t < opt.threads; ++t) {
            workers.emplace_back([&, t]() {
                maybe_pin_worker(t, opt.threads, opt.affinity);
                SearchWorkspace ws;
                ws.prepare(total_volume() + 2 + std::max(0, opt.split_max_depth));
                for (;;) {
                    if (max_limit_reached()) {
                        queue.close_and_clear();
                        break;
                    }
                    SearchTask task;
                    if (!queue.pop(task)) break;
                    dfs_ws_dynamic(task.chosen, task.covered, task.volume_sum,
                                   task.allowed, task.depth, ws, queue);
                    flush_state_counter();
                    queue.finish_task();
                    if (max_limit_reached()) queue.close_and_clear();
                }
                flush_state_counter();
            });
        }
        for (auto& th : workers) th.join();
        stats.prefix_tasks.store(stats.tasks_created.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    void run_parallel(std::vector<int>& root_chosen, DynBitset& root_allowed) {
        if (opt.scheduler == "work-steal") {
            run_parallel_work_steal(root_chosen, root_allowed);
            return;
        }
        if (opt.scheduler == "static-hybrid") {
            run_parallel_static_hybrid(root_chosen, root_allowed);
            return;
        }
        std::vector<SearchTask> tasks;
        tasks.reserve(4096);
        collect_parallel_tasks(tasks, root_chosen, 0, 0, root_allowed, 0, opt.parallel_depth);
        flush_state_counter();
        stats.prefix_tasks.store(tasks.size(), std::memory_order_relaxed);

        if (!opt.quiet) {
            std::cerr << "parallel search: " << tasks.size()
                      << " initial task" << (tasks.size() == 1 ? "" : "s")
                      << " at min split depth " << opt.parallel_depth
                      << " max prefix depth " << opt.prefix_max_depth
                      << " target " << opt.prefix_task_target
                      << " using " << opt.threads << " threads\n";
        }

        std::atomic<std::size_t> next{0};
        std::vector<std::thread> workers;
        std::size_t tcount = std::min<std::size_t>(opt.threads, std::max<std::size_t>(1, tasks.size()));
        workers.reserve(tcount);
        for (std::size_t t = 0; t < tcount; ++t) {
            workers.emplace_back([&, t]() {
                maybe_pin_worker(t, opt.threads, opt.affinity);
                for (;;) {
                    if (max_limit_reached()) break;
                    std::size_t k = next.fetch_add(1, std::memory_order_relaxed);
                    if (k >= tasks.size()) break;
                    SearchTask& task = tasks[k];
                    SearchWorkspace ws;
                    ws.prepare(total_volume() + 2);
                    dfs_ws(task.chosen, task.covered, task.volume_sum, task.allowed, task.depth, ws);
                }
                flush_state_counter();
            });
        }
        for (auto& th : workers) th.join();
    }


    struct CanonAutInfo {
        std::vector<std::vector<uint8_t>> perms;
        std::vector<std::vector<int>> vertex_orbits;
    };

    bool perm_fixes_vmasks(const std::vector<uint64_t>& state_vm, const std::vector<uint8_t>& perm) const {
        if (state_vm.empty()) return true;
        std::vector<uint64_t> t;
        t.reserve(state_vm.size());
        for (uint64_t vm : state_vm) t.push_back(is->hs.permute_vmask_by_vector(vm, perm));
        std::sort(t.begin(), t.end());
        return t == state_vm;
    }

    CanonAutInfo exact_automorphism_info(const std::vector<int>& chosen) {
        auto t0 = std::chrono::steady_clock::now();
        std::size_t tested_perms = 0;
        int n = is->hs.n;
        std::vector<uint64_t> state_vm;
        state_vm.reserve(chosen.size());
        for (int idx : chosen) state_vm.push_back(is->vmask_list[idx]);
        std::sort(state_vm.begin(), state_vm.end());

        CanonAutInfo info;
        info.perms.reserve(64);

        auto add_if_aut = [&](const std::vector<uint8_t>& perm) {
            ++tested_perms;
            if (perm_fixes_vmasks(state_vm, perm)) info.perms.push_back(perm);
        };

        if (chosen.empty()) {
            info.perms.reserve(static_cast<std::size_t>(is->pt.count()));
            for (const auto& p : is->pt.perms) {
                std::vector<uint8_t> pv(n);
                for (int i = 0; i < n; ++i) pv[i] = p[i];
                info.perms.push_back(std::move(pv));
            }
            tested_perms = static_cast<std::size_t>(is->pt.count());
        } else {
            auto rd = canon.graph_refine_blocks_for_vmasks(state_vm);
            std::size_t prod = canon.factorial_product(rd.blocks);
            if (prod <= opt.canon_aug_aut_max_perms) {
                canon.enumerate_within_block_perms(rd.blocks, [&](const std::vector<uint8_t>& perm) {
                    add_if_aut(perm);
                });
            } else {
                // With n<=8 this fallback still means at most 40320 permutations.
                for (const auto& p : is->pt.perms) {
                    std::vector<uint8_t> pv(n);
                    for (int i = 0; i < n; ++i) pv[i] = p[i];
                    add_if_aut(pv);
                }
            }
        }

        // Build vertex orbits of Delta(3,n) under Aut(chosen).  This is used only
        // for feasibility/progress diagnostics and future invariant pruning; the
        // canonical augmentation implementation deliberately branches over all
        // candidate cells to avoid conflicts between coverage-first branching and
        // the canonical construction path.
        std::vector<int> parent(is->hs.num_vertices);
        std::iota(parent.begin(), parent.end(), 0);
        auto findp = [&](int x) {
            while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
            return x;
        };
        auto unite = [&](int a, int b) {
            int ra = findp(a), rb = findp(b);
            if (ra != rb) parent[rb] = ra;
        };
        for (const auto& perm : info.perms) {
            for (int vi = 0; vi < is->hs.num_vertices; ++vi) {
                uint16_t pe = is->hs.permute_subset_by_vector(is->hs.vertex_elem_masks[vi], perm);
                int dst = is->hs.index_of_vertex_mask(pe);
                unite(vi, dst);
            }
        }
        std::map<int, std::vector<int>> mp;
        for (int vi = 0; vi < is->hs.num_vertices; ++vi) mp[findp(vi)].push_back(vi);
        for (auto& [r, orb] : mp) info.vertex_orbits.push_back(std::move(orb));

        stats.automorphism_computations.fetch_add(1, std::memory_order_relaxed);
        stats.automorphism_perms.fetch_add(tested_perms, std::memory_order_relaxed);
        auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
        stats.time_aut_ns.fetch_add(static_cast<std::uint64_t>(dt), std::memory_order_relaxed);
        return info;
    }

    DynBitset reduce_candidates_by_exact_aut(const DynBitset& candidates, const CanonAutInfo& aut) const {
        if (aut.perms.size() <= 1) return candidates;
        DynBitset remaining = candidates;
        DynBitset keep(is->images.size());
        while (!remaining.empty()) {
            std::size_t seed = *remaining.first_set();
            int min_idx = static_cast<int>(seed);
            std::vector<std::size_t> orbit_indices;
            orbit_indices.reserve(aut.perms.size());
            uint64_t vm = is->vmask_list[seed];

            for (const auto& perm : aut.perms) {
                uint64_t tvm = is->hs.permute_vmask_by_vector(vm, perm);
                auto it = is->min_image_by_vmask.find(tvm);
                if (it == is->min_image_by_vmask.end()) continue;
                std::size_t j = static_cast<std::size_t>(it->second);
                if (candidates.test(j)) {
                    orbit_indices.push_back(j);
                    if (static_cast<int>(j) < min_idx) min_idx = static_cast<int>(j);
                }
            }

            if (orbit_indices.empty()) {
                orbit_indices.push_back(seed);
                min_idx = static_cast<int>(seed);
            }
            for (std::size_t j : orbit_indices) remaining.reset(j);
            keep.set(static_cast<std::size_t>(min_idx));
        }
        return keep;
    }

    void volume_filter_into(const DynBitset& allowed, int remaining, DynBitset& out) {
        if (remaining < 0) {
            out.resize(is->images.size());
            out.clear_all();
            return;
        }
        int capped = std::min<int>(remaining, static_cast<int>(volume_le_masks.size()) - 1);
        out.copy_from(allowed);
        out.and_assign(volume_le_masks[static_cast<std::size_t>(capped)]);
        stats.bitset_and_bytes.fetch_add(out.bytes(), std::memory_order_relaxed);
    }

    DynBitset volume_filtered_allowed(const DynBitset& allowed, int remaining) {
        DynBitset out;
        volume_filter_into(allowed, remaining, out);
        return out;
    }

    bool volume_subset_feasible(const DynBitset& allowed_volume_filtered, int remaining) const {
        if (remaining < 0) return false;
        if (remaining == 0) return true;

        std::vector<unsigned char> dp(static_cast<std::size_t>(remaining + 1), 0);
        dp[0] = 1;
        for (int v : positive_volumes) {
            if (v > remaining) break;
            std::size_t cnt = allowed_volume_filtered.count_intersection(volume_eq_masks[static_cast<std::size_t>(v)]);
            if (cnt == 0) continue;

            // Exact bounded subset-sum, but cap multiplicity by remaining/v.
            std::size_t use = std::min<std::size_t>(cnt, static_cast<std::size_t>(remaining / v));
            for (std::size_t copy = 0; copy < use; ++copy) {
                for (int s = remaining; s >= v; --s) {
                    if (!dp[static_cast<std::size_t>(s)] && dp[static_cast<std::size_t>(s - v)]) {
                        dp[static_cast<std::size_t>(s)] = 1;
                    }
                }
                if (dp[static_cast<std::size_t>(remaining)]) return true;
            }
        }
        return dp[static_cast<std::size_t>(remaining)] != 0;
    }

    bool coverage_feasible(const DynBitset& allowed_volume_filtered, uint64_t covered) const {
        uint64_t uncovered = is->hs.all_vertices_mask & ~covered;
        while (uncovered) {
            unsigned v = std::countr_zero(uncovered);
            if (!allowed_volume_filtered.intersects(is->by_vertex[v])) return false;
            uncovered &= uncovered - 1;
        }
        return true;
    }

    bool deep_cover_volume_feasible(const DynBitset& allowed_volume_filtered, uint64_t covered, int remaining) {
        if (!opt.deep_cover_dp || remaining <= 0) return true;
        if (remaining >= 31) return true; // optimized bitset DP packs volume in uint32_t
        uint64_t uncovered_mask = is->hs.all_vertices_mask & ~covered;
        int ucount = popcount_u64(uncovered_mask);
        if (ucount == 0 || ucount > opt.deep_cover_dp_max_uncovered) return true;

        std::size_t cand_count = allowed_volume_filtered.count();
        if (cand_count == 0 || cand_count > opt.deep_cover_dp_max_candidates) return true;

        auto t0 = std::chrono::steady_clock::now();
        stats.deep_cover_dp_calls.fetch_add(1, std::memory_order_relaxed);

        std::vector<int> uverts;
        uverts.reserve(static_cast<std::size_t>(ucount));
        uint64_t tmpu = uncovered_mask;
        while (tmpu) {
            unsigned v = std::countr_zero(tmpu);
            uverts.push_back(static_cast<int>(v));
            tmpu &= tmpu - 1;
        }

        const uint32_t full_cover = (uint32_t{1} << ucount) - 1u;
        struct Item { uint32_t fp; int vol; std::size_t count; };

        // Multiplicity-compress identical relaxed covering moves.  This DP ignores
        // future pairwise compatibility, so failure is a rigorous prune.
        std::unordered_map<uint64_t, std::size_t> mult;
        mult.reserve(std::min<std::size_t>(cand_count, 8192));
        allowed_volume_filtered.for_each_set_bit([&](std::size_t idx) {
            int vol = is->vol_list[idx];
            if (vol <= 0 || vol > remaining) return;
            uint64_t vm = is->vmask_list[idx];
            uint32_t fp = 0;
            for (int p = 0; p < ucount; ++p) {
                if ((vm >> uverts[static_cast<std::size_t>(p)]) & 1ULL) fp |= (uint32_t{1} << p);
            }
            uint64_t key = (uint64_t{fp} << 6) | static_cast<uint64_t>(vol);
            mult[key]++;
        });

        if (mult.empty()) return true;

        std::vector<Item> items;
        items.reserve(mult.size());
        for (auto& kv : mult) {
            uint32_t fp = static_cast<uint32_t>(kv.first >> 6);
            int vol = static_cast<int>(kv.first & 63u);
            std::size_t cap = static_cast<std::size_t>(remaining / vol);
            items.push_back(Item{fp, vol, std::min(kv.second, cap)});
        }

        const std::size_t masks = std::size_t{1} << ucount;
        const uint32_t vol_mask = (uint32_t{1} << (remaining + 1)) - 1u;
        const uint32_t target_bit = (uint32_t{1} << remaining);
        std::vector<uint32_t> dp(masks, 0);
        dp[0] = 1u;

        for (const Item& it : items) {
            for (std::size_t copy = 0; copy < it.count; ++copy) {
                std::vector<uint32_t> next = dp;
                for (uint32_t m = 0; m <= full_cover; ++m) {
                    uint32_t bits = dp[m];
                    if (!bits) continue;
                    uint32_t shifted = (bits << it.vol) & vol_mask;
                    if (!shifted) continue;
                    next[m | it.fp] |= shifted;
                }
                dp.swap(next);
                if (dp[full_cover] & target_bit) {
                    auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
                    stats.time_deep_cover_dp_ns.fetch_add(static_cast<std::uint64_t>(dt), std::memory_order_relaxed);
                    return true;
                }
            }
        }

        bool ok = (dp[full_cover] & target_bit) != 0;
        auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
        stats.time_deep_cover_dp_ns.fetch_add(static_cast<std::uint64_t>(dt), std::memory_order_relaxed);
        return ok;
    }

    bool feasibility_prune(const DynBitset& allowed, uint64_t covered, int remaining, DynBitset& allowed_volume_filtered) {
        stats.candidates_before_volume.fetch_add(allowed.count(), std::memory_order_relaxed);
        volume_filter_into(allowed, remaining, allowed_volume_filtered);
        stats.candidates_after_volume.fetch_add(allowed_volume_filtered.count(), std::memory_order_relaxed);
        if (remaining > 0 && allowed_volume_filtered.empty()) {
            stats.volume_prunes.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        if (!volume_subset_feasible(allowed_volume_filtered, remaining)) {
            stats.volume_prunes.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        if (!coverage_feasible(allowed_volume_filtered, covered)) {
            stats.coverage_prunes.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        if (!deep_cover_volume_feasible(allowed_volume_filtered, covered, remaining)) {
            stats.deep_cover_dp_prunes.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    DynBitset canonical_candidate_set_nonroot(const DynBitset& volume_filtered_allowed,
                                              const CanonAutInfo& aut) const {
        // For correctness of canonical construction-path generation, the candidate
        // set must be invariant under Aut(current_state).  The safest invariant
        // choice is all currently allowed compatible images, after exact volume
        // filtering.  Coverage heuristics are used only in ordering and feasibility
        // pruning, not as a branching restriction.
        return reduce_candidates_by_exact_aut(volume_filtered_allowed, aut);
    }

    bool canonical_parent_accepts(const std::vector<int>& parent, const std::vector<int>& child) {
        auto t0 = std::chrono::steady_clock::now();
        stats.canonical_parent_tests.fetch_add(1, std::memory_order_relaxed);

        bool ok = false;
        if (opt.use_graph_canonical_parent) {
            if (parent.empty()) {
                ok = canon.canonical_parent_key_graph_for_indices(child).empty();
            } else {
                std::string parent_key = canon.canonical_key_graph_for_indices(parent);
                std::string child_parent_key = canon.canonical_parent_key_graph_for_indices(child);
                ok = (parent_key == child_parent_key);
            }
        } else {
            // Debug/reference mode.
            if (parent.empty()) {
                ok = canon.canonical_parent_key_brute_for_indices(child).empty();
            } else {
                std::string parent_key = canon.canonical_key_brute_for_indices(parent);
                std::string child_parent_key = canon.canonical_parent_key_brute_for_indices(child);
                ok = (parent_key == child_parent_key);
            }
        }

        if (ok) stats.canonical_parent_accepts.fetch_add(1, std::memory_order_relaxed);
        auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
        stats.time_canonical_ns.fetch_add(static_cast<std::uint64_t>(dt), std::memory_order_relaxed);
        return ok;
    }

    DynBitset canonical_candidates_for_state(const std::vector<int>& chosen,
                                             const DynBitset& volume_filtered_allowed) {
        if (chosen.empty() && opt.quotient && opt.symbreak_root_orbits) {
            DynBitset candidates = root_orbit_candidates(volume_filtered_allowed);
            stats.candidates_after_aut.fetch_add(candidates.count(), std::memory_order_relaxed);
            return candidates;
        }
        CanonAutInfo aut = exact_automorphism_info(chosen);
        DynBitset candidates = canonical_candidate_set_nonroot(volume_filtered_allowed, aut);
        stats.candidates_after_aut.fetch_add(candidates.count(), std::memory_order_relaxed);
        return candidates;
    }

    void run_parallel_canonical(std::vector<int>& root_chosen, DynBitset& root_allowed) {
        std::vector<SearchTask> tasks;
        tasks.reserve(4096);
        collect_parallel_tasks_canonical(tasks, root_chosen, 0, 0, root_allowed, 0, opt.parallel_depth);

        if (!opt.quiet) {
            std::cerr << "parallel canonical augmentation: " << tasks.size()
                      << " initial task" << (tasks.size() == 1 ? "" : "s")
                      << " at split depth " << opt.parallel_depth
                      << " using " << opt.threads << " threads\n";
        }

        std::atomic<std::size_t> next{0};
        std::vector<std::thread> workers;
        std::size_t tcount = std::min<std::size_t>(opt.threads, std::max<std::size_t>(1, tasks.size()));
        workers.reserve(tcount);
        for (std::size_t t = 0; t < tcount; ++t) {
            workers.emplace_back([&, t]() {
                maybe_pin_worker(t, opt.threads, opt.affinity);
                for (;;) {
                    if (max_limit_reached()) break;
                    std::size_t k = next.fetch_add(1, std::memory_order_relaxed);
                    if (k >= tasks.size()) break;
                    SearchTask& task = tasks[k];
                    SearchWorkspace ws;
                    ws.prepare(total_volume() + 2);
                    dfs_canonical_ws(task.chosen, task.covered, task.volume_sum, task.allowed, task.depth, ws);
                }
                flush_state_counter();
            });
        }
        for (auto& th : workers) th.join();
    }

    void collect_parallel_tasks_canonical(std::vector<SearchTask>& tasks,
                                          std::vector<int>& chosen,
                                          uint64_t covered,
                                          int volume_sum,
                                          DynBitset& allowed,
                                          int depth,
                                          int cutoff_depth) {
        if (max_limit_reached()) return;
        int tot = total_volume();
        if (volume_sum > tot) return;
        int remaining = tot - volume_sum;

        if (volume_sum == tot) {
            if (covered == is->hs.all_vertices_mask) handle_solution(chosen);
            return;
        }

        DynBitset allowedR;
        if (feasibility_prune(allowed, covered, remaining, allowedR)) return;

        if (depth >= cutoff_depth) {
            tasks.push_back(SearchTask{chosen, covered, volume_sum, allowedR, depth});
            return;
        }

        record_state_visited();

        DynBitset candidates = canonical_candidates_for_state(chosen, allowedR);
        std::vector<int> ordered = order_candidates(candidates, covered, remaining);

        for (int idx : ordered) {
            if (max_limit_reached()) return;
            if (!allowedR.test(static_cast<std::size_t>(idx))) continue;
            std::vector<int> next_chosen = sorted_insert(chosen, idx);
            if (next_chosen.size() == chosen.size()) continue;
            if (!canonical_parent_accepts(chosen, next_chosen)) continue;

            uint64_t new_covered = covered | is->vmask_list[idx];
            int new_volume = volume_sum + is->vol_list[idx];

            DynBitset new_allowed;
            compat_intersect_into(idx, allowedR, new_allowed);

            collect_parallel_tasks_canonical(tasks, next_chosen, new_covered, new_volume,
                                             new_allowed, depth + 1, cutoff_depth);
        }
    }

    bool should_emit_prefix_task(std::size_t current_task_count, int depth, int min_depth) const {
        int max_depth = std::max(min_depth, opt.prefix_max_depth);
        std::size_t target = opt.prefix_task_target;
        if (target == 0) {
            return depth >= min_depth;
        }
        if (depth >= max_depth) return true;
        if (depth >= min_depth && current_task_count >= target) return true;
        return false;
    }

    void collect_parallel_tasks(std::vector<SearchTask>& tasks,
                                std::vector<int>& chosen,
                                uint64_t covered,
                                int volume_sum,
                                DynBitset& allowed,
                                int depth,
                                int cutoff_depth) {
        if (max_limit_reached()) return;
        int tot = total_volume();
        if (volume_sum > tot) return;
        int remaining = tot - volume_sum;

        if (volume_sum == tot) {
            if (covered == is->hs.all_vertices_mask) {
                if (opt.prefix_collect_complete) tasks.push_back(SearchTask{chosen, covered, volume_sum, allowed, depth});
                else handle_solution(chosen);
            }
            return;
        }

        DynBitset allowedR;
        if (feasibility_prune(allowed, covered, remaining, allowedR)) return;

        // Important production fix:
        // At the fixed split frontier (the common --auto path, prefix_task_target==0),
        // emit the task before forced-cell propagation.  Forced propagation is exact,
        // but doing it here serializes expensive work before worker threads are started.
        // The worker will run the same propagation inside dfs_ws(), so this changes only
        // scheduling, not the search tree or correctness.
        if (should_emit_prefix_task(tasks.size(), depth, cutoff_depth)) {
            tasks.push_back(SearchTask{chosen, covered, volume_sum, allowedR, depth});
            return;
        }

        // Adaptive prefix refinement is explicit.  If the user asks for it, then we
        // may do some exact forced propagation while constructing deeper prefix tasks,
        // but auto mode no longer does this because it can look like a single-thread hang.
        SearchWorkspace prefix_ws;
        prefix_ws.prepare(total_volume() + 2);
        auto prop = propagate_forced_cells(chosen, covered, volume_sum, allowedR, depth, prefix_ws);
        if (prop == PropagationStatus::Prune || prop == PropagationStatus::Complete) return;
        remaining = total_volume() - volume_sum;

        record_state_visited();

        DynBitset candidates = candidate_set(chosen, covered, allowedR, depth);
        candidates = reduce_by_stabilizer(chosen, candidates, depth);
        stats.candidates_after_aut.fetch_add(candidates.count(), std::memory_order_relaxed);
        std::vector<int> ordered = order_candidates(candidates, covered, remaining);

        for (int idx : ordered) {
            if (max_limit_reached()) return;
            if (!allowedR.test(static_cast<std::size_t>(idx))) continue;
            std::vector<int> next_chosen = sorted_insert(chosen, idx);
            if (next_chosen.size() == chosen.size()) continue;
            if (!insert_state_or_skip(next_chosen)) continue;

            uint64_t new_covered = covered | is->vmask_list[idx];
            int new_volume = volume_sum + is->vol_list[idx];

            DynBitset new_allowed;
            compat_intersect_into(idx, allowedR, new_allowed);

            collect_parallel_tasks(tasks, next_chosen, new_covered, new_volume,
                                   new_allowed, depth + 1, cutoff_depth);
        }
    }

    void insert_initial_state() {
        if (opt.state_cache_mode == "none") return;
        auto res = state_cache.insert("");
        if (res.evicted) stats.state_cache_evictions.fetch_add(res.evicted, std::memory_order_relaxed);
    }

    std::size_t state_cache_size() {
        return state_cache.size();
    }

    std::size_t estimated_state_cache_live_bytes() {
        return state_cache.live_bytes();
    }

    std::size_t state_cache_allocated_bytes() {
        return state_cache.allocated_bytes();
    }

    void shrink_state_cache_budget() {
        if (opt.state_cache_mode != "bounded" || opt.state_cache_max <= 1000) return;
        opt.state_cache_max = std::max<std::size_t>(1000, opt.state_cache_max / 2);
        if (opt.state_cache_mb > 0) opt.state_cache_mb = std::max<std::size_t>(1, opt.state_cache_mb / 2);
        state_cache.set_max_entries(opt.state_cache_max);
        state_cache.set_max_bytes(opt.state_cache_mb > 0 ? opt.state_cache_mb * 1024ULL * 1024ULL : std::size_t{0});
        std::size_t ev = state_cache.trim_to_max();
        if (ev) stats.state_cache_evictions.fetch_add(ev, std::memory_order_relaxed);
    }

    void enforce_memory_guard(std::size_t rss_mb) {
        if (opt.memory_limit_mb == 0 || rss_mb == 0 || rss_mb < opt.memory_limit_mb) return;
        stats.memory_guard_events.fetch_add(1, std::memory_order_relaxed);
        if (opt.memory_policy == "abort") {
            std::cerr << "memory guard abort: rss_mb=" << rss_mb
                      << " limit_mb=" << opt.memory_limit_mb << "\n";
            std::exit(3);
        }
        if (opt.memory_policy == "shrink") {
            shrink_state_cache_budget();
            compat.clear_lru();
            canon.clear_memo();
            std::cerr << "memory guard shrink: rss_mb=" << rss_mb
                      << " limit_mb=" << opt.memory_limit_mb
                      << " new_state_cache_max=" << opt.state_cache_max
                      << " new_state_cache_mb=" << opt.state_cache_mb
                      << " compat_lru_cleared=1 canon_lru_cleared=1\n";
        } else {
            std::cerr << "memory guard warning: rss_mb=" << rss_mb
                      << " limit_mb=" << opt.memory_limit_mb
                      << " policy=warn\n";
        }
    }

    std::size_t record_state_visited() {
        // Batched per-thread state counter: this removes a hot atomic increment from
        // every DFS node while preserving exact final counts after worker flushes.
        if (tls_state_counter.owner != nullptr && tls_state_counter.owner != this) {
            if (tls_state_counter.pending != 0) {
                const_cast<SearchEngine*>(tls_state_counter.owner)->flush_state_counter();
            }
            tls_state_counter.pending = 0;
        }
        tls_state_counter.owner = this;
        ++tls_state_counter.pending;
        constexpr std::size_t batch = 4096;
        if (tls_state_counter.pending >= batch) {
            return flush_state_counter();
        }
        return stats.states.load(std::memory_order_relaxed) + tls_state_counter.pending;
    }

    std::size_t flush_state_counter() {
        if (tls_state_counter.owner != this || tls_state_counter.pending == 0) {
            return stats.states.load(std::memory_order_relaxed);
        }
        std::size_t n = tls_state_counter.pending;
        tls_state_counter.pending = 0;
        std::size_t g = stats.states.fetch_add(n, std::memory_order_relaxed) + n;
        maybe_report_progress(g);
        return g;
    }

    void maybe_report_progress(std::size_t state_count) {
        if (opt.quiet || opt.progress_interval == 0) return;
        std::size_t target = next_progress_report.load(std::memory_order_relaxed);
        if (target == 0) {
            std::size_t init = opt.progress_interval;
            next_progress_report.compare_exchange_strong(target, init, std::memory_order_relaxed);
            target = next_progress_report.load(std::memory_order_relaxed);
        }
        while (state_count >= target) {
            if (next_progress_report.compare_exchange_weak(target, target + opt.progress_interval, std::memory_order_relaxed)) {
                std::lock_guard<std::mutex> out_lock(output_mutex);
                std::cerr << "progress states=" << state_count
                          << " solutions=" << stats.solutions.load(std::memory_order_relaxed)
                          << " solution_candidates=" << stats.solution_candidates.load(std::memory_order_relaxed)
                          << " duplicate_states=" << stats.duplicate_states.load(std::memory_order_relaxed)
                          << " generation_mode=" << opt.generation_mode
                          << " state_cache_mode=" << opt.state_cache_mode
                          << " state_cache_size=" << state_cache_size()
                          << " state_cache_evictions=" << stats.state_cache_evictions.load(std::memory_order_relaxed)
                          << " compat_hits=" << compat.hits.load(std::memory_order_relaxed)
                          << " compat_misses=" << compat.misses.load(std::memory_order_relaxed)
                          << " compat_rows_dense=" << compat.rows_dense.load(std::memory_order_relaxed)
                          << " compat_rows_sparse_incompat=" << compat.rows_sparse.load(std::memory_order_relaxed)
                          << " forced_cells=" << stats.forced_cells.load(std::memory_order_relaxed)
                          << " volume_prunes=" << stats.volume_prunes.load(std::memory_order_relaxed)
                          << " coverage_prunes=" << stats.coverage_prunes.load(std::memory_order_relaxed)
                          << " deep_cover_dp_prunes=" << stats.deep_cover_dp_prunes.load(std::memory_order_relaxed)
                          << " parent_tests=" << stats.canonical_parent_tests.load(std::memory_order_relaxed)
                          << " parent_accepts=" << stats.canonical_parent_accepts.load(std::memory_order_relaxed)
                          << " aut_computations=" << stats.automorphism_computations.load(std::memory_order_relaxed)
                          << " scheduler=" << opt.scheduler
                          << " active_workers=" << stats.active_workers.load(std::memory_order_relaxed)
                          << " peak_active_workers=" << stats.peak_active_workers.load(std::memory_order_relaxed)
                          << " ready_tasks=" << stats.ready_tasks.load(std::memory_order_relaxed)
                          << " tasks_created=" << stats.tasks_created.load(std::memory_order_relaxed)
                          << " tasks_completed=" << stats.tasks_completed.load(std::memory_order_relaxed)
                          << " tasks_donated=" << stats.tasks_donated.load(std::memory_order_relaxed)
                          << " dynamic_splits=" << stats.dynamic_splits.load(std::memory_order_relaxed)
                          << " scheduler_waits=" << stats.scheduler_waits.load(std::memory_order_relaxed)
                          << " local_dup_hits=" << stats.local_duplicate_hits.load(std::memory_order_relaxed);
                std::size_t rss = current_rss_mb();
                if (rss) std::cerr << " rss_mb=" << rss;
                if (opt.state_cache_mode == "bounded") {
                    std::cerr << " state_cache_live_mb=" << (estimated_state_cache_live_bytes() / (1024ULL * 1024ULL))
                              << " state_cache_alloc_mb=" << (state_cache_allocated_bytes() / (1024ULL * 1024ULL));
                }
                std::cerr << "\n";
                if (rss) enforce_memory_guard(rss);
                break;
            }
        }
    }

    std::string state_key(const std::vector<int>& chosen) {
        std::string key = opt.quotient ? canon.canonical_key_for_indices(chosen)
                                       : canon.raw_indices_key(chosen);
        if (opt.state_key_format == "hex" && opt.quotient) return binary_key_to_hex(key);
        return key;
    }

    bool local_duplicate_contains(const std::string& key) {
        if (opt.duplicate_local_cache_max == 0) return false;
        if (tls_local_duplicates.owner != this) {
            tls_local_duplicates.owner = this;
            tls_local_duplicates.set.clear();
            tls_local_duplicates.fifo.clear();
            tls_local_duplicates.set.reserve(std::min<std::size_t>(opt.duplicate_local_cache_max, 8192));
        }
        return tls_local_duplicates.set.find(key) != tls_local_duplicates.set.end();
    }

    void remember_local_duplicate(std::string key) {
        if (opt.duplicate_local_cache_max == 0) return;
        if (tls_local_duplicates.owner != this) {
            tls_local_duplicates.owner = this;
            tls_local_duplicates.set.clear();
            tls_local_duplicates.fifo.clear();
            tls_local_duplicates.set.reserve(std::min<std::size_t>(opt.duplicate_local_cache_max, 8192));
        }
        if (tls_local_duplicates.set.find(key) != tls_local_duplicates.set.end()) return;
        tls_local_duplicates.fifo.push_back(key);
        tls_local_duplicates.set.insert(std::move(key));
        while (tls_local_duplicates.fifo.size() > opt.duplicate_local_cache_max) {
            std::string old = std::move(tls_local_duplicates.fifo.front());
            tls_local_duplicates.fifo.pop_front();
            tls_local_duplicates.set.erase(old);
        }
    }

    bool insert_state_or_skip(const std::vector<int>& chosen) {
        if (opt.state_cache_mode == "none") return true;

        if (opt.labeled_state_cache_max > 0) {
            // Cheap exact prefilter: if the identical sorted labeled image-index
            // tuple has already been processed, skip before quotient canonicalization.
            // This never merges non-identical states and therefore cannot undercount.
            std::string raw = canon.raw_indices_key(chosen);
            auto raw_res = labeled_state_cache.insert(std::move(raw));
            if (raw_res.duplicate) {
                stats.duplicate_states.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        }

        std::string key = state_key(chosen);
        stats.state_cache_queries.fetch_add(1, std::memory_order_relaxed);

        if (opt.state_cache_scope == "thread-local") {
            if (tls_thread_state_cache.owner != this) {
                tls_thread_state_cache.owner = this;
                tls_thread_state_cache.set.clear();
                tls_thread_state_cache.fifo.clear();
                tls_thread_state_cache.set.reserve(std::min<std::size_t>(opt.state_cache_max, 1'000'000));
            }
            auto [it, inserted] = tls_thread_state_cache.set.insert(key);
            if (!inserted) {
                stats.duplicate_states.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            tls_thread_state_cache.fifo.push_back(key);
            std::size_t cap = opt.state_cache_max ? std::max<std::size_t>(1, opt.state_cache_max / std::max<std::size_t>(1, opt.threads)) : 1'000'000;
            while (tls_thread_state_cache.fifo.size() > cap && !tls_thread_state_cache.fifo.empty()) {
                std::string old = std::move(tls_thread_state_cache.fifo.front());
                tls_thread_state_cache.fifo.pop_front();
                tls_thread_state_cache.set.erase(old);
            }
            return true;
        }

        if (local_duplicate_contains(key)) {
            stats.local_duplicate_hits.fetch_add(1, std::memory_order_relaxed);
            stats.duplicate_states.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        std::string key_copy = key;
        auto res = state_cache.insert(std::move(key_copy));
        if (res.duplicate) {
            remember_local_duplicate(std::move(key));
            stats.duplicate_states.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (res.evicted) stats.state_cache_evictions.fetch_add(res.evicted, std::memory_order_relaxed);
        return true;
    }

    void compat_intersect_into(int idx, const DynBitset& allowedR, DynBitset& new_allowed) {
        auto tc0 = std::chrono::steady_clock::now();
        compat.intersect_into(static_cast<std::size_t>(idx), allowedR, new_allowed);
        auto tcd = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - tc0).count();
        stats.time_compat_ns.fetch_add(static_cast<std::uint64_t>(tcd), std::memory_order_relaxed);
        stats.compat_intersect_ops.fetch_add(1, std::memory_order_relaxed);
        stats.bitset_and_bytes.fetch_add(new_allowed.bytes(), std::memory_order_relaxed);
        new_allowed.reset(static_cast<std::size_t>(idx));
    }

    std::vector<int> sorted_insert(const std::vector<int>& chosen, int idx) const {
        std::vector<int> out = chosen;
        auto it = std::lower_bound(out.begin(), out.end(), idx);
        if (it == out.end() || *it != idx) out.insert(it, idx);
        return out;
    }

    std::optional<int> choose_uncovered_vertex(uint64_t covered, const DynBitset& allowed) const {
        uint64_t uncovered = is->hs.all_vertices_mask & ~covered;
        if (uncovered == 0) return std::nullopt;
        int best_v = -1;
        std::size_t best_count = std::numeric_limits<std::size_t>::max();
        while (uncovered) {
            unsigned v = std::countr_zero(uncovered);
            std::size_t c = allowed.count_intersection(is->by_vertex[v]);
            if (c < best_count) {
                best_count = c;
                best_v = static_cast<int>(v);
                if (c == 0) break;
            }
            uncovered &= uncovered - 1;
        }
        return best_v;
    }

    DynBitset root_orbit_candidates(const DynBitset& allowed) const {
        DynBitset cand(is->images.size());
        for (std::size_t oi = 0; oi < is->root_rep_by_orbit_index.size(); ++oi) {
            if (opt.root_shard_count > 1 && (oi % opt.root_shard_count) != opt.root_shard_index) continue;
            int idx = is->root_rep_by_orbit_index[oi];
            if (idx >= 0 && allowed.test(static_cast<std::size_t>(idx))) cand.set(static_cast<std::size_t>(idx));
        }
        return cand;
    }

    DynBitset reduce_by_stabilizer(const std::vector<int>& chosen, const DynBitset& candidates, int depth) {
        if (!opt.quotient || !opt.symbreak_stabilizer || chosen.empty()) return candidates;
        if (depth > opt.symbreak_stab_depth) return candidates;
        if (candidates.count() > opt.symbreak_stab_candidate_max) return candidates;

        auto rd = canon.refine_blocks_for_indices(chosen);
        std::size_t prod = canon.factorial_product(rd.blocks);
        if (prod > opt.symbreak_stab_max_perms) return candidates;

        std::vector<uint64_t> state_vm;
        for (int idx : chosen) state_vm.push_back(is->vmask_list[idx]);
        std::sort(state_vm.begin(), state_vm.end());

        std::vector<std::vector<uint8_t>> stabilizer;
        canon.enumerate_within_block_perms(rd.blocks, [&](const std::vector<uint8_t>& perm) {
            std::vector<uint64_t> t;
            t.reserve(state_vm.size());
            for (uint64_t vm : state_vm) t.push_back(is->hs.permute_vmask_by_vector(vm, perm));
            std::sort(t.begin(), t.end());
            if (t == state_vm) stabilizer.push_back(perm);
        });
        if (stabilizer.size() <= 1) return candidates;

        DynBitset keep(is->images.size());
        candidates.for_each_set_bit([&](std::size_t idx) {
            int min_idx = static_cast<int>(idx);
            uint64_t vm = is->vmask_list[idx];
            for (const auto& perm : stabilizer) {
                uint64_t tvm = is->hs.permute_vmask_by_vector(vm, perm);
                auto it = is->min_image_by_vmask.find(tvm);
                if (it != is->min_image_by_vmask.end()) min_idx = std::min(min_idx, it->second);
            }
            if (static_cast<int>(idx) == min_idx) keep.set(idx);
        });
        return keep;
    }

    void order_candidates_into(const DynBitset& cand, uint64_t covered, int remaining, std::vector<int>& out) const {
        // Store only image indices.  Earlier versions stored a 3-int score record
        // plus a second output vector; with many worker threads this temporary
        // candidate storage could multiply into several GB.
        out.clear();
        std::size_t c = cand.count();
        if (out.capacity() < c) out.reserve(c);
        cand.for_each_set_bit([&](std::size_t idx) {
            if (is->vol_list[idx] <= remaining) out.push_back(static_cast<int>(idx));
        });
        uint64_t notcov = is->hs.all_vertices_mask & ~covered;
        std::sort(out.begin(), out.end(), [&](int a, int b) {
            if (covered == 0 && !root_perf_score.empty()) {
                double as = (static_cast<std::size_t>(a) < root_perf_score.size()) ? root_perf_score[static_cast<std::size_t>(a)] : 0.0;
                double bs = (static_cast<std::size_t>(b) < root_perf_score.size()) ? root_perf_score[static_cast<std::size_t>(b)] : 0.0;
                if (as != bs) return as > bs;
            }
            int anew = popcount_u64(is->vmask_list[a] & notcov);
            int bnew = popcount_u64(is->vmask_list[b] & notcov);
            if (anew != bnew) return anew > bnew;
            int av = is->vol_list[a];
            int bv = is->vol_list[b];
            if (av != bv) return av > bv;
            return a < b;
        });
    }

    std::vector<int> order_candidates(const DynBitset& cand, uint64_t covered, int remaining) const {
        std::vector<int> out;
        order_candidates_into(cand, covered, remaining, out);
        return out;
    }

    DynBitset candidate_set(const std::vector<int>& /*chosen*/, uint64_t covered, const DynBitset& allowed, int depth) const {
        if (depth == 0 && opt.quotient && opt.symbreak_root_orbits) {
            return root_orbit_candidates(allowed);
        }
        auto v = choose_uncovered_vertex(covered, allowed);
        if (!v) {
            // All vertices are already covered but the volume sum is still too small.
            // This happens in genuine tilings: further face-fitting cells may add
            // volume without adding new hypersimplex vertices.
            return allowed;
        }
        DynBitset out;
        out.assign_and(allowed, is->by_vertex[*v]);
        return out;
    }

    enum class PropagationStatus { Continue, Prune, Complete };

    std::optional<int> unique_candidate_covering_vertex(const DynBitset& allowedR, int vertex) const {
        const DynBitset& bv = is->by_vertex[static_cast<std::size_t>(vertex)];
        if (allowedR.count_intersection_limited(bv, 2) != 1) return std::nullopt;
        auto idx = allowedR.first_intersection(bv);
        if (!idx) return std::nullopt;
        return static_cast<int>(*idx);
    }

    PropagationStatus propagate_forced_cells(std::vector<int>& chosen,
                                             uint64_t& covered,
                                             int& volume_sum,
                                             DynBitset& allowedR,
                                             int& depth,
                                             SearchWorkspace& ws) {
        if (!opt.force_propagation || opt.generation_mode != "state-cache") return PropagationStatus::Continue;

        const int tot = total_volume();
        DynBitset current_allowed;
        current_allowed.copy_from(allowedR);

        for (;;) {
            if (max_limit_reached()) return PropagationStatus::Prune;
            if (volume_sum > tot) {
                stats.volume_prunes.fetch_add(1, std::memory_order_relaxed);
                return PropagationStatus::Prune;
            }
            int remaining = tot - volume_sum;
            if (volume_sum == tot) {
                if (covered == is->hs.all_vertices_mask) {
                    handle_solution(chosen);
                    return PropagationStatus::Complete;
                }
                return PropagationStatus::Prune;
            }

            uint64_t uncovered = is->hs.all_vertices_mask & ~covered;
            if (uncovered == 0) {
                allowedR.copy_from(current_allowed);
                return PropagationStatus::Continue;
            }

            int forced_idx = -1;
            std::size_t best_count = std::numeric_limits<std::size_t>::max();

            uint64_t tmp = uncovered;
            while (tmp) {
                unsigned v = std::countr_zero(tmp);
                const DynBitset& bv = is->by_vertex[v];
                std::size_t c = current_allowed.count_intersection_limited(bv, 2);
                if (c == 0) {
                    stats.coverage_prunes.fetch_add(1, std::memory_order_relaxed);
                    return PropagationStatus::Prune;
                }
                if (c == 1) {
                    auto u = current_allowed.first_intersection(bv);
                    if (u) {
                        forced_idx = static_cast<int>(*u);
                        break;
                    }
                }
                best_count = std::min(best_count, c);
                tmp &= tmp - 1;
            }

            if (forced_idx < 0) {
                allowedR.copy_from(current_allowed);
                return PropagationStatus::Continue;
            }

            if (is->vol_list[forced_idx] > remaining) {
                stats.volume_prunes.fetch_add(1, std::memory_order_relaxed);
                return PropagationStatus::Prune;
            }

            ws.ensure(depth + 1, is->images.size());
            std::vector<int>& next_chosen = ws.chosen_stack[static_cast<std::size_t>(depth + 1)];
            next_chosen = chosen;
            auto it = std::lower_bound(next_chosen.begin(), next_chosen.end(), forced_idx);
            if (it != next_chosen.end() && *it == forced_idx) {
                // Should not happen because chosen cells are reset out of allowed, but
                // treating it as a prune avoids an infinite propagation loop.
                stats.forced_prunes.fetch_add(1, std::memory_order_relaxed);
                return PropagationStatus::Prune;
            }
            next_chosen.insert(it, forced_idx);

            if (!insert_state_or_skip(next_chosen)) {
                stats.forced_prunes.fetch_add(1, std::memory_order_relaxed);
                return PropagationStatus::Prune;
            }

            DynBitset& next_allowed = ws.allowed_stack[static_cast<std::size_t>(depth + 1)];
            compat_intersect_into(forced_idx, current_allowed, next_allowed);
            current_allowed.copy_from(next_allowed);

            chosen = next_chosen;
            covered |= is->vmask_list[forced_idx];
            volume_sum += is->vol_list[forced_idx];
            ++depth;
            stats.forced_cells.fetch_add(1, std::memory_order_relaxed);

            remaining = tot - volume_sum;
            if (remaining < 0) {
                stats.volume_prunes.fetch_add(1, std::memory_order_relaxed);
                return PropagationStatus::Prune;
            }

            DynBitset filtered;
            if (feasibility_prune(current_allowed, covered, remaining, filtered)) {
                return PropagationStatus::Prune;
            }
            current_allowed.copy_from(filtered);
        }
    }

    std::string solution_key_for_chosen(const std::vector<int>& chosen) {
        if (opt.quotient) {
            // Final solution deduplication is deliberately brute-force canonical.
            // Refinement is used for partial-state pruning, but final counts must
            // be independent of any auxiliary coloring.
            std::vector<uint64_t> final_vmasks;
            final_vmasks.reserve(chosen.size());
            for (int idx : chosen) final_vmasks.push_back(is->vmask_list[idx]);
            return canon.canonical_key_brute(final_vmasks);
        }
        return canon.raw_indices_key(chosen);
    }

    static std::string indices_csv(const std::vector<int>& chosen) {
        std::ostringstream os;
        for (std::size_t i = 0; i < chosen.size(); ++i) {
            if (i) os << ',';
            os << chosen[i];
        }
        return os.str();
    }

    static std::vector<int> parse_indices_csv(const std::string& s) {
        std::vector<int> out;
        std::stringstream ss(s);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            tok = trim(tok);
            if (!tok.empty()) out.push_back(std::stoi(tok));
        }
        return out;
    }

    std::vector<SearchTask> build_prefix_tasks() {
        std::vector<SearchTask> tasks;
        tasks.reserve(std::max<std::size_t>(4096, opt.prefix_task_target));
        insert_initial_state();
        std::vector<int> root_chosen;
        DynBitset root_allowed(is->images.size(), true);
        collect_parallel_tasks(tasks, root_chosen, 0, 0, root_allowed, 0, opt.parallel_depth);
        flush_state_counter();
        stats.prefix_tasks.store(tasks.size(), std::memory_order_relaxed);
        return tasks;
    }

    std::size_t estimate_task_weight(const SearchTask& task) const {
        int remaining = std::max(0, total_volume() - task.volume_sum);
        std::size_t uncovered = static_cast<std::size_t>(popcount_u64(is->hs.all_vertices_mask & ~task.covered));
        std::size_t allowed_count = task.allowed.count();
        // Saturating multiplicative weight.  It is only a scheduling hint for
        // prefix-file sharding; it never affects correctness.
        long double w = static_cast<long double>(std::max<std::size_t>(1, allowed_count))
                      * static_cast<long double>(std::max<std::size_t>(1, uncovered))
                      * static_cast<long double>(std::max<int>(1, remaining));
        if (w > static_cast<long double>(std::numeric_limits<std::size_t>::max())) {
            return std::numeric_limits<std::size_t>::max();
        }
        return std::max<std::size_t>(1, static_cast<std::size_t>(w));
    }

    void write_prefix_tasks_file(const std::filesystem::path& path) {
        bool old_prefix_collect = opt.prefix_collect_complete;
        bool old_force = opt.force_propagation;
        bool old_count_only = opt.count_only;
        opt.prefix_collect_complete = true;
        opt.force_propagation = false; // avoid emitting completed forced states while writing tasks
        opt.count_only = true;
        auto tasks = build_prefix_tasks();
        opt.count_only = old_count_only;
        opt.force_propagation = old_force;
        opt.prefix_collect_complete = old_prefix_collect;
        std::ofstream f(path);
        if (!f) throw std::runtime_error("Cannot write prefix task file: " + path.string());
        std::size_t k = 0;
        for (const auto& task : tasks) {
            std::size_t weight = estimate_task_weight(task);
            f << "{\"task_index\":" << k++
              << ",\"weight\":" << weight
              << ",\"n\":" << is->hs.n
              << ",\"volume_mode\":\"" << opt.volume_mode << "\""
              << ",\"chosen\":[";
            for (std::size_t i = 0; i < task.chosen.size(); ++i) {
                if (i) f << ',';
                f << task.chosen[i];
            }
            f << "],\"covered\":\"" << hex_u64(task.covered)
              << "\",\"volume_sum\":" << task.volume_sum
              << "}\n";
        }
        if (!opt.quiet) {
            std::cerr << "wrote " << tasks.size() << " prefix tasks to " << path << "\n";
        }
    }

    static std::vector<int> parse_prefix_chosen_line(const std::string& line) {
        std::size_t p = line.find("\"chosen\"");
        if (p == std::string::npos) return {};
        p = line.find('[', p);
        std::size_t q = line.find(']', p);
        if (p == std::string::npos || q == std::string::npos || q <= p) return {};
        return parse_indices_csv(line.substr(p + 1, q - p - 1));
    }

    SearchTask reconstruct_prefix_task(const std::vector<int>& chosen_input) {
        SearchTask task;
        task.allowed.resize(is->images.size(), true);
        task.chosen.clear();
        task.covered = 0;
        task.volume_sum = 0;
        task.depth = static_cast<int>(chosen_input.size());

        std::vector<int> chosen = chosen_input;
        std::sort(chosen.begin(), chosen.end());
        chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());

        DynBitset cur(is->images.size(), true);
        DynBitset next;
        for (int idx : chosen) {
            if (idx < 0 || static_cast<std::size_t>(idx) >= is->images.size()) {
                throw std::runtime_error("Prefix task contains invalid image index.");
            }
            task.chosen.push_back(idx);
            task.covered |= is->vmask_list[static_cast<std::size_t>(idx)];
            task.volume_sum += is->vol_list[static_cast<std::size_t>(idx)];
            compat_intersect_into(idx, cur, next);
            cur.copy_from(next);
        }
        task.allowed.copy_from(cur);
        return task;
    }

    void run_from_prefix_chosen_tasks(const std::vector<std::vector<int>>& prefix_chosen, std::ostream& output_stream) {
        out = &output_stream;
        stats.start = std::chrono::steady_clock::now();
        opt.threads = normalize_threads(opt.threads);
        if (opt.solution_dedup == "disk") open_solution_spool();
        start_output_writer();
        insert_initial_state();

        std::vector<SearchTask> tasks;
        tasks.reserve(prefix_chosen.size());
        for (const auto& ch : prefix_chosen) {
            SearchTask task = reconstruct_prefix_task(ch);
            if (!insert_state_or_skip(task.chosen)) continue;
            tasks.push_back(std::move(task));
        }
        stats.prefix_tasks.store(tasks.size(), std::memory_order_relaxed);

        if (!opt.quiet) {
            std::cerr << "prefix-task search: " << tasks.size()
                      << " task" << (tasks.size() == 1 ? "" : "s")
                      << " using " << opt.threads << " threads\n";
        }

        if (opt.scheduler == "work-steal") {
            std::size_t target = std::max<std::size_t>(opt.threads * std::max<std::size_t>(1, opt.task_target_per_thread),
                                                       opt.threads * 4);
            DynamicTaskQueue queue(target, &stats);
            for (auto& task : tasks) queue.push(std::move(task));
            std::vector<std::thread> workers;
            workers.reserve(opt.threads);
            for (std::size_t t = 0; t < opt.threads; ++t) {
                workers.emplace_back([&, t]() {
                    maybe_pin_worker(t, opt.threads, opt.affinity);
                    SearchWorkspace ws;
                    ws.prepare(total_volume() + 2 + std::max(0, opt.split_max_depth));
                    for (;;) {
                        if (max_limit_reached()) {
                            queue.close_and_clear();
                            break;
                        }
                        SearchTask task;
                        if (!queue.pop(task)) break;
                        dfs_ws_dynamic(task.chosen, task.covered, task.volume_sum,
                                       task.allowed, task.depth, ws, queue);
                        flush_state_counter();
                        queue.finish_task();
                        if (max_limit_reached()) queue.close_and_clear();
                    }
                    flush_state_counter();
                });
            }
            for (auto& th : workers) th.join();
        } else {
            std::atomic<std::size_t> next_task{0};
            std::size_t tcount = std::min<std::size_t>(opt.threads, std::max<std::size_t>(1, tasks.size()));
            std::vector<std::thread> workers;
            workers.reserve(tcount);
            for (std::size_t t = 0; t < tcount; ++t) {
                workers.emplace_back([&, t]() {
                    maybe_pin_worker(t, opt.threads, opt.affinity);
                    for (;;) {
                        if (max_limit_reached()) break;
                        std::size_t k = next_task.fetch_add(1, std::memory_order_relaxed);
                        if (k >= tasks.size()) break;
                        SearchWorkspace ws;
                        ws.prepare(total_volume() + 2);
                        SearchTask& task = tasks[k];
                        dfs_ws(task.chosen, task.covered, task.volume_sum, task.allowed, task.depth, ws);
                    }
                    flush_state_counter();
                });
            }
            for (auto& th : workers) th.join();
        }
        flush_state_counter();

        if (opt.solution_dedup == "disk") finalize_solution_spool();
        if (!opt.record_hot_compat_rows.empty()) {
            compat.save_hot_rows(opt.record_hot_compat_rows);
        }
        save_root_performance_file();
        stop_output_writer();

        if (!opt.quiet) {
            auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - stats.start).count();
            std::cerr << "prefix states=" << stats.states.load()
                      << " solutions=" << stats.solutions.load()
                      << " solution_candidates=" << stats.solution_candidates.load()
                      << " duplicate_states=" << stats.duplicate_states.load()
                      << " prefix_tasks=" << stats.prefix_tasks.load()
                      << " elapsed_sec=" << std::fixed << std::setprecision(3) << elapsed << "\n";
        }
    }

    void open_solution_spool() {
        std::filesystem::path dir = opt.solution_spool_dir.empty() ? opt.cache_dir : opt.solution_spool_dir;
        if (dir.empty()) dir = ".alexeev_cache";
        std::filesystem::create_directories(dir);
        std::ostringstream name;
        name << "r3n" << is->hs.n << "_" << opt.volume_mode
             << "_solutions_" << opt.root_shard_index << "of" << opt.root_shard_count
             << "_" << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count()
             << ".spool";
        solution_spool_path = dir / name.str();
        solution_spool.open(solution_spool_path);
        if (!solution_spool) throw std::runtime_error("Cannot open solution spool: " + solution_spool_path.string());
        if (!opt.quiet) std::cerr << "solution-dedup disk spool: " << solution_spool_path << "\n";
    }

    void spool_solution(const std::vector<int>& chosen, const std::string& key) {
        std::string printable_key = opt.quotient ? binary_key_to_hex(key) : key;
        std::lock_guard<std::mutex> lock(solution_mutex);
        if (max_limit_reached()) return;
        solution_spool << printable_key << '\t' << indices_csv(chosen) << '\n';
        stats.solution_candidates.fetch_add(1, std::memory_order_relaxed);
    }

    void finalize_solution_spool() {
        if (solution_spool.is_open()) solution_spool.close();
        std::ifstream in(solution_spool_path);
        if (!in) throw std::runtime_error("Cannot reopen solution spool: " + solution_spool_path.string());

        std::vector<std::pair<std::string,std::string>> recs;
        std::string line;
        while (std::getline(in, line)) {
            std::size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            recs.emplace_back(line.substr(0, tab), line.substr(tab + 1));
        }
        std::sort(recs.begin(), recs.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });

        std::size_t unique = 0;
        std::string last_key;
        bool have = false;
        for (const auto& r : recs) {
            if (have && r.first == last_key) continue;
            have = true;
            last_key = r.first;
            ++unique;
            if (!opt.count_only) {
                std::vector<int> chosen = parse_indices_csv(r.second);
                std::string raw_key = opt.quotient ? hex_key_to_binary(r.first) : r.first;
                emit_solution(chosen, unique, raw_key);
            }
        }
        stats.solutions.store(unique, std::memory_order_relaxed);
        if (!opt.quiet) {
            std::cerr << "solution spool candidates=" << recs.size()
                      << " unique_final=" << unique << "\n";
        }
    }

    void handle_solution(const std::vector<int>& chosen) {
        if (root_solution_counts && !chosen.empty()) {
            // Approximate root-productivity signal; used only for future ordering hints.
            std::size_t root_idx = static_cast<std::size_t>(chosen.front());
            if (root_idx < root_solution_count_size) root_solution_counts[root_idx].fetch_add(1, std::memory_order_relaxed);
        }
        std::string key = solution_key_for_chosen(chosen);

        if (opt.solution_dedup == "disk") {
            spool_solution(chosen, key);
            return;
        }

        std::size_t tiling_index = 0;
        {
            std::lock_guard<std::mutex> lock(solution_mutex);
            if (max_limit_reached()) return;
            stats.solution_candidates.fetch_add(1, std::memory_order_relaxed);
            if (!solution_keys.insert(key).second) return;
            tiling_index = stats.solutions.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        if (!opt.count_only) emit_solution(chosen, tiling_index, key);
    }



    void dfs_canonical_ws(std::vector<int>& chosen,
                          uint64_t covered,
                          int volume_sum,
                          DynBitset& allowed,
                          int depth,
                          SearchWorkspace& ws) {
        if (max_limit_reached()) return;
        record_state_visited();
        int tot = total_volume();
        if (volume_sum > tot) return;
        int remaining = tot - volume_sum;

        if (volume_sum == tot) {
            if (covered == is->hs.all_vertices_mask) handle_solution(chosen);
            return;
        }

        ws.ensure(depth, is->images.size());
        DynBitset& allowedR = ws.allowed_stack[static_cast<std::size_t>(depth)];
        if (feasibility_prune(allowed, covered, remaining, allowedR)) return;

        DynBitset candidates = canonical_candidates_for_state(chosen, allowedR);
        std::vector<int>& ordered = ws.ordered_stack[static_cast<std::size_t>(depth)];
        order_candidates_into(candidates, covered, remaining, ordered);

        ws.ensure(depth + 1, is->images.size());
        for (int idx : ordered) {
            if (max_limit_reached()) return;
            if (!allowedR.test(static_cast<std::size_t>(idx))) continue;

            std::vector<int>& next_chosen = ws.chosen_stack[static_cast<std::size_t>(depth + 1)];
            next_chosen = chosen;
            auto it = std::lower_bound(next_chosen.begin(), next_chosen.end(), idx);
            if (it != next_chosen.end() && *it == idx) continue;
            next_chosen.insert(it, idx);

            if (!canonical_parent_accepts(chosen, next_chosen)) continue;

            uint64_t new_covered = covered | is->vmask_list[idx];
            int new_volume = volume_sum + is->vol_list[idx];

            DynBitset& new_allowed = ws.allowed_stack[static_cast<std::size_t>(depth + 1)];
            compat_intersect_into(idx, allowedR, new_allowed);

            dfs_canonical_ws(next_chosen, new_covered, new_volume, new_allowed, depth + 1, ws);
        }
    }

    void dfs_canonical(std::vector<int>& chosen, uint64_t covered, int volume_sum, DynBitset& allowed, int depth) {
        if (max_limit_reached()) return;
        record_state_visited();
        int tot = total_volume();
        if (volume_sum > tot) return;
        int remaining = tot - volume_sum;

        if (volume_sum == tot) {
            if (covered == is->hs.all_vertices_mask) handle_solution(chosen);
            return;
        }

        DynBitset allowedR;
        if (feasibility_prune(allowed, covered, remaining, allowedR)) return;

        DynBitset candidates = canonical_candidates_for_state(chosen, allowedR);
        std::vector<int> ordered = order_candidates(candidates, covered, remaining);

        for (int idx : ordered) {
            if (max_limit_reached()) return;
            if (!allowedR.test(static_cast<std::size_t>(idx))) continue;
            std::vector<int> next_chosen = sorted_insert(chosen, idx);
            if (next_chosen.size() == chosen.size()) continue;
            if (!canonical_parent_accepts(chosen, next_chosen)) continue;

            uint64_t new_covered = covered | is->vmask_list[idx];
            int new_volume = volume_sum + is->vol_list[idx];

            DynBitset new_allowed;
            compat_intersect_into(idx, allowedR, new_allowed);

            dfs_canonical(next_chosen, new_covered, new_volume, new_allowed, depth + 1);
        }
    }


    void enqueue_child_task(DynamicTaskQueue& queue,
                            const std::vector<int>& chosen,
                            uint64_t covered,
                            int volume_sum,
                            const DynBitset& allowed,
                            int depth) {
        SearchTask task;
        task.chosen = chosen;
        task.covered = covered;
        task.volume_sum = volume_sum;
        task.allowed.copy_from(allowed);
        task.depth = depth;
        queue.push(std::move(task));
        stats.tasks_donated.fetch_add(1, std::memory_order_relaxed);
    }

    void dfs_ws_dynamic(std::vector<int>& chosen,
                        uint64_t covered,
                        int volume_sum,
                        DynBitset& allowed,
                        int depth,
                        SearchWorkspace& ws,
                        DynamicTaskQueue& queue) {
        if (max_limit_reached()) return;
        record_state_visited();
        int tot = total_volume();
        if (volume_sum > tot) return;
        int remaining = tot - volume_sum;

        if (volume_sum == tot) {
            if (covered == is->hs.all_vertices_mask) handle_solution(chosen);
            return;
        }

        ws.ensure(depth, is->images.size());
        DynBitset& allowedR = ws.allowed_stack[static_cast<std::size_t>(depth)];
        if (feasibility_prune(allowed, covered, remaining, allowedR)) return;

        auto prop = propagate_forced_cells(chosen, covered, volume_sum, allowedR, depth, ws);
        if (prop == PropagationStatus::Prune || prop == PropagationStatus::Complete) return;
        remaining = total_volume() - volume_sum;
        ws.ensure(depth, is->images.size());

        DynBitset candidates = candidate_set(chosen, covered, allowedR, depth);
        candidates = reduce_by_stabilizer(chosen, candidates, depth);
        stats.candidates_after_aut.fetch_add(candidates.count(), std::memory_order_relaxed);

        std::vector<int>& ordered = ws.ordered_stack[static_cast<std::size_t>(depth)];
        order_candidates_into(candidates, covered, remaining, ordered);

        const bool may_split = (opt.scheduler == "static-hybrid")
            ? queue.should_split_late(depth, ordered.size(),
                                      opt.split_max_depth,
                                      opt.split_min_candidates,
                                      opt.threads,
                                      opt.donate_when_active_below)
            : queue.should_split(depth, ordered.size(),
                                 opt.split_max_depth,
                                 opt.split_min_candidates);
        if (may_split) stats.dynamic_splits.fetch_add(1, std::memory_order_relaxed);

        ws.ensure(depth + 1, is->images.size());

        // Donation policy: create ready work before diving into the first large child.
        // This is the key difference from the older static DFS.  The mathematical
        // child set is unchanged; we only choose which child subtrees become tasks.
        bool have_local_child = false;
        std::vector<int> local_chosen;
        uint64_t local_covered = 0;
        int local_volume = 0;
        DynBitset local_allowed;

        for (int idx : ordered) {
            if (max_limit_reached()) return;
            if (!allowedR.test(static_cast<std::size_t>(idx))) continue;

            std::vector<int>& next_chosen_ref = ws.chosen_stack[static_cast<std::size_t>(depth + 1)];
            next_chosen_ref = chosen;
            auto it = std::lower_bound(next_chosen_ref.begin(), next_chosen_ref.end(), idx);
            if (it != next_chosen_ref.end() && *it == idx) continue;
            next_chosen_ref.insert(it, idx);

            if (!insert_state_or_skip(next_chosen_ref)) continue;

            uint64_t new_covered = covered | is->vmask_list[idx];
            int new_volume = volume_sum + is->vol_list[idx];

            DynBitset& new_allowed_ref = ws.allowed_stack[static_cast<std::size_t>(depth + 1)];
            compat_intersect_into(idx, allowedR, new_allowed_ref);

            if (may_split && ((opt.scheduler == "static-hybrid")
                              ? queue.needs_more_tasks_late(opt.threads, opt.donate_when_active_below)
                              : queue.needs_more_tasks())) {
                if (!have_local_child) {
                    have_local_child = true;
                    local_chosen = next_chosen_ref;
                    local_covered = new_covered;
                    local_volume = new_volume;
                    local_allowed.copy_from(new_allowed_ref);
                } else {
                    enqueue_child_task(queue, next_chosen_ref, new_covered, new_volume,
                                       new_allowed_ref, depth + 1);
                }
                continue;
            }

            dfs_ws_dynamic(next_chosen_ref, new_covered, new_volume,
                           new_allowed_ref, depth + 1, ws, queue);
        }

        if (have_local_child && !max_limit_reached()) {
            dfs_ws_dynamic(local_chosen, local_covered, local_volume,
                           local_allowed, depth + 1, ws, queue);
        }

    }

    void dfs_ws(std::vector<int>& chosen,
                uint64_t covered,
                int volume_sum,
                DynBitset& allowed,
                int depth,
                SearchWorkspace& ws) {
        if (max_limit_reached()) return;
        record_state_visited();
        int tot = total_volume();
        if (volume_sum > tot) return;
        int remaining = tot - volume_sum;

        if (volume_sum == tot) {
            if (covered == is->hs.all_vertices_mask) handle_solution(chosen);
            return;
        }

        ws.ensure(depth, is->images.size());
        DynBitset& allowedR = ws.allowed_stack[static_cast<std::size_t>(depth)];
        if (feasibility_prune(allowed, covered, remaining, allowedR)) return;
        auto prop = propagate_forced_cells(chosen, covered, volume_sum, allowedR, depth, ws);
        if (prop == PropagationStatus::Prune || prop == PropagationStatus::Complete) return;
        remaining = total_volume() - volume_sum;
        ws.ensure(depth, is->images.size());

        DynBitset candidates = candidate_set(chosen, covered, allowedR, depth);
        candidates = reduce_by_stabilizer(chosen, candidates, depth);
        stats.candidates_after_aut.fetch_add(candidates.count(), std::memory_order_relaxed);

        std::vector<int>& ordered = ws.ordered_stack[static_cast<std::size_t>(depth)];
        order_candidates_into(candidates, covered, remaining, ordered);

        ws.ensure(depth + 1, is->images.size());
        for (int idx : ordered) {
            if (max_limit_reached()) return;
            if (!allowedR.test(static_cast<std::size_t>(idx))) continue;

            std::vector<int>& next_chosen = ws.chosen_stack[static_cast<std::size_t>(depth + 1)];
            next_chosen = chosen;
            auto it = std::lower_bound(next_chosen.begin(), next_chosen.end(), idx);
            if (it != next_chosen.end() && *it == idx) continue;
            next_chosen.insert(it, idx);

            if (!insert_state_or_skip(next_chosen)) continue;

            uint64_t new_covered = covered | is->vmask_list[idx];
            int new_volume = volume_sum + is->vol_list[idx];

            DynBitset& new_allowed = ws.allowed_stack[static_cast<std::size_t>(depth + 1)];
            compat_intersect_into(idx, allowedR, new_allowed);

            dfs_ws(next_chosen, new_covered, new_volume, new_allowed, depth + 1, ws);
        }
    }

    void dfs(std::vector<int>& chosen, uint64_t covered, int volume_sum, DynBitset& allowed, int depth) {
        SearchWorkspace ws;
        ws.prepare(total_volume() + 2);
        dfs_ws(chosen, covered, volume_sum, allowed, depth, ws);
    }

    std::string format_solution_record(const std::vector<int>& chosen, std::size_t tiling_index, const std::string& key) {
        std::ostringstream ss;
        if (opt.jsonl) {
            std::string printable_key = opt.quotient ? binary_key_to_hex(key) : key;
            ss << "{\"n\":" << is->hs.n << ",\"tiling_index\":" << tiling_index
               << ",\"cell_count\":" << chosen.size() << ",\"canonical_key\":\"" << printable_key << "\",\"cells\":[";
            for (std::size_t t = 0; t < chosen.size(); ++t) {
                int idx = chosen[t];
                const auto& im = is->images[idx];
                if (t) ss << ',';
                ss << "{\"orbit_id\":" << is->orbits[im.orbit_index].orbit_id
                   << ",\"perm\":[";
                const auto& p = is->pt.perms[im.perm_index];
                for (int i = 0; i < is->hs.n; ++i) { if (i) ss << ','; ss << int(p[i]); }
                ss << "],\"vmask\":\"" << hex_u64(im.vmask) << "\",\"label\":\""
                   << is->ineq_label_for_image(idx) << "\",\"volume_sha\":"
                   << is->orbits[im.orbit_index].volume_sha << ",\"volume_lattice\":"
                   << is->orbits[im.orbit_index].volume_lattice << ",\"ineqs\":[";
                auto ineqs = is->image_ineqs(idx);
                for (std::size_t q = 0; q < ineqs.size(); ++q) {
                    if (q) ss << ',';
                    ss << "{\"subset_mask\":" << ineqs[q].subset_mask << ",\"k\":" << int(ineqs[q].k) << "}";
                }
                ss << "]}";
            }
            ss << "]}\n";
        } else {
            ss << "Delta(3," << is->hs.n << ") | " << tiling_index << " | ";
            for (std::size_t t = 0; t < chosen.size(); ++t) {
                if (t) ss << " + ";
                ss << "[" << is->ineq_label_for_image(chosen[t]) << "]";
            }
            ss << "\n";
        }
        return ss.str();
    }

    void start_output_writer() {
        if (!opt.async_output || opt.count_only || output_started) return;
        output_done = false;
        output_started = true;
        output_thread = std::thread([this]() {
            std::size_t since_flush = 0;
            bool first_record_flushed = false;
            for (;;) {
                std::deque<std::string> local;
                {
                    std::unique_lock<std::mutex> lock(output_queue_mutex);
                    output_queue_cv.wait(lock, [&]() { return output_done || !output_queue.empty(); });
                    local.swap(output_queue);
                    if (local.empty() && output_done) break;
                }
                for (const auto& rec : local) {
                    (*out) << rec;
                    ++since_flush;
                    // In async mode, make the trivial tiling / first accepted tiling visible
                    // immediately, then switch to batched flushing for throughput.
                    if (!first_record_flushed) {
                        out->flush();
                        first_record_flushed = true;
                        since_flush = 0;
                    } else if (opt.flush_every > 0 && since_flush >= opt.flush_every) {
                        out->flush();
                        since_flush = 0;
                    }
                }
                if (output_done && local.empty()) break;
            }
            out->flush();
        });
    }

    void stop_output_writer() {
        if (!output_started) return;
        {
            std::lock_guard<std::mutex> lock(output_queue_mutex);
            output_done = true;
        }
        output_queue_cv.notify_all();
        if (output_thread.joinable()) output_thread.join();
        output_started = false;
    }

    void emit_solution(const std::vector<int>& chosen, std::size_t tiling_index, const std::string& key) {
        std::string rec = format_solution_record(chosen, tiling_index, key);
        if (opt.async_output && !opt.count_only) {
            {
                std::lock_guard<std::mutex> lock(output_queue_mutex);
                output_queue.push_back(std::move(rec));
            }
            stats.async_output_enqueued.fetch_add(1, std::memory_order_relaxed);
            output_queue_cv.notify_one();
            return;
        }
        std::lock_guard<std::mutex> out_lock(output_mutex);
        (*out) << rec;
        out->flush();
    }
};

inline bool validate_face_tests(const ImageSet& is, bool verbose=false) {
    FaceTester ft(is);
    bool ok = true;
    for (std::size_t i = 0; i < is.images.size(); ++i) {
        for (std::size_t j = 0; j < is.images.size(); ++j) {
            uint64_t inter = is.vmask_list[i] & is.vmask_list[j];
            bool a = ft.is_face_of_image(i, inter);
            bool b = ft.is_face_by_rep_pullback(i, inter);
            if (a != b) {
                std::cerr << "Face test mismatch: i=" << i << " j=" << j
                          << " inter=" << hex_u64(inter) << " closure=" << a << " pullback=" << b << "\n";
                ok = false;
                return ok;
            }
        }
        if (verbose && (i+1)%200==0) std::cerr << "face validation rows " << (i+1) << "/" << is.images.size() << "\n";
    }
    return ok;
}

inline std::size_t run_count(ImageSet& is, SearchOptions opt) {
    opt.count_only = true;
    opt.quiet = true;
    std::ostringstream sink;
    SearchEngine engine(is, opt);
    engine.run(sink);
    return engine.stats.solutions.load();
}

inline bool validate_all(ImageSet& is, const SearchOptions& base_opt, bool verbose=true) {
    bool ok = true;
    int n = is.hs.n;
    auto ec = expected_orbit_counts();
    if (ec.count(n)) {
        bool pass = static_cast<int>(is.orbits.size()) == ec[n];
        if (verbose) std::cerr << "orbit_count: got " << is.orbits.size() << " expected " << ec[n] << (pass ? " OK\n" : " FAIL\n");
        ok &= pass;
    }

    int expected_vertices = 0;
    for (int a = 0; a < n; ++a) for (int b = a+1; b < n; ++b) for (int c = b+1; c < n; ++c) expected_vertices++;
    bool vpass = is.hs.num_vertices == expected_vertices;
    if (verbose) std::cerr << "vertex_count: got " << is.hs.num_vertices << " expected " << expected_vertices << (vpass ? " OK\n" : " FAIL\n");
    ok &= vpass;

    bool uniform_ok = false;
    for (const auto& o : is.orbits) {
        if (o.ineqs.empty()) {
            uniform_ok = (o.rep_vmask == is.hs.all_vertices_mask);
            if (base_opt.volume_mode == "sha") uniform_ok &= (o.volume_sha == (n-3)*(n-3));
            break;
        }
    }
    if (verbose) std::cerr << "uniform_orbit: " << (uniform_ok ? "OK\n" : "FAIL\n");
    ok &= uniform_ok;

    auto ei = expected_image_counts();
    if (ei.count(n)) {
        bool pass = static_cast<int>(is.images.size()) == ei[n];
        if (verbose) std::cerr << "image_count: got " << is.images.size() << " expected " << ei[n] << (pass ? " OK\n" : " FAIL\n");
        ok &= pass;
    }

    if (n <= 6) {
        bool fpass = validate_face_tests(is, verbose);
        if (verbose) std::cerr << "face_test_pullback: " << (fpass ? "OK\n" : "FAIL\n");
        ok &= fpass;

        auto et = expected_quotient_tiling_counts();
        if (et.count(n)) {
            SearchOptions opt = base_opt;
            opt.search_mode = "quotient";
            opt.quotient = true;
            opt.canon_mode = "refine";
            opt.count_only = true;
            std::size_t got = run_count(is, opt);
            bool pass = static_cast<int>(got) == et[n];
            if (verbose) std::cerr << "quotient_tiling_count_refine: got " << got << " expected " << et[n] << (pass ? " OK\n" : " FAIL\n");
            ok &= pass;

            opt.canon_mode = "brute";
            std::size_t gotb = run_count(is, opt);
            bool passb = gotb == got;
            if (verbose) std::cerr << "quotient_tiling_count_brute: got " << gotb << " refine " << got << (passb ? " OK\n" : " FAIL\n");
            ok &= passb;
        }
    }
    return ok;
}


// ============================================================================
// Experimental Delta(3,9) support.
// ----------------------------------------------------------------------------
// The n<=8 production engine above intentionally keeps 64-bit vertex masks for
// speed.  Delta(3,9) has C(9,3)=84 hypersimplex vertices, so it requires a
// separate 128-bit vertex-mask path.  Keeping this path separate preserves the
// measured-fast n=8 implementation while enabling n=9 cache building,
// validation, and exact tiling search from an allr3n9.txt orbit file.
// ============================================================================

struct alignas(16) Mask128 {
    uint64_t lo = 0;
    uint64_t hi = 0; // bits 64..127; for Delta(3,9) only bits 0..19 are used.

    constexpr Mask128() = default;
    constexpr Mask128(uint64_t l, uint64_t h=0) : lo(l), hi(h) {}

#if defined(__SIZEOF_INT128__)
#  if defined(__GNUC__) || defined(__clang__)
    __extension__ using native_uint128_t = unsigned __int128;
#  else
    using native_uint128_t = unsigned __int128;
#  endif
    native_uint128_t to_native() const {
        return (static_cast<native_uint128_t>(hi) << 64) | static_cast<native_uint128_t>(lo);
    }
    static constexpr Mask128 from_native(native_uint128_t x) {
        return Mask128(static_cast<uint64_t>(x), static_cast<uint64_t>(x >> 64));
    }
#endif

    static Mask128 bit(std::size_t i) {
        return (i < 64) ? Mask128(uint64_t{1} << i, 0) : Mask128(0, uint64_t{1} << (i - 64));
    }
    void set(std::size_t i) {
        if (i < 64) lo |= (uint64_t{1} << i);
        else hi |= (uint64_t{1} << (i - 64));
    }
    bool test(std::size_t i) const {
        return (i < 64) ? ((lo >> i) & 1ULL) : ((hi >> (i - 64)) & 1ULL);
    }
    bool empty() const { return (lo | hi) == 0; }
    int count() const { return static_cast<int>(std::popcount(lo) + std::popcount(hi)); }
    bool subset_of(const Mask128& o) const { return ((lo & ~o.lo) | (hi & ~o.hi)) == 0; }

    std::vector<int> bits() const {
        std::vector<int> out;
        uint64_t x = lo;
        while (x) {
            unsigned b = std::countr_zero(x);
            out.push_back(static_cast<int>(b));
            x &= x - 1;
        }
        x = hi;
        while (x) {
            unsigned b = std::countr_zero(x);
            out.push_back(static_cast<int>(64 + b));
            x &= x - 1;
        }
        return out;
    }

    bool operator==(const Mask128& o) const { return lo == o.lo && hi == o.hi; }
    bool operator!=(const Mask128& o) const { return !(*this == o); }
    bool operator<(const Mask128& o) const {
        if (hi != o.hi) return hi < o.hi;
        return lo < o.lo;
    }
};

inline Mask128 operator&(Mask128 a, Mask128 b) { return Mask128(a.lo & b.lo, a.hi & b.hi); }
inline Mask128 operator|(Mask128 a, Mask128 b) { return Mask128(a.lo | b.lo, a.hi | b.hi); }
inline Mask128 operator^(Mask128 a, Mask128 b) { return Mask128(a.lo ^ b.lo, a.hi ^ b.hi); }

struct Mask128Hash {
    std::size_t operator()(const Mask128& m) const noexcept {
        uint64_t x = m.lo ^ (m.hi + 0x9e3779b97f4a7c15ULL + (m.lo << 6) + (m.lo >> 2));
        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return static_cast<std::size_t>(x);
    }
};

inline std::string hex_mask128(Mask128 m) {
    std::ostringstream os;
    os << std::hex << std::setw(16) << std::setfill('0') << m.hi
       << std::setw(16) << std::setfill('0') << m.lo;
    return os.str();
}

inline void append_mask128_binary(std::string& s, Mask128 m) {
    append_u64_le(s, m.lo);
    append_u64_le(s, m.hi);
}

inline uint16_t permute_subset_by_array9(uint16_t subset, const std::array<uint8_t,9>& perm) {
    uint16_t out = 0;
    uint16_t m = subset;
    while (m) {
        unsigned b = std::countr_zero(static_cast<unsigned>(m));
        out |= static_cast<uint16_t>(1u << perm[b]);
        m &= static_cast<uint16_t>(m - 1);
    }
    return out;
}

class HypersimplexR3Wide {
public:
    int n = 0;
    int num_vertices = 0;
    std::vector<uint16_t> vertex_elem_masks;
    std::vector<int> vertex_index;
    Mask128 all_vertices_mask;
    std::vector<Mask128> coord0_mask;
    std::vector<Mask128> coord1_mask;
    std::vector<std::array<Mask128,4>> le_mask;
    std::vector<std::array<Mask128,4>> eq_mask;

    void init(int n_) {
        if (n_ != 9) throw std::runtime_error("The wide-mask path currently supports n=9 only.");
        n = n_;
        int sub_count = 1 << n;
        vertex_index.assign(sub_count, -1);
        vertex_elem_masks.clear();
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                for (int k = j + 1; k < n; ++k) {
                    uint16_t m = static_cast<uint16_t>((1u << i) | (1u << j) | (1u << k));
                    vertex_index[m] = static_cast<int>(vertex_elem_masks.size());
                    vertex_elem_masks.push_back(m);
                }
        num_vertices = static_cast<int>(vertex_elem_masks.size()); // C(9,3)=84
        all_vertices_mask = Mask128{};
        for (int i = 0; i < num_vertices; ++i) all_vertices_mask.set(static_cast<std::size_t>(i));

        coord0_mask.assign(n, Mask128{});
        coord1_mask.assign(n, Mask128{});
        for (int vi = 0; vi < num_vertices; ++vi) {
            uint16_t vm = vertex_elem_masks[vi];
            for (int e = 0; e < n; ++e) {
                if ((vm >> e) & 1u) coord1_mask[e].set(vi);
                else coord0_mask[e].set(vi);
            }
        }

        le_mask.assign(sub_count, {});
        eq_mask.assign(sub_count, {});
        for (int S = 0; S < sub_count; ++S) {
            for (int kk = 0; kk <= 3; ++kk) {
                Mask128 le, eq;
                for (int vi = 0; vi < num_vertices; ++vi) {
                    int c = std::popcount(static_cast<unsigned>(vertex_elem_masks[vi] & S));
                    if (c <= kk) le.set(vi);
                    if (c == kk) eq.set(vi);
                }
                le_mask[S][kk] = le;
                eq_mask[S][kk] = eq;
            }
        }
    }

    int index_of_vertex_mask(uint16_t m) const {
        if (m >= vertex_index.size() || vertex_index[m] < 0) throw std::runtime_error("Not a rank-3 vertex mask.");
        return vertex_index[m];
    }
};

class PermTableWide {
public:
    const HypersimplexR3Wide* hs = nullptr;
    int n = 0;
    std::vector<std::array<uint8_t,9>> perms;
    std::vector<std::array<uint8_t,9>> inv_perms;
    std::vector<uint16_t> vperm_flat;
    std::vector<uint16_t> vinv_flat;

    void init(const HypersimplexR3Wide& h) {
        hs = &h;
        n = h.n;
        perms.clear(); inv_perms.clear(); vperm_flat.clear(); vinv_flat.clear();
        std::vector<int> p(n);
        std::iota(p.begin(), p.end(), 0);
        do {
            std::array<uint8_t,9> a{};
            for (int i = 0; i < n; ++i) a[i] = static_cast<uint8_t>(p[i]);
            perms.push_back(a);
        } while (std::next_permutation(p.begin(), p.end()));
        inv_perms.resize(perms.size());
        vperm_flat.reserve(perms.size() * h.num_vertices);
        vinv_flat.reserve(perms.size() * h.num_vertices);
        for (std::size_t pi = 0; pi < perms.size(); ++pi) {
            auto a = perms[pi];
            std::array<uint8_t,9> inv{};
            for (int i = 0; i < n; ++i) inv[a[i]] = static_cast<uint8_t>(i);
            inv_perms[pi] = inv;
            for (int vi = 0; vi < h.num_vertices; ++vi) {
                uint16_t pe = permute_subset_by_array9(h.vertex_elem_masks[vi], a);
                vperm_flat.push_back(static_cast<uint16_t>(h.index_of_vertex_mask(pe)));
            }
            for (int vi = 0; vi < h.num_vertices; ++vi) {
                uint16_t pe = permute_subset_by_array9(h.vertex_elem_masks[vi], inv);
                vinv_flat.push_back(static_cast<uint16_t>(h.index_of_vertex_mask(pe)));
            }
        }
    }

    int count() const { return static_cast<int>(perms.size()); }

    uint16_t permute_subset(uint16_t subset_mask, int perm_idx) const {
        return permute_subset_by_array9(subset_mask, perms[static_cast<std::size_t>(perm_idx)]);
    }

    Mask128 permute_vmask(Mask128 vmask, int perm_idx) const {
        const std::size_t off = static_cast<std::size_t>(perm_idx) * hs->num_vertices;
        Mask128 out;
        uint64_t x = vmask.lo;
        while (x) {
            unsigned vi = std::countr_zero(x);
            out.set(vperm_flat[off + vi]);
            x &= x - 1;
        }
        x = vmask.hi;
        while (x) {
            unsigned b = std::countr_zero(x);
            unsigned vi = 64 + b;
            out.set(vperm_flat[off + vi]);
            x &= x - 1;
        }
        return out;
    }
};

struct CellOrbitWide {
    int n = 9;
    int orbit_id = 0;
    int volume_sha = 0;
    int volume_lattice = 0;
    int active_volume = 0;
    std::vector<Ineq> ineqs;
    Mask128 rep_vmask;
    std::vector<Mask128> facet_masks;

    void finalize(const HypersimplexR3Wide& hs) {
        rep_vmask = hs.all_vertices_mask;
        for (auto ie : ineqs) {
            if (ie.k > 3) throw std::runtime_error("Inequality k outside rank-3 range.");
            rep_vmask = rep_vmask & hs.le_mask[ie.subset_mask][ie.k];
        }
        compute_facets(hs);
    }

    void compute_facets(const HypersimplexR3Wide& hs) {
        facet_masks.clear();
        auto add = [&](Mask128 f) {
            if (!f.empty() && f != rep_vmask) facet_masks.push_back(f);
        };
        for (int i = 0; i < hs.n; ++i) {
            add(rep_vmask & hs.coord0_mask[i]);
            add(rep_vmask & hs.coord1_mask[i]);
        }
        for (auto ie : ineqs) add(rep_vmask & hs.eq_mask[ie.subset_mask][ie.k]);
        std::sort(facet_masks.begin(), facet_masks.end());
        facet_masks.erase(std::unique(facet_masks.begin(), facet_masks.end()), facet_masks.end());
    }
};

struct ImageRecordWide {
    uint32_t orbit_index = 0;
    uint32_t perm_index = 0;
    Mask128 vmask;
    int active_volume = 0;
};

struct LoadedDataWide {
    HypersimplexR3Wide hs;
    std::vector<CellOrbitWide> orbits;
};

inline LoadedDataWide load_orbits_for_n9(const std::filesystem::path& data_dir, const std::string& volume_mode) {
    LoadedDataWide d;
    d.hs.init(9);
    std::filesystem::path path = find_data_file(9, data_dir);
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open " + path.string());

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        CellOrbitWide o;
        o.n = parse_int_field(line, "n");
        if (o.n != 9) throw std::runtime_error("Input n mismatch in " + path.string());
        o.orbit_id = parse_int_field(line, "orbit_id");
        o.volume_sha = parse_int_field(line, "volume_sha", false, parse_int_field(line, "volume", false, 0));
        o.volume_lattice = parse_int_field(line, "volume_lattice", false, 0);
        if (volume_mode == "sha") o.active_volume = o.volume_sha;
        else if (volume_mode == "lattice") o.active_volume = o.volume_lattice;
        else throw std::runtime_error("Unknown volume mode: " + volume_mode);
        o.ineqs = parse_ineqs(line);
        o.finalize(d.hs);
        d.orbits.push_back(std::move(o));
    }
    std::sort(d.orbits.begin(), d.orbits.end(), [](const CellOrbitWide& a, const CellOrbitWide& b){ return a.orbit_id < b.orbit_id; });
    return d;
}

class ImageSetWide {
public:
    HypersimplexR3Wide hs;
    std::vector<CellOrbitWide> orbits;
    PermTableWide pt;
    std::vector<ImageRecordWide> images;
    std::vector<Mask128> vmask_list;
    std::vector<int> vol_list;
    std::vector<uint8_t> vertex_count_list;
    std::vector<uint32_t> orbit_index_list;
    std::vector<uint32_t> perm_index_list;
    std::vector<DynBitset> by_vertex;
    std::vector<int> root_rep_by_orbit_index;

    void init(LoadedDataWide data, bool verbose=false, std::size_t threads=1) {
        hs = std::move(data.hs);
        orbits = std::move(data.orbits);
        pt.init(hs);
        build_images(verbose, threads);
    }

    void build_images(bool verbose=false, std::size_t threads=1) {
        images.clear();
        threads = normalize_threads(threads);
        if (verbose) {
            std::cerr << "[n=9 wide] Generating labeled images from " << orbits.size()
                      << " orbit representatives and " << pt.count() << " permutations"
                      << " using " << threads << " thread" << (threads == 1 ? "" : "s") << "...\n";
        }
        std::vector<std::vector<ImageRecordWide>> per_orbit(orbits.size());
        parallel_for_indices(0, orbits.size(), threads, [&](std::size_t oi) {
            const CellOrbitWide& orb = orbits[oi];
            std::unordered_map<Mask128, uint32_t, Mask128Hash> seen;
            seen.reserve(static_cast<std::size_t>(std::min<int>(pt.count(), 65536)));
            for (int pi = 0; pi < pt.count(); ++pi) {
                Mask128 vm = pt.permute_vmask(orb.rep_vmask, pi);
                if (seen.find(vm) == seen.end()) seen.emplace(vm, static_cast<uint32_t>(pi));
            }
            auto& local = per_orbit[oi];
            local.reserve(seen.size());
            for (auto const& kv : seen) {
                local.push_back(ImageRecordWide{static_cast<uint32_t>(oi), kv.second, kv.first, orb.active_volume});
            }
        });
        for (auto& local : per_orbit) images.insert(images.end(), local.begin(), local.end());
        std::sort(images.begin(), images.end(), [](const ImageRecordWide& a, const ImageRecordWide& b) {
            if (a.vmask != b.vmask) return a.vmask < b.vmask;
            return std::tie(a.orbit_index, a.perm_index) < std::tie(b.orbit_index, b.perm_index);
        });

        vmask_list.resize(images.size());
        vol_list.resize(images.size());
        vertex_count_list.resize(images.size());
        orbit_index_list.resize(images.size());
        perm_index_list.resize(images.size());
        root_rep_by_orbit_index.assign(orbits.size(), -1);
        for (std::size_t i = 0; i < images.size(); ++i) {
            vmask_list[i] = images[i].vmask;
            vol_list[i] = images[i].active_volume;
            vertex_count_list[i] = static_cast<uint8_t>(images[i].vmask.count());
            orbit_index_list[i] = images[i].orbit_index;
            perm_index_list[i] = images[i].perm_index;
            int& r = root_rep_by_orbit_index[images[i].orbit_index];
            if (r < 0 || images[i].vmask < images[static_cast<std::size_t>(r)].vmask) r = static_cast<int>(i);
        }

        by_vertex.assign(hs.num_vertices, DynBitset(images.size()));
        for (std::size_t i = 0; i < images.size(); ++i) {
            for (int v : images[i].vmask.bits()) by_vertex[static_cast<std::size_t>(v)].set(i);
        }
        if (verbose) std::cerr << "[n=9 wide] Generated " << images.size() << " distinct labeled images.\n";
    }
};

class FaceTesterWide {
public:
    const ImageSetWide* is = nullptr;
    explicit FaceTesterWide(const ImageSetWide& s) : is(&s) {}

    bool is_face(std::size_t image_idx, Mask128 inter) const {
        if (inter.empty()) return true;
        Mask128 cell = is->vmask_list[image_idx];
        if (inter == cell) return true;
        if (inter.count() <= 1) return true;

        Mask128 closure = cell;
        for (int e = 0; e < is->hs.n; ++e) {
            Mask128 E0 = is->hs.coord0_mask[e];
            if (inter.subset_of(E0)) {
                closure = closure & E0;
                if (closure == inter) return true;
            }
            Mask128 E1 = is->hs.coord1_mask[e];
            if (inter.subset_of(E1)) {
                closure = closure & E1;
                if (closure == inter) return true;
            }
        }
        uint32_t oi = is->orbit_index_list[image_idx];
        uint32_t pi = is->perm_index_list[image_idx];
        const auto& orb = is->orbits[oi];
        for (auto ie : orb.ineqs) {
            uint16_t Simg = is->pt.permute_subset(ie.subset_mask, static_cast<int>(pi));
            Mask128 E = is->hs.eq_mask[Simg][ie.k];
            if (inter.subset_of(E)) {
                closure = closure & E;
                if (closure == inter) return true;
            }
        }
        return closure == inter;
    }

    bool compatible(std::size_t i, std::size_t j) const {
        Mask128 inter = is->vmask_list[i] & is->vmask_list[j];
        if (inter.count() <= 1) return true;
        return is_face(i, inter) && is_face(j, inter);
    }
};

class CompatCacheWide {
public:
    const ImageSetWide* is = nullptr;
    FaceTesterWide face;
    std::size_t cap_rows = 4096;
    std::mutex mtx;
    std::unordered_map<std::size_t, std::shared_ptr<DynBitset>> rows;
    std::unordered_map<std::size_t, std::size_t> row_counts;
    std::deque<std::size_t> fifo;
    std::atomic<std::size_t> hits{0}, misses{0};

    CompatCacheWide(const ImageSetWide& s, std::size_t cap) : is(&s), face(s), cap_rows(std::max<std::size_t>(1, cap)) {}

    std::shared_ptr<DynBitset> get(std::size_t idx) {
        {
            std::lock_guard<std::mutex> lk(mtx);
            ++row_counts[idx];
            auto it = rows.find(idx);
            if (it != rows.end()) { hits.fetch_add(1, std::memory_order_relaxed); return it->second; }
        }
        misses.fetch_add(1, std::memory_order_relaxed);
        auto built = std::make_shared<DynBitset>(build_row(idx));
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = rows.find(idx);
            if (it != rows.end()) return it->second;
            rows.emplace(idx, built);
            fifo.push_back(idx);
            while (rows.size() > cap_rows && !fifo.empty()) {
                rows.erase(fifo.front());
                fifo.pop_front();
            }
        }
        return built;
    }

    DynBitset build_row(std::size_t idx) const {
        DynBitset compat(is->images.size(), true);
        DynBitset seen(is->images.size());
        DynBitset two(is->images.size());
        for (int v : is->vmask_list[idx].bits()) {
            const DynBitset& row = is->by_vertex[static_cast<std::size_t>(v)];
            two.or_assign_intersection(seen, row);
            seen.or_assign(row);
        }
        two.for_each_set_bit([&](std::size_t j) {
            if (j == idx) return;
            Mask128 inter = is->vmask_list[idx] & is->vmask_list[j];
            if (inter.count() <= 1) return;
            if (!face.is_face(idx, inter) || !face.is_face(j, inter)) compat.reset(j);
        });
        return compat;
    }

    void prewarm_from_file(const std::filesystem::path& path, std::size_t limit, bool verbose, std::size_t threads) {
        if (path.empty() || limit == 0) return;
        std::ifstream in(path);
        if (!in) return;
        std::vector<std::size_t> ids;
        std::string line;
        while (ids.size() < limit && std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream iss(line);
            std::size_t row = 0, count = 0;
            if (iss >> row) {
                if (row < is->images.size()) ids.push_back(row);
            }
            (void)count;
        }
        if (ids.empty()) return;
        if (verbose) std::cerr << "[n=9 wide] prewarming " << ids.size() << " hot compatibility rows from " << path << "\n";
        parallel_for_indices(0, ids.size(), threads, [&](std::size_t k) { (void)get(ids[k]); });
    }

    void write_hot_rows(const std::filesystem::path& path, std::size_t limit = 10000) {
        if (path.empty()) return;
        std::vector<std::pair<std::size_t,std::size_t>> rows_local;
        {
            std::lock_guard<std::mutex> lk(mtx);
            rows_local.reserve(row_counts.size());
            for (auto const& kv : row_counts) rows_local.push_back(kv);
        }
        std::sort(rows_local.begin(), rows_local.end(), [](auto const& a, auto const& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });
        if (limit && rows_local.size() > limit) rows_local.resize(limit);
        std::error_code ec;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path);
        if (!out) return;
        out << "# row_id count\n";
        for (auto const& kv : rows_local) out << kv.first << " " << kv.second << "\n";
    }

    void intersect_into(std::size_t idx, const DynBitset& src, DynBitset& dst) {
        auto row = get(idx);
        dst.assign_and(src, *row);
    }
};

inline std::string subset_label_digits9(uint16_t subset, int n=9) {
    std::string s;
    for (int i = 0; i < n; ++i) if ((subset >> i) & 1u) s.push_back(static_cast<char>('1' + i));
    return s;
}

inline std::string image_label_wide(const ImageSetWide& is, std::size_t idx) {
    uint32_t oi = is.orbit_index_list[idx];
    uint32_t pi = is.perm_index_list[idx];
    const auto& orb = is.orbits[oi];
    if (orb.ineqs.empty()) return "\xE2\x88\x85"; // ∅
    std::vector<std::string> parts;
    for (auto ie : orb.ineqs) {
        uint16_t Simg = is.pt.permute_subset(ie.subset_mask, static_cast<int>(pi));
        parts.push_back(subset_label_digits9(Simg, is.hs.n) + "<=" + std::to_string(int(ie.k)));
    }
    std::sort(parts.begin(), parts.end());
    std::ostringstream os;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) os << ",";
        os << parts[i];
    }
    return os.str();
}

class SearchEngineWide {
public:
    ImageSetWide* is = nullptr;
    SearchOptions opt;
    CompatCacheWide compat;
    std::size_t total_volume = 0;
    std::vector<DynBitset> volume_le_masks;

    std::atomic<std::size_t> states{0}, solutions{0}, duplicate_states{0}, volume_prunes{0}, coverage_prunes{0}, forced_cells{0};
    std::mutex state_mtx, solution_mtx, output_mtx;
    std::unordered_set<std::string> state_cache;
    std::deque<std::string> state_fifo;
    std::unordered_set<std::string> solution_keys;
    std::ostream* out = &std::cout;

    SearchEngineWide(ImageSetWide& s, SearchOptions o)
        : is(&s), opt(std::move(o)), compat(s, compute_compat_cap(s, opt)) {
        total_volume = compute_total_volume();
        build_volume_masks();
        if (opt.state_cache_mb > 0) {
            // In the wide n=9 path, --state-cache-mb is the authoritative bounded-cache
            // knob.  The previous prototype only honored it when state_cache_max was
            // zero, so auto/gather runs silently used the small 1M-entry default.
            opt.state_cache_max = std::max<std::size_t>(1000, (opt.state_cache_mb * 1024ULL * 1024ULL) / 512);
        }
        if (opt.state_cache_mode == "auto") opt.state_cache_mode = "bounded";
    }

    static std::size_t compute_compat_cap(const ImageSetWide& s, const SearchOptions& opt) {
        if (opt.compat_mem_rows) return opt.compat_mem_rows;
        std::size_t row_b = dense_row_bytes(s.images.size());
        if (row_b == 0) return 1;
        return std::max<std::size_t>(1, (opt.compat_mem_budget_mb * 1024ULL * 1024ULL) / row_b);
    }

    std::size_t compute_total_volume() const {
        for (auto const& o : is->orbits) if (o.ineqs.empty()) return static_cast<std::size_t>(o.active_volume);
        return static_cast<std::size_t>((is->hs.n - 3) * (is->hs.n - 3));
    }

    void build_volume_masks() {
        volume_le_masks.assign(total_volume + 1, DynBitset(is->images.size()));
        for (std::size_t i = 0; i < is->images.size(); ++i) {
            int v = is->vol_list[i];
            for (std::size_t r = 0; r <= total_volume; ++r) if (v >= 0 && static_cast<std::size_t>(v) <= r) volume_le_masks[r].set(i);
        }
    }

    std::string labeled_state_key(const std::vector<uint32_t>& chosen) const {
        std::string s;
        s.reserve(chosen.size() * 4);
        for (uint32_t x : chosen) {
            for (int b = 0; b < 4; ++b) s.push_back(static_cast<char>((x >> (8*b)) & 0xffu));
        }
        return s;
    }

    bool seen_state(const std::vector<uint32_t>& chosen) {
        if (opt.state_cache_mode == "none") return false;
        std::string key = labeled_state_key(chosen);
        std::lock_guard<std::mutex> lk(state_mtx);
        auto [it, ins] = state_cache.insert(key);
        if (!ins) { duplicate_states.fetch_add(1, std::memory_order_relaxed); return true; }
        if (opt.state_cache_mode == "bounded" && opt.state_cache_max > 0) {
            state_fifo.push_back(*it);
            while (state_cache.size() > opt.state_cache_max && !state_fifo.empty()) {
                state_cache.erase(state_fifo.front());
                state_fifo.pop_front();
            }
        }
        return false;
    }

    std::string canonical_solution_key(const std::vector<uint32_t>& chosen) const {
        std::vector<Mask128> cells;
        cells.reserve(chosen.size());
        std::string best;
        bool have = false;
        for (int pi = 0; pi < is->pt.count(); ++pi) {
            cells.clear();
            for (uint32_t idx : chosen) cells.push_back(is->pt.permute_vmask(is->vmask_list[idx], pi));
            std::sort(cells.begin(), cells.end());
            std::string key;
            key.reserve(cells.size() * 16);
            for (Mask128 m : cells) append_mask128_binary(key, m);
            if (!have || key < best) { best = std::move(key); have = true; }
        }
        return best;
    }

    void emit_solution(const std::vector<uint32_t>& chosen) {
        solutions.fetch_add(1, std::memory_order_relaxed);
        if (opt.count_only) return;
        std::ostringstream os;
        os << "Delta(3,9) | " << solutions.load() << " | ";
        for (std::size_t i = 0; i < chosen.size(); ++i) {
            if (i) os << " + ";
            os << "[" << image_label_wide(*is, chosen[i]) << "]";
        }
        os << "\n";
        std::lock_guard<std::mutex> lk(output_mtx);
        (*out) << os.str();
        out->flush();
    }

    bool accept_solution(const std::vector<uint32_t>& chosen, Mask128 covered, int vol) {
        if (static_cast<std::size_t>(vol) != total_volume) return false;
        if (covered != is->hs.all_vertices_mask) return false;
        std::string key = opt.quotient ? canonical_solution_key(chosen) : labeled_state_key(chosen);
        std::lock_guard<std::mutex> lk(solution_mtx);
        if (!solution_keys.insert(std::move(key)).second) return false;
        return true;
    }

    int choose_uncovered_vertex(Mask128 covered, const DynBitset& allowed, std::size_t remaining) {
        Mask128 unc = is->hs.all_vertices_mask ^ (covered & is->hs.all_vertices_mask);
        int best_v = -1;
        std::size_t best_count = std::numeric_limits<std::size_t>::max();
        DynBitset tmp;
        tmp.assign_and(allowed, volume_le_masks[remaining]);
        for (int v : unc.bits()) {
            std::size_t c = tmp.count_intersection(is->by_vertex[static_cast<std::size_t>(v)]);
            if (c < best_count) { best_count = c; best_v = v; if (c == 0) break; }
        }
        return best_v;
    }

    std::vector<uint32_t> ordered_candidates(const DynBitset& cand, Mask128 covered) {
        std::vector<uint32_t> out;
        cand.for_each_set_bit([&](std::size_t i){ out.push_back(static_cast<uint32_t>(i)); });
        std::sort(out.begin(), out.end(), [&](uint32_t a, uint32_t b) {
            int na = (is->vmask_list[a] & Mask128(~covered.lo, ~covered.hi)).count();
            int nb = (is->vmask_list[b] & Mask128(~covered.lo, ~covered.hi)).count();
            if (na != nb) return na > nb;
            if (is->vol_list[a] != is->vol_list[b]) return is->vol_list[a] > is->vol_list[b];
            return a < b;
        });
        return out;
    }

    bool force_propagate(std::vector<uint32_t>& chosen, Mask128& covered, int& vol, DynBitset& allowed) {
        if (!opt.force_propagation) return true;
        bool changed = true;
        while (changed) {
            changed = false;
            if (static_cast<std::size_t>(vol) > total_volume) return false;
            std::size_t remaining = total_volume - static_cast<std::size_t>(vol);
            DynBitset allowedR;
            allowedR.assign_and(allowed, volume_le_masks[remaining]);
            Mask128 unc = is->hs.all_vertices_mask ^ (covered & is->hs.all_vertices_mask);
            for (int v : unc.bits()) {
                DynBitset cv;
                cv.assign_and(allowedR, is->by_vertex[static_cast<std::size_t>(v)]);
                std::size_t c = cv.count();
                if (c == 0) { coverage_prunes.fetch_add(1, std::memory_order_relaxed); return false; }
                if (c == 1) {
                    auto one = cv.first_set();
                    if (!one) return false;
                    uint32_t idx = static_cast<uint32_t>(*one);
                    chosen.push_back(idx);
                    std::sort(chosen.begin(), chosen.end());
                    vol += is->vol_list[idx];
                    covered = covered | is->vmask_list[idx];
                    DynBitset next;
                    compat.intersect_into(idx, allowed, next);
                    next.reset(idx);
                    allowed = std::move(next);
                    forced_cells.fetch_add(1, std::memory_order_relaxed);
                    changed = true;
                    break;
                }
            }
        }
        return true;
    }

    void dfs(std::vector<uint32_t> chosen, Mask128 covered, int vol, DynBitset allowed) {
        if (opt.max_solutions && solutions.load(std::memory_order_relaxed) >= opt.max_solutions) return;
        if (opt.max_states && states.load(std::memory_order_relaxed) >= opt.max_states) return;
        if (!force_propagate(chosen, covered, vol, allowed)) return;
        if (static_cast<std::size_t>(vol) > total_volume) { volume_prunes.fetch_add(1, std::memory_order_relaxed); return; }
        if (seen_state(chosen)) return;
        std::size_t st = states.fetch_add(1, std::memory_order_relaxed) + 1;
        if (opt.progress_interval && st % opt.progress_interval == 0 && !opt.quiet) {
            std::cerr << "[n=9 wide] progress states=" << st
                      << " solutions=" << solutions.load()
                      << " duplicate_states=" << duplicate_states.load()
                      << " compat_hits=" << compat.hits.load()
                      << " compat_misses=" << compat.misses.load()
                      << " state_cache_size=" << state_cache.size()
                      << "\n";
        }
        if (static_cast<std::size_t>(vol) == total_volume) {
            if (accept_solution(chosen, covered, vol)) emit_solution(chosen);
            return;
        }
        std::size_t remaining = total_volume - static_cast<std::size_t>(vol);
        int v = choose_uncovered_vertex(covered, allowed, remaining);
        if (v < 0) return;
        DynBitset allowedR;
        allowedR.assign_and(allowed, volume_le_masks[remaining]);
        DynBitset cand;
        cand.assign_and(allowedR, is->by_vertex[static_cast<std::size_t>(v)]);
        if (cand.empty()) { coverage_prunes.fetch_add(1, std::memory_order_relaxed); return; }

        auto ord = ordered_candidates(cand, covered);
        for (uint32_t idx : ord) {
            if (opt.max_solutions && solutions.load(std::memory_order_relaxed) >= opt.max_solutions) break;
            if (opt.max_states && states.load(std::memory_order_relaxed) >= opt.max_states) break;
            std::vector<uint32_t> ch = chosen;
            ch.push_back(idx);
            std::sort(ch.begin(), ch.end());
            Mask128 cov2 = covered | is->vmask_list[idx];
            int vol2 = vol + is->vol_list[idx];
            DynBitset next;
            compat.intersect_into(idx, allowed, next);
            next.reset(idx);
            dfs(std::move(ch), cov2, vol2, std::move(next));
        }
    }

    void run(std::ostream& os) {
        out = &os;
        if (!opt.prewarm_hot_compat_rows.empty() && opt.prewarm_hot_compat_limit > 0) {
            compat.prewarm_from_file(opt.prewarm_hot_compat_rows, opt.prewarm_hot_compat_limit,
                                     !opt.quiet, opt.threads);
        }
        DynBitset all(is->images.size(), true);

        // Quotient root symmetry breaking remains exact for final quotient output:
        // every tiling orbit contains a relabeling in which one of its cells is the
        // chosen minimal image of that cell orbit.  Partial-state cache is labeled
        // only, and final canonical deduplication is exact under S_9.
        std::vector<uint32_t> roots;
        if (opt.quotient) {
            for (int r : is->root_rep_by_orbit_index) if (r >= 0) roots.push_back(static_cast<uint32_t>(r));
        }

        if (opt.quotient && !roots.empty() && opt.threads > 1) {
            std::atomic<std::size_t> next_root{0};
            std::size_t T = normalize_threads(opt.threads);
            std::vector<std::thread> workers;
            workers.reserve(T);
            if (!opt.quiet) std::cerr << "[n=9 wide] parallel root search: " << roots.size() << " root-orbit tasks using " << T << " threads\n";
            for (std::size_t t = 0; t < T; ++t) {
                workers.emplace_back([&, t]() {
                    maybe_pin_worker(t, T, opt.affinity);
                    for (;;) {
                        if (opt.max_solutions && solutions.load(std::memory_order_relaxed) >= opt.max_solutions) break;
                        if (opt.max_states && states.load(std::memory_order_relaxed) >= opt.max_states) break;
                        std::size_t k = next_root.fetch_add(1, std::memory_order_relaxed);
                        if (k >= roots.size()) break;
                        uint32_t idx = roots[k];
                        std::vector<uint32_t> chosen{idx};
                        Mask128 covered = is->vmask_list[idx];
                        int vol = is->vol_list[idx];
                        DynBitset allowed;
                        compat.intersect_into(idx, all, allowed);
                        allowed.reset(idx);
                        dfs(std::move(chosen), covered, vol, std::move(allowed));
                    }
                });
            }
            for (auto& th : workers) th.join();
        } else if (opt.quotient && !roots.empty()) {
            for (uint32_t idx : roots) {
                std::vector<uint32_t> chosen{idx};
                Mask128 covered = is->vmask_list[idx];
                int vol = is->vol_list[idx];
                DynBitset allowed;
                compat.intersect_into(idx, all, allowed);
                allowed.reset(idx);
                dfs(std::move(chosen), covered, vol, std::move(allowed));
            }
        } else {
            std::vector<uint32_t> empty;
            dfs(empty, Mask128{}, 0, all);
        }
        if (!opt.record_hot_compat_rows.empty()) {
            compat.write_hot_rows(opt.record_hot_compat_rows);
        }
    }
};

inline bool validate_n9(const ImageSetWide& is, const SearchOptions& opt, bool verbose=true) {
    bool ok = true;
    if (verbose) {
        std::cerr << "[n=9 wide] hypersimplex vertices: " << is.hs.num_vertices << " (expected 84)\n";
        std::cerr << "[n=9 wide] orbit representatives: " << is.orbits.size() << " (no built-in expected count)\n";
        std::cerr << "[n=9 wide] labeled images: " << is.images.size() << " (no built-in expected count)\n";
    }
    ok &= (is.hs.num_vertices == 84);
    bool has_uniform = false;
    for (auto const& o : is.orbits) {
        if (o.ineqs.empty()) {
            has_uniform = true;
            bool vm_ok = (o.rep_vmask == is.hs.all_vertices_mask);
            bool vol_ok = opt.volume_mode == "sha" ? (o.volume_sha == 36) : (o.volume_lattice > 0);
            if (verbose) std::cerr << "[n=9 wide] uniform orbit: " << (vm_ok && vol_ok ? "OK" : "CHECK") << "\n";
            ok &= vm_ok;
        }
    }
    if (!has_uniform) {
        if (verbose) std::cerr << "[n=9 wide] warning: no uniform orbit with empty inequality list found.\n";
    }
    return ok;
}

inline void print_memory_plan_n9(const ImageSetWide& is, const SearchOptions& opt, std::size_t compat_rows) {
    if (opt.quiet) return;
    std::size_t row_b = dense_row_bytes(is.images.size());
    std::cerr << "\n[n=9 wide] Memory/resource plan before DFS\n";
    std::cerr << "----------------------------------------\n";
    std::cerr << "n                                  9\n";
    std::cerr << "hypersimplex vertices              " << is.hs.num_vertices << "\n";
    std::cerr << "orbit representatives              " << is.orbits.size() << "\n";
    std::cerr << "labeled images                     " << is.images.size() << "\n";
    std::cerr << "cell vertex mask type              Mask128 / __uint128-compatible two-limb storage\n";
    std::cerr << "dense image-index row              " << row_b << " bytes = "
              << std::fixed << std::setprecision(2) << (double(row_b)/(1024.0*1024.0)) << " MiB\n";
    std::cerr << "threads                            " << opt.threads << "\n";
    std::cerr << "state-cache mode                   " << opt.state_cache_mode << " (labeled partial states; final quotient dedup under S_9)\n";
    std::cerr << "compat row cap                     " << compat_rows << " rows\n";
    std::cerr << "volume mode                        " << opt.volume_mode << "\n";
    std::cerr << "Note: n=8 still uses the original 64-bit production path; this wide path is isolated for n=9.\n\n";
}

inline int run_n9_wide_main(const std::filesystem::path& data_dir,
                            const std::filesystem::path& out_path,
                            SearchOptions opt,
                            bool validate,
                            bool build_cache_only) {
    if (opt.volume_mode.empty()) opt.volume_mode = "sha";
    if (opt.threads == 0) opt.threads = hardware_threads();
    if (opt.threads == 1 && opt.auto_tune) opt.threads = std::max<std::size_t>(1, hardware_threads() / 2);
    if (opt.state_cache_mode == "auto") opt.state_cache_mode = "bounded";
    if (opt.compat_mem_budget_mb == 0) opt.compat_mem_budget_mb = 32768;
    if (opt.state_cache_mb == 0 && opt.state_cache_mode == "bounded") opt.state_cache_mb = 65536;
    if (opt.progress_interval == 0) opt.progress_interval = 1000000;

    LoadedDataWide data = load_orbits_for_n9(data_dir, opt.volume_mode);
    if (!opt.quiet) std::cerr << "[n=9 wide] Loaded " << data.orbits.size() << " orbit representatives for Delta(3,9).\n";
    ImageSetWide is;
    is.init(std::move(data), !opt.quiet, opt.threads);

    std::size_t row_b = dense_row_bytes(is.images.size());
    std::size_t compat_rows = opt.compat_mem_rows ? opt.compat_mem_rows :
        std::max<std::size_t>(1, (opt.compat_mem_budget_mb * 1024ULL * 1024ULL) / std::max<std::size_t>(row_b, 1));
    print_memory_plan_n9(is, opt, compat_rows);

    if (validate) {
        bool ok = validate_n9(is, opt, !opt.quiet);
        if (!ok) return 2;
    }
    if (build_cache_only) return 0;

    opt.compat_mem_rows = compat_rows;
    if (opt.quotient && opt.search_mode != "labeled" && !opt.quiet) {
        std::cerr << "[n=9 wide] quotient output uses exact final S_9 canonical deduplication.\n"
                  << "[n=9 wide] partial-state cache is labeled to avoid S_9 canonicalization at every node.\n";
    }

    std::ofstream fout;
    std::ostream* os = &std::cout;
    if (!out_path.empty()) {
        fout.open(out_path);
        if (!fout) throw std::runtime_error("Cannot open output file: " + out_path.string());
        os = &fout;
    }
    SearchEngineWide engine(is, opt);
    engine.run(*os);
    if (opt.count_only) std::cout << engine.solutions.load() << "\n";
    if (!opt.quiet) {
        std::cerr << "[n=9 wide] final states=" << engine.states.load()
                  << " solutions=" << engine.solutions.load()
                  << " duplicate_states=" << engine.duplicate_states.load()
                  << " compat_hits=" << engine.compat.hits.load()
                  << " compat_misses=" << engine.compat.misses.load()
                  << "\n";
    }
    return 0;
}

} // namespace alexeev


#include "alexeev.hpp"
#include <random>

using namespace alexeev;

struct Cli {
    int n = -1;
    std::filesystem::path data_dir = "data";
    std::filesystem::path cache_dir;
    std::filesystem::path out_path;
    SearchOptions opt;
    bool validate = false;
    bool self_test = false;
    bool build_cache_only = false;
    bool allow_mismatch = false;
    bool print_memory_plan = true;
    bool threads_explicit = false;
    bool compat_explicit = false;
    bool state_cache_explicit = false;
    bool memory_limit_explicit = false;
    bool prefix_task_target_explicit = false;
    bool async_output_explicit = false;
    bool task_target_explicit = false;
    bool split_max_explicit = false;
    bool split_min_explicit = false;
    bool scheduler_explicit = false;
    bool compat_policy_explicit = false;
    bool flush_every_explicit = false;
    bool auto_pilot_explicit = false;
    bool duplicate_local_explicit = false;
    bool labeled_state_explicit = false;
    bool auto_learn_explicit = false;
    bool affinity_explicit = false;
    bool bitset_kernel_explicit = false;
    bool state_key_format_explicit = false;
    bool worker_compat_cache_explicit = false;
    bool gather_data = false;
    std::size_t gather_solutions = 10000;
    std::size_t gather_states = 0;
    std::string gather_cache_mode = "shared"; // shared|reset|warm|warm-profiled
    std::string gather_matrix = "full";       // quick|core|full
    std::size_t gather_warm_solutions = 1000; // used by --gather-cache-mode warm
    bool gather_randomize_order = false;      // intentionally off by default for reproducibility
    std::filesystem::path auto_learn_path;    // optional perf_data.txt to learn machine-specific profile
    std::string auto_profile = "balanced"; // balanced|throughput|interactive|memory-saver|learned
    std::filesystem::path write_prefix_tasks;
    std::filesystem::path read_prefix_tasks;
    std::size_t prefix_shard_index = 0;
    std::size_t prefix_shard_count = 1;
};

static void usage() {
    std::cerr <<
R"(alexeev_r3_cpp: enumerate Alexeev-style rank-3 matroid tilings of Delta(3,n)

Usage:
  alexeev_r3_cpp <n> [options]

Supported n:
  n=3..8 use the measured-fast uint64_t production engine.
  n=9 uses an isolated experimental Mask128 wide engine and requires allr3n9.txt.

Production defaults:
  The default generation mode is now the fast state-cache quotient DFS.
  Canonical augmentation is still available but is not the production default.

Resource/auto options:
  --auto                         detect CPUs, memory, LLC cache and choose production parameters
  --auto-profile balanced|throughput|interactive|memory-saver|learned
                                 auto tuning profile; default balanced. throughput uses more cores/RAM;
                                 interactive leaves more headroom; memory-saver is conservative.
                                 learned reads a perf_data file using --auto-learn.
  --auto-learn <perf_data.txt>    choose the best safe production profile from a previous gather-data file.
                                 This is exact: it only selects parameters; it does not change the search.
  --auto-pilot-states <N>        optional feedback pilot: run N accepted states, measure duplicate ratio,
                                 compatibility miss rate, worker utilization, then retune cache/split knobs
  --no-auto-pilot                disable feedback pilot even if a future profile default enables it
  --gather-data                  run a benchmark matrix and write perf_data.txt in the current directory
                                 For n=9 this uses the isolated Mask128 wide engine.  The n=9
                                 matrix is focused around the first measured optimum near 128
                                 threads on a 96c/192t dual Threadripper system.
  --gather-solutions <K>         stop each gather-data run after K quotient tilings (default: 10000)
  --gather-states <K>            alternatively stop each gather-data run after K accepted DFS states
  --gather-cache-mode shared|reset|warm|warm-profiled
                                 shared: sequential experiments share disk compatibility rows;
                                 reset: each experiment runs cold with disk row cache disabled;
                                 warm: first run a fixed warmup, then run the matrix against the warmed cache;
                                 warm-profiled: warmup records hot compatibility rows and each experiment prewarms them
  --gather-matrix quick|core|full
                                 quick: baseline + a few key profiles; core: scheduler/cache/thread essentials;
                                 full: extended matrix including repeats and donation variants (default)
  --gather-warm-solutions <K>    warmup tiling target used by --gather-cache-mode warm (default: 1000)
  --gather-randomize-order        randomize experiment order with a fixed seed to expose cache/order bias
  --memory-limit-mb <MB>         RSS guard limit; 0 disables
  --memory-policy warn|shrink|abort
                                 action if RSS reaches --memory-limit-mb (default: warn)
  --print-memory-plan            print pre-search memory plan (default unless --quiet)
  --no-memory-plan               suppress pre-search memory plan
  --threads <T>                  worker threads; 1 serial, 0 all logical CPUs
  --parallel-depth <D>           minimum DFS depth used by static task splitting (default: 1)
  --scheduler auto|static|static-hybrid|work-steal
                                 scheduler; auto resolves by profile from perf_data feedback.
                                 balanced uses static-hybrid; throughput/interactive use static;
                                 full work-steal is experimental.
  --task-target-per-thread <K>   dynamic ready-task target per worker (default: 64)
  --split-max-depth <D>          max depth where work-steal may donate subtrees (default: 4)
  --split-min-candidates <K>     minimum children before donating subtrees (default: 16)
  --donate-when-active-below <F> static-hybrid late-donation threshold, e.g. 0.75
  --labeled-state-cache-max <N>  optional exact labeled-state duplicate prefilter before canonicalization
  --prefix-task-target <N>       adaptive prefix tasks target; 0 disables serial adaptive refinement
  --prefix-max-depth <D>         maximum adaptive prefix depth (default: 2)

Core options:
  --data-dir <path>              directory containing allr3n{n}.txt (default: data)
  --data <path>                  alias for --data-dir
  --volume-mode sha|lattice      volume convention (default: sha)
  --search-mode quotient|labeled search traversal (default: quotient)
  --canon-mode graph|refine|brute canonicalization when quotienting (default: graph)
  --no-quotient                  disable S_n quotienting
  --max <K>                      stop after K emitted/candidate solutions
  --count-only                   count but do not print tilings
  --out <file>                   output file
  --jsonl                        emit JSONL records instead of text labels
  --cache-dir <path>             cache directory (default: .alexeev_cache)
  --build-cache-only             build image/by-vertex cache and exit

Cache and memory options:
  --compat-mem-budget-mb <MB>    compatibility cache memory budget; converted to rows
  --compat-mem-max-rows <N>      explicit compatibility row capacity
  --compat-cache-policy auto|lru|lru-notouch|direct
                                 lru-notouch avoids LRU recency mutation on hits; direct avoids global LRU hit locks
  --compat-prewarm <K>           precompute K expensive compatibility rows
  --state-cache auto|full|bounded|none
                                 partial-state cache for state-cache search
  --state-cache-max <N>          max retained states in bounded mode
  --state-cache-mb <MB>          preferred bounded state-cache budget
  --duplicate-local-cache-max <N>
                                 per-thread exact confirmed-duplicate cache size
  --canon-cache-mb <MB>          approximate canonicalization LRU budget
  --solution-dedup memory|disk   memory streaming or disk-spooled final dedup
  --solution-spool-dir <path>    directory for disk solution spool
  --async-output / --no-async-output
                                 writer thread for accepted solution records
  --flush-every <K>              async writer flush cadence in records

Generation modes:
  --generation-mode state-cache|canonical
  --canonical-augmentation       alias for --generation-mode canonical
  --legacy-state-cache-search    alias for --generation-mode state-cache

Safe pruning:
  --force-propagation / --no-force-propagation
                                 exact forced-cell propagation on uniquely coverable uncovered vertices
  --deep-cover-dp / --no-deep-cover-dp
  --deep-cover-dp-max-uncovered <K>
  --deep-cover-dp-max-candidates <K>

Sharding:
  --root-shard <i>/<N>           process sharding over canonical root candidates
  --write-prefix-tasks <file>    generate adaptive prefix tasks and exit
  --read-prefix-tasks <file>     search only tasks listed in a prefix-task JSONL file
  --prefix-shard <i>/<N>         process sharding over prefix-task file lines
  --shard-index <i>              process sharding index, alternative to --root-shard
  --shard-count <N>              process sharding count, alternative to --root-shard

Symmetry options:
  --symbreak-root-orbits / --no-symbreak-root-orbits
  --symbreak-stabilizer / --no-symbreak-stabilizer
  --symbreak-stab-depth <D>
  --symbreak-stab-max-perms <K>
  --symbreak-stab-candidate-max <K>

Validation:
  --validate                     run input/image/face/count validations
  --self-test                    run validations for n=4,5,6
  --allow-mismatch               allow continuing if validation count differs
  --quiet                        suppress progress and memory plan

Examples:
  ./alexeev_r3_cpp --self-test --data-dir data --threads 4
  ./alexeev_r3_cpp 7 --data-dir data --auto --out r3n7_tilings.txt
  ./alexeev_r3_cpp 8 --data-dir data --auto --out r3n8_tilings.txt
  ./alexeev_r3_cpp 8 --data-dir data --auto --auto-profile throughput --out r3n8_tilings.txt
  ./alexeev_r3_cpp 8 --data-dir data --auto --auto-profile learned --auto-learn perf_data.txt --out r3n8_tilings.txt
  ./alexeev_r3_cpp 8 --data-dir data --auto --gather-data --gather-solutions 10000
  ./alexeev_r3_cpp 9 --data-dir data --auto --gather-data --gather-matrix core --gather-states 100000
  ./alexeev_r3_cpp 8 --data-dir data --threads 88 --scheduler work-steal \\
      --task-target-per-thread 128 --split-max-depth 5 --split-min-candidates 8 \\
      --state-cache bounded --state-cache-mb 160000 --compat-mem-budget-mb 32768 \\
      --compat-cache-policy direct --memory-limit-mb 430000 --memory-policy shrink \\
      --async-output --flush-every 1024 --solution-dedup memory --out r3n8_tilings.txt
)";
}

static Cli parse_cli(int argc, char** argv) {
    Cli c;
    c.opt.volume_mode = "sha";
    c.opt.search_mode = "quotient";
    c.opt.canon_mode = "graph";
    c.opt.quotient = true;
    c.opt.cache_dir = ".alexeev_cache";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const std::string& name)->std::string {
            if (i+1 >= argc) throw std::runtime_error("Missing value for " + name);
            return argv[++i];
        };

        if (a == "-h" || a == "--help") { usage(); std::exit(0); }
        else if (a == "--data-dir" || a == "--data") c.data_dir = need(a);
        else if (a == "--volume-mode") c.opt.volume_mode = need(a);
        else if (a == "--search-mode") c.opt.search_mode = need(a);
        else if (a == "--canon-mode") c.opt.canon_mode = need(a);
        else if (a == "--no-quotient") c.opt.quotient = false;
        else if (a == "--max") c.opt.max_solutions = static_cast<std::size_t>(std::stoull(need(a)));
        else if (a == "--count-only") c.opt.count_only = true;
        else if (a == "--out") c.out_path = need(a);
        else if (a == "--jsonl") c.opt.jsonl = true;
        else if (a == "--cache-dir") { c.opt.cache_dir = need(a); c.cache_dir = c.opt.cache_dir; }
        else if (a == "--build-cache-only") c.build_cache_only = true;
        else if (a == "--no-cache-build") { /* accepted; this implementation builds image cache in memory */ }
        else if (a == "--compat-mem-budget-mb") { c.opt.compat_mem_budget_mb = static_cast<std::size_t>(std::stoull(need(a))); c.compat_explicit = true; }
        else if (a == "--compat-mem-max-rows") { c.opt.compat_mem_rows = static_cast<std::size_t>(std::stoull(need(a))); c.compat_explicit = true; }
        else if (a == "--compat-cache-policy") { c.opt.compat_cache_policy = need(a); c.compat_policy_explicit = true; }
        else if (a == "--compat-disk-commit-every") { (void)need(a); /* accepted for Python CLI compatibility */ }
        else if (a == "--compat-disk-compress-level") { (void)need(a); /* accepted */ }
        else if (a == "--compat-prewarm") c.opt.compat_prewarm = static_cast<std::size_t>(std::stoull(need(a)));
        else if (a == "--auto" || a == "--auto-tune") c.opt.auto_tune = true;
        else if (a == "--auto-profile") { c.auto_profile = need(a); c.opt.auto_tune = true; }
        else if (a == "--auto-learn") { c.auto_learn_path = need(a); c.auto_learn_explicit = true; c.opt.auto_tune = true; if (c.auto_profile == "balanced") c.auto_profile = "learned"; }
        else if (a == "--auto-pilot-states") { c.opt.auto_pilot_states = static_cast<std::size_t>(std::stoull(need(a))); c.opt.auto_tune = true; c.auto_pilot_explicit = true; }
        else if (a == "--no-auto-pilot") { c.opt.auto_pilot_states = 0; c.auto_pilot_explicit = true; }
        else if (a == "--gather-data") { c.gather_data = true; c.opt.auto_tune = true; }
        else if (a == "--gather-solutions") { c.gather_solutions = static_cast<std::size_t>(std::stoull(need(a))); c.gather_data = true; }
        else if (a == "--gather-states") { c.gather_states = static_cast<std::size_t>(std::stoull(need(a))); c.gather_data = true; }
        else if (a == "--gather-cache-mode") { c.gather_cache_mode = need(a); c.gather_data = true; c.opt.auto_tune = true; }
        else if (a == "--gather-matrix") { c.gather_matrix = need(a); c.gather_data = true; c.opt.auto_tune = true; }
        else if (a == "--gather-warm-solutions") { c.gather_warm_solutions = static_cast<std::size_t>(std::stoull(need(a))); c.gather_data = true; c.opt.auto_tune = true; }
        else if (a == "--gather-randomize-order") { c.gather_randomize_order = true; c.gather_data = true; c.opt.auto_tune = true; }
        else if (a == "--affinity") { c.opt.affinity = need(a); c.affinity_explicit = true; }
        else if (a == "--numa-policy") c.opt.numa_policy = need(a);
        else if (a == "--state-cache-scope") c.opt.state_cache_scope = need(a);
        else if (a == "--bitset-kernel") { c.opt.bitset_kernel = need(a); c.bitset_kernel_explicit = true; }
        else if (a == "--state-key-format") { c.opt.state_key_format = need(a); c.state_key_format_explicit = true; }
        else if (a == "--worker-compat-cache-size") { c.opt.worker_compat_cache_size = static_cast<std::size_t>(std::stoull(need(a))); c.worker_compat_cache_explicit = true; }
        else if (a == "--record-hot-compat-rows") c.opt.record_hot_compat_rows = need(a);
        else if (a == "--prewarm-hot-compat-rows") c.opt.prewarm_hot_compat_rows = need(a);
        else if (a == "--prewarm-hot-compat-limit") c.opt.prewarm_hot_compat_limit = static_cast<std::size_t>(std::stoull(need(a)));
        else if (a == "--record-root-performance") c.opt.record_root_performance = need(a);
        else if (a == "--use-root-performance") c.opt.use_root_performance = need(a);
        else if (a == "--memory-limit-mb") { c.opt.memory_limit_mb = static_cast<std::size_t>(std::stoull(need(a))); c.memory_limit_explicit = true; }
        else if (a == "--memory-policy") c.opt.memory_policy = need(a);
        else if (a == "--print-memory-plan") c.print_memory_plan = true;
        else if (a == "--no-memory-plan") c.print_memory_plan = false;
        else if (a == "--state-cache-mb") { c.opt.state_cache_mb = static_cast<std::size_t>(std::stoull(need(a))); c.state_cache_explicit = true; }
        else if (a == "--canon-cache-mb") c.opt.canon_cache_mb = static_cast<std::size_t>(std::stoull(need(a)));
        else if (a == "--solution-key-cache-mb") c.opt.solution_key_cache_mb = static_cast<std::size_t>(std::stoull(need(a)));
        else if (a == "--solution-dedup") c.opt.solution_dedup = need(a);
        else if (a == "--solution-spool-dir") c.opt.solution_spool_dir = need(a);
        else if (a == "--deep-cover-dp") c.opt.deep_cover_dp = true;
        else if (a == "--no-deep-cover-dp") c.opt.deep_cover_dp = false;
        else if (a == "--deep-cover-dp-max-uncovered") c.opt.deep_cover_dp_max_uncovered = std::stoi(need(a));
        else if (a == "--deep-cover-dp-max-candidates") c.opt.deep_cover_dp_max_candidates = static_cast<std::size_t>(std::stoull(need(a)));
        else if (a == "--generation-mode") c.opt.generation_mode = need(a);
        else if (a == "--canonical-augmentation") c.opt.generation_mode = "canonical";
        else if (a == "--legacy-state-cache-search") c.opt.generation_mode = "state-cache";
        else if (a == "--state-cache") { c.opt.state_cache_mode = need(a); c.state_cache_explicit = true; }
        else if (a == "--state-cache-max") { c.opt.state_cache_max = static_cast<std::size_t>(std::stoull(need(a))); c.state_cache_explicit = true; }
        else if (a == "--duplicate-local-cache-max") { c.opt.duplicate_local_cache_max = static_cast<std::size_t>(std::stoull(need(a))); c.duplicate_local_explicit = true; }
        else if (a == "--progress-interval") c.opt.progress_interval = static_cast<std::size_t>(std::stoull(need(a)));
        else if (a == "--threads") { c.opt.threads = static_cast<std::size_t>(std::stoull(need(a))); c.threads_explicit = true; }
        else if (a == "--parallel-depth") c.opt.parallel_depth = std::stoi(need(a));
        else if (a == "--scheduler") { c.opt.scheduler = need(a); c.scheduler_explicit = true; }
        else if (a == "--task-target-per-thread") { c.opt.task_target_per_thread = static_cast<std::size_t>(std::stoull(need(a))); c.task_target_explicit = true; }
        else if (a == "--split-max-depth") { c.opt.split_max_depth = std::stoi(need(a)); c.split_max_explicit = true; }
        else if (a == "--split-min-candidates") { c.opt.split_min_candidates = static_cast<std::size_t>(std::stoull(need(a))); c.split_min_explicit = true; }
        else if (a == "--donate-when-active-below") c.opt.donate_when_active_below = std::stod(need(a));
        else if (a == "--labeled-state-cache-max") { c.opt.labeled_state_cache_max = static_cast<std::size_t>(std::stoull(need(a))); c.labeled_state_explicit = true; }
        else if (a == "--prefix-task-target") { c.opt.prefix_task_target = static_cast<std::size_t>(std::stoull(need(a))); c.prefix_task_target_explicit = true; }
        else if (a == "--prefix-max-depth") c.opt.prefix_max_depth = std::stoi(need(a));
        else if (a == "--write-prefix-tasks") c.write_prefix_tasks = need(a);
        else if (a == "--read-prefix-tasks") c.read_prefix_tasks = need(a);
        else if (a == "--prefix-shard") {
            std::string s = need(a);
            std::size_t slash = s.find('/');
            if (slash == std::string::npos) throw std::runtime_error("--prefix-shard must have form i/N");
            c.prefix_shard_index = static_cast<std::size_t>(std::stoull(s.substr(0, slash)));
            c.prefix_shard_count = static_cast<std::size_t>(std::stoull(s.substr(slash + 1)));
        }
        else if (a == "--async-output") { c.opt.async_output = true; c.async_output_explicit = true; }
        else if (a == "--no-async-output") { c.opt.async_output = false; c.async_output_explicit = true; }
        else if (a == "--flush-every") { c.opt.flush_every = static_cast<std::size_t>(std::stoull(need(a))); c.flush_every_explicit = true; }
        else if (a == "--force-propagation") c.opt.force_propagation = true;
        else if (a == "--no-force-propagation") c.opt.force_propagation = false;
        else if (a == "--root-shard") {
            std::string s = need(a);
            std::size_t slash = s.find('/');
            if (slash == std::string::npos) throw std::runtime_error("--root-shard must have form i/N");
            c.opt.root_shard_index = static_cast<std::size_t>(std::stoull(s.substr(0, slash)));
            c.opt.root_shard_count = static_cast<std::size_t>(std::stoull(s.substr(slash + 1)));
        }
        else if (a == "--shard-index") c.opt.root_shard_index = static_cast<std::size_t>(std::stoull(need(a)));
        else if (a == "--shard-count") c.opt.root_shard_count = static_cast<std::size_t>(std::stoull(need(a)));
        else if (a == "--parent-canon") {
            std::string pc = need(a);
            if (pc == "graph") c.opt.use_graph_canonical_parent = true;
            else if (pc == "brute") c.opt.use_graph_canonical_parent = false;
            else throw std::runtime_error("--parent-canon must be graph or brute");
        }
        else if (a == "--quiet") c.opt.quiet = true;
        else if (a == "--validate") c.validate = true;
        else if (a == "--self-test") c.self_test = true;
        else if (a == "--allow-mismatch") c.allow_mismatch = true;
        else if (a == "--symbreak-root-orbits") c.opt.symbreak_root_orbits = true;
        else if (a == "--no-symbreak-root-orbits") c.opt.symbreak_root_orbits = false;
        else if (a == "--symbreak-stabilizer") c.opt.symbreak_stabilizer = true;
        else if (a == "--no-symbreak-stabilizer") c.opt.symbreak_stabilizer = false;
        else if (a == "--symbreak-stab-depth") c.opt.symbreak_stab_depth = std::stoi(need(a));
        else if (a == "--symbreak-stab-max-perms") c.opt.symbreak_stab_max_perms = static_cast<std::size_t>(std::stoull(need(a)));
        else if (a == "--symbreak-stab-candidate-max") c.opt.symbreak_stab_candidate_max = static_cast<std::size_t>(std::stoull(need(a)));
        else if (a == "--symbreak-stab-cache-maxsize") { (void)need(a); /* accepted */ }
        else if (a == "--canon-memo-maxsize") c.opt.canon_memo_max = static_cast<std::size_t>(std::stoull(need(a)));
        else if (a == "--aut-orders" || a == "--aut-info" || a == "--aut-coset-max-group" || a == "--no-aut-refine") {
            if (a != "--no-aut-refine") (void)need(a);
            // accepted for CLI compatibility; correctness does not depend on automorphism sidecars
        }
        else if (!a.empty() && a[0] != '-' && c.n < 0) c.n = std::stoi(a);
        else throw std::runtime_error("Unknown argument: " + a);
    }

    if (c.self_test && c.n < 0) c.n = 4;
    if (c.n < 0) throw std::runtime_error("Missing positional n.");
    if (c.opt.volume_mode != "sha" && c.opt.volume_mode != "lattice") throw std::runtime_error("--volume-mode must be sha or lattice.");
    if (c.opt.search_mode != "quotient" && c.opt.search_mode != "labeled") throw std::runtime_error("--search-mode must be quotient or labeled.");
    if (c.opt.canon_mode != "graph" && c.opt.canon_mode != "refine" && c.opt.canon_mode != "brute") throw std::runtime_error("--canon-mode must be graph, refine, or brute.");
    if (c.opt.root_shard_count == 0 || c.opt.root_shard_index >= c.opt.root_shard_count) throw std::runtime_error("--root-shard requires 0 <= i < N.");
    if (c.prefix_shard_count == 0 || c.prefix_shard_index >= c.prefix_shard_count) throw std::runtime_error("--prefix-shard requires 0 <= i < N.");
    if (c.opt.generation_mode != "canonical" && c.opt.generation_mode != "state-cache") {
        throw std::runtime_error("--generation-mode must be canonical or state-cache.");
    }
    if (c.opt.state_cache_mode != "auto" && c.opt.state_cache_mode != "full" &&
        c.opt.state_cache_mode != "bounded" && c.opt.state_cache_mode != "none") {
        throw std::runtime_error("--state-cache must be auto, full, bounded, or none.");
    }
    if (c.opt.memory_policy != "warn" && c.opt.memory_policy != "shrink" && c.opt.memory_policy != "abort") {
        throw std::runtime_error("--memory-policy must be warn, shrink, or abort.");
    }
    if (c.opt.scheduler != "auto" && c.opt.scheduler != "static" &&
        c.opt.scheduler != "static-hybrid" && c.opt.scheduler != "work-steal") {
        throw std::runtime_error("--scheduler must be auto, static, static-hybrid, or work-steal.");
    }
    if (c.auto_profile != "balanced" && c.auto_profile != "throughput" &&
        c.auto_profile != "interactive" && c.auto_profile != "memory-saver" &&
        c.auto_profile != "learned") {
        throw std::runtime_error("--auto-profile must be balanced, throughput, interactive, memory-saver, or learned.");
    }
    if (c.opt.compat_cache_policy != "auto" && c.opt.compat_cache_policy != "lru" &&
        c.opt.compat_cache_policy != "lru-notouch" && c.opt.compat_cache_policy != "direct") {
        throw std::runtime_error("--compat-cache-policy must be auto, lru, lru-notouch, or direct.");
    }
    if (c.gather_cache_mode != "shared" && c.gather_cache_mode != "reset" &&
        c.gather_cache_mode != "warm" && c.gather_cache_mode != "warm-profiled") {
        throw std::runtime_error("--gather-cache-mode must be shared, reset, warm, or warm-profiled.");
    }
    if (c.gather_matrix != "quick" && c.gather_matrix != "core" && c.gather_matrix != "full") {
        throw std::runtime_error("--gather-matrix must be quick, core, or full.");
    }
    if (c.opt.solution_dedup != "memory" && c.opt.solution_dedup != "disk") {
        throw std::runtime_error("--solution-dedup must be memory or disk.");
    }

    c.opt.threads = normalize_threads(c.opt.threads);
    c.opt.parallel_depth = std::max(1, c.opt.parallel_depth);
    c.opt.split_max_depth = std::max(c.opt.parallel_depth, c.opt.split_max_depth);
    c.opt.split_min_candidates = std::max<std::size_t>(1, c.opt.split_min_candidates);
    c.opt.task_target_per_thread = std::max<std::size_t>(1, c.opt.task_target_per_thread);
    c.opt.prefix_max_depth = std::max(c.opt.parallel_depth, c.opt.prefix_max_depth);

    if (c.opt.search_mode == "quotient") c.opt.quotient = true;
    if (c.opt.search_mode == "labeled" && c.opt.quotient) {
        // Labeled traversal with quotienting is not a distinct mode in this C++ engine.
        // It still canonical-dedupes under S_n when quotient is true.
    }

    if (c.opt.cache_dir.empty()) c.opt.cache_dir = ".alexeev_cache";
    return c;
}

static void save_build_cache(const ImageSet& is, const std::filesystem::path& cache_dir, const std::string& volume_mode) {
    std::filesystem::create_directories(cache_dir);
    std::filesystem::path img_path = cache_dir / ("r3n" + std::to_string(is.hs.n) + "_" + volume_mode + "_images.bin");
    std::ofstream out(img_path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write " + img_path.string());
    uint64_t magic = 0x41335233494D4731ULL; // A3R3IMG1
    uint32_t n = static_cast<uint32_t>(is.hs.n);
    uint64_t m = static_cast<uint64_t>(is.images.size());
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));
    out.write(reinterpret_cast<const char*>(&m), sizeof(m));
    for (const auto& im : is.images) {
        out.write(reinterpret_cast<const char*>(&im.orbit_index), sizeof(im.orbit_index));
        out.write(reinterpret_cast<const char*>(&im.perm_index), sizeof(im.perm_index));
        out.write(reinterpret_cast<const char*>(&im.vmask), sizeof(im.vmask));
        int32_t vol = im.active_volume;
        out.write(reinterpret_cast<const char*>(&vol), sizeof(vol));
    }
    out.close();

    std::filesystem::path byv_path = cache_dir / ("r3n" + std::to_string(is.hs.n) + "_" + volume_mode + "_by_vertex.bin");
    std::ofstream bv(byv_path, std::ios::binary);
    uint64_t vcount = static_cast<uint64_t>(is.by_vertex.size());
    uint64_t nbits = static_cast<uint64_t>(is.images.size());
    bv.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    bv.write(reinterpret_cast<const char*>(&n), sizeof(n));
    bv.write(reinterpret_cast<const char*>(&vcount), sizeof(vcount));
    bv.write(reinterpret_cast<const char*>(&nbits), sizeof(nbits));
    for (const auto& bs : is.by_vertex) {
        uint64_t wc = static_cast<uint64_t>(bs.words.size());
        bv.write(reinterpret_cast<const char*>(&wc), sizeof(wc));
        bv.write(reinterpret_cast<const char*>(bs.words.data()), static_cast<std::streamsize>(bs.words.size()*sizeof(uint64_t)));
    }
    bv.close();

    std::cerr << "Wrote image cache: " << img_path << "\n";
    std::cerr << "Wrote by-vertex cache: " << byv_path << "\n";
}




static bool stdout_appears_redirected() {
#if defined(__linux__) || defined(__APPLE__)
    return ::isatty(STDOUT_FILENO) == 0;
#else
    return false;
#endif
}


static std::optional<std::string> json_get_string_field(const std::string& line, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (std::regex_search(line, m, re)) return m[1].str();
    return std::nullopt;
}

static std::optional<long double> json_get_number_field(const std::string& line, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch m;
    if (std::regex_search(line, m, re)) {
        try { return std::stold(m[1].str()); } catch (...) { return std::nullopt; }
    }
    return std::nullopt;
}

static bool json_get_bool_field_default(const std::string& line, const std::string& key, bool def) {
    std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch m;
    if (std::regex_search(line, m, re)) return m[1].str() == "true";
    return def;
}

struct LearnedProfileRecord {
    std::string name;
    std::string scheduler = "static";
    std::string compat_cache_policy = "lru";
    std::string affinity = "none";
    std::string numa_policy = "none";
    std::string bitset_kernel = "auto";
    std::string state_key_format = "binary";
    std::string state_cache_scope = "global";
    std::size_t worker_compat_cache_size = 0;
    std::size_t prewarm_hot_compat_limit = 0;
    std::filesystem::path prewarm_hot_compat_rows;
    std::size_t threads = 1;
    int parallel_depth = 1;
    int split_max_depth = 4;
    std::size_t split_min_candidates = 16;
    std::size_t task_target_per_thread = 64;
    double donate_when_active_below = 0.75;
    std::size_t duplicate_local_cache_max = 65536;
    std::size_t labeled_state_cache_max = 0;
    std::size_t state_cache_mb = 0;
    std::size_t compat_mem_budget_mb = 0;
    bool force_propagation = true;
    bool deep_cover_dp = false;
    std::size_t solutions = 0;
    std::size_t states = 0;
    double solutions_per_sec = 0.0;
    double states_per_sec = 0.0;
    double elapsed_sec = 0.0;
    bool solution_based = false;
    bool state_based = false;
};

static bool profile_name_is_unsafe_reference(const std::string& n) {
    auto has = [&](const std::string& s){ return n.find(s) != std::string::npos; };
    return has("depth2") || has("work_steal") || has("prefilter") || has("canonical")
        || has("compact_affinity_reference") || has("spread_affinity_reference")
        || has("thread_local_state_cache_reference") || has("thread-local");
}

static std::optional<LearnedProfileRecord>
select_learned_profile_from_file(const std::filesystem::path& path, int n, std::size_t image_count, bool quiet) {
    std::ifstream in(path);
    if (!in) {
        if (!quiet) std::cerr << "auto-learn: cannot read " << path << "; using built-in auto profile.\n";
        return std::nullopt;
    }

    std::vector<LearnedProfileRecord> solution_candidates;
    std::vector<LearnedProfileRecord> state_candidates;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto rn = json_get_number_field(line, "n");
        if (!rn || static_cast<int>(*rn) != n) continue;
        auto imgs = json_get_number_field(line, "images");
        if (imgs && image_count > 0 && static_cast<std::size_t>(*imgs) != image_count) continue;

        LearnedProfileRecord r;
        r.name = json_get_string_field(line, "name").value_or("");
        r.scheduler = json_get_string_field(line, "scheduler").value_or("static");
        r.compat_cache_policy = json_get_string_field(line, "compat_cache_policy").value_or("lru");
        r.affinity = json_get_string_field(line, "affinity").value_or("none");
        r.numa_policy = json_get_string_field(line, "numa_policy").value_or("none");
        r.bitset_kernel = json_get_string_field(line, "bitset_kernel").value_or("auto");
        r.state_key_format = json_get_string_field(line, "state_key_format").value_or("binary");
        r.state_cache_scope = json_get_string_field(line, "state_cache_scope").value_or("global");
        r.worker_compat_cache_size = static_cast<std::size_t>(json_get_number_field(line, "worker_compat_cache_size").value_or(0));
        r.prewarm_hot_compat_limit = static_cast<std::size_t>(json_get_number_field(line, "prewarm_hot_compat_limit").value_or(0));
        r.prewarm_hot_compat_rows = json_get_string_field(line, "prewarm_hot_compat_rows").value_or("");
        r.threads = static_cast<std::size_t>(json_get_number_field(line, "threads").value_or(1));
        r.parallel_depth = static_cast<int>(json_get_number_field(line, "parallel_depth").value_or(1));
        r.split_max_depth = static_cast<int>(json_get_number_field(line, "split_max_depth").value_or(4));
        r.split_min_candidates = static_cast<std::size_t>(json_get_number_field(line, "split_min_candidates").value_or(16));
        r.task_target_per_thread = static_cast<std::size_t>(json_get_number_field(line, "task_target_per_thread").value_or(64));
        r.donate_when_active_below = static_cast<double>(json_get_number_field(line, "donate_when_active_below").value_or(0.75));
        r.duplicate_local_cache_max = static_cast<std::size_t>(json_get_number_field(line, "duplicate_local_cache_max").value_or(65536));
        r.labeled_state_cache_max = static_cast<std::size_t>(json_get_number_field(line, "labeled_state_cache_max").value_or(0));
        r.state_cache_mb = static_cast<std::size_t>(json_get_number_field(line, "state_cache_mb").value_or(0));
        r.compat_mem_budget_mb = static_cast<std::size_t>(json_get_number_field(line, "compat_mem_budget_mb").value_or(0));
        r.force_propagation = json_get_bool_field_default(line, "force_propagation", true);
        r.deep_cover_dp = json_get_bool_field_default(line, "deep_cover_dp", false);
        r.solutions = static_cast<std::size_t>(json_get_number_field(line, "solutions").value_or(0));
        r.states = static_cast<std::size_t>(json_get_number_field(line, "states").value_or(0));
        r.solutions_per_sec = static_cast<double>(json_get_number_field(line, "solutions_per_sec").value_or(0.0));
        r.states_per_sec = static_cast<double>(json_get_number_field(line, "states_per_sec").value_or(0.0));
        r.elapsed_sec = static_cast<double>(json_get_number_field(line, "elapsed_sec").value_or(0.0));

        // Production-safe filters.  The gathered files showed depth-2, full work-steal,
        // and labeled-state prefilter profiles are not good production defaults.  Keep
        // them in perf_data for diagnostics but never auto-select them silently.
        if (r.parallel_depth != 1) continue;
        if (r.scheduler == "work-steal") continue;
        if (profile_name_is_unsafe_reference(r.name)) continue;
        if (r.labeled_state_cache_max != 0) continue;
        if (n == 9) {
            // n=9 wide records use a labeled partial-state cache and write
            // state_cache_scope="labeled-global".  That is exact for the wide
            // path because final quotient deduplication is still by brute S_9.
            if (r.state_cache_scope != "global" && r.state_cache_scope != "labeled-global") continue;
            if (r.scheduler == "root-static") r.scheduler = "static";
        } else {
            if (r.state_cache_scope != "global") continue;
        }
        if (r.affinity == "compact" || r.affinity == "spread") continue;
        if (r.bitset_kernel == "unrolled" || r.bitset_kernel == "avx2" || r.bitset_kernel == "avx512") continue;
        if (r.threads == 0) continue;
        const std::size_t solution_threshold = (n == 9 ? 1 : 5000);
        const std::size_t state_threshold = (n == 9 ? 1000 : 50000);
        const double min_elapsed = (n == 9 ? 0.0 : 5.0);
        if (r.solutions >= solution_threshold && r.solutions_per_sec > 0.0 && r.elapsed_sec >= min_elapsed) {
            r.solution_based = true;
            solution_candidates.push_back(r);
        } else if (r.states >= state_threshold && r.states_per_sec > 0.0 && r.elapsed_sec >= min_elapsed) {
            r.state_based = true;
            state_candidates.push_back(r);
        }    }

    auto better_solution = [](const LearnedProfileRecord& a, const LearnedProfileRecord& b) {
        if (a.solutions_per_sec != b.solutions_per_sec) return a.solutions_per_sec < b.solutions_per_sec;
        return a.states_per_sec < b.states_per_sec;
    };
    auto better_state = [](const LearnedProfileRecord& a, const LearnedProfileRecord& b) {
        if (a.states_per_sec != b.states_per_sec) return a.states_per_sec < b.states_per_sec;
        return a.solutions_per_sec < b.solutions_per_sec;
    };

    if (!solution_candidates.empty()) {
        return *std::max_element(solution_candidates.begin(), solution_candidates.end(), better_solution);
    }
    if (!state_candidates.empty()) {
        return *std::max_element(state_candidates.begin(), state_candidates.end(), better_state);
    }
    if (!quiet) std::cerr << "auto-learn: no safe production record found in " << path << "; using built-in auto profile.\n";
    return std::nullopt;
}


static std::filesystem::path first_existing_path(const std::vector<std::filesystem::path>& candidates) {
    for (const auto& p : candidates) {
        if (!p.empty()) {
            std::error_code ec;
            if (std::filesystem::exists(p, ec) && !ec) return p;
        }
    }
    return {};
}

static std::filesystem::path default_hot_compat_rows_path(const Cli& cli, int n) {
    std::vector<std::filesystem::path> c;
    auto add_dir = [&](const std::filesystem::path& d) {
        if (d.empty()) return;
        c.push_back(d / ("hot_compat_rows_n" + std::to_string(n) + ".txt"));
        c.push_back(d / ("perf_hot_compat_rows_n" + std::to_string(n) + ".txt"));
        c.push_back(d / "perf_hot_compat_rows.txt");
        c.push_back(d / "hot_compat_rows.txt");
    };
    add_dir(cli.opt.cache_dir);
    add_dir(cli.cache_dir);
    add_dir(std::filesystem::current_path());
    return first_existing_path(c);
}

static void maybe_enable_profiled_hot_prewarm(Cli& cli, int n, std::size_t limit) {
    if (limit == 0) return;
    if (!cli.opt.prewarm_hot_compat_rows.empty() || cli.opt.prewarm_hot_compat_limit != 0) return;
    std::filesystem::path p = default_hot_compat_rows_path(cli, n);
    if (!p.empty()) {
        cli.opt.prewarm_hot_compat_rows = p;
        cli.opt.prewarm_hot_compat_limit = limit;
    }
}

static void apply_learned_profile(Cli& cli, const ImageSet& is) {
    if (!cli.opt.auto_tune) return;
    if (cli.auto_profile != "learned" && !cli.auto_learn_explicit) return;

    std::filesystem::path path = cli.auto_learn_path;
    if (path.empty()) path = std::filesystem::current_path() / "perf_data.txt";
    auto rec = select_learned_profile_from_file(path, is.hs.n, is.images.size(), cli.opt.quiet);
    if (!rec) return;

    SystemInfo sys = detect_system_info();
    std::size_t logical = std::max<std::size_t>(1, sys.logical_cpus);
    const auto& r = *rec;

    if (!cli.threads_explicit) cli.opt.threads = std::min<std::size_t>(std::max<std::size_t>(1, r.threads), logical);
    if (!cli.scheduler_explicit) cli.opt.scheduler = r.scheduler;
    if (!cli.compat_policy_explicit) cli.opt.compat_cache_policy = r.compat_cache_policy;
    if (!cli.affinity_explicit) cli.opt.affinity = r.affinity;
    if (!cli.bitset_kernel_explicit) cli.opt.bitset_kernel = r.bitset_kernel;
    if (!cli.state_key_format_explicit) cli.opt.state_key_format = r.state_key_format;
    if (!cli.worker_compat_cache_explicit) cli.opt.worker_compat_cache_size = r.worker_compat_cache_size;
    if (r.prewarm_hot_compat_limit > 0 && cli.opt.prewarm_hot_compat_limit == 0 && cli.opt.prewarm_hot_compat_rows.empty()) {
        std::vector<std::filesystem::path> candidates;
        if (!r.prewarm_hot_compat_rows.empty()) candidates.push_back(r.prewarm_hot_compat_rows);
        candidates.push_back(default_hot_compat_rows_path(cli, is.hs.n));
        std::filesystem::path learned_hot = first_existing_path(candidates);
        if (!learned_hot.empty()) {
            cli.opt.prewarm_hot_compat_rows = learned_hot;
            cli.opt.prewarm_hot_compat_limit = r.prewarm_hot_compat_limit;
        }
    }
    cli.opt.numa_policy = r.numa_policy;
    cli.opt.state_cache_scope = r.state_cache_scope;
    if (!cli.task_target_explicit) cli.opt.task_target_per_thread = std::max<std::size_t>(1, r.task_target_per_thread);
    if (!cli.split_max_explicit) cli.opt.split_max_depth = std::max(1, r.split_max_depth);
    if (!cli.split_min_explicit) cli.opt.split_min_candidates = std::max<std::size_t>(1, r.split_min_candidates);
    // Learn cache budgets only if they fit the current RSS guard.  This lets a
    // large-machine perf_data file be inspected on a small laptop without silently
    // requesting impossible cache caps.
    std::size_t guard = cli.opt.memory_limit_mb;
    if (!cli.compat_explicit && r.compat_mem_budget_mb > 0) {
        if (guard == 0 || r.compat_mem_budget_mb + 2048 < guard / 2) cli.opt.compat_mem_budget_mb = r.compat_mem_budget_mb;
    }
    if (!cli.state_cache_explicit && r.state_cache_mb > 0) {
        if (guard == 0 || r.state_cache_mb + cli.opt.compat_mem_budget_mb + 4096 < guard) cli.opt.state_cache_mb = r.state_cache_mb;
    }
    if (!cli.duplicate_local_explicit) cli.opt.duplicate_local_cache_max = r.duplicate_local_cache_max;
    if (!cli.labeled_state_explicit) cli.opt.labeled_state_cache_max = r.labeled_state_cache_max;
    cli.opt.donate_when_active_below = r.donate_when_active_below;
    cli.opt.force_propagation = r.force_propagation;
    cli.opt.deep_cover_dp = r.deep_cover_dp;
    cli.opt.parallel_depth = 1; // production-safe learned profiles are filtered to depth 1

    if (!cli.opt.quiet) {
        std::cerr << "auto-learn selected profile from " << path << ":\n"
                  << "  name=" << r.name
                  << " scheduler=" << r.scheduler
                  << " threads=" << cli.opt.threads
                  << " compat=" << r.compat_cache_policy
                  << " affinity=" << r.affinity
                  << " bitset=" << r.bitset_kernel
                  << " key=" << r.state_key_format
                  << " worker_compat_cache=" << r.worker_compat_cache_size
                  << " prewarm_limit=" << r.prewarm_hot_compat_limit
                  << " sol/s=" << std::fixed << std::setprecision(3) << r.solutions_per_sec
                  << " states/s=" << std::fixed << std::setprecision(3) << r.states_per_sec
                  << (r.solution_based ? " (solution benchmark)" : " (state benchmark)")
                  << "\n";
    }
}


static void apply_auto_preload(Cli& cli) {
    if (!cli.opt.auto_tune) return;
    SystemInfo sys = detect_system_info();

    if (!cli.threads_explicit) {
        std::size_t logical = std::max<std::size_t>(1, sys.logical_cpus);
        std::size_t physical = sys.physical_cores ? sys.physical_cores : logical;
        bool likely_smt = (sys.physical_cores > 0 && logical >= physical * 3 / 2);

        // Feedback-tuned rule from the latest dual-Threadripper gather-data:
        //   throughput  -> static-hybrid at about 5/3 physical cores, leaving ~32
        //                  logical CPUs free on a 96c/192t box (160 threads).
        //   balanced    -> static-hybrid at about 3/2 physical cores, leaving ~48
        //                  logical CPUs free on a 96c/192t box (144 threads).
        //   interactive -> physical-core count on SMT systems (96 on 96c/192t),
        //                  leaving all SMT siblings for OS/browser activity.
        //   memory-saver-> half physical.
        std::size_t chosen = physical;
        if (cli.auto_profile == "throughput") {
            if (likely_smt) {
                std::size_t reserve = (logical > 32 ? logical - 32 : logical);
                std::size_t five_thirds = (physical * 5 + 2) / 3;
                chosen = std::min<std::size_t>(reserve, std::min<std::size_t>(logical, std::max<std::size_t>(physical, five_thirds)));
            } else chosen = logical;
        } else if (cli.auto_profile == "balanced") {
            if (likely_smt) {
                std::size_t reserve = (logical > 48 ? logical - 48 : logical);
                std::size_t three_halves = (physical * 3 + 1) / 2;
                chosen = std::min<std::size_t>(reserve, std::min<std::size_t>(logical, std::max<std::size_t>(physical, three_halves)));
            } else chosen = physical;
        } else if (cli.auto_profile == "interactive") {
            if (likely_smt) chosen = physical;
            else {
                std::size_t reserve = (physical >= 64 ? 16 : (physical >= 32 ? 8 : 2));
                chosen = (physical > reserve ? physical - reserve : std::max<std::size_t>(1, physical / 2));
            }
        } else if (cli.auto_profile == "memory-saver") {
            chosen = std::max<std::size_t>(1, physical / 2);
        } else { // learned or unknown: start conservative; learned profile may override later
            chosen = physical;
        }

        chosen = std::min<std::size_t>(chosen, logical);
        cli.opt.threads = std::max<std::size_t>(1, chosen);
    }

    cli.opt.generation_mode = "state-cache";
    if (!cli.state_cache_explicit) cli.opt.state_cache_mode = (cli.n <= 7 ? "full" : "bounded");
    if (!cli.scheduler_explicit && cli.opt.scheduler == "auto") {
        // Do not use full work-stealing as an auto default.  The perf data showed
        // it had good core utilization but much worse time-to-first-10K tilings.
        // The exact scheduler is resolved after images are loaded because it depends
        // on n and the selected auto profile.
        cli.opt.scheduler = "auto";
    }
}

static void apply_auto_after_images(Cli& cli, const ImageSet& is) {
    SystemInfo sys = detect_system_info();

    if (cli.opt.auto_tune) {
        std::size_t total = sys.mem_total_mb ? sys.mem_total_mb : 65536;
        std::size_t available = sys.mem_available_mb ? sys.mem_available_mb : total;

        // Conservative RSS guard.  For very large machines we reserve roughly one sixth
        // of RAM, with a hard minimum 64 GiB.  This reproduces the empirically good
        // ~430 GiB guard on the 512 GiB dual-Threadripper run while still leaving room
        // for OS page cache, a shell, browser activity, and monitoring tools.
        std::size_t reserve = std::max<std::size_t>(4096, total / 8);
        if (total >= 384ULL * 1024ULL) reserve = std::max<std::size_t>(64ULL * 1024ULL, total / 6);
        else if (total >= 256ULL * 1024ULL) reserve = std::max<std::size_t>(48ULL * 1024ULL, total / 7);
        else if (total >= 96ULL * 1024ULL) reserve = std::max<std::size_t>(24ULL * 1024ULL, total / 8);
        else if (total >= 48ULL * 1024ULL) reserve = std::max<std::size_t>(8ULL * 1024ULL, total / 6);

        std::size_t usable_total = (total > reserve) ? (total - reserve) : (total * 3 / 4);
        std::size_t usable_available = static_cast<std::size_t>(static_cast<long double>(available) * 0.88L);
        std::size_t usable = std::min<std::size_t>(usable_total, usable_available ? usable_available : usable_total);
        if (usable == 0) usable = total * 3 / 4;

        if (cli.auto_profile == "throughput") {
            usable = std::min<std::size_t>(static_cast<std::size_t>(static_cast<long double>(available) * 0.92L),
                                           total > 32ULL * 1024ULL ? total - 32ULL * 1024ULL : usable);
        } else if (cli.auto_profile == "interactive") {
            usable = std::min<std::size_t>(usable, total > 96ULL * 1024ULL ? total - std::max<std::size_t>(96ULL * 1024ULL, total / 5) : usable);
        } else if (cli.auto_profile == "memory-saver") {
            usable = std::min<std::size_t>(usable, total / 2);
        }

        if (!cli.memory_limit_explicit) cli.opt.memory_limit_mb = usable;
        if (cli.opt.memory_policy == "warn") cli.opt.memory_policy = "shrink";

        if (is.hs.n >= 7 && cli.opt.generation_mode == "state-cache" && cli.opt.state_cache_mode == "auto") {
            cli.opt.state_cache_mode = (is.hs.n <= 7) ? "full" : "bounded";
        }

        // Regression fix: the older 64 GiB LRU compatibility-cache budget was
        // faster in the user's n=8 timing than the later 32 GiB/direct-cache
        // profile.  The miss rate is small, but the LRU path plus static scheduler
        // has the best measured time-to-solution so far.
        if (!cli.compat_explicit && is.hs.n >= 7) {
            std::size_t compat_mb = 4096;
            if (total >= 384ULL * 1024ULL) compat_mb = 65536;
            else if (total >= 256ULL * 1024ULL) compat_mb = 32768;
            else if (total >= 128ULL * 1024ULL) compat_mb = 16384;
            else if (total >= 64ULL * 1024ULL) compat_mb = 12288;
            else compat_mb = 4096;

            if (cli.auto_profile == "memory-saver") compat_mb = std::max<std::size_t>(2048, compat_mb / 2);
            cli.opt.compat_mem_budget_mb = compat_mb;
        }

        // The same log showed duplicate_states/(states+duplicates) ≈ 0.75 and about
        // 595 MiB per million state-cache entries.  Spend high-memory budgets on the
        // state cache first; evictions are exact but create repeated work.
        if (!cli.state_cache_explicit && is.hs.n >= 8) {
            std::size_t state_mb = 8192;
            if (total >= 384ULL * 1024ULL) {
                // The 65 GiB state-cache cap was the fastest measured baseline
                // before the regression.  Larger budgets remain useful for very
                // long runs, but they should be requested explicitly; auto optimizes
                // time-to-first-large-batch and avoids excess cache-management cost.
                if (cli.auto_profile == "memory-saver") state_mb = 32768;
                else state_mb = 65536;
            } else if (total >= 256ULL * 1024ULL) {
                state_mb = (cli.auto_profile == "throughput") ? 98304 : 65536;
            } else if (total >= 128ULL * 1024ULL) {
                state_mb = (cli.auto_profile == "memory-saver") ? 16384 : 32768;
            } else if (total >= 64ULL * 1024ULL) {
                state_mb = (cli.auto_profile == "memory-saver") ? 4096 : 12288;
            } else {
                state_mb = 2048;
            }

            // Do not let cache budgets exceed the RSS guard after compatibility/canon
            // reservations.  Keep this conservative; the runtime RSS guard can shrink.
            std::size_t max_state_by_guard = (cli.opt.memory_limit_mb > 0 && cli.opt.memory_limit_mb > cli.opt.compat_mem_budget_mb + 8192)
                ? (cli.opt.memory_limit_mb - cli.opt.compat_mem_budget_mb - 8192)
                : state_mb;
            state_mb = std::min<std::size_t>(state_mb, max_state_by_guard);
            cli.opt.state_cache_mb = std::max<std::size_t>(512, state_mb);
            cli.opt.state_cache_mode = "bounded";
        }

        if (cli.opt.canon_cache_mb == 0) cli.opt.canon_cache_mb = (total >= 128ULL * 1024ULL) ? 1024 : 256;
        if (cli.opt.solution_key_cache_mb == 0) cli.opt.solution_key_cache_mb = (total >= 128ULL * 1024ULL) ? 2048 : 512;

        if (is.hs.n >= 8 && cli.opt.threads > 1) {
            if (!cli.scheduler_explicit && cli.opt.scheduler == "auto") {
                // Latest 10K-solution gather data: static-hybrid depth-1 is the
                // winning production backbone at 144/160 threads.  Full work-steal
                // and static depth-2 remain anti-patterns.
                if (cli.auto_profile == "balanced" || cli.auto_profile == "throughput" || cli.auto_profile == "interactive")
                    cli.opt.scheduler = "static-hybrid";
                else
                    cli.opt.scheduler = "static";
            }

            // Feedback-tuned defaults from perf_data.txt:
            //   * full work-steal is not automatic;
            //   * static-hybrid is used for balanced/throughput/interactive profiles;
            //   * parallel-depth 2 is deliberately never selected automatically.
            cli.opt.parallel_depth = 1;
            if (cli.opt.scheduler == "static-hybrid") {
                if (!cli.task_target_explicit) cli.opt.task_target_per_thread = 64;
                if (!cli.split_max_explicit) cli.opt.split_max_depth = 4;
                if (!cli.split_min_explicit) cli.opt.split_min_candidates = 16;
                cli.opt.donate_when_active_below = 0.75;
            } else if (cli.opt.scheduler == "work-steal") {
                if (!cli.split_max_explicit) cli.opt.split_max_depth = std::max(cli.opt.split_max_depth, 5);
                if (!cli.split_min_explicit) cli.opt.split_min_candidates = 8;
                if (!cli.task_target_explicit) cli.opt.task_target_per_thread = std::max<std::size_t>(cli.opt.task_target_per_thread, 128);
            } else { // static
                if (!cli.task_target_explicit) cli.opt.task_target_per_thread = 64;
                if (!cli.split_max_explicit) cli.opt.split_max_depth = 4;
                if (!cli.split_min_explicit) cli.opt.split_min_candidates = 16;
            }

            if (!cli.compat_policy_explicit && cli.opt.compat_cache_policy == "auto") {
                // Latest cache-affinity gather data: LRU wins the best 144/160-thread
                // static-hybrid profiles.  direct/lru-notouch remain gather-data variants.
                cli.opt.compat_cache_policy = "lru";
            }
            if (!cli.affinity_explicit) {
                // Latest warm-profiled data:
                //   * throughput 160t + NUMA affinity was slower than no affinity;
                //   * interactive 96t + NUMA affinity was faster than no affinity;
                //   * compact/spread remain anti-patterns on the dual Threadripper.
                if (cli.auto_profile == "interactive" && detect_numa_cpu_lists().size() >= 2)
                    cli.opt.affinity = "numa";
                else
                    cli.opt.affinity = "none";
            }
            if (!cli.bitset_kernel_explicit) cli.opt.bitset_kernel = "auto";
            if (!cli.state_key_format_explicit) cli.opt.state_key_format = "binary";
            if (!cli.worker_compat_cache_explicit) {
                // Latest warm-profiled 10K-solution data:
                //   160-thread static-hybrid/LRU + worker_compat_cache_size=64
                //   is the best measured throughput profile.  The 144/96-thread
                //   rows do not benefit as consistently, so keep the tiny cache
                //   as a throughput-only default and leave it benchmark-gated for
                //   balanced/interactive profiles.
                cli.opt.worker_compat_cache_size = (cli.auto_profile == "throughput" ? 64 : 0);
            }
            if (is.hs.n >= 8 && (cli.auto_profile == "throughput" || cli.auto_profile == "balanced" || cli.auto_profile == "interactive")) {
                // If the user has gathered hot compatibility rows, re-use the top
                // 512 rows.  Missing files are ignored; this never delays first runs.
                maybe_enable_profiled_hot_prewarm(cli, is.hs.n, 512);

                // Also record hot compatibility rows automatically for future runs.
                // This is a low-overhead profiler: it only increments per-row counters
                // on row access and writes a small sorted text file at the end.
                // Thus a first production run creates the profile that later --auto
                // runs can prewarm without requiring the user to remember an extra flag.
                if (cli.opt.record_hot_compat_rows.empty() && !cli.opt.cache_dir.empty()) {
                    std::filesystem::create_directories(cli.opt.cache_dir);
                    cli.opt.record_hot_compat_rows = cli.opt.cache_dir /
                        ("hot_compat_rows_n" + std::to_string(is.hs.n) + ".txt");
                }
            }
            cli.opt.state_cache_scope = "global";
            if (!cli.duplicate_local_explicit) cli.opt.duplicate_local_cache_max = 65536;
            if (!cli.labeled_state_explicit) cli.opt.labeled_state_cache_max = 0;
            cli.opt.force_propagation = true;

            // Optional machine-specific override from a previous gather-data file.
            // This happens after the baked-in safe defaults, before cache row caps are computed.
            apply_learned_profile(cli, is);
        }

        if (!cli.prefix_task_target_explicit && is.hs.n >= 8 && cli.opt.threads > 1) {
            // Keep serial prefix construction disabled in ordinary auto runs.
            // The work-stealing scheduler performs parallel frontier creation.
            cli.opt.prefix_task_target = 0;
            cli.opt.prefix_max_depth = std::max(cli.opt.prefix_max_depth, 2);
        }

        if (!cli.async_output_explicit) {
            bool smoke = (cli.opt.max_solutions > 0 && cli.opt.max_solutions <= 100);
            bool redirected_or_file = !cli.out_path.empty() || stdout_appears_redirected();
            if (smoke) {
                cli.opt.async_output = false;
                if (!cli.flush_every_explicit) cli.opt.flush_every = 1;
            } else if (redirected_or_file && is.hs.n >= 8) {
                // Regression fix: the fastest measured baseline streamed directly.
                // Async output is still available explicitly, but --auto no longer
                // enables it silently.
                cli.opt.async_output = false;
                if (!cli.flush_every_explicit) cli.opt.flush_every = 1;
            } else {
                cli.opt.async_output = false;
                if (!cli.flush_every_explicit) cli.opt.flush_every = 1;
            }
        }
        // Auto mode still does not prewarm compatibility rows before a normal search:
        // the observed miss rate is tiny, and prewarming delays time-to-first-solution.
    }

    if (cli.opt.compat_mem_rows == 0 && is.hs.n >= 7) {
        std::size_t rb = dense_row_bytes(is.images.size());
        cli.opt.compat_mem_rows = std::max<std::size_t>(
            1, (cli.opt.compat_mem_budget_mb * 1024ULL * 1024ULL) / std::max<std::size_t>(rb, 1));
    }

    if (cli.opt.generation_mode == "state-cache" && cli.opt.state_cache_mode == "auto") {
        cli.opt.state_cache_mode = (is.hs.n <= 7) ? "full" : "bounded";
    }

    if (cli.opt.state_cache_mode == "bounded" && cli.opt.state_cache_mb > 0) {
        std::size_t per = estimate_state_cache_entry_bytes(
            cli.opt.volume_mode == "sha" ? (is.hs.n - 3) * (is.hs.n - 3) : 64);
        cli.opt.state_cache_max = std::max<std::size_t>(
            1, (cli.opt.state_cache_mb * 1024ULL * 1024ULL) / std::max<std::size_t>(per, 1));
    }

    if (cli.opt.canon_cache_mb > 0) {
        cli.opt.canon_memo_max = std::max<std::size_t>(
            1, (cli.opt.canon_cache_mb * 1024ULL * 1024ULL) / 512ULL);
    }
}


static void recompute_cache_caps_from_budgets(Cli& cli, const ImageSet& is) {
    if (cli.opt.compat_mem_rows == 0 || cli.opt.auto_tune) {
        std::size_t rb = dense_row_bytes(is.images.size());
        if (cli.opt.compat_mem_budget_mb > 0) {
            cli.opt.compat_mem_rows = std::max<std::size_t>(
                1, (cli.opt.compat_mem_budget_mb * 1024ULL * 1024ULL) / std::max<std::size_t>(rb, 1));
        }
    }
    if (cli.opt.state_cache_mode == "bounded" && cli.opt.state_cache_mb > 0) {
        std::size_t per = estimate_state_cache_entry_bytes(
            cli.opt.volume_mode == "sha" ? (is.hs.n - 3) * (is.hs.n - 3) : 64);
        cli.opt.state_cache_max = std::max<std::size_t>(
            1, (cli.opt.state_cache_mb * 1024ULL * 1024ULL) / std::max<std::size_t>(per, 1));
    }
}

static void apply_auto_pilot_feedback(Cli& cli, const ImageSet& is) {
    if (!cli.opt.auto_tune || cli.opt.auto_pilot_states == 0) return;
    if (cli.opt.generation_mode != "state-cache") return;

    SearchOptions pilot = cli.opt;
    pilot.quiet = true;
    pilot.count_only = true;
    pilot.jsonl = false;
    pilot.max_solutions = 0;
    pilot.max_states = cli.opt.auto_pilot_states;
    pilot.async_output = false;
    pilot.flush_every = 1;
    pilot.progress_interval = 0;
    // Keep the pilot exact but avoid disk solution spooling.  The compatibility
    // disk cache may still be warmed, which is useful for the real run.
    pilot.solution_dedup = "memory";

    std::ostringstream sink;
    auto t0 = std::chrono::steady_clock::now();
    SearchEngine pilot_engine(const_cast<ImageSet&>(is), pilot);
    pilot_engine.run(sink);
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::size_t states = pilot_engine.stats.states.load();
    std::size_t dup = pilot_engine.stats.duplicate_states.load();
    std::size_t attempts = states + dup;
    long double dup_ratio = attempts ? static_cast<long double>(dup) / static_cast<long double>(attempts) : 0.0L;

    std::size_t ch = pilot_engine.compat.hits.load();
    std::size_t cm = pilot_engine.compat.misses.load();
    long double miss_rate = (ch + cm) ? static_cast<long double>(cm) / static_cast<long double>(ch + cm) : 0.0L;

    std::size_t peak = pilot_engine.stats.peak_active_workers.load();
    long double active_ratio = cli.opt.threads ? static_cast<long double>(peak) / static_cast<long double>(cli.opt.threads) : 1.0L;

    if (!cli.opt.quiet) {
        std::cerr << "\nAuto feedback pilot\n";
        std::cerr << "-------------------\n";
        std::cerr << "pilot_states                       " << states << "\n";
        std::cerr << "pilot_duplicate_states             " << dup << "\n";
        std::cerr << "pilot_duplicate_ratio              " << std::fixed << std::setprecision(3) << static_cast<double>(dup_ratio) << "\n";
        std::cerr << "pilot_compat_hits                  " << ch << "\n";
        std::cerr << "pilot_compat_misses                " << cm << "\n";
        std::cerr << "pilot_compat_miss_rate             " << std::fixed << std::setprecision(5) << static_cast<double>(miss_rate) << "\n";
        std::cerr << "pilot_peak_active_workers          " << peak << "/" << cli.opt.threads << "\n";
        std::cerr << "pilot_elapsed_sec                  " << std::fixed << std::setprecision(3) << elapsed << "\n";
    }

    const std::size_t old_state_mb = cli.opt.state_cache_mb;
    const std::size_t old_compat_mb = cli.opt.compat_mem_budget_mb;
    const std::size_t old_tpt = cli.opt.task_target_per_thread;
    const int old_split_depth = cli.opt.split_max_depth;
    const std::size_t old_split_min = cli.opt.split_min_candidates;

    if (!cli.state_cache_explicit && is.hs.n >= 8 && dup_ratio > 0.50L) {
        // High duplicate pressure means state-cache memory is buying real work
        // reduction.  Increase toward 40% of the RSS guard but cap at 256 GiB.
        std::size_t target_by_guard = cli.opt.memory_limit_mb ? (cli.opt.memory_limit_mb * 2 / 5) : cli.opt.state_cache_mb;
        std::size_t grown = std::max<std::size_t>(cli.opt.state_cache_mb, cli.opt.state_cache_mb + cli.opt.state_cache_mb / 4);
        if (dup_ratio > 0.70L) grown = std::max<std::size_t>(grown, cli.opt.state_cache_mb + cli.opt.state_cache_mb / 2);
        cli.opt.state_cache_mb = std::min<std::size_t>(std::max<std::size_t>(grown, 8192), std::min<std::size_t>(target_by_guard ? target_by_guard : grown, 262144));
        cli.opt.state_cache_mode = "bounded";
    }

    if (!cli.compat_explicit && is.hs.n >= 8 && miss_rate < 0.005L) {
        // Pilot observes that compatibility misses are rare, but timing data shows
        // that switching to the direct cache was a regression in the early n=8 path.
        // Keep the LRU fast path unless the user explicitly asks for direct.
        cli.opt.compat_cache_policy = "lru";
        cli.opt.compat_mem_budget_mb = std::max<std::size_t>(4096, cli.opt.compat_mem_budget_mb);
        cli.opt.compat_mem_rows = 0; // recompute from budget below
    }

    if (!cli.task_target_explicit && is.hs.n >= 8 && active_ratio < 0.85L) {
        cli.opt.task_target_per_thread = std::min<std::size_t>(512, std::max<std::size_t>(cli.opt.task_target_per_thread * 2, 128));
    }
    if (!cli.split_max_explicit && is.hs.n >= 8 && active_ratio < 0.85L) {
        cli.opt.split_max_depth = std::min<int>(7, std::max<int>(cli.opt.split_max_depth + 1, 5));
    }
    if (!cli.split_min_explicit && is.hs.n >= 8 && active_ratio < 0.85L) {
        cli.opt.split_min_candidates = std::max<std::size_t>(4, cli.opt.split_min_candidates / 2);
    }

    if (pilot_engine.stats.coverage_prunes.load() == 0 && pilot_engine.stats.deep_cover_dp_prunes.load() == 0) {
        // Keep the exact DP available only when it is genuinely deep/small.
        cli.opt.deep_cover_dp_max_uncovered = std::min<int>(cli.opt.deep_cover_dp_max_uncovered, 18);
        cli.opt.deep_cover_dp_max_candidates = std::min<std::size_t>(cli.opt.deep_cover_dp_max_candidates, 2500);
    }

    recompute_cache_caps_from_budgets(cli, is);

    if (!cli.opt.quiet) {
        std::cerr << "pilot adjustments\n";
        std::cerr << "  state_cache_mb                   " << old_state_mb << " -> " << cli.opt.state_cache_mb << "\n";
        std::cerr << "  compat_mem_budget_mb             " << old_compat_mb << " -> " << cli.opt.compat_mem_budget_mb << "\n";
        std::cerr << "  task_target_per_thread           " << old_tpt << " -> " << cli.opt.task_target_per_thread << "\n";
        std::cerr << "  split_max_depth                  " << old_split_depth << " -> " << cli.opt.split_max_depth << "\n";
        std::cerr << "  split_min_candidates             " << old_split_min << " -> " << cli.opt.split_min_candidates << "\n\n";
    }
}

static void print_memory_plan(const Cli& cli, const ImageSet& is) {
    if (cli.opt.quiet || !cli.print_memory_plan) return;

    SystemInfo sys = detect_system_info();
    std::size_t rb = dense_row_bytes(is.images.size());
    std::size_t fixed_image_bytes =
        is.images.size() * (sizeof(ImageRecord) + sizeof(uint64_t) + sizeof(uint16_t) * 2);
    std::size_t by_vertex_bytes = is.by_vertex.size() * rb;
    std::size_t compat_payload_cap = cli.opt.compat_mem_rows * rb;
    std::size_t state_entry = estimate_state_cache_entry_bytes(
        cli.opt.volume_mode == "sha" ? (is.hs.n - 3) * (is.hs.n - 3) : 64);
    std::size_t state_cap = (cli.opt.state_cache_mode == "bounded") ? cli.opt.state_cache_max * state_entry : 0;
    std::size_t canon_cap = cli.opt.canon_memo_max * 512ULL;
    std::size_t depth = static_cast<std::size_t>((cli.opt.volume_mode == "sha") ? ((is.hs.n - 3) * (is.hs.n - 3) + 2) : 32);
    std::size_t workspace_per_thread = depth * rb * 2 + depth * 4096;
    std::size_t workspace_cap = workspace_per_thread * std::max<std::size_t>(1, cli.opt.threads);
    std::size_t managed_cap = fixed_image_bytes + by_vertex_bytes + compat_payload_cap + state_cap + canon_cap + workspace_cap;

    std::cerr << "\nMemory/resource plan before DFS\n";
    std::cerr << "-------------------------------\n";
    std::cerr << "detected logical_cpus              " << sys.logical_cpus << "\n";
    std::cerr << "detected physical_cores            " << (sys.physical_cores ? std::to_string(sys.physical_cores) : std::string("unknown")) << "\n";
    std::cerr << "detected mem_total_mb              " << sys.mem_total_mb << "\n";
    std::cerr << "detected mem_available_mb          " << sys.mem_available_mb << "\n";
    std::cerr << "detected last_level_cache_kib      " << sys.llc_kib << "\n";
    std::cerr << "n                                  " << is.hs.n << "\n";
    std::cerr << "images                             " << is.images.size() << "\n";
    std::cerr << "hypersimplex vertices              " << is.hs.num_vertices << "\n";
    std::cerr << "dense image-index row              " << rb << " bytes = " << mib_fmt(rb) << "\n";
    if (cli.opt.auto_tune) {
        std::cerr << "auto_profile                       " << cli.auto_profile << "\n";
        std::cerr << "auto_pilot_states                  " << cli.opt.auto_pilot_states << "\n";
    }
    std::cerr << "generation_mode                    " << cli.opt.generation_mode << "\n";
    std::cerr << "state_cache_mode                   " << cli.opt.state_cache_mode << "\n";
    std::cerr << "threads                            " << cli.opt.threads << "\n";
    std::cerr << "scheduler                          " << cli.opt.scheduler << "\n";
    std::cerr << "parallel_depth                     " << cli.opt.parallel_depth << "\n";
    std::cerr << "split_max_depth                    " << cli.opt.split_max_depth << "\n";
    std::cerr << "split_min_candidates               " << cli.opt.split_min_candidates << "\n";
    std::cerr << "task_target_per_thread             " << cli.opt.task_target_per_thread << "\n";
    std::cerr << "prefix_task_target                 " << cli.opt.prefix_task_target << "\n";
    std::cerr << "prefix_max_depth                   " << cli.opt.prefix_max_depth << "\n";
    std::cerr << "force_propagation                  " << (cli.opt.force_propagation ? "on" : "off") << "\n";
    std::cerr << "async_output                       " << (cli.opt.async_output ? "on" : "off")
              << " flush_every=" << cli.opt.flush_every << "\n";
    std::cerr << "affinity                           " << cli.opt.affinity << "\n";
    std::cerr << "numa_policy                        " << cli.opt.numa_policy << "\n";
    std::cerr << "bitset_kernel                      " << cli.opt.bitset_kernel << "\n";
    std::cerr << "state_key_format                   " << cli.opt.state_key_format << "\n";
    std::cerr << "worker_compat_cache_size           " << cli.opt.worker_compat_cache_size << "\n";
    if (!cli.opt.prewarm_hot_compat_rows.empty()) {
        std::cerr << "prewarm_hot_compat_rows            " << cli.opt.prewarm_hot_compat_rows << "\n";
        std::cerr << "prewarm_hot_compat_limit           " << cli.opt.prewarm_hot_compat_limit << "\n";
    }
    if (!cli.opt.record_hot_compat_rows.empty()) {
        std::cerr << "record_hot_compat_rows             " << cli.opt.record_hot_compat_rows << "\n";
    }
    std::cerr << "\n";

    std::cerr << "Fixed/derived memory estimates\n";
    std::cerr << "  image metadata                   " << mib_fmt(fixed_image_bytes) << "\n";
    std::cerr << "  by_vertex membership             " << mib_fmt(by_vertex_bytes) << "\n";
    std::cerr << "  thread DFS workspaces            " << mib_fmt(workspace_cap)
              << " (" << mib_fmt(workspace_per_thread) << " per thread)\n\n";

    std::cerr << "Bounded caches\n";
    std::cerr << "  state-cache backend              legacy-speed exact string shards\n";
    std::cerr << "  compatibility cache policy       " << cli.opt.compat_cache_policy << "\n";
    std::cerr << "  compatibility row capacity       " << cli.opt.compat_mem_rows
              << " dense-row equivalent cap " << mib_fmt(compat_payload_cap) << "\n";
    if (cli.opt.state_cache_mode == "bounded") {
        std::cerr << "  state-cache entries              " << cli.opt.state_cache_max
                  << " estimated-entry cap " << mib_fmt(state_cap)
                  << " using " << state_entry << " bytes/entry estimate\n";
        if (cli.opt.state_cache_mb > 0) {
            std::cerr << "  state-cache byte budget          " << cli.opt.state_cache_mb
                      << " MiB requested cache budget (RSS guard is authoritative)\n";
        }
    } else {
        std::cerr << "  state-cache entries              "
                  << (cli.opt.state_cache_mode == "full" ? "unbounded/full exact cache" : "none") << "\n";
    }
    std::cerr << "  canonicalization LRU             " << cli.opt.canon_memo_max
              << " estimated cap " << mib_fmt(canon_cap) << "\n";
    std::cerr << "  solution dedup                   " << cli.opt.solution_dedup << "\n";
    std::cerr << "  RSS memory guard                 "
              << (cli.opt.memory_limit_mb ? std::to_string(cli.opt.memory_limit_mb) + " MB, policy=" + cli.opt.memory_policy : std::string("disabled")) << "\n";
    std::cerr << "  managed-memory estimate          " << mib_fmt(managed_cap) << "\n";
    if (cli.opt.state_cache_mode == "bounded" && state_entry > 0) {
        long double states_per_gib = (1024.0L * 1024.0L * 1024.0L) / static_cast<long double>(state_entry);
        std::cerr << "  state-cache projected capacity   ~"
                  << static_cast<unsigned long long>(states_per_gib * (static_cast<long double>(state_cap) / (1024.0L * 1024.0L * 1024.0L)) / 1000000.0L)
                  << " million entries before eviction\n";
    }
    std::cerr << "Notes: STL allocator overhead and OS page cache are not part of the managed-cache estimate; "
                 "the RSS guard is the hard runtime backstop. Auto mode was tuned from observed n=8 logs: "
                 "high duplicate-state pressure, tiny compatibility-miss rate, and root-task imbalance.\n\n";
}


static std::uint64_t parse_prefix_weight_line(const std::string& line) {
    std::size_t p = line.find("\"weight\"");
    if (p == std::string::npos) return 1;
    p = line.find(':', p);
    if (p == std::string::npos) return 1;
    ++p;
    while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) ++p;
    std::size_t q = p;
    while (q < line.size() && std::isdigit(static_cast<unsigned char>(line[q]))) ++q;
    if (q <= p) return 1;
    try { return static_cast<std::uint64_t>(std::stoull(line.substr(p, q-p))); }
    catch (...) { return 1; }
}

static std::vector<std::vector<int>> load_prefix_task_file(const std::filesystem::path& path,
                                                           std::size_t shard_index,
                                                           std::size_t shard_count) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot read prefix task file: " + path.string());

    struct PrefixTaskLine {
        std::vector<int> chosen;
        std::uint64_t weight = 1;
        std::size_t original = 0;
    };
    std::vector<PrefixTaskLine> all;
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        PrefixTaskLine t;
        t.chosen = SearchEngine::parse_prefix_chosen_line(line);
        t.weight = std::max<std::uint64_t>(1, parse_prefix_weight_line(line));
        t.original = line_no++;
        all.push_back(std::move(t));
    }

    if (shard_count <= 1) {
        std::vector<std::vector<int>> tasks;
        tasks.reserve(all.size());
        for (auto& t : all) tasks.push_back(std::move(t.chosen));
        return tasks;
    }

    // Weighted greedy bin packing gives much better balance than line_no % N
    // because prefix subtrees can differ by orders of magnitude.
    std::vector<std::size_t> order(all.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (all[a].weight != all[b].weight) return all[a].weight > all[b].weight;
        return all[a].original < all[b].original;
    });
    std::vector<long double> bin_weight(shard_count, 0.0L);
    std::vector<std::vector<std::size_t>> bins(shard_count);
    for (std::size_t idx : order) {
        std::size_t best = 0;
        for (std::size_t b = 1; b < shard_count; ++b) {
            if (bin_weight[b] < bin_weight[best]) best = b;
        }
        bins[best].push_back(idx);
        bin_weight[best] += static_cast<long double>(all[idx].weight);
    }

    std::vector<std::vector<int>> tasks;
    for (std::size_t idx : bins[shard_index]) {
        tasks.push_back(std::move(all[idx].chosen));
    }
    return tasks;
}

static bool run_one_validation(int n, const Cli& cli) {
    SearchOptions opt = cli.opt;
    opt.quiet = cli.opt.quiet;
    LoadedData data = load_orbits_for_n(n, cli.data_dir, opt.volume_mode);
    ImageSet is(std::move(data), !opt.quiet, opt.threads);
    if (opt.compat_mem_rows == 0 && n >= 7) {
        std::size_t row_bytes = ((is.images.size() + 63) / 64) * sizeof(uint64_t);
        opt.compat_mem_rows = std::max<std::size_t>(1, (static_cast<std::size_t>(opt.compat_mem_budget_mb) * 1024ULL * 1024ULL) / std::max<std::size_t>(row_bytes,1));
    }
    return validate_all(is, opt, !opt.quiet);
}


struct GatherResult {
    std::string name;
    std::string gather_cache_mode = "shared";
    std::string gather_matrix = "full";
    std::size_t experiment_index = 0;
    SearchOptions opt;
    double elapsed_sec = 0.0;
    double user_sec = 0.0;
    std::size_t states = 0;
    std::size_t solutions = 0;
    std::size_t solution_candidates = 0;
    std::size_t duplicate_states = 0;
    std::size_t local_dup_hits = 0;
    std::size_t compat_hits = 0;
    std::size_t compat_misses = 0;
    std::size_t compat_evictions = 0;
    std::size_t state_cache_size = 0;
    std::size_t state_cache_evictions = 0;
    std::size_t volume_prunes = 0;
    std::size_t coverage_prunes = 0;
    std::size_t forced_cells = 0;
    std::size_t tasks_created = 0;
    std::size_t tasks_completed = 0;
    std::size_t tasks_donated = 0;
    std::size_t dynamic_splits = 0;
    std::size_t peak_active_workers = 0;
    std::size_t rss_mb = 0;
    std::uint64_t time_compat_ns = 0;
    std::uint64_t time_canon_ns = 0;
};

#if defined(__linux__)
static double process_cpu_seconds() {
    std::ifstream in("/proc/self/stat");
    std::string s;
    if (!std::getline(in, s)) return 0.0;
    // /proc/self/stat has fields with comm in parentheses.  Parse after the last ')'.
    std::size_t rparen = s.rfind(')');
    if (rparen == std::string::npos || rparen + 2 >= s.size()) return 0.0;
    std::string rest = s.substr(rparen + 2);
    std::istringstream iss(rest);
    std::vector<std::string> fields;
    std::string tok;
    while (iss >> tok) fields.push_back(tok);
    // rest begins at field 3. utime is field 14, stime is field 15 => indices 11 and 12.
    if (fields.size() <= 12) return 0.0;
    long ticks = sysconf(_SC_CLK_TCK);
    if (ticks <= 0) ticks = 100;
    unsigned long long ut = 0, st = 0;
    try {
        ut = std::stoull(fields[11]);
        st = std::stoull(fields[12]);
    } catch (...) { return 0.0; }
    return static_cast<double>(ut + st) / static_cast<double>(ticks);
}
#else
static double process_cpu_seconds() { return 0.0; }
#endif

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

static GatherResult run_gather_experiment(ImageSet& is, const SearchOptions& base, const std::string& name) {
    GatherResult gr;
    gr.name = name;
    gr.opt = base;
    gr.opt.count_only = true;
    gr.opt.quiet = true;
    gr.opt.progress_interval = 0;
    gr.opt.solution_dedup = "memory";
    gr.opt.async_output = false; // count-only core-search benchmark; output can be benchmarked separately
    std::ostringstream sink;

    double cpu0 = process_cpu_seconds();
    auto t0 = std::chrono::steady_clock::now();
    SearchEngine engine(is, gr.opt);
    engine.run(sink);
    auto t1 = std::chrono::steady_clock::now();
    double cpu1 = process_cpu_seconds();

    gr.elapsed_sec = std::chrono::duration<double>(t1 - t0).count();
    gr.user_sec = std::max(0.0, cpu1 - cpu0);
    gr.states = engine.stats.states.load(std::memory_order_relaxed);
    gr.solutions = engine.stats.solutions.load(std::memory_order_relaxed);
    gr.solution_candidates = engine.stats.solution_candidates.load(std::memory_order_relaxed);
    gr.duplicate_states = engine.stats.duplicate_states.load(std::memory_order_relaxed);
    gr.local_dup_hits = engine.stats.local_duplicate_hits.load(std::memory_order_relaxed);
    gr.compat_hits = engine.compat.hits.load(std::memory_order_relaxed);
    gr.compat_misses = engine.compat.misses.load(std::memory_order_relaxed);
    gr.compat_evictions = engine.compat.evictions.load(std::memory_order_relaxed);
    gr.state_cache_size = engine.state_cache_size();
    gr.state_cache_evictions = engine.stats.state_cache_evictions.load(std::memory_order_relaxed);
    gr.volume_prunes = engine.stats.volume_prunes.load(std::memory_order_relaxed);
    gr.coverage_prunes = engine.stats.coverage_prunes.load(std::memory_order_relaxed);
    gr.forced_cells = engine.stats.forced_cells.load(std::memory_order_relaxed);
    gr.tasks_created = engine.stats.tasks_created.load(std::memory_order_relaxed);
    gr.tasks_completed = engine.stats.tasks_completed.load(std::memory_order_relaxed);
    gr.tasks_donated = engine.stats.tasks_donated.load(std::memory_order_relaxed);
    gr.dynamic_splits = engine.stats.dynamic_splits.load(std::memory_order_relaxed);
    gr.peak_active_workers = engine.stats.peak_active_workers.load(std::memory_order_relaxed);
    gr.rss_mb = current_rss_mb();
    gr.time_compat_ns = engine.stats.time_compat_ns.load(std::memory_order_relaxed);
    gr.time_canon_ns = engine.stats.time_canonical_ns.load(std::memory_order_relaxed);
    return gr;
}

static void write_gather_result(std::ofstream& out, const GatherResult& r, const ImageSet& is) {
    long double attempted = static_cast<long double>(r.states + r.duplicate_states);
    long double dup_ratio = attempted > 0 ? static_cast<long double>(r.duplicate_states) / attempted : 0.0L;
    long double compat_total = static_cast<long double>(r.compat_hits + r.compat_misses);
    long double compat_miss_rate = compat_total > 0 ? static_cast<long double>(r.compat_misses) / compat_total : 0.0L;
    long double sol_per_sec = r.elapsed_sec > 0 ? static_cast<long double>(r.solutions) / r.elapsed_sec : 0.0L;
    long double states_per_sec = r.elapsed_sec > 0 ? static_cast<long double>(r.states) / r.elapsed_sec : 0.0L;
    long double effective_cores = r.elapsed_sec > 0 ? static_cast<long double>(r.user_sec) / r.elapsed_sec : 0.0L;
    out << "{"
        << "\"name\":\"" << json_escape(r.name) << "\""
        << ",\"experiment_index\":" << r.experiment_index
        << ",\"gather_cache_mode\":\"" << json_escape(r.gather_cache_mode) << "\""
        << ",\"gather_matrix\":\"" << json_escape(r.gather_matrix) << "\""
        << ",\"n\":" << is.hs.n
        << ",\"images\":" << is.images.size()
        << ",\"threads\":" << r.opt.threads
        << ",\"scheduler\":\"" << json_escape(r.opt.scheduler) << "\""
        << ",\"parallel_depth\":" << r.opt.parallel_depth
        << ",\"prefix_max_depth\":" << r.opt.prefix_max_depth
        << ",\"split_max_depth\":" << r.opt.split_max_depth
        << ",\"split_min_candidates\":" << r.opt.split_min_candidates
        << ",\"task_target_per_thread\":" << r.opt.task_target_per_thread
        << ",\"donate_when_active_below\":" << std::fixed << std::setprecision(3) << r.opt.donate_when_active_below
        << ",\"compat_cache_policy\":\"" << json_escape(r.opt.compat_cache_policy) << "\""
        << ",\"affinity\":\"" << json_escape(r.opt.affinity) << "\""
        << ",\"numa_policy\":\"" << json_escape(r.opt.numa_policy) << "\""
        << ",\"bitset_kernel\":\"" << json_escape(r.opt.bitset_kernel) << "\""
        << ",\"state_key_format\":\"" << json_escape(r.opt.state_key_format) << "\""
        << ",\"state_cache_scope\":\"" << json_escape(r.opt.state_cache_scope) << "\""
        << ",\"worker_compat_cache_size\":" << r.opt.worker_compat_cache_size
        << ",\"prewarm_hot_compat_limit\":" << r.opt.prewarm_hot_compat_limit
        << ",\"prewarm_hot_compat_rows\":\"" << json_escape(r.opt.prewarm_hot_compat_rows.empty() ? std::string("") : r.opt.prewarm_hot_compat_rows.string()) << "\""
        << ",\"compat_mem_budget_mb\":" << r.opt.compat_mem_budget_mb
        << ",\"state_cache_mode\":\"" << json_escape(r.opt.state_cache_mode) << "\""
        << ",\"state_cache_mb\":" << r.opt.state_cache_mb
        << ",\"duplicate_local_cache_max\":" << r.opt.duplicate_local_cache_max
        << ",\"labeled_state_cache_max\":" << r.opt.labeled_state_cache_max
        << ",\"force_propagation\":" << (r.opt.force_propagation ? "true" : "false")
        << ",\"deep_cover_dp\":" << (r.opt.deep_cover_dp ? "true" : "false")
        << ",\"elapsed_sec\":" << std::fixed << std::setprecision(6) << r.elapsed_sec
        << ",\"cpu_sec\":" << std::fixed << std::setprecision(6) << r.user_sec
        << ",\"effective_cores\":" << std::fixed << std::setprecision(3) << effective_cores
        << ",\"states\":" << r.states
        << ",\"solutions\":" << r.solutions
        << ",\"solution_candidates\":" << r.solution_candidates
        << ",\"duplicate_states\":" << r.duplicate_states
        << ",\"duplicate_ratio\":" << std::fixed << std::setprecision(5) << dup_ratio
        << ",\"states_per_sec\":" << std::fixed << std::setprecision(3) << states_per_sec
        << ",\"solutions_per_sec\":" << std::fixed << std::setprecision(3) << sol_per_sec
        << ",\"local_dup_hits\":" << r.local_dup_hits
        << ",\"compat_hits\":" << r.compat_hits
        << ",\"compat_misses\":" << r.compat_misses
        << ",\"compat_miss_rate\":" << std::fixed << std::setprecision(6) << compat_miss_rate
        << ",\"compat_evictions\":" << r.compat_evictions
        << ",\"state_cache_size\":" << r.state_cache_size
        << ",\"state_cache_evictions\":" << r.state_cache_evictions
        << ",\"volume_prunes\":" << r.volume_prunes
        << ",\"coverage_prunes\":" << r.coverage_prunes
        << ",\"forced_cells\":" << r.forced_cells
        << ",\"tasks_created\":" << r.tasks_created
        << ",\"tasks_completed\":" << r.tasks_completed
        << ",\"tasks_donated\":" << r.tasks_donated
        << ",\"dynamic_splits\":" << r.dynamic_splits
        << ",\"peak_active_workers\":" << r.peak_active_workers
        << ",\"rss_mb\":" << r.rss_mb
        << ",\"time_compat_sec\":" << std::fixed << std::setprecision(6) << (static_cast<double>(r.time_compat_ns) / 1e9)
        << ",\"time_canon_sec\":" << std::fixed << std::setprecision(6) << (static_cast<double>(r.time_canon_ns) / 1e9)
        << "}\n";
}

static void run_gather_data(Cli cli, ImageSet& is) {
    SearchOptions base = cli.opt;
    base.auto_tune = false; // parameters are already resolved by apply_auto_after_images
    base.generation_mode = "state-cache";
    base.search_mode = "quotient";
    base.quotient = true;
    base.count_only = true;
    base.quiet = true;
    base.progress_interval = 0;
    base.max_solutions = cli.gather_solutions;
    base.max_states = cli.gather_states;
    if (base.max_solutions == 0 && base.max_states == 0) base.max_solutions = 10000;
    if (base.state_cache_mode == "auto") base.state_cache_mode = (is.hs.n <= 7 ? "full" : "bounded");
    if (base.compat_cache_policy == "auto") base.compat_cache_policy = "lru";
    base.solution_dedup = "memory";
    base.async_output = false;
    base.parallel_depth = 1;
    base.prefix_task_target = 0;
    base.labeled_state_cache_max = 0;
    base.force_propagation = true;

    SystemInfo sys = detect_system_info();
    std::size_t logical = std::max<std::size_t>(1, sys.logical_cpus);
    std::size_t phys = sys.physical_cores ? sys.physical_cores : logical;
    bool likely_smt = (sys.physical_cores > 0 && logical >= phys * 3 / 2);
    std::size_t physical_threads = std::min<std::size_t>(logical, phys);
    std::size_t throughput_threads = likely_smt
        ? std::min<std::size_t>(logical > 32 ? logical - 32 : logical,
                                std::min<std::size_t>(logical, std::max<std::size_t>(phys, (phys * 5 + 2) / 3)))
        : logical;
    std::size_t balanced_threads = likely_smt
        ? std::min<std::size_t>(logical > 48 ? logical - 48 : logical,
                                std::min<std::size_t>(logical, std::max<std::size_t>(phys, (phys * 3 + 1) / 2)))
        : physical_threads;
    std::size_t interactive_threads = likely_smt ? physical_threads :
                                      ((phys >= 64) ? std::max<std::size_t>(1, phys - 16) :
                                       (phys >= 32) ? std::max<std::size_t>(1, phys - 8) :
                                                      std::max<std::size_t>(1, phys / 2));
    if (is.hs.n < 8) throughput_threads = balanced_threads = interactive_threads = physical_threads;

    std::filesystem::path profiled_hot_rows = std::filesystem::current_path() / "perf_hot_compat_rows.txt";

    std::vector<std::pair<std::string, SearchOptions>> experiments;
    std::set<std::string> names_seen;
    auto normalized = [&](SearchOptions opt) {
        opt.count_only = true;
        opt.quiet = true;
        opt.progress_interval = 0;
        opt.max_solutions = base.max_solutions;
        opt.max_states = base.max_states;
        opt.solution_dedup = "memory";
        opt.async_output = false;
        opt.prefix_task_target = 0;
        opt.parallel_depth = std::max(1, opt.parallel_depth);
        opt.split_max_depth = std::max(opt.parallel_depth, opt.split_max_depth);
        if (opt.compat_cache_policy == "auto") opt.compat_cache_policy = "lru";
        if (opt.scheduler == "auto") opt.scheduler = "static";
        return opt;
    };
    auto add = [&](std::string name, SearchOptions opt) {
        if (!names_seen.insert(name).second) return;
        experiments.emplace_back(std::move(name), normalized(std::move(opt)));
    };

    auto fast_static = base;
    fast_static.scheduler = "static";
    fast_static.compat_cache_policy = "lru";
    fast_static.parallel_depth = 1;
    fast_static.split_max_depth = 4;
    fast_static.split_min_candidates = 16;
    fast_static.task_target_per_thread = 64;
    fast_static.duplicate_local_cache_max = 65536;
    fast_static.labeled_state_cache_max = 0;

    auto fast_hybrid = fast_static;
    fast_hybrid.scheduler = "static-hybrid";
    fast_hybrid.donate_when_active_below = 0.75;

    auto with_threads = [](SearchOptions opt, std::size_t t) {
        opt.threads = std::max<std::size_t>(1, t);
        return opt;
    };

    // Core matrix: these are the profiles most relevant to the latest Threadripper data.
    // Keep the legacy 96-thread static/LRU baseline visible, but make the tuned
    // balanced/throughput profiles reflect the measured 144/160-thread static-hybrid winners.
    add("baseline_static_lru_depth1", with_threads(fast_static, physical_threads));
    add("balanced_static_hybrid_lru", with_threads(fast_hybrid, balanced_threads));
    add("throughput_static_hybrid_lru", with_threads(fast_hybrid, throughput_threads));
    add("throughput_static_lru_reference", with_threads(fast_static, throughput_threads));
    add("interactive_static_hybrid_lru", with_threads(fast_hybrid, interactive_threads));

    SearchOptions o = fast_static;
    o.compat_cache_policy = "lru-notouch";
    add("static_lru_notouch_depth1", with_threads(o, physical_threads));

    o = fast_static;
    o.compat_cache_policy = "direct";
    add("static_direct_depth1", with_threads(o, physical_threads));

    o = fast_hybrid;
    o.compat_cache_policy = "lru-notouch";
    add("static_hybrid_lru_notouch", with_threads(o, physical_threads));

    o = fast_hybrid;
    o.compat_cache_policy = "direct";
    add("static_hybrid_direct", with_threads(o, physical_threads));

    o = fast_static;
    o.duplicate_local_cache_max = 0;
    add("local_duplicate_cache_off", with_threads(o, physical_threads));

    o = fast_static;
    o.duplicate_local_cache_max = 65536;
    add("local_duplicate_cache_65536", with_threads(o, physical_threads));

    if (cli.gather_matrix == "core" || cli.gather_matrix == "full") {
        // Reference anti-patterns retained to keep future regressions visible.
        o = fast_static;
        o.parallel_depth = 2;
        o.prefix_max_depth = std::max(o.prefix_max_depth, 2);
        add("static_lru_depth2_reference", with_threads(o, physical_threads));

        o = fast_static;
        o.scheduler = "work-steal";
        o.task_target_per_thread = 64;
        o.split_max_depth = 4;
        o.split_min_candidates = 16;
        add("work_steal_reference", with_threads(o, physical_threads));

        // Thread sweeps.  The 192-logical/96-physical system showed that 128
        // threads can win time-to-10K, so include modest SMT oversubscription.
        std::set<std::size_t> thread_tests;
        for (std::size_t t : {std::size_t{32}, std::size_t{64}, std::size_t{80}, std::size_t{96}, std::size_t{112}, std::size_t{128}, std::size_t{144}, std::size_t{152}, std::size_t{160}, std::size_t{168}, std::size_t{176}}) {
            if (t >= 1 && t <= std::max<std::size_t>(logical, throughput_threads) && t <= logical) thread_tests.insert(t);
        }
        thread_tests.insert(balanced_threads);
        thread_tests.insert(throughput_threads);
        thread_tests.insert(interactive_threads);
        for (std::size_t t : thread_tests) {
            add("threads_" + std::to_string(t) + "_static_lru", with_threads(fast_static, t));

            // New v2 probes: lru-notouch and direct have lower compatibility time in
            // some warm-cache runs, but only a fair matrix tells whether they improve
            // complete time-to-K on a given machine.
            o = fast_static;
            o.compat_cache_policy = "lru-notouch";
            add("threads_" + std::to_string(t) + "_static_lru_notouch", with_threads(o, t));

            o = fast_static;
            o.compat_cache_policy = "direct";
            add("threads_" + std::to_string(t) + "_static_direct", with_threads(o, t));
        }

        // Hybrid donation variants at key thread counts.  v2 showed 96-thread
        // static-hybrid+lru-notouch can beat plain 96-thread static for time-to-10K,
        // while short state probes suggest 128/160 should be measured explicitly.
        std::set<std::size_t> hybrid_thread_tests{balanced_threads, throughput_threads};
        if (likely_smt) {
            hybrid_thread_tests.insert(std::min<std::size_t>(logical, phys + phys / 2)); // 144 on a 96c/192t box
            hybrid_thread_tests.insert(std::min<std::size_t>(logical, (phys * 5 + 2) / 3)); // 160 on 96c/192t
        }
        for (std::size_t t : hybrid_thread_tests) {
            add("threads_" + std::to_string(t) + "_static_hybrid_lru", with_threads(fast_hybrid, t));

            o = fast_hybrid;
            o.compat_cache_policy = "lru-notouch";
            add("threads_" + std::to_string(t) + "_static_hybrid_lru_notouch", with_threads(o, t));

            o = fast_hybrid;
            o.compat_cache_policy = "direct";
            add("threads_" + std::to_string(t) + "_static_hybrid_direct", with_threads(o, t));
        }

        // High-priority combination probes from the latest cache-affinity data.
        // These are narrow tests around the measured winners, not broad rewrites.
        auto add_high_priority_hybrid = [&](std::size_t t, const std::string& label) {
            add(label + "_static_hybrid_lru", with_threads(fast_hybrid, t));

            o = fast_hybrid;
            o.affinity = "numa";
            add(label + "_static_hybrid_lru_affinity_numa", with_threads(o, t));

            o = fast_hybrid;
            o.worker_compat_cache_size = 64;
            add(label + "_static_hybrid_lru_worker_cache_64", with_threads(o, t));

            o = fast_hybrid;
            o.affinity = "numa";
            o.worker_compat_cache_size = 64;
            add(label + "_static_hybrid_lru_affinity_numa_worker_cache_64", with_threads(o, t));
        };

        add_high_priority_hybrid(throughput_threads, "threads_" + std::to_string(throughput_threads));
        add_high_priority_hybrid(balanced_threads, "threads_" + std::to_string(balanced_threads));
        add_high_priority_hybrid(interactive_threads, "threads_" + std::to_string(interactive_threads));

        if (likely_smt) {
            std::size_t t128 = std::min<std::size_t>(logical, std::max<std::size_t>(phys, (phys * 4 + 2) / 3));
            o = fast_hybrid;
            o.compat_cache_policy = "direct";
            o.worker_compat_cache_size = 64;
            add("threads_" + std::to_string(t128) + "_static_hybrid_direct_worker_cache_64", with_threads(o, t128));
        }

        // Profile-guided hot-row prewarm variants.  In warm-profiled gather mode,
        // the warmup records perf_hot_compat_rows.txt before these experiments run.
        // In shared mode, these rows are active only if a previous hot-row file exists.
        o = fast_hybrid;
        o.prewarm_hot_compat_rows = profiled_hot_rows;
        o.prewarm_hot_compat_limit = 512;
        add("threads_" + std::to_string(throughput_threads) + "_static_hybrid_lru_hot_prewarm_512", with_threads(o, throughput_threads));

        o = fast_hybrid;
        o.prewarm_hot_compat_rows = profiled_hot_rows;
        o.prewarm_hot_compat_limit = 2048;
        add("threads_" + std::to_string(throughput_threads) + "_static_hybrid_lru_hot_prewarm_2048", with_threads(o, throughput_threads));

        // v3 winner and close-combination retests.  The 2026-04-27 data found
        // throughput_threads + static-hybrid + LRU + worker cache 64 to be the
        // fastest time-to-10K row; keep it explicit and repeat it to control for
        // cache-order bias.
        o = fast_hybrid;
        o.worker_compat_cache_size = 64;
        o.prewarm_hot_compat_rows = profiled_hot_rows;
        o.prewarm_hot_compat_limit = 512;
        add("throughput_best_v3_static_hybrid_lru_worker_cache64_prewarm512", with_threads(o, throughput_threads));

        o = fast_hybrid;
        o.worker_compat_cache_size = 64;
        o.prewarm_hot_compat_rows = profiled_hot_rows;
        o.prewarm_hot_compat_limit = 0;
        add("throughput_worker_cache64_no_hot_prewarm", with_threads(o, throughput_threads));

        o = fast_hybrid;
        o.compat_cache_policy = "lru-notouch";
        o.worker_compat_cache_size = 64;
        o.prewarm_hot_compat_rows = profiled_hot_rows;
        o.prewarm_hot_compat_limit = 512;
        add("throughput_lru_notouch_worker_cache64_prewarm512", with_threads(o, throughput_threads));

        o = fast_hybrid;
        o.compat_cache_policy = "direct";
        o.worker_compat_cache_size = 64;
        o.prewarm_hot_compat_rows = profiled_hot_rows;
        o.prewarm_hot_compat_limit = 512;
        add("throughput_direct_worker_cache64_prewarm512", with_threads(o, throughput_threads));

        // v4 focused retests from the 2026-04-27 86-experiment warm-profiled run.
        // The best rows were 160-thread static-hybrid with worker cache 64,
        // using either LRU or direct compatibility policy.  Repeat these near the
        // end of the matrix as well so cache-order bias is visible.
        o = fast_hybrid;
        o.worker_compat_cache_size = 64;
        o.prewarm_hot_compat_rows = profiled_hot_rows;
        o.prewarm_hot_compat_limit = 512;
        add("v4_best_threads_" + std::to_string(throughput_threads) + "_hybrid_lru_worker64_prewarm512", with_threads(o, throughput_threads));

        o = fast_hybrid;
        o.compat_cache_policy = "direct";
        o.worker_compat_cache_size = 64;
        o.prewarm_hot_compat_rows = profiled_hot_rows;
        o.prewarm_hot_compat_limit = 512;
        add("v4_near_best_threads_" + std::to_string(throughput_threads) + "_hybrid_direct_worker64_prewarm512", with_threads(o, throughput_threads));

        o = fast_hybrid;
        o.worker_compat_cache_size = 64;
        o.prewarm_hot_compat_rows = profiled_hot_rows;
        o.prewarm_hot_compat_limit = 2048;
        add("v4_threads_" + std::to_string(throughput_threads) + "_hybrid_lru_worker64_prewarm2048", with_threads(o, throughput_threads));

        o = fast_hybrid;
        o.worker_compat_cache_size = 128;
        o.prewarm_hot_compat_rows = profiled_hot_rows;
        o.prewarm_hot_compat_limit = 512;
        add("v4_threads_" + std::to_string(throughput_threads) + "_hybrid_lru_worker128_prewarm512", with_threads(o, throughput_threads));
    }

    if (cli.gather_matrix == "full") {
        // Donation variants: keep static traversal, but vary late donation thresholds.
        for (double frac : {0.60, 0.70, 0.80, 0.85, 0.90}) {
            o = fast_hybrid;
            o.donate_when_active_below = frac;
            std::ostringstream nm;
            nm << "static_hybrid_donate_" << std::fixed << std::setprecision(2) << frac;
            std::string name = nm.str();
            std::replace(name.begin(), name.end(), '.', '_');
            add(name, with_threads(o, balanced_threads));
        }
        for (std::size_t mc : {std::size_t{8}, std::size_t{12}, std::size_t{16}, std::size_t{24}, std::size_t{32}, std::size_t{64}}) {
            o = fast_hybrid;
            o.split_min_candidates = mc;
            add("static_hybrid_min_cand_" + std::to_string(mc), with_threads(o, balanced_threads));
        }
        for (int sd : {3,4,5}) {
            o = fast_hybrid;
            o.split_max_depth = sd;
            add("static_hybrid_split_depth_" + std::to_string(sd), with_threads(o, balanced_threads));
        }


        // Cache-affinity and low-level data-layout experiments.  These are opt-in
        // benchmark candidates; none changes the mathematical enumeration.
        for (const std::string& aff : {std::string("none"), std::string("compact"), std::string("spread"), std::string("numa")}) {
            o = fast_hybrid;
            o.affinity = aff;
            add("affinity_" + aff + "_static_hybrid", with_threads(o, balanced_threads));
        }

        for (const std::string& kernel : {std::string("auto"), std::string("scalar"), std::string("unrolled"), std::string("avx2"), std::string("avx512")}) {
            o = fast_hybrid;
            o.bitset_kernel = kernel;
            add("bitset_" + kernel + "_static_hybrid", with_threads(o, balanced_threads));
        }

        for (const std::string& keyfmt : {std::string("binary"), std::string("hex")}) {
            o = fast_hybrid;
            o.state_key_format = keyfmt;
            add("state_key_" + keyfmt + "_static_hybrid", with_threads(o, balanced_threads));
        }

        for (std::size_t wc : {std::size_t{0}, std::size_t{16}, std::size_t{64}}) {
            o = fast_hybrid;
            o.worker_compat_cache_size = wc;
            add("worker_compat_cache_" + std::to_string(wc) + "_static_hybrid", with_threads(o, balanced_threads));
        }

        for (const std::string& scope : {std::string("global"), std::string("thread-local")}) {
            o = fast_hybrid;
            o.state_cache_scope = scope;
            add("state_cache_scope_" + scope + "_static_hybrid", with_threads(o, balanced_threads));
        }

        o = fast_static;
        o.labeled_state_cache_max = 1000000;
        add("labeled_state_prefilter_1M_reference", with_threads(o, balanced_threads));

        // Warm/repeat sentinels.  In shared-cache mode these make cache-order effects visible.
        add("baseline_static_lru_depth1_repeat_end", with_threads(fast_static, balanced_threads));
        add("balanced_static_hybrid_lru_repeat_end", with_threads(fast_hybrid, balanced_threads));

        o = fast_hybrid;
        o.worker_compat_cache_size = 64;
        o.prewarm_hot_compat_rows = profiled_hot_rows;
        o.prewarm_hot_compat_limit = 512;
        add("v4_best_repeat_end_threads_" + std::to_string(throughput_threads) + "_hybrid_lru_worker64_prewarm512", with_threads(o, throughput_threads));

        o = fast_static;
        o.compat_cache_policy = "direct";
        add("static_direct_depth1_repeat_end", with_threads(o, balanced_threads));
    }

    if (cli.gather_randomize_order) {
        std::mt19937_64 rng(0xA13E3E77C0FFEEULL);
        std::shuffle(experiments.begin(), experiments.end(), rng);
    }

    std::filesystem::path path = std::filesystem::current_path() / "perf_data.txt";
    std::ofstream out(path, std::ios::app);
    if (!out) throw std::runtime_error("Cannot write perf_data.txt in current directory.");

    auto now = std::chrono::system_clock::now();
    std::time_t now_t = std::chrono::system_clock::to_time_t(now);
    out << "# alexeev_r3_cpp gather-data run " << std::put_time(std::localtime(&now_t), "%F %T") << "\n";
    out << "# Each following line is JSON.  The run is exact but truncated by max_solutions/max_states for benchmarking.\n";
    out << "# n=" << is.hs.n << " images=" << is.images.size()
        << " max_solutions=" << base.max_solutions
        << " max_states=" << base.max_states
        << " experiments=" << experiments.size()
        << " matrix=" << cli.gather_matrix
        << " cache_mode=" << cli.gather_cache_mode
        << " randomize_order=" << (cli.gather_randomize_order ? "true" : "false") << "\n";
    out.flush();

    std::cerr << "gather-data: writing " << experiments.size()
              << " benchmark records to " << path << "\n";
    std::cerr << "gather-data: matrix=" << cli.gather_matrix
              << " cache_mode=" << cli.gather_cache_mode
              << " per_experiment max_solutions=" << base.max_solutions
              << " max_states=" << base.max_states << "\n";

    std::filesystem::path original_cache = base.cache_dir;
    std::filesystem::path warm_cache_dir = std::filesystem::current_path() / ".alexeev_gather_warm_cache";

    if (cli.gather_cache_mode == "warm" || cli.gather_cache_mode == "warm-profiled") {
        std::error_code ec;
        std::filesystem::remove_all(warm_cache_dir, ec);
        std::filesystem::create_directories(warm_cache_dir, ec);

        SearchOptions warm = with_threads(fast_static, balanced_threads);
        warm.cache_dir = warm_cache_dir;
        warm.max_solutions = cli.gather_warm_solutions;
        warm.max_states = 0;
        warm.count_only = true;
        warm.quiet = true;
        warm.progress_interval = 0;
        warm.solution_dedup = "memory";
        warm.async_output = false;
        if (cli.gather_cache_mode == "warm-profiled") {
            warm.record_hot_compat_rows = profiled_hot_rows;
        }

        std::cerr << "gather-data warmup: " << cli.gather_warm_solutions
                  << " solutions into " << warm_cache_dir << "\n";
        GatherResult wr = run_gather_experiment(is, warm, "warmup_static_lru");
        wr.gather_cache_mode = "warmup";
        wr.gather_matrix = cli.gather_matrix;
        wr.experiment_index = 0;
        write_gather_result(out, wr, is);
        out.flush();
    }

    std::size_t k = 0;
    for (auto [name, opt] : experiments) {
        ++k;
        if (cli.gather_cache_mode == "reset") {
            // Cold-cache fairness: disable disk compatibility rows for the experiment.
            // Each SearchEngine still builds its own exact in-memory cache.
            opt.cache_dir = std::filesystem::path{};
        } else if (cli.gather_cache_mode == "warm" || cli.gather_cache_mode == "warm-profiled") {
            opt.cache_dir = warm_cache_dir;
            if (cli.gather_cache_mode == "warm-profiled") {
                opt.prewarm_hot_compat_rows = profiled_hot_rows;
                opt.prewarm_hot_compat_limit = opt.prewarm_hot_compat_limit ? opt.prewarm_hot_compat_limit : 512;
            }
        } else {
            opt.cache_dir = original_cache;
        }

        std::cerr << "gather-data [" << k << "/" << experiments.size() << "] " << name << " ...\n";
        GatherResult r = run_gather_experiment(is, opt, name);
        r.gather_cache_mode = cli.gather_cache_mode;
        r.gather_matrix = cli.gather_matrix;
        r.experiment_index = k;
        write_gather_result(out, r, is);
        out.flush();
        std::cerr << "  done: elapsed=" << std::fixed << std::setprecision(3) << r.elapsed_sec
                  << "s solutions=" << r.solutions
                  << " states=" << r.states
                  << " dup=" << r.duplicate_states
                  << " eff_cores=" << (r.elapsed_sec > 0 ? r.user_sec / r.elapsed_sec : 0.0)
                  << " compat_misses=" << r.compat_misses
                  << "\n";
    }
    out << "# end gather-data\n";
    out.close();
    std::cerr << "gather-data complete: " << path << "\n";
}



// -----------------------------------------------------------------------------
// n=9 wide-path gather-data support
// -----------------------------------------------------------------------------
//
// The n=9 engine is deliberately isolated from the measured-fast n<=8 engine.
// It uses Mask128 cell vertex masks and a conservative root-parallel search.  The
// purpose of the gather-data path here is to let users collect the same kind of
// machine-specific evidence that drove the n=8 auto profiles, without pretending
// that the n=8 scheduler/cache choices have already been validated for n=9.

static void apply_n9_auto_pre_run(Cli& cli) {
    if (!cli.opt.auto_tune) return;

    SystemInfo sys = detect_system_info();
    std::size_t total = sys.mem_total_mb ? sys.mem_total_mb : 65536;
    std::size_t available = sys.mem_available_mb ? sys.mem_available_mb : total;

    if (!cli.memory_limit_explicit) {
        // n=9 may allocate far larger image/compatibility structures than n=8.
        // Keep a conservative guard and let users raise it explicitly.
        std::size_t reserve = std::max<std::size_t>(8192, total / 6);
        if (total >= 384ULL * 1024ULL) reserve = std::max<std::size_t>(96ULL * 1024ULL, total / 5);
        std::size_t guard_by_total = total > reserve ? total - reserve : total * 3 / 4;
        std::size_t guard_by_avail = static_cast<std::size_t>(static_cast<long double>(available) * 0.85L);
        cli.opt.memory_limit_mb = std::min<std::size_t>(guard_by_total, guard_by_avail ? guard_by_avail : guard_by_total);
    }
    if (cli.opt.memory_policy == "warn") cli.opt.memory_policy = "shrink";

    if (!cli.state_cache_explicit) {
        cli.opt.state_cache_mode = "bounded";
        if (total >= 384ULL * 1024ULL) {
            cli.opt.state_cache_mb = (cli.auto_profile == "memory-saver") ? 32768 : 65536;
        } else if (total >= 128ULL * 1024ULL) {
            cli.opt.state_cache_mb = (cli.auto_profile == "memory-saver") ? 8192 : 32768;
        } else if (total >= 64ULL * 1024ULL) {
            cli.opt.state_cache_mb = (cli.auto_profile == "memory-saver") ? 4096 : 12288;
        } else {
            cli.opt.state_cache_mb = 2048;
        }
    }

    if (!cli.compat_explicit) {
        if (total >= 384ULL * 1024ULL) cli.opt.compat_mem_budget_mb = 65536;
        else if (total >= 256ULL * 1024ULL) cli.opt.compat_mem_budget_mb = 32768;
        else if (total >= 128ULL * 1024ULL) cli.opt.compat_mem_budget_mb = 16384;
        else if (total >= 64ULL * 1024ULL) cli.opt.compat_mem_budget_mb = 8192;
        else cli.opt.compat_mem_budget_mb = 2048;
        if (cli.auto_profile == "memory-saver") cli.opt.compat_mem_budget_mb = std::max<std::size_t>(1024, cli.opt.compat_mem_budget_mb / 2);
    }

    if (!cli.threads_explicit) {
        std::size_t logical = std::max<std::size_t>(1, sys.logical_cpus);
        std::size_t phys = sys.physical_cores ? sys.physical_cores : logical;
        bool likely_smt = (sys.physical_cores > 0 && logical >= phys * 3 / 2);

        auto clamp_threads = [&](std::size_t t) {
            t = std::max<std::size_t>(1, t);
            return std::min<std::size_t>(logical, t);
        };

        // n=9 feedback, 2026-04-28:
        // On the 96 physical / 192 logical dual Threadripper system, the best
        // state-bounded wide-engine profile was root-static with 128 threads.
        // 144 threads was close but slightly slower; 160+ threads regressed, and
        // 192 was substantially worse.  Unlike n=8, the n=9 wide path is dominated
        // by huge image-index bitsets and compatibility rows, so modest SMT
        // oversubscription helps, but full logical-core saturation does not.
        if (cli.auto_profile == "memory-saver") {
            cli.opt.threads = clamp_threads(std::max<std::size_t>(1, phys / 2));
        } else if (cli.auto_profile == "interactive") {
            cli.opt.threads = clamp_threads(phys);
        } else if (likely_smt) {
            cli.opt.threads = clamp_threads((phys * 4 + 2) / 3); // 96 -> 128
        } else {
            cli.opt.threads = clamp_threads(phys);
        }
    }

    if (!cli.scheduler_explicit) {
        // The wide engine currently implements root-parallel static scheduling.
        // Keep the label "static" so perf_data.txt makes this limitation explicit.
        cli.opt.scheduler = "static";
    }
    if (!cli.compat_policy_explicit && cli.opt.compat_cache_policy == "auto") cli.opt.compat_cache_policy = "lru";

    // n=9 hot compatibility-row profiling.  A wide compatibility row is much
    // larger than an n=8 row, so we never blindly prewarm thousands of rows.
    // If a profile exists, use a small default top-128 prewarm; always record
    // row usage for the next run when a cache directory is available.
    if (cli.opt.auto_tune) {
        std::filesystem::path hot = default_hot_compat_rows_path(cli, 9);
        if (cli.opt.prewarm_hot_compat_rows.empty() && cli.opt.prewarm_hot_compat_limit == 0 && !hot.empty()) {
            cli.opt.prewarm_hot_compat_rows = hot;
            cli.opt.prewarm_hot_compat_limit = 128;
        }
        if (cli.opt.record_hot_compat_rows.empty()) {
            std::filesystem::path d = cli.opt.cache_dir.empty() ? cli.cache_dir : cli.opt.cache_dir;
            if (!d.empty()) cli.opt.record_hot_compat_rows = d / "hot_compat_rows_n9.txt";
        }
    }

    // Optional learned profile for n=9.  This uses n=9 records from perf_data.txt
    // and applies only parameters meaningful for the wide engine.
    if (cli.auto_profile == "learned" || cli.auto_learn_explicit) {
        std::filesystem::path learn_path = cli.auto_learn_path.empty()
            ? (std::filesystem::current_path() / "perf_data.txt")
            : cli.auto_learn_path;
        auto rec = select_learned_profile_from_file(learn_path, 9, 0, cli.opt.quiet);
        if (rec) {
            SystemInfo sys2 = detect_system_info();
            std::size_t logical2 = std::max<std::size_t>(1, sys2.logical_cpus);
            if (!cli.threads_explicit) cli.opt.threads = std::min<std::size_t>(std::max<std::size_t>(1, rec->threads), logical2);
            if (!cli.compat_explicit && rec->compat_mem_budget_mb > 0) cli.opt.compat_mem_budget_mb = rec->compat_mem_budget_mb;
            if (!cli.state_cache_explicit && rec->state_cache_mb > 0) {
                cli.opt.state_cache_mb = rec->state_cache_mb;
                cli.opt.state_cache_mode = "bounded";
            }
            cli.opt.force_propagation = rec->force_propagation;
            if (!cli.opt.quiet) {
                std::cerr << "[n=9 wide] auto-learn selected profile from " << learn_path
                          << ": name=" << rec->name
                          << " threads=" << cli.opt.threads
                          << " state_mb=" << cli.opt.state_cache_mb
                          << " compat_mb=" << cli.opt.compat_mem_budget_mb
                          << " sol/s=" << std::fixed << std::setprecision(3) << rec->solutions_per_sec
                          << " states/s=" << std::fixed << std::setprecision(3) << rec->states_per_sec << "\n";
            }
        }
    }

    if (!cli.duplicate_local_explicit) cli.opt.duplicate_local_cache_max = 65536;
    if (!cli.labeled_state_explicit) cli.opt.labeled_state_cache_max = 0;
    if (!cli.bitset_kernel_explicit) cli.opt.bitset_kernel = "auto";
    if (!cli.state_key_format_explicit) cli.opt.state_key_format = "binary";
    cli.opt.force_propagation = true;
    cli.opt.parallel_depth = 1;
    cli.opt.prefix_task_target = 0;
}

static void recompute_n9_cache_caps(SearchOptions& opt, const ImageSetWide& is) {
    if (opt.compat_mem_rows == 0 || opt.auto_tune) {
        std::size_t rb = dense_row_bytes(is.images.size());
        if (opt.compat_mem_budget_mb > 0) {
            opt.compat_mem_rows = std::max<std::size_t>(
                1, (opt.compat_mem_budget_mb * 1024ULL * 1024ULL) / std::max<std::size_t>(rb, 1));
        }
    }
    if (opt.state_cache_mode == "bounded" && opt.state_cache_mb > 0) {
        opt.state_cache_max = std::max<std::size_t>(
            1000, (opt.state_cache_mb * 1024ULL * 1024ULL) / 512ULL);
    }
}

static GatherResult run_gather_experiment_n9(ImageSetWide& is, const SearchOptions& base, const std::string& name) {
    GatherResult gr;
    gr.name = name;
    gr.opt = base;
    gr.opt.count_only = true;
    gr.opt.quiet = true;
    gr.opt.progress_interval = 0;
    gr.opt.solution_dedup = "memory";
    gr.opt.async_output = false;
    gr.opt.scheduler = "static"; // current wide engine scheduling semantics
    recompute_n9_cache_caps(gr.opt, is);

    std::ostringstream sink;
    double cpu0 = process_cpu_seconds();
    auto t0 = std::chrono::steady_clock::now();
    SearchEngineWide engine(is, gr.opt);
    engine.run(sink);
    auto t1 = std::chrono::steady_clock::now();
    double cpu1 = process_cpu_seconds();

    gr.elapsed_sec = std::chrono::duration<double>(t1 - t0).count();
    gr.user_sec = std::max(0.0, cpu1 - cpu0);
    gr.states = engine.states.load(std::memory_order_relaxed);
    gr.solutions = engine.solutions.load(std::memory_order_relaxed);
    gr.solution_candidates = engine.solutions.load(std::memory_order_relaxed);
    gr.duplicate_states = engine.duplicate_states.load(std::memory_order_relaxed);
    gr.compat_hits = engine.compat.hits.load(std::memory_order_relaxed);
    gr.compat_misses = engine.compat.misses.load(std::memory_order_relaxed);
    gr.state_cache_size = engine.state_cache.size();
    gr.volume_prunes = engine.volume_prunes.load(std::memory_order_relaxed);
    gr.coverage_prunes = engine.coverage_prunes.load(std::memory_order_relaxed);
    gr.forced_cells = engine.forced_cells.load(std::memory_order_relaxed);
    gr.rss_mb = current_rss_mb();
    return gr;
}

static void write_gather_result_n9(std::ofstream& out, const GatherResult& r, const ImageSetWide& is) {
    long double attempted = static_cast<long double>(r.states + r.duplicate_states);
    long double dup_ratio = attempted > 0 ? static_cast<long double>(r.duplicate_states) / attempted : 0.0L;
    long double compat_total = static_cast<long double>(r.compat_hits + r.compat_misses);
    long double compat_miss_rate = compat_total > 0 ? static_cast<long double>(r.compat_misses) / compat_total : 0.0L;
    long double sol_per_sec = r.elapsed_sec > 0 ? static_cast<long double>(r.solutions) / r.elapsed_sec : 0.0L;
    long double states_per_sec = r.elapsed_sec > 0 ? static_cast<long double>(r.states) / r.elapsed_sec : 0.0L;
    long double effective_cores = r.elapsed_sec > 0 ? static_cast<long double>(r.user_sec) / r.elapsed_sec : 0.0L;
    out << "{"
        << "\"name\":\"" << json_escape(r.name) << "\""
        << ",\"experiment_index\":" << r.experiment_index
        << ",\"gather_cache_mode\":\"" << json_escape(r.gather_cache_mode) << "\""
        << ",\"gather_matrix\":\"" << json_escape(r.gather_matrix) << "\""
        << ",\"engine\":\"n9-wide\""
        << ",\"n\":9"
        << ",\"vertices\":84"
        << ",\"orbits\":" << is.orbits.size()
        << ",\"images\":" << is.images.size()
        << ",\"threads\":" << r.opt.threads
        << ",\"scheduler\":\"root-static\""
        << ",\"parallel_depth\":1"
        << ",\"compat_cache_policy\":\"wide-lru\""
        << ",\"affinity\":\"" << json_escape(r.opt.affinity) << "\""
        << ",\"numa_policy\":\"" << json_escape(r.opt.numa_policy) << "\""
        << ",\"bitset_kernel\":\"" << json_escape(r.opt.bitset_kernel) << "\""
        << ",\"state_cache_scope\":\"labeled-global\""
        << ",\"compat_mem_budget_mb\":" << r.opt.compat_mem_budget_mb
        << ",\"compat_row_capacity\":" << r.opt.compat_mem_rows
        << ",\"state_cache_mode\":\"" << json_escape(r.opt.state_cache_mode) << "\""
        << ",\"state_cache_mb\":" << r.opt.state_cache_mb
        << ",\"state_cache_max\":" << r.opt.state_cache_max
        << ",\"force_propagation\":" << (r.opt.force_propagation ? "true" : "false")
        << ",\"quotient_final_dedup\":" << (r.opt.quotient ? "true" : "false")
        << ",\"elapsed_sec\":" << std::fixed << std::setprecision(6) << r.elapsed_sec
        << ",\"cpu_sec\":" << std::fixed << std::setprecision(6) << r.user_sec
        << ",\"effective_cores\":" << std::fixed << std::setprecision(3) << effective_cores
        << ",\"states\":" << r.states
        << ",\"solutions\":" << r.solutions
        << ",\"solution_candidates\":" << r.solution_candidates
        << ",\"duplicate_states\":" << r.duplicate_states
        << ",\"duplicate_ratio\":" << std::fixed << std::setprecision(5) << dup_ratio
        << ",\"states_per_sec\":" << std::fixed << std::setprecision(3) << states_per_sec
        << ",\"solutions_per_sec\":" << std::fixed << std::setprecision(3) << sol_per_sec
        << ",\"compat_hits\":" << r.compat_hits
        << ",\"compat_misses\":" << r.compat_misses
        << ",\"compat_miss_rate\":" << std::fixed << std::setprecision(6) << compat_miss_rate
        << ",\"state_cache_size\":" << r.state_cache_size
        << ",\"volume_prunes\":" << r.volume_prunes
        << ",\"coverage_prunes\":" << r.coverage_prunes
        << ",\"forced_cells\":" << r.forced_cells
        << ",\"rss_mb\":" << r.rss_mb
        << "}\n";
}

static int run_gather_data_n9(Cli cli) {
    apply_n9_auto_pre_run(cli);

    LoadedDataWide data = load_orbits_for_n9(cli.data_dir, cli.opt.volume_mode);
    if (!cli.opt.quiet) std::cerr << "[n=9 wide] Loaded " << data.orbits.size() << " orbit representatives for Delta(3,9).\n";
    ImageSetWide is;
    is.init(std::move(data), !cli.opt.quiet, cli.opt.threads);
    recompute_n9_cache_caps(cli.opt, is);
    print_memory_plan_n9(is, cli.opt, cli.opt.compat_mem_rows);

    SearchOptions base = cli.opt;
    base.auto_tune = false;
    base.count_only = true;
    base.quiet = true;
    base.progress_interval = 0;
    base.max_solutions = cli.gather_solutions;
    base.max_states = cli.gather_states;
    if (base.max_solutions == 0 && base.max_states == 0) {
        // For n=9, a state-bounded benchmark is usually more useful initially
        // because complete/early solutions depend strongly on the unknown input file.
        base.max_states = 100000;
    }
    base.solution_dedup = "memory";
    base.async_output = false;
    base.generation_mode = "state-cache";
    base.state_cache_mode = (base.state_cache_mode == "auto") ? "bounded" : base.state_cache_mode;
    base.scheduler = "static";
    base.compat_cache_policy = "lru";
    base.parallel_depth = 1;
    base.force_propagation = true;

    SystemInfo sys = detect_system_info();
    std::size_t logical = std::max<std::size_t>(1, sys.logical_cpus);
    std::size_t phys = sys.physical_cores ? sys.physical_cores : logical;
    bool likely_smt = (sys.physical_cores > 0 && logical >= phys * 3 / 2);

    auto clamp_thread = [&](std::size_t t) {
        if (t < 1) t = 1;
        return std::min<std::size_t>(logical, t);
    };

    // n=9 v1 feedback matrix, 2026-04-28:
    // The first real dual-Threadripper wide-engine data showed the best
    // state-bounded 10k-state point near 128 threads:
    //   96  threads: ~90.2s
    //   128 threads: ~85.3s
    //   144 threads: ~89.6s
    //   160+ threads: regressed
    // Therefore the focused matrix now probes around 128 rather than spending
    // most time on already-bad full-logical-core saturation.
    std::set<std::size_t> core_thread_tests;
    auto add_core = [&](std::size_t t) {
        if (t >= 1 && t <= logical) core_thread_tests.insert(t);
    };
    if (likely_smt) {
        add_core(phys);                         // 96 on the Threadripper system
        add_core((phys * 7 + 3) / 6);           // 112
        add_core((phys * 5 + 3) / 4);           // 120
        add_core((phys * 4 + 2) / 3);           // 128, current best
        add_core((phys * 17 + 6) / 12);         // 136
        add_core((phys * 3 + 1) / 2);           // 144
        add_core((phys * 19 + 6) / 12);         // 152
        add_core((phys * 5 + 2) / 3);           // 160, regression sentinel
    } else {
        add_core(phys);
        add_core(logical);
    }
    add_core(base.threads);

    std::set<std::size_t> full_thread_tests = core_thread_tests;
    auto add_full = [&](std::size_t t) {
        if (t >= 1 && t <= logical) full_thread_tests.insert(t);
    };
    add_full(std::max<std::size_t>(1, phys / 2));
    if (likely_smt) {
        add_full(logical > 16 ? logical - 16 : logical);
        add_full(logical);
    }

    std::vector<std::pair<std::string, SearchOptions>> experiments;
    std::set<std::string> names_seen;
    auto add = [&](std::string name, SearchOptions o) {
        if (!names_seen.insert(name).second) return;
        o.count_only = true; o.quiet = true; o.progress_interval = 0;
        o.max_solutions = base.max_solutions; o.max_states = base.max_states;
        o.scheduler = "static"; o.parallel_depth = 1; o.compat_cache_policy = "lru";
        o.solution_dedup = "memory"; o.async_output = false;
        recompute_n9_cache_caps(o, is);
        experiments.emplace_back(std::move(name), std::move(o));
    };

    add("n9_baseline_auto_root_static", base);

    auto add_thread_sweep = [&](const std::set<std::size_t>& tests) {
        for (std::size_t t : tests) {
            SearchOptions o = base; o.threads = t;
            add("n9_threads_" + std::to_string(t) + "_root_static", o);
        }
    };

    if (cli.gather_matrix == "quick") {
        // Fast feedback: only the known good neighborhood plus a 160-thread
        // regression sentinel.
        std::set<std::size_t> quick_threads;
        quick_threads.insert(clamp_thread(phys));
        quick_threads.insert(clamp_thread((phys * 4 + 2) / 3)); // 128 on 96c
        quick_threads.insert(clamp_thread((phys * 3 + 1) / 2)); // 144
        quick_threads.insert(clamp_thread((phys * 5 + 2) / 3)); // 160
        add_thread_sweep(quick_threads);
    } else {
        add_thread_sweep(cli.gather_matrix == "core" ? core_thread_tests : full_thread_tests);

        // Focus cache-budget tests at the current best thread count rather than
        // at the baseline thread count.  The first n=9 data showed 128 threads
        // was better than 144 on the dual Threadripper system.
        std::size_t best_probe_threads = clamp_thread(likely_smt ? (phys * 4 + 2) / 3 : phys);

        for (std::size_t mb : {std::size_t{8192}, std::size_t{32768}, std::size_t{65536}}) {
            SearchOptions o = base; o.threads = best_probe_threads;
            o.state_cache_mode = "bounded"; o.state_cache_mb = mb;
            add("n9_threads_" + std::to_string(best_probe_threads) + "_state_mb_" + std::to_string(mb), o);
        }

        for (std::size_t mb : {std::size_t{8192}, std::size_t{32768}, std::size_t{65536}, std::size_t{98304}}) {
            SearchOptions o = base; o.threads = best_probe_threads;
            o.compat_mem_budget_mb = mb; o.compat_mem_rows = 0;
            add("n9_threads_" + std::to_string(best_probe_threads) + "_compat_mb_" + std::to_string(mb), o);
        }

        // n=9-specific affinity probes.  n=8 disliked compact/spread on the
        // dual Threadripper; for n=9 we only test NUMA as a plausible locality
        // improvement plus the existing none baseline.
        SearchOptions o = base;
        o.threads = best_probe_threads;
        o.affinity = "numa";
        add("n9_threads_" + std::to_string(best_probe_threads) + "_affinity_numa", o);

        if (cli.gather_matrix == "full") {
            o = base;
            o.state_cache_mode = "none";
            add("n9_state_cache_none_reference", o);

            o = base;
            o.force_propagation = false;
            add("n9_no_force_propagation_reference", o);

            // A very low compatibility budget is now only a full-matrix
            // reference.  The first data showed 2 GiB compatibility cache was
            // far slower than 64 GiB for the wide engine.
            o = base;
            o.threads = best_probe_threads;
            o.compat_mem_budget_mb = 2048; o.compat_mem_rows = 0;
            add("n9_threads_" + std::to_string(best_probe_threads) + "_compat_mb_2048_reference", o);
        }

        // Deliberately omitted from the default n=9 gather matrix:
        //   n9_labeled_no_final_quotient_reference
        // On real n=9 inputs this reference can be orders of magnitude slower
        // than the other probes because it explores labeled search without the
        // quotient/final-dedup structure that the production engine relies on.
        // Users can still run labeled n=9 searches explicitly from the CLI, but
        // gather-data must stay bounded and useful for feedback tuning.

        if (cli.gather_matrix == "full") {
            std::set<std::size_t> budget_threads;
            budget_threads.insert(clamp_thread(phys));
            budget_threads.insert(clamp_thread((phys * 4 + 2) / 3));
            budget_threads.insert(clamp_thread((phys * 3 + 1) / 2));
            budget_threads.insert(clamp_thread((phys * 5 + 2) / 3));
            for (std::size_t t : budget_threads) {
                for (std::size_t state_mb : {std::size_t{32768}, std::size_t{65536}}) {
                    SearchOptions q = base; q.threads = t; q.state_cache_mb = state_mb;
                    add("n9_threads_" + std::to_string(t) + "_state_mb_" + std::to_string(state_mb), q);
                }
            }
        }
    }

    if (cli.gather_randomize_order) {
        std::mt19937_64 rng(0x9A9A9A9A12345678ULL);
        std::shuffle(experiments.begin(), experiments.end(), rng);
    }

    std::filesystem::path path = std::filesystem::current_path() / "perf_data.txt";
    std::ofstream out(path, std::ios::app);
    if (!out) throw std::runtime_error("Cannot write perf_data.txt in current directory.");

    auto now = std::chrono::system_clock::now();
    std::time_t now_t = std::chrono::system_clock::to_time_t(now);
    out << "# alexeev_r3_cpp n=9 wide gather-data run " << std::put_time(std::localtime(&now_t), "%F %T") << "\n";
    out << "# Each following line is JSON.  n=9 uses Mask128 wide cell masks; root-static search; labeled partial-state cache; exact final S_9 quotient dedup.\n";
    out << "# n=9 images=" << is.images.size()
        << " orbits=" << is.orbits.size()
        << " max_solutions=" << base.max_solutions
        << " max_states=" << base.max_states
        << " experiments=" << experiments.size()
        << " matrix=" << cli.gather_matrix
        << " cache_mode=" << cli.gather_cache_mode << "\n";
    out.flush();

    std::cerr << "[n=9 wide] gather-data: writing " << experiments.size()
              << " benchmark records to " << path << "\n";
    std::cerr << "[n=9 wide] gather-data: matrix=" << cli.gather_matrix
              << " per_experiment max_solutions=" << base.max_solutions
              << " max_states=" << base.max_states << "\n";

    std::size_t k = 0;
    for (auto [name, opt] : experiments) {
        ++k;
        std::cerr << "[n=9 wide] gather-data [" << k << "/" << experiments.size() << "] " << name << " ...\n";
        GatherResult r = run_gather_experiment_n9(is, opt, name);
        r.gather_cache_mode = cli.gather_cache_mode;
        r.gather_matrix = cli.gather_matrix;
        r.experiment_index = k;
        write_gather_result_n9(out, r, is);
        out.flush();
        std::cerr << "  done: elapsed=" << std::fixed << std::setprecision(3) << r.elapsed_sec
                  << "s solutions=" << r.solutions
                  << " states=" << r.states
                  << " dup=" << r.duplicate_states
                  << " eff_cores=" << (r.elapsed_sec > 0 ? r.user_sec / r.elapsed_sec : 0.0)
                  << " compat_misses=" << r.compat_misses << "\n";
    }
    out << "# end n=9 wide gather-data\n";
    out.close();
    std::cerr << "[n=9 wide] gather-data complete: " << path << "\n";
    return 0;
}


int main(int argc, char** argv) {
    try {
        Cli cli = parse_cli(argc, argv);
        apply_auto_preload(cli);

        if (cli.self_test) {
            bool ok = true;
            for (int n : {4,5,6}) {
                std::cerr << "=== self-test n=" << n << " ===\n";
                ok &= run_one_validation(n, cli);
            }
            if (!ok) {
                std::cerr << "self-test FAILED\n";
                return cli.allow_mismatch ? 0 : 2;
            }
            std::cerr << "self-test OK\n";
            return 0;
        }

        if (cli.n == 9) {
            apply_n9_auto_pre_run(cli);
            if (cli.gather_data) {
                return run_gather_data_n9(cli);
            }
            return run_n9_wide_main(cli.data_dir, cli.out_path, cli.opt, cli.validate, cli.build_cache_only);
        }

        LoadedData data = load_orbits_for_n(cli.n, cli.data_dir, cli.opt.volume_mode);
        if (!cli.opt.quiet) {
            std::cerr << "Loaded " << data.orbits.size() << " orbit representatives for Delta(3," << cli.n << ").\n";
        }
        ImageSet is(std::move(data), !cli.opt.quiet, cli.opt.threads);
        apply_auto_after_images(cli, is);
        if (cli.opt.auto_pilot_states > 0 && !cli.build_cache_only && !cli.validate && cli.write_prefix_tasks.empty()) {
            apply_auto_pilot_feedback(cli, is);
        }
        recompute_cache_caps_from_budgets(cli, is);
        if (!cli.opt.quiet) std::cerr << "compat row cache capacity: " << cli.opt.compat_mem_rows << " rows\n";
        print_memory_plan(cli, is);

        if (cli.validate) {
            bool ok = validate_all(is, cli.opt, !cli.opt.quiet);
            if (!ok) {
                std::cerr << "validation FAILED\n";
                if (!cli.allow_mismatch) return 2;
            } else {
                std::cerr << "validation OK\n";
            }
        }

        if (cli.build_cache_only) {
            save_build_cache(is, cli.opt.cache_dir, cli.opt.volume_mode);
            if (cli.opt.compat_prewarm && cli.n >= 7) {
                if (!cli.opt.quiet) {
                    std::cerr << "Prewarming " << cli.opt.compat_prewarm
                              << " compatibility rows with " << cli.opt.threads << " threads...\n";
                }
                CompatCache compat(is, cli.opt.compat_mem_rows,
                                   SearchEngine::compat_dir_for(is, cli.opt),
                                   cli.opt.threads,
                                   cli.opt.compat_cache_policy);
                compat.prewarm(cli.opt.compat_prewarm, !cli.opt.quiet, cli.opt.threads);
            }
            return 0;
        }

        if (!cli.write_prefix_tasks.empty()) {
            SearchEngine engine(is, cli.opt);
            engine.write_prefix_tasks_file(cli.write_prefix_tasks);
            return 0;
        }

        if (cli.gather_data) {
            run_gather_data(cli, is);
            return 0;
        }

        std::ofstream fout;
        std::ostream* os = &std::cout;
        if (!cli.out_path.empty()) {
            fout.open(cli.out_path);
            if (!fout) throw std::runtime_error("Cannot open output file: " + cli.out_path.string());
            os = &fout;
        }

        SearchEngine engine(is, cli.opt);
        if (!cli.read_prefix_tasks.empty()) {
            auto prefix_tasks = load_prefix_task_file(cli.read_prefix_tasks, cli.prefix_shard_index, cli.prefix_shard_count);
            if (!cli.opt.quiet) {
                std::cerr << "Loaded " << prefix_tasks.size() << " prefix task"
                          << (prefix_tasks.size() == 1 ? "" : "s")
                          << " from " << cli.read_prefix_tasks
                          << " shard " << cli.prefix_shard_index << "/" << cli.prefix_shard_count << "\n";
            }
            engine.run_from_prefix_chosen_tasks(prefix_tasks, *os);
        } else {
            engine.run(*os);
        }
        if (cli.opt.count_only) {
            std::cout << engine.stats.solutions.load() << "\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        std::cerr << "Use --help for usage.\n";
        return 1;
    }
}

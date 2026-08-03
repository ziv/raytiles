#include "detail/tile_source.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#define NOGDI
#define NOUSER
#endif
#include "httplib.h"

// stb_image function prototypes only — the implementation lives inside
// raylib (rtextures.o exports stbi_load_from_memory / stbi_image_free with
// external linkage). raylib's LoadImageFromMemory itself is NOT thread-safe
// (it routes through TraceLog and other globals), but the stb_image core
// is, so workers call into it directly.
#define STBI_ONLY_PNG
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#include "external/stb_image.h"

namespace raytiles {
namespace {
enum class asset { texture, heightmap, normals };

// tiny thread-safe logger: serializes writes to stderr behind a single
// mutex so log lines never interleave. used by workers in place of
// TraceLog, which is not thread-safe.
void log_warning(const std::string_view msg) {
  static std::mutex log_mtx;
  std::lock_guard lock(log_mtx);
  std::fprintf(stderr, "WARNING: %.*s\n", static_cast<int>(msg.size()), msg.data());
}

// replaces first occurrence by design — each token appears once
void replace(std::string& str, const std::string& from, const std::string& to) {
  if (from.empty()) return;
  const size_t pos = str.find(from);
  if (pos == std::string::npos) return;
  str.replace(pos, from.length(), to);
}

std::string make_url(std::string url_template, const int zoom, const int x, const int y) {
  replace(url_template, ":zoom:", std::to_string(zoom));
  replace(url_template, ":x:", std::to_string(x));
  replace(url_template, ":y:", std::to_string(y));
  return url_template;
}

std::string make_cache_path(const std::string& path_template, const int zoom, const int x, const int z) {
  return std::vformat(path_template, std::make_format_args(zoom, x, z));
}

// decode a PNG byte buffer into a raii::image. pixels are allocated
// with stb_image's allocator (malloc) which matches raylib's default
// RL_FREE, so UnloadImage (raii::image's deleter) is correct on the
// result. throws std::runtime_error on decode failure or unsupported
// channel count (only 3 / 4 channel inputs are supported, matching
// the formats utils::get_height_from_image accepts).
raii::image decode_png(const std::string& bytes) {
  int w = 0, h = 0, comp = 0;
  stbi_uc* data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &comp, 0);
  if (!data) throw std::runtime_error("PNG decode failed");
  if (comp != 3 && comp != 4) {
    stbi_image_free(data);
    throw std::runtime_error(std::format("unsupported PNG channel count: {}", comp));
  }
  Image img{};
  img.data = data;
  img.width = w;
  img.height = h;
  img.mipmaps = 1;
  img.format = (comp == 4) ? PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 : PIXELFORMAT_UNCOMPRESSED_R8G8B8;
  return raii::image{img};
}

std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    throw std::runtime_error("failed to open cached file: " + path);
  }
  std::string out((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return out;
}

std::string fetch(httplib::Client& cli, const std::string& url) {
  auto res = cli.Get(url);
  if (!res || res->status != 200) {
    const int status = res ? res->status : -1;
    const std::string err = res ? std::string{} : httplib::to_string(res.error());
    throw std::runtime_error(std::format("download failed: {} status={} err={}", url, status, err));
  }
  return std::move(res->body);
}

[[nodiscard]] httplib::Client create_client(const std::string& host, const bool allow_insecure_tls) {
  httplib::Client cli(host);
  cli.set_follow_location(true);
  cli.set_connection_timeout(5);
  cli.set_read_timeout(3);
  cli.set_keep_alive(true);
  cli.enable_server_certificate_verification(!allow_insecure_tls);
  return cli;
}

// atomically materialize a downloaded asset in the on-disk cache:
// write to a sibling .tmp file, then rename over the final path, so a
// crash mid-write never leaves a truncated PNG the next run would
// trust.
void write_atomic(const std::string& path, const std::string& bytes) {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  const std::string tmp_path = path + ".tmp";
  std::FILE* f = std::fopen(tmp_path.c_str(), "wb");
  if (!f) {
    throw std::runtime_error("fopen failed: " + tmp_path);
  }
  std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  std::error_code ec;
  std::filesystem::rename(tmp_path, path, ec);
  if (ec) {
    throw std::runtime_error("rename failed: " + path);
  }
}

// fetch + decode one of the three assets of a tile: disk cache first,
// network on miss (populating the cache on the way). `clients` holds
// one persistent keep-alive connection per host, owned by the calling
// worker thread — no synchronization needed.
raii::image fetch_asset(const source_options& opts, std::unordered_map<std::string, httplib::Client>& clients, const asset kind, const int zoom,
                        const int abs_x, const int abs_z) {
  const auto& cache_template = kind == asset::texture     ? opts.texture_cache_path
                               : kind == asset::heightmap ? opts.heightmap_cache_path
                                                          : opts.normals_cache_path;
  const auto& host = kind == asset::texture ? opts.texture_host : kind == asset::heightmap ? opts.heightmap_host : opts.normals_host;
  const auto& url_template = kind == asset::texture ? opts.texture_url_path : kind == asset::heightmap ? opts.heightmap_url_path : opts.normals_url_path;

  const auto path = make_cache_path(cache_template, zoom, abs_x, abs_z);

  std::string bytes;
  if (std::filesystem::exists(path)) {
    bytes = read_file(path);
  } else {
    if (!clients.contains(host)) clients.try_emplace(host, create_client(host, opts.allow_insecure_tls));
    auto body = fetch(clients.at(host), make_url(url_template, zoom, abs_x, abs_z));
    write_atomic(path, body);
    bytes = std::move(body);
  }
  return decode_png(bytes);
}
}  // namespace

tile_source::tile_source(source_options opts) : options(std::move(opts)) {
  workers.reserve(options.download_threads);
  for (int i = 0; i < options.download_threads; ++i) workers.emplace_back([this](const std::stop_token& st) { worker_loop(st); });
}

tile_source::~tile_source() {
  for (auto& w : workers) w.request_stop();
  cv.notify_all();
  workers.clear();  // joins
}

void tile_source::request(const tile_key& key, const int abs_x, const int abs_z) {
  std::lock_guard lock(mtx);

  // a re-request always wins over a pending cancellation: if the job is
  // still in flight (queued or mid-fetch) clearing the mark is enough —
  // the existing job will deliver normally.
  cancelled_.erase(key);

  if (in_flight_.contains(key)) return;

  in_flight_.insert(key);
  queue_.push(job{key, abs_x, abs_z});
  cv.notify_one();
}

void tile_source::cancel(const tile_key& key) {
  std::lock_guard lock(mtx);
  // only meaningful while the job exists; marking keys that already
  // delivered (or were never requested) would leak stale entries.
  if (in_flight_.contains(key)) {
    cancelled_.insert(key);
  }
}

std::vector<tile_payload> tile_source::drain() {
  std::vector<tile_payload> out;
  std::lock_guard lock(mtx);
  out.swap(ready_);
  return out;
}

std::vector<tile_key> tile_source::drain_failures() {
  std::vector<tile_key> out;
  std::lock_guard lock(mtx);
  out.swap(failed_);
  return out;
}

void tile_source::worker_loop(const std::stop_token& st) {
  // persistent http clients per host. endpoints all live under a few
  // hosts, so we keep TLS connections alive across many tiles instead
  // of paying handshake cost per fetch. owned by this worker thread; no
  // synchronization needed.
  std::unordered_map<std::string, httplib::Client> clients;

  while (true) {
    job j;
    {
      std::unique_lock lock(mtx);
      if (!cv.wait(lock, st, [this] { return !queue_.empty(); })) return;
      j = queue_.front();
      queue_.pop();

      // cancelled before pickup: the job dies here, silently
      if (cancelled_.erase(j.key)) {
        in_flight_.erase(j.key);
        continue;
      }
    }

    // fetch the three assets, checking for cancellation between them
    // so an obsolete tile doesn't waste up to two extra downloads.
    tile_payload payload{};
    payload.key = j.key;
    bool ok = true;
    bool skipped = false;  // some assets were skipped after a mid-fetch cancel
    try {
      const auto cancelled_midway = [&] {
        std::lock_guard lock(mtx);
        return cancelled_.contains(j.key);
      };
      payload.albedo = fetch_asset(options, clients, asset::texture, j.key.zoom, j.abs_x, j.abs_z);
      skipped = cancelled_midway();
      if (!skipped) {
        payload.height = fetch_asset(options, clients, asset::heightmap, j.key.zoom, j.abs_x, j.abs_z);
        skipped = cancelled_midway();
      }
      if (!skipped) {
        payload.normals = fetch_asset(options, clients, asset::normals, j.key.zoom, j.abs_x, j.abs_z);
      }
    } catch (const std::exception& e) {
      log_warning(std::format("tile {}/{}/{} failed: {}", j.key.zoom, j.abs_x, j.abs_z, e.what()));
      ok = false;
    }

    // deliver (or discard) under one lock so the in-flight/cancelled
    // bookkeeping and the queue push are a single atomic decision.
    {
      std::lock_guard lock(mtx);
      if (cancelled_.erase(j.key)) {
        // discarded; raii frees the decoded pixels (plain free, no GL)
        in_flight_.erase(j.key);
        continue;
      }
      if (skipped) {
        // the cancel that made us skip assets was cleared by a
        // re-request before delivery: this payload is incomplete,
        // so run the job again (kept in_flight_; skipped assets
        // now come from the disk cache).
        queue_.push(j);
        cv.notify_one();
        continue;
      }
      in_flight_.erase(j.key);
      if (ok) {
        ready_.push_back(std::move(payload));
      } else {
        failed_.push_back(j.key);
      }
    }
  }
}
}  // namespace raytiles

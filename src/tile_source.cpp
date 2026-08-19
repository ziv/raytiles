#include "detail/tile_source.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "detail/terrain_synth.hpp"  // pure synthesis helpers, worker-safe
#include "detail/utils.hpp"          // build_height_grid (pure CPU math, worker-safe)

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

// stb_image_write's PNG-to-memory encoder, linked from raylib's rtextures
// object (compiled there for SUPPORT_IMAGE_EXPORT, external linkage).
// stb_image_write.h only declares it inside its implementation section, so
// the prototype is declared here directly — signature verified against the
// vendored header (v1.16). Used to PNG-encode derived heightmaps on
// workers; raylib's ExportImage* routes through TraceLog and is not
// worker-safe.
extern "C" unsigned char* stbi_write_png_to_mem(const unsigned char* pixels, int stride_bytes, int x, int y, int n, int* out_len);

namespace raytiles {
namespace {
// provider tiles are 256x256; generated fallback/derived assets match
constexpr int default_tile_resolution = 256;

// replaces first occurrence by design
void replace(std::string& str, const std::string& from, const std::string& to) {
  if (from.empty()) return;
  const size_t pos = str.find(from);
  if (pos == std::string::npos) return;
  str.replace(pos, from.length(), to);
}

std::string get_url(std::string url, const int zoom, const int x, const int y) {
  replace(url, ":zoom:", std::to_string(zoom));
  replace(url, ":x:", std::to_string(x));
  replace(url, ":y:", std::to_string(y));
  return url;
}

// decode a PNG byte buffer into a raylib Image. pixels are allocated
// with stb_image's allocator (malloc) which matches raylib's default
// RL_FREE, so UnloadImage is the correct deleter on the result.
// throws std::runtime_error on decode failure or unsupported channel
// count (only 3 / 4 channel inputs are supported, matching the
// formats utils::get_height_from_image accepts).
Image decode_png(const std::string& bytes) {
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
  return img;
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

std::pair<std::string, std::string> split_url(const std::string& url) {
  const auto scheme = url.find("://");
  if (scheme == std::string::npos) throw std::runtime_error("invalid url (no scheme): " + url);

  const auto path_pos = url.find('/', scheme + 3);
  if (path_pos == std::string::npos) return {url, "/"};

  return {url.substr(0, path_pos), url.substr(path_pos)};
}

[[nodiscard]] httplib::Client create_client(const std::string& host, const int connection_timeout_sec, const int read_timeout_sec,
                                            const bool allow_insecure_tls) {
  httplib::Client cli(host);
  cli.set_follow_location(true);
  cli.set_connection_timeout(connection_timeout_sec);
  cli.set_read_timeout(read_timeout_sec);
  cli.set_keep_alive(true);
  cli.enable_server_certificate_verification(!allow_insecure_tls);
  return cli;
}

void write_atomic(const std::string& path, const std::string& bytes) {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  // unique tmp per writer: background derive tasks (step 5) can race a direct
  // job on the same cache path; a shared ".tmp" would interleave the writes
  // before the atomic rename. the rename race itself is benign — both
  // writers produce identical bytes.
  static std::atomic<unsigned> tmp_counter{0};
  const std::string tmp_path = path + ".tmp" + std::to_string(tmp_counter.fetch_add(1, std::memory_order_relaxed));
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

// PNG-encode an RGB8 image and atomically write it to the cache. stb's
// buffer comes from STBIW_MALLOC (malloc under raylib's config).
void write_png_atomic(const std::string& path, const Image& img) {
  int len = 0;
  unsigned char* png = stbi_write_png_to_mem(static_cast<const unsigned char*>(img.data), img.width * 3, img.width, img.height, 3, &len);
  if (!png) throw std::runtime_error("PNG encode failed: " + path);
  std::string bytes(reinterpret_cast<const char*>(png), static_cast<std::size_t>(len));
  std::free(png);
  write_atomic(path, bytes);
}

// worker-side free that must not touch raylib: pixel buffers come from
// stb_image, so stbi_image_free is the matching deleter
void free_partial(Image& img) {
  if (img.data != nullptr) {
    stbi_image_free(img.data);
    img.data = nullptr;
  }
}
}  // namespace

tile_source::resolved tile_source::resolve(const network_config& net) {
  if (net.native_terrain_zoom < min_supported_zoom || net.native_terrain_zoom > max_supported_zoom) {
    throw std::runtime_error(std::format("native_terrain_zoom {} outside [{}, {}]", net.native_terrain_zoom, min_supported_zoom, max_supported_zoom));
  }
  auto [texture_host, texture_url_path] = split_url(net.texture_url);
  auto [heightmap_host, heightmap_url_path] = split_url(net.heightmap_url);
  auto [normals_host, normals_url_path] = split_url(net.normals_url);
  return resolved{
      net.threads,
      net.allow_insecure_tls,
      net.connection_timeout_sec,
      net.read_timeout_sec,
      net.native_terrain_zoom,
      net.cache_dir + "/texture/{}/{}/{}.png",
      net.cache_dir + "/heightmap/{}/{}/{}.png",
      net.cache_dir + "/normals/{}/{}/{}.png",
      std::move(texture_host),
      std::move(texture_url_path),
      std::move(heightmap_host),
      std::move(heightmap_url_path),
      std::move(normals_host),
      std::move(normals_url_path),
  };
}

tile_source::tile_source(const network_config& net) : options(resolve(net)) {
  workers.reserve(static_cast<std::size_t>(options.threads));
  for (int i = 0; i < options.threads; ++i) workers.emplace_back([this](const std::stop_token& st) { worker_loop(st); });
}

tile_source::~tile_source() {
  for (auto& w : workers) w.request_stop();
  cv.notify_all();
  workers.clear();  // joins; pending jobs and undrained payloads die on this (main) thread
}

void tile_source::request(const tile_request& req) {
  std::lock_guard lock(mtx);
  if (in_flight.contains(req.key)) return;

  auto flag = std::make_shared<std::atomic_bool>(false);
  in_flight.emplace(req.key, flag);
  pending.push(job{req, std::move(flag)});
  cv.notify_one();
}

void tile_source::cancel(const tile_key& key) {
  std::lock_guard lock(mtx);
  if (const auto it = in_flight.find(key); it != in_flight.end()) {
    it->second->store(true, std::memory_order_relaxed);
  }
}

void tile_source::drain(std::vector<tile_payload>& ready_out, std::vector<drop>& dropped_out) {
  ready_out.clear();
  dropped_out.clear();
  std::lock_guard lock(mtx);
  std::swap(ready, ready_out);
  std::swap(dropped, dropped_out);
}

void tile_source::deliver_drop(const tile_key& key, const bool cancelled, std::string reason) {
  std::lock_guard lock(mtx);
  in_flight.erase(key);
  dropped.push_back(drop{key, cancelled, std::move(reason)});
}

void tile_source::enqueue_derive(const int parent_x, const int parent_z, const int target_zoom) {
  std::lock_guard lock(mtx);
  const tile_key token{target_zoom, parent_x, parent_z};
  if (background_done.contains(token)) return;
  background_done.insert(token);
  background.push(derive_task{parent_x, parent_z, target_zoom});
  cv.notify_one();
}

void tile_source::run_derive(const derive_task& task, const std::stop_token& st) {
  // best-effort: generate every missing derived heightmap under one native
  // parent down to target_zoom. the parent was cached by the synchronous
  // path that enqueued us; if anything is off (pruned cache, corrupt file),
  // skip silently — a direct request will retry synchronously.
  std::vector<float> floats;
  int w = 0, h = 0;
  try {
    const auto parent_path = std::vformat(options.heightmap_cache_path, std::make_format_args(options.native_terrain_zoom, task.parent_x, task.parent_z));
    Image parent = decode_png(read_file(parent_path));
    w = parent.width;
    h = parent.height;
    floats = synth::decode_terrarium_floats(parent);
    free_partial(parent);
  } catch (...) {
    return;
  }

  const auto gen = [&](auto&& self, const std::vector<float>& level_floats, const int level, const int x, const int z) -> void {
    if (level >= task.target_zoom || st.stop_requested()) return;
    for (int qz = 0; qz < 2; ++qz) {
      for (int qx = 0; qx < 2; ++qx) {
        if (st.stop_requested()) return;
        auto child = synth::upsample_quadrant(level_floats, w, h, qx, qz);
        const int child_level = level + 1;
        const int cx = x * 2 + qx;
        const int cz = z * 2 + qz;
        const auto path = std::vformat(options.heightmap_cache_path, std::make_format_args(child_level, cx, cz));
        if (!std::filesystem::exists(path)) {
          try {
            Image img = synth::encode_terrarium(child, w, h);
            write_png_atomic(path, img);
            free_partial(img);
          } catch (...) {
            // best-effort; the child stays derivable on demand
          }
        }
        self(self, child, child_level, cx, cz);
      }
    }
  };
  gen(gen, floats, options.native_terrain_zoom, task.parent_x, task.parent_z);
}

void tile_source::worker_loop(const std::stop_token& st) {
  // persistent http clients per host. endpoints usually live under a
  // single host, so the TLS connection stays alive across many tiles
  // instead of paying handshake cost per fetch. owned by the worker
  // thread, so no synchronization is needed.
  std::unordered_map<std::string, httplib::Client> clients;

  // cache-or-network raw bytes of one asset (fetched bytes are written
  // through to the cache)
  const auto fetch_bytes_cached = [&](const std::string& cache_template, const std::string& host, const std::string& url_path, const int zoom, const int x,
                                      const int z) -> std::string {
    const auto path = std::vformat(cache_template, std::make_format_args(zoom, x, z));
    if (std::filesystem::exists(path)) return read_file(path);
    if (!clients.contains(host))
      clients.try_emplace(host, create_client(host, options.connection_timeout_sec, options.read_timeout_sec, options.allow_insecure_tls));
    auto body = fetch(clients.at(host), get_url(url_path, zoom, x, z));
    write_atomic(path, body);
    return body;
  };

  // cache-or-network fetch of one asset, decoded to a CPU image
  const auto fetch_asset = [&](const std::string& cache_template, const std::string& host, const std::string& url_path, const tile_request& req) -> Image {
    return decode_png(fetch_bytes_cached(cache_template, host, url_path, req.key.zoom, req.x, req.z));
  };

  // heightmaps above the native zoom are synthesized from their native-zoom
  // ancestor (docs/greater-zoom-plan.md): decode the ancestor to float
  // heights, upsample only the quadrant chain containing the target, encode,
  // cache, serve — then hand the remaining descendants to a low-priority
  // background task. no HTTP is attempted above the native zoom.
  const auto fetch_heightmap = [&](const tile_request& req) -> Image {
    if (req.key.zoom <= options.native_terrain_zoom) {
      return fetch_asset(options.heightmap_cache_path, options.heightmap_host, options.heightmap_url_path, req);
    }

    const auto path = std::vformat(options.heightmap_cache_path, std::make_format_args(req.key.zoom, req.x, req.z));
    if (std::filesystem::exists(path)) return decode_png(read_file(path));

    const int dz = req.key.zoom - options.native_terrain_zoom;
    const int ax = req.x >> dz;
    const int az = req.z >> dz;
    Image parent =
        decode_png(fetch_bytes_cached(options.heightmap_cache_path, options.heightmap_host, options.heightmap_url_path, options.native_terrain_zoom, ax, az));
    const int w = parent.width;
    const int h = parent.height;
    auto floats = synth::decode_terrarium_floats(parent);
    free_partial(parent);

    for (int level = options.native_terrain_zoom + 1; level <= req.key.zoom; ++level) {
      const int shift = req.key.zoom - level;
      floats = synth::upsample_quadrant(floats, w, h, (req.x >> shift) & 1, (req.z >> shift) & 1);
    }

    Image out = synth::encode_terrarium(floats, w, h);
    try {
      write_png_atomic(path, out);
    } catch (...) {
      free_partial(out);
      throw;
    }
    enqueue_derive(ax, az, req.key.zoom);
    return out;
  };

  // normals never fail a tile: a pre-baked cache entry is honored at any
  // zoom; above the native zoom no HTTP is attempted (the provider has
  // none) and the flat default is generated in memory; below it, any
  // failure to end up with a valid image also falls back to the default —
  // normals are a lighting refinement, not load-bearing.
  const auto fetch_normals = [&](const tile_request& req) -> Image {
    try {
      const auto path = std::vformat(options.normals_cache_path, std::make_format_args(req.key.zoom, req.x, req.z));
      if (std::filesystem::exists(path)) return decode_png(read_file(path));
      if (req.key.zoom > options.native_terrain_zoom) return synth::default_normals_image(default_tile_resolution);
      return fetch_asset(options.normals_cache_path, options.normals_host, options.normals_url_path, req);
    } catch (const std::exception&) {
      return synth::default_normals_image(default_tile_resolution);
    }
  };

  while (true) {
    job j;
    {
      std::unique_lock lock(mtx);
      if (!cv.wait(lock, st, [this] { return !pending.empty() || !background.empty(); })) return;
      if (pending.empty()) {
        // low-priority synthesis: only runs when no real job waits
        const derive_task task = background.front();
        background.pop();
        lock.unlock();
        run_derive(task, st);
        continue;
      }
      j = std::move(pending.front());
      pending.pop();
    }

    const auto is_cancelled = [&] { return j.cancelled->load(std::memory_order_relaxed); };

    if (is_cancelled()) {
      deliver_drop(j.req.key, true, {});
      continue;
    }

    Image tex{}, hm{}, nl{};
    try {
      tex = fetch_asset(options.texture_cache_path, options.texture_host, options.texture_url_path, j.req);
      if (!is_cancelled()) {
        hm = fetch_heightmap(j.req);
      }
      if (!is_cancelled()) {
        nl = fetch_normals(j.req);
      }
    } catch (const std::exception& e) {
      free_partial(tex);
      free_partial(hm);
      free_partial(nl);
      deliver_drop(j.req.key, false, e.what());
      continue;
    }

    if (is_cancelled()) {
      free_partial(tex);
      free_partial(hm);
      free_partial(nl);
      deliver_drop(j.req.key, true, {});
      continue;
    }

    // derive the CPU query grid here so the main thread's upload
    // budget never pays for it (pure math, no raylib)
    height_grid heights = utils::build_height_grid(hm);

    std::lock_guard lock(mtx);
    in_flight.erase(j.req.key);
    ready.push_back(tile_payload{j.req.key, raii::image{tex}, raii::image{hm}, raii::image{nl}, std::move(heights)});
  }
}
}  // namespace raytiles

#pragma once

#include "raylib.h"

namespace raytiles::raii {

template <typename T, void (*Unload)(T)>
class resource {
 public:
  resource() noexcept : value_{}, owned_(false) {}

  explicit resource(T value) noexcept : value_(value), owned_(true) {}

  resource(const resource&) = delete;
  resource& operator=(const resource&) = delete;

  resource(resource&& other) noexcept : value_(other.value_), owned_(other.owned_) { other.owned_ = false; }

  resource& operator=(resource&& other) noexcept {
    if (this != &other) {
      reset();
      value_ = other.value_;
      owned_ = other.owned_;
      other.owned_ = false;
    }
    return *this;
  }

  ~resource() { reset(); }

  void reset() noexcept {
    if (owned_) {
      Unload(value_);
      owned_ = false;
    }
  }

  void reset(T value) noexcept {
    reset();
    value_ = value;
    owned_ = true;
  }

  [[nodiscard]] T release() noexcept {
    owned_ = false;
    return value_;
  }

  [[nodiscard]] const T& get() const noexcept { return value_; }
  [[nodiscard]] T& get() noexcept { return value_; }

  [[nodiscard]] bool valid() const noexcept { return owned_; }
  explicit operator bool() const noexcept { return owned_; }

  const T* operator->() const noexcept { return &value_; }
  T* operator->() noexcept { return &value_; }

  const T& operator*() const noexcept { return value_; }
  T& operator*() noexcept { return value_; }

 private:
  T value_;
  bool owned_;
};

using image = resource<Image, UnloadImage>;
using texture = resource<Texture2D, UnloadTexture>;
using shader = resource<Shader, UnloadShader>;
using model = resource<Model, UnloadModel>;

inline texture load_texture_from_image(const Image& img) { return texture{LoadTextureFromImage(img)}; }

inline shader load_shader_from_memory(const char* vs_code, const char* fs_code) { return shader{LoadShaderFromMemory(vs_code, fs_code)}; }

inline model load_model_from_mesh(Mesh m) { return model{LoadModelFromMesh(m)}; }

}  // namespace raytiles::raii


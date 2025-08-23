/* Minimal subset from renodx2 utils/data.hpp (no gtl dependency) */
#pragma once

#include <cstdint>
#include <include/reshade.hpp>

namespace utils::data {

template <typename T>
inline T* Get(const reshade::api::api_object* api_object) {
  uint64_t res;
  api_object->get_private_data(reinterpret_cast<const uint8_t*>(&__uuidof(T)), &res);
  return reinterpret_cast<T*>(static_cast<uintptr_t>(res));
}

template <typename T, typename... Args>
inline T* Create(reshade::api::api_object* api_object, Args&&... args) {
  uint64_t res;
  res = reinterpret_cast<uintptr_t>(new T(static_cast<Args&&>(args)...));
  api_object->set_private_data(reinterpret_cast<const uint8_t*>(&__uuidof(T)), res);
  return reinterpret_cast<T*>(static_cast<uintptr_t>(res));
}

template <typename T, typename... Args>
inline bool CreateOrGet(reshade::api::api_object* api_object, T*& private_data, Args&&... args) {
  uint64_t res;
  api_object->get_private_data(reinterpret_cast<const uint8_t*>(&__uuidof(T)), &res);
  if (res == 0) {
    res = reinterpret_cast<uintptr_t>(new T(static_cast<Args&&>(args)...));
    api_object->set_private_data(reinterpret_cast<const uint8_t*>(&__uuidof(T)), res);
    private_data = reinterpret_cast<T*>(static_cast<uintptr_t>(res));
    return true;
  }
  private_data = reinterpret_cast<T*>(static_cast<uintptr_t>(res));
  return false;
}

template <typename T>
inline void Delete(reshade::api::api_object* api_object) {
  delete api_object->get_private_data<T>();
  api_object->set_private_data(reinterpret_cast<const uint8_t*>(&__uuidof(T)), 0);
}

}  // namespace utils::data



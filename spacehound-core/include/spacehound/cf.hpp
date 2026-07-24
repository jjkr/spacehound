#pragma once

#include <ApplicationServices/ApplicationServices.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace spacehound::cf {

template <typename Ref>
class view;

template <typename Ref>
class retained;

/// Wraps a Core Foundation handle that is already retained by the caller, matching the adopt/create rule.
template <typename Ref>
[[nodiscard]] constexpr auto adopt(Ref ref) noexcept -> retained<Ref>;

/// A lightweight non-owning wrapper around a Core Foundation reference such as `CFStringRef`.
template <typename Ref>
class view final {
 public:
  /// The wrapped Core Foundation reference type.
  using ref_type = Ref;

  /// Creates an empty view handle.
  constexpr view() noexcept = default;
  /// Creates an empty view handle from `nullptr`.
  constexpr view(std::nullptr_t) noexcept {}
  /// Creates a view handle from a raw Core Foundation reference.
  explicit constexpr view(Ref ref) noexcept : ref_(ref) {}

  /// Returns the wrapped raw reference.
  [[nodiscard]] constexpr auto get() const noexcept -> Ref {
    return ref_;
  }

  /// Returns whether the wrapper currently holds a non-null reference.
  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return ref_ != nullptr;
  }

 private:
  Ref ref_ = nullptr;
};

/// A move-only owning wrapper that releases its Core Foundation reference with `CFRelease`.
template <typename Ref>
class retained final {
 public:
  /// The wrapped Core Foundation reference type.
  using ref_type = Ref;

  /// Creates an empty retained handle.
  retained() noexcept = default;
  /// Creates an empty retained handle from `nullptr`.
  retained(std::nullptr_t) noexcept {}

  /// Transfers ownership from another wrapper.
  retained(retained &&other) noexcept : ref_(other.release()) {}

  /// Replaces this wrapper with ownership transferred from another wrapper.
  auto operator=(retained &&other) noexcept -> retained & {
    if (this != &other) {
      reset(other.release());
    }

    return *this;
  }

  /// Retained Core Foundation handles are intentionally move-only.
  retained(const retained &) = delete;
  /// Retained Core Foundation handles are intentionally move-only.
  auto operator=(const retained &) -> retained & = delete;

  /// Releases the retained reference, if any.
  ~retained() {
    reset();
  }

  /// Returns the wrapped raw reference without changing ownership.
  [[nodiscard]] auto get() const noexcept -> Ref {
    return ref_;
  }

  /// Returns whether the wrapper currently owns a non-null reference.
  [[nodiscard]] explicit operator bool() const noexcept {
    return ref_ != nullptr;
  }

  /// Relinquishes ownership to the caller and skips the eventual `CFRelease`.
  [[nodiscard]] auto release() noexcept -> Ref {
    return std::exchange(ref_, nullptr);
  }

  /// Replaces the retained reference, calling `CFRelease` on the previous value first.
  void reset(Ref ref = nullptr) noexcept {
    if (ref_ != nullptr) {
      CFRelease(ref_);
    }

    ref_ = ref;
  }

  /// Exchanges ownership with another wrapper.
  void swap(retained &other) noexcept {
    std::swap(ref_, other.ref_);
  }

 private:
  explicit retained(Ref ref) noexcept : ref_(ref) {}

  Ref ref_ = nullptr;

  template <typename OtherRef>
  friend constexpr auto adopt(OtherRef ref) noexcept -> retained<OtherRef>;
};

template <typename Ref>
[[nodiscard]] constexpr auto adopt(Ref ref) noexcept -> retained<Ref> {
  return retained<Ref>{ref};
}

/// Retains a raw Core Foundation reference with `CFRetain` into a retained wrapper.
template <typename Ref>
[[nodiscard]] auto retain(Ref ref) noexcept -> retained<Ref> {
  if (ref == nullptr) {
    return {};
  }

  using pointee_type = std::remove_pointer_t<Ref>;
  using mutable_pointee_type = std::remove_const_t<pointee_type>;

  const auto *retained =
      static_cast<const mutable_pointee_type *>(CFRetain(ref));
  return adopt(static_cast<Ref>(const_cast<mutable_pointee_type *>(retained)));
}

/// Retains a view Core Foundation reference with `CFRetain` into a retained wrapper.
template <typename Ref>
[[nodiscard]] auto retain(view<Ref> ref) noexcept -> retained<Ref> {
  return retain(ref.get());
}

/// Exchanges the ownership of two Core Foundation wrappers.
template <typename Ref>
void swap(retained<Ref> &lhs, retained<Ref> &rhs) noexcept {
  lhs.swap(rhs);
}

/// Maps a Core Foundation handle type to the `CFTypeID` returned by the corresponding native API.
template <typename Ref>
struct type_traits;

class type_view;
class type;
class string_view;
class string;
struct dictionary_entry;
class dictionary_view;
class dictionary;

/// Type traits for dynamically typed `CFTypeRef` values.
template <>
struct type_traits<CFTypeRef> {
  /// Returns the runtime type identifier for a dynamically typed CF value via `CFGetTypeID`.
  [[nodiscard]] static auto type_id(CFTypeRef value) noexcept -> CFTypeID {
    if (value == nullptr) {
      return CFTypeID{};
    }

    return CFGetTypeID(value);
  }
};

/// Type traits for `CFStringRef`.
template <>
struct type_traits<CFStringRef> {
  /// Returns the runtime type identifier for `CFStringRef` via `CFStringGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return CFStringGetTypeID();
  }
};

/// Type traits for `CFNumberRef`.
template <>
struct type_traits<CFNumberRef> {
  /// Returns the runtime type identifier for `CFNumberRef` via `CFNumberGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return CFNumberGetTypeID();
  }
};

/// Type traits for `CFBooleanRef`.
template <>
struct type_traits<CFBooleanRef> {
  /// Returns the runtime type identifier for `CFBooleanRef` via `CFBooleanGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return CFBooleanGetTypeID();
  }
};

/// Type traits for `CFArrayRef`.
template <>
struct type_traits<CFArrayRef> {
  /// Returns the runtime type identifier for `CFArrayRef` via `CFArrayGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return CFArrayGetTypeID();
  }
};

/// Type traits for `CFDictionaryRef`.
template <>
struct type_traits<CFDictionaryRef> {
  /// Returns the runtime type identifier for `CFDictionaryRef` via `CFDictionaryGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return CFDictionaryGetTypeID();
  }
};

/// Type traits for `CFDataRef`.
template <>
struct type_traits<CFDataRef> {
  /// Returns the runtime type identifier for `CFDataRef` via `CFDataGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return CFDataGetTypeID();
  }
};

/// Type traits for `CFRunLoopRef`.
template <>
struct type_traits<CFRunLoopRef> {
  /// Returns the runtime type identifier for `CFRunLoopRef` via `CFRunLoopGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return CFRunLoopGetTypeID();
  }
};

/// Type traits for `CFRunLoopSourceRef`.
template <>
struct type_traits<CFRunLoopSourceRef> {
  /// Returns the runtime type identifier for `CFRunLoopSourceRef` via `CFRunLoopSourceGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return CFRunLoopSourceGetTypeID();
  }
};

/// Type traits for `CFMachPortRef`.
template <>
struct type_traits<CFMachPortRef> {
  /// Returns the runtime type identifier for `CFMachPortRef` via `CFMachPortGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return CFMachPortGetTypeID();
  }
};

/// Type traits for `CFUUIDRef`.
template <>
struct type_traits<CFUUIDRef> {
  /// Returns the runtime type identifier for `CFUUIDRef` via `CFUUIDGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return CFUUIDGetTypeID();
  }
};

/// Type traits for `CGEventRef`.
template <>
struct type_traits<CGEventRef> {
  /// Returns the runtime type identifier for `CGEventRef` via `CGEventGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return CGEventGetTypeID();
  }
};

/// Type traits for `CGEventSourceRef`.
template <>
struct type_traits<CGEventSourceRef> {
  /// Returns the runtime type identifier for `CGEventSourceRef` via `CGEventSourceGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return CGEventSourceGetTypeID();
  }
};

/// Type traits for `AXValueRef`.
template <>
struct type_traits<AXValueRef> {
  /// Returns the runtime type identifier for `AXValueRef` via `AXValueGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return AXValueGetTypeID();
  }
};

/// Type traits for `AXUIElementRef`.
template <>
struct type_traits<AXUIElementRef> {
  /// Returns the runtime type identifier for `AXUIElementRef` via `AXUIElementGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return AXUIElementGetTypeID();
  }
};

/// Type traits for `AXObserverRef`.
template <>
struct type_traits<AXObserverRef> {
  /// Returns the runtime type identifier for `AXObserverRef` via `AXObserverGetTypeID`.
  [[nodiscard]] static auto type_id() noexcept -> CFTypeID {
    return AXObserverGetTypeID();
  }
};

/// Returns whether a dynamically typed Core Foundation value matches the `CFTypeID` reported by the native API.
template <typename Ref>
[[nodiscard]] auto is(view<CFTypeRef> value) noexcept -> bool {
  if constexpr (std::same_as<Ref, CFTypeRef>) {
    return static_cast<bool>(value);
  } else {
    if (!value) {
      return false;
    }

    return CFGetTypeID(value.get()) == type_traits<Ref>::type_id();
  }
}

/// Casts a dynamically typed Core Foundation value when its runtime type matches.
template <typename Ref>
[[nodiscard]] auto cast(view<CFTypeRef> value) noexcept -> view<Ref> {
  if constexpr (std::same_as<Ref, CFTypeRef>) {
    return view{value.get()};
  } else {
    if (!is<Ref>(value)) {
      return {};
    }

    return view{static_cast<Ref>(value.get())};
  }
}

/// Returns the element from `CFArrayGetValueAtIndex` when it has the requested runtime type.
template <typename Ref>
[[nodiscard]] auto array_at(
    view<CFArrayRef> array,
    CFIndex index) noexcept -> view<Ref> {
  if (!array) {
    return {};
  }

  const CFIndex count = CFArrayGetCount(array.get());
  if (index < 0 || index >= count) {
    return {};
  }

  return cast<Ref>(view{static_cast<CFTypeRef>(
      CFArrayGetValueAtIndex(array.get(), index))});
}

/// Returns a dictionary entry from `CFDictionaryGetValueIfPresent` when it has the requested runtime type.
template <typename Ref>
[[nodiscard]] auto dictionary_find(
    view<CFDictionaryRef> dictionary,
    view<CFStringRef> key) noexcept -> view<Ref> {
  if (!dictionary || !key) {
    return {};
  }

  const void *value = nullptr;
  if (!CFDictionaryGetValueIfPresent(dictionary.get(), key.get(), &value) ||
      value == nullptr) {
    return {};
  }

  return cast<Ref>(view{static_cast<CFTypeRef>(value)});
}

namespace detail {

template <typename>
inline constexpr bool always_false_v = false;

template <std::signed_integral T>
[[nodiscard]] constexpr auto cf_number_type() noexcept -> CFNumberType {
  if constexpr (sizeof(T) <= sizeof(std::int8_t)) {
    return kCFNumberSInt8Type;
  } else if constexpr (sizeof(T) <= sizeof(std::int16_t)) {
    return kCFNumberSInt16Type;
  } else if constexpr (sizeof(T) <= sizeof(std::int32_t)) {
    return kCFNumberSInt32Type;
  } else {
    return kCFNumberSInt64Type;
  }
}

template <std::floating_point T>
[[nodiscard]] constexpr auto cf_number_type() noexcept -> CFNumberType {
  if constexpr (std::same_as<T, float>) {
    return kCFNumberFloat32Type;
  } else if constexpr (std::same_as<T, double>) {
    return kCFNumberFloat64Type;
  } else {
    static_assert(always_false_v<T>, "long double is not supported by cf::number_get");
  }
}

}  // namespace detail

template <typename T>
/// Converts a `CFNumberRef` with `CFNumberGetValue` when the requested arithmetic conversion is valid.
[[nodiscard]] auto number_get(view<CFNumberRef> number) noexcept
    -> std::optional<T> {
  if (!number) {
    return std::nullopt;
  }

  if constexpr (std::same_as<T, bool>) {
    static_assert(detail::always_false_v<T>, "bool is not supported by cf::number_get");
  } else if constexpr (std::signed_integral<T>) {
    T value{};
    if (!CFNumberGetValue(number.get(), detail::cf_number_type<T>(), &value)) {
      return std::nullopt;
    }

    return value;
  } else if constexpr (std::unsigned_integral<T>) {
    std::int64_t value{};
    if (!CFNumberGetValue(number.get(), kCFNumberSInt64Type, &value)) {
      return std::nullopt;
    }

    if (value < 0 ||
        static_cast<std::uint64_t>(value) > std::numeric_limits<T>::max()) {
      return std::nullopt;
    }

    return static_cast<T>(value);
  } else if constexpr (std::floating_point<T>) {
    T value{};
    if (!CFNumberGetValue(number.get(), detail::cf_number_type<T>(), &value)) {
      return std::nullopt;
    }

    return value;
  } else {
    static_assert(
        detail::always_false_v<T>,
        "cf::number_get only supports integral and floating-point types");
  }
}

/// Returns the current run loop via `CFRunLoopGetCurrent`.
[[nodiscard]] inline auto current_run_loop() noexcept -> view<CFRunLoopRef> {
  return view{CFRunLoopGetCurrent()};
}

/// Adds a run-loop source via `CFRunLoopAddSource`.
inline void add_source(
    view<CFRunLoopRef> run_loop,
    view<CFRunLoopSourceRef> source,
    string_view mode) noexcept;

/// Runs the current run loop via `CFRunLoopRun`.
inline void run() noexcept {
  CFRunLoopRun();
}

/// Creates a string representation of a UUID via `CFUUIDCreateString`.
[[nodiscard]] inline auto uuid_create_string(view<CFUUIDRef> uuid) noexcept -> class string;

/// A lightweight non-owning wrapper around `CFTypeRef`.
class type_view final {
 public:
  /// Creates an empty non-owning Core Foundation type view.
  constexpr type_view() noexcept = default;
  /// Creates an empty non-owning Core Foundation type view from `nullptr`.
  constexpr type_view(std::nullptr_t) noexcept {}
  /// Wraps a raw `CFTypeRef` without retaining it.
  explicit constexpr type_view(CFTypeRef ref) noexcept : ref_(view{ref}) {}
  /// Wraps an existing generic Core Foundation view reference.
  explicit constexpr type_view(view<CFTypeRef> ref) noexcept : ref_(ref) {}

  /// Returns the wrapped raw Core Foundation reference.
  [[nodiscard]] constexpr auto get() const noexcept -> CFTypeRef {
    return ref_.get();
  }

  /// Returns whether the view references a Core Foundation object.
  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns the runtime type identifier via `CFGetTypeID`.
  [[nodiscard]] auto type_id() const noexcept -> CFTypeID {
    return type_traits<CFTypeRef>::type_id(get());
  }

  /// Returns whether the wrapped value has the requested runtime Core Foundation type.
  template <typename Ref>
  [[nodiscard]] auto is() const noexcept -> bool {
    return cf::is<Ref>(ref_);
  }

  /// Casts the wrapped value when its runtime type matches.
  template <typename Ref>
  [[nodiscard]] auto cast() const noexcept -> view<Ref> {
    return cf::cast<Ref>(ref_);
  }

 private:
  view<CFTypeRef> ref_{};
};

/// A move-only owning wrapper around `CFTypeRef`.
class type final {
 public:
  /// Creates an empty owning Core Foundation type.
  type() noexcept = default;
  /// Creates an empty owning Core Foundation type from `nullptr`.
  type(std::nullptr_t) noexcept {}
  /// Transfers ownership from another type wrapper.
  type(type &&) noexcept = default;
  /// Replaces this wrapper with ownership from another type wrapper.
  auto operator=(type &&) noexcept -> type & = default;

  /// Core Foundation type wrappers are intentionally move-only.
  type(const type &) = delete;
  /// Core Foundation type wrappers are intentionally move-only.
  auto operator=(const type &) -> type & = delete;

  /// Adopts an already-retained `CFTypeRef`.
  [[nodiscard]] static auto adopt(CFTypeRef ref) noexcept -> type {
    return type{cf::adopt(ref)};
  }

  /// Retains a Core Foundation type view reference with `CFRetain`.
  [[nodiscard]] static auto retain(type_view ref) noexcept -> type {
    return type{cf::retain(ref.get())};
  }

  /// Returns the wrapped raw Core Foundation reference.
  [[nodiscard]] auto get() const noexcept -> CFTypeRef {
    return ref_.get();
  }

  /// Returns whether the wrapper owns a Core Foundation object.
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns a non-owning view of the wrapped value.
  [[nodiscard]] auto view() const noexcept -> type_view {
    return type_view{get()};
  }

  /// Returns the runtime type identifier via `CFGetTypeID`.
  [[nodiscard]] auto type_id() const noexcept -> CFTypeID {
    return view().type_id();
  }

  /// Returns whether the wrapped value has the requested runtime Core Foundation type.
  template <typename Ref>
  [[nodiscard]] auto is() const noexcept -> bool {
    return view().template is<Ref>();
  }

  /// Casts the wrapped value when its runtime type matches.
  template <typename Ref>
  [[nodiscard]] auto cast() const noexcept -> cf::view<Ref> {
    return view().template cast<Ref>();
  }

 private:
  explicit type(retained<CFTypeRef> ref) noexcept : ref_(std::move(ref)) {}

  retained<CFTypeRef> ref_{};
};

/// A lightweight non-owning wrapper around `CFStringRef`.
class string_view final {
 public:
  /// Creates an empty non-owning Core Foundation string view.
  constexpr string_view() noexcept = default;
  /// Creates an empty non-owning Core Foundation string view from `nullptr`.
  constexpr string_view(std::nullptr_t) noexcept {}
  /// Wraps a raw `CFStringRef` without retaining it.
  explicit constexpr string_view(CFStringRef ref) noexcept : ref_(view{ref}) {}
  /// Wraps an existing generic Core Foundation string view reference.
  explicit constexpr string_view(view<CFStringRef> ref) noexcept : ref_(ref) {}

  /// Returns the wrapped raw Core Foundation string reference.
  [[nodiscard]] constexpr auto get() const noexcept -> CFStringRef {
    return ref_.get();
  }

  /// Returns whether the view references a Core Foundation string.
  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns the UTF-16 code-unit length via `CFStringGetLength`.
  [[nodiscard]] auto length() const noexcept -> CFIndex {
    if (!ref_) {
      return 0;
    }

    return CFStringGetLength(get());
  }

  /// Returns whether the wrapped string is empty.
  [[nodiscard]] auto empty() const noexcept -> bool {
    return length() == 0;
  }

  /// Returns whether two strings compare equal via `CFStringCompare`.
  [[nodiscard]] auto equals(string_view other) const noexcept -> bool {
    if (!ref_ || !other.ref_) {
      return get() == other.get();
    }

    return CFStringCompare(get(), other.get(), 0) == kCFCompareEqualTo;
  }

  /// Converts the string to an owning UTF-8 `std::string`.
  [[nodiscard]] auto to_utf8() const noexcept -> std::optional<std::string> {
    if (!ref_) {
      return std::nullopt;
    }

    if (const char *cstring =
            CFStringGetCStringPtr(get(), kCFStringEncodingUTF8);
        cstring != nullptr) {
      return std::string{cstring};
    }

    const CFIndex maximum_size = CFStringGetMaximumSizeForEncoding(
        CFStringGetLength(get()), kCFStringEncodingUTF8);
    if (maximum_size < 0) {
      return std::nullopt;
    }

    std::string value(static_cast<std::size_t>(maximum_size) + 1U, '\0');
    if (!CFStringGetCString(
            get(),
            value.data(),
            static_cast<CFIndex>(value.size()),
            kCFStringEncodingUTF8)) {
      return std::nullopt;
    }

    value.resize(std::char_traits<char>::length(value.c_str()));
    return value;
  }

 private:
  view<CFStringRef> ref_{};
};

/// A move-only owning wrapper around `CFStringRef`.
class string final {
 public:
  /// Creates an empty owning Core Foundation string.
  string() noexcept = default;
  /// Creates an empty owning Core Foundation string from `nullptr`.
  string(std::nullptr_t) noexcept {}
  /// Transfers ownership from another string wrapper.
  string(string &&) noexcept = default;
  /// Replaces this wrapper with ownership from another string wrapper.
  auto operator=(string &&) noexcept -> string & = default;

  /// Core Foundation string wrappers are intentionally move-only.
  string(const string &) = delete;
  /// Core Foundation string wrappers are intentionally move-only.
  auto operator=(const string &) -> string & = delete;

  /// Adopts an already-retained `CFStringRef`.
  [[nodiscard]] static auto adopt(CFStringRef ref) noexcept -> string {
    return string{cf::adopt(ref)};
  }

  /// Retains a Core Foundation string view reference with `CFRetain`.
  [[nodiscard]] static auto retain(string_view ref) noexcept -> string {
    return string{cf::retain(ref.get())};
  }

  /// Creates a Core Foundation string from UTF-8 bytes.
  [[nodiscard]] static auto from_utf8(std::string_view value) noexcept -> string {
    const auto *bytes =
        reinterpret_cast<const UInt8 *>(value.empty() ? "" : value.data());
    return adopt(CFStringCreateWithBytes(
        kCFAllocatorDefault,
        bytes,
        static_cast<CFIndex>(value.size()),
        kCFStringEncodingUTF8,
        false));
  }

  /// Returns the wrapped raw Core Foundation string reference.
  [[nodiscard]] auto get() const noexcept -> CFStringRef {
    return ref_.get();
  }

  /// Returns whether the wrapper owns a Core Foundation string.
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns a non-owning view of the wrapped string.
  [[nodiscard]] auto view() const noexcept -> string_view {
    return string_view{get()};
  }

  /// Returns the UTF-16 code-unit length via `CFStringGetLength`.
  [[nodiscard]] auto length() const noexcept -> CFIndex {
    return view().length();
  }

  /// Returns whether the wrapped string is empty.
  [[nodiscard]] auto empty() const noexcept -> bool {
    return view().empty();
  }

  /// Returns whether two strings compare equal via `CFStringCompare`.
  [[nodiscard]] auto equals(string_view other) const noexcept -> bool {
    return view().equals(other);
  }

  /// Converts the string to an owning UTF-8 `std::string`.
  [[nodiscard]] auto to_utf8() const noexcept -> std::optional<std::string> {
    return view().to_utf8();
  }

 private:
  explicit string(retained<CFStringRef> ref) noexcept : ref_(std::move(ref)) {}

  retained<CFStringRef> ref_{};
};

inline void add_source(
    view<CFRunLoopRef> run_loop,
    view<CFRunLoopSourceRef> source,
    string_view mode) noexcept {
  CFRunLoopAddSource(run_loop.get(), source.get(), mode.get());
}

/// A key/value pair used to create immutable Core Foundation dictionaries.
struct dictionary_entry final {
  string_view key{};
  type_view value{};
};

/// A lightweight non-owning wrapper around `CFDictionaryRef`.
class dictionary_view final {
 public:
  /// Creates an empty non-owning Core Foundation dictionary view.
  constexpr dictionary_view() noexcept = default;
  /// Creates an empty non-owning Core Foundation dictionary view from `nullptr`.
  constexpr dictionary_view(std::nullptr_t) noexcept {}
  /// Wraps a raw `CFDictionaryRef` without retaining it.
  explicit constexpr dictionary_view(CFDictionaryRef ref) noexcept : ref_(view{ref}) {}
  /// Wraps an existing generic Core Foundation dictionary view reference.
  explicit constexpr dictionary_view(view<CFDictionaryRef> ref) noexcept : ref_(ref) {}

  /// Returns the wrapped raw Core Foundation dictionary reference.
  [[nodiscard]] constexpr auto get() const noexcept -> CFDictionaryRef {
    return ref_.get();
  }

  /// Returns whether the view references a Core Foundation dictionary.
  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns the number of entries via `CFDictionaryGetCount`.
  [[nodiscard]] auto count() const noexcept -> CFIndex {
    if (!ref_) {
      return 0;
    }

    return CFDictionaryGetCount(get());
  }

  /// Returns whether the dictionary contains no entries.
  [[nodiscard]] auto empty() const noexcept -> bool {
    return count() == 0;
  }

  /// Returns whether a key is present via `CFDictionaryGetValueIfPresent`.
  [[nodiscard]] auto contains(string_view key) const noexcept -> bool {
    if (!ref_ || !key) {
      return false;
    }

    const void *value = nullptr;
    return CFDictionaryGetValueIfPresent(get(), key.get(), &value) && value != nullptr;
  }

  /// Returns a dictionary entry when it has the requested runtime type.
  template <typename Ref>
  [[nodiscard]] auto find(string_view key) const noexcept -> view<Ref> {
    return cf::dictionary_find<Ref>(ref_, view<CFStringRef>{key.get()});
  }

 private:
  view<CFDictionaryRef> ref_{};
};

/// A move-only owning wrapper around `CFDictionaryRef`.
class dictionary final {
 public:
  /// Creates an empty owning Core Foundation dictionary.
  dictionary() noexcept = default;
  /// Creates an empty owning Core Foundation dictionary from `nullptr`.
  dictionary(std::nullptr_t) noexcept {}
  /// Transfers ownership from another dictionary wrapper.
  dictionary(dictionary &&) noexcept = default;
  /// Replaces this wrapper with ownership from another dictionary wrapper.
  auto operator=(dictionary &&) noexcept -> dictionary & = default;

  /// Core Foundation dictionary wrappers are intentionally move-only.
  dictionary(const dictionary &) = delete;
  /// Core Foundation dictionary wrappers are intentionally move-only.
  auto operator=(const dictionary &) -> dictionary & = delete;

  /// Adopts an already-retained `CFDictionaryRef`.
  [[nodiscard]] static auto adopt(CFDictionaryRef ref) noexcept -> dictionary {
    return dictionary{cf::adopt(ref)};
  }

  /// Retains a Core Foundation dictionary view reference with `CFRetain`.
  [[nodiscard]] static auto retain(dictionary_view ref) noexcept -> dictionary {
    return dictionary{cf::retain(ref.get())};
  }

  /// Creates an immutable dictionary with Core Foundation type callbacks.
  [[nodiscard]] static auto create(std::span<const dictionary_entry> entries) noexcept
      -> dictionary {
    if (entries.empty()) {
      return adopt(CFDictionaryCreate(
          kCFAllocatorDefault,
          nullptr,
          nullptr,
          0,
          &kCFTypeDictionaryKeyCallBacks,
          &kCFTypeDictionaryValueCallBacks));
    }

    std::vector<const void *> keys;
    std::vector<const void *> values;
    keys.reserve(entries.size());
    values.reserve(entries.size());

    for (const auto &entry : entries) {
      if (!entry.key || !entry.value) {
        return {};
      }

      keys.push_back(entry.key.get());
      values.push_back(entry.value.get());
    }

    return adopt(CFDictionaryCreate(
        kCFAllocatorDefault,
        keys.data(),
        values.data(),
        static_cast<CFIndex>(entries.size()),
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks));
  }

  /// Returns the wrapped raw Core Foundation dictionary reference.
  [[nodiscard]] auto get() const noexcept -> CFDictionaryRef {
    return ref_.get();
  }

  /// Returns whether the wrapper owns a Core Foundation dictionary.
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns a non-owning view of the wrapped dictionary.
  [[nodiscard]] auto view() const noexcept -> dictionary_view {
    return dictionary_view{get()};
  }

  /// Returns the number of entries via `CFDictionaryGetCount`.
  [[nodiscard]] auto count() const noexcept -> CFIndex {
    return view().count();
  }

  /// Returns whether the dictionary contains no entries.
  [[nodiscard]] auto empty() const noexcept -> bool {
    return view().empty();
  }

  /// Returns whether a key is present via `CFDictionaryGetValueIfPresent`.
  [[nodiscard]] auto contains(string_view key) const noexcept -> bool {
    return view().contains(key);
  }

  /// Returns a dictionary entry when it has the requested runtime type.
  template <typename Ref>
  [[nodiscard]] auto find(string_view key) const noexcept -> cf::view<Ref> {
    return view().template find<Ref>(key);
  }

 private:
  explicit dictionary(retained<CFDictionaryRef> ref) noexcept
      : ref_(std::move(ref)) {}

  retained<CFDictionaryRef> ref_{};
};

inline auto uuid_create_string(view<CFUUIDRef> uuid) noexcept -> string {
  if (!uuid) {
    return {};
  }

  return string::adopt(CFUUIDCreateString(kCFAllocatorDefault, uuid.get()));
}

}  // namespace spacehound::cf

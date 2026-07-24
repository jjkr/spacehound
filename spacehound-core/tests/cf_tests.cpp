#include <gtest/gtest.h>

#include <ApplicationServices/ApplicationServices.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include <spacehound/cf.hpp>

namespace {

namespace cf = spacehound::cf;

static_assert(!std::is_copy_constructible_v<cf::retained<CFStringRef>>);
static_assert(!std::is_copy_assignable_v<cf::retained<CFStringRef>>);
static_assert(std::is_trivially_copyable_v<cf::view<CFStringRef>>);
static_assert(!std::is_convertible_v<cf::retained<CFStringRef>, CFStringRef>);
static_assert(!std::is_copy_constructible_v<cf::type>);
static_assert(!std::is_copy_assignable_v<cf::type>);
static_assert(std::is_trivially_copyable_v<cf::type_view>);
static_assert(!std::is_convertible_v<cf::type, CFTypeRef>);
static_assert(!std::is_copy_constructible_v<cf::string>);
static_assert(!std::is_copy_assignable_v<cf::string>);
static_assert(std::is_trivially_copyable_v<cf::string_view>);
static_assert(!std::is_convertible_v<cf::string, CFStringRef>);
static_assert(!std::is_copy_constructible_v<cf::dictionary>);
static_assert(!std::is_copy_assignable_v<cf::dictionary>);
static_assert(std::is_trivially_copyable_v<cf::dictionary_view>);
static_assert(!std::is_convertible_v<cf::dictionary, CFDictionaryRef>);

auto make_string(const char *value) -> cf::retained<CFStringRef> {
  // Helpers keep the tests focused on wrapper behavior instead of CF allocation details.
  return cf::adopt(CFStringCreateWithCString(
      kCFAllocatorDefault, value, kCFStringEncodingUTF8));
}

auto make_number(std::int64_t value) -> cf::retained<CFNumberRef> {
  return cf::adopt(CFNumberCreate(
      kCFAllocatorDefault, kCFNumberSInt64Type, &value));
}

auto make_double_number(double value) -> cf::retained<CFNumberRef> {
  return cf::adopt(CFNumberCreate(
      kCFAllocatorDefault, kCFNumberFloat64Type, &value));
}

TEST(cf_tests, view_and_adopt_wrap_handles_without_copying) {
  const auto string = make_string("borrow-adopt");
  ASSERT_TRUE(string);

  const auto viewed = cf::view{string.get()};
  EXPECT_TRUE(viewed);
  EXPECT_EQ(viewed.get(), string.get());

  const auto adopted = cf::adopt(static_cast<CFStringRef>(nullptr));
  EXPECT_FALSE(adopted);
}

TEST(cf_tests, retained_release_and_reset_manage_lifetime) {
  auto string = make_string("release-reset");
  ASSERT_TRUE(string);

  CFStringRef released = string.release();
  ASSERT_NE(released, nullptr);
  EXPECT_FALSE(string);
  CFRelease(released);

  string.reset(CFStringCreateWithCString(
      kCFAllocatorDefault, "replacement", kCFStringEncodingUTF8));
  ASSERT_TRUE(string);

  string.reset();
  EXPECT_FALSE(string);
}

TEST(cf_tests, retained_move_and_swap_transfer_ownership) {
  auto left = make_string("left");
  auto right = make_string("right");
  ASSERT_TRUE(left);
  ASSERT_TRUE(right);

  const CFStringRef left_ref = left.get();
  const CFStringRef right_ref = right.get();

  auto moved = std::move(left);
  EXPECT_FALSE(left);
  ASSERT_TRUE(moved);
  EXPECT_EQ(moved.get(), left_ref);

  swap(moved, right);
  EXPECT_EQ(moved.get(), right_ref);
  EXPECT_EQ(right.get(), left_ref);
}

TEST(cf_tests, retain_accepts_raw_refs_and_view_refs) {
  const auto retained_raw = cf::retain(kCFBooleanTrue);
  ASSERT_TRUE(retained_raw);
  EXPECT_EQ(retained_raw.get(), kCFBooleanTrue);

  const auto retained_view = cf::retain(cf::view{kCFBooleanFalse});
  ASSERT_TRUE(retained_view);
  EXPECT_EQ(retained_view.get(), kCFBooleanFalse);

  const auto retained_null = cf::retain(static_cast<CFStringRef>(nullptr));
  EXPECT_FALSE(retained_null);
}

TEST(cf_tests, type_traits_match_core_foundation_type_ids) {
  const auto string = make_string("type-id");
  ASSERT_TRUE(string);

  EXPECT_EQ(cf::type_traits<CFTypeRef>::type_id(string.get()), CFStringGetTypeID());
  EXPECT_EQ(cf::type_traits<CFTypeRef>::type_id(nullptr), CFTypeID{});
  EXPECT_EQ(cf::type_traits<CFStringRef>::type_id(), CFStringGetTypeID());
  EXPECT_EQ(cf::type_traits<CFNumberRef>::type_id(), CFNumberGetTypeID());
  EXPECT_EQ(cf::type_traits<CFBooleanRef>::type_id(), CFBooleanGetTypeID());
  EXPECT_EQ(cf::type_traits<CFArrayRef>::type_id(), CFArrayGetTypeID());
  EXPECT_EQ(cf::type_traits<CFDictionaryRef>::type_id(), CFDictionaryGetTypeID());
  EXPECT_EQ(cf::type_traits<CFDataRef>::type_id(), CFDataGetTypeID());
  EXPECT_EQ(cf::type_traits<CFRunLoopRef>::type_id(), CFRunLoopGetTypeID());
  EXPECT_EQ(cf::type_traits<CFRunLoopSourceRef>::type_id(), CFRunLoopSourceGetTypeID());
  EXPECT_EQ(cf::type_traits<CFMachPortRef>::type_id(), CFMachPortGetTypeID());
  EXPECT_EQ(cf::type_traits<CFUUIDRef>::type_id(), CFUUIDGetTypeID());
  EXPECT_EQ(cf::type_traits<CGEventRef>::type_id(), CGEventGetTypeID());
  EXPECT_EQ(cf::type_traits<CGEventSourceRef>::type_id(), CGEventSourceGetTypeID());
  EXPECT_EQ(cf::type_traits<AXUIElementRef>::type_id(), AXUIElementGetTypeID());
  EXPECT_EQ(cf::type_traits<AXObserverRef>::type_id(), AXObserverGetTypeID());
}

TEST(cf_tests, is_and_cast_validate_runtime_types) {
  const auto string = make_string("cast");
  ASSERT_TRUE(string);

  const auto type_value = cf::view{static_cast<CFTypeRef>(string.get())};
  EXPECT_TRUE(cf::is<CFTypeRef>(type_value));
  EXPECT_TRUE(cf::is<CFStringRef>(type_value));
  EXPECT_FALSE(cf::is<CFNumberRef>(type_value));
  EXPECT_FALSE(cf::is<CFStringRef>(cf::view{static_cast<CFTypeRef>(nullptr)}));

  const auto cast_string = cf::cast<CFStringRef>(type_value);
  ASSERT_TRUE(cast_string);
  EXPECT_EQ(cast_string.get(), string.get());

  const auto cast_type = cf::cast<CFTypeRef>(type_value);
  ASSERT_TRUE(cast_type);
  EXPECT_EQ(cast_type.get(), type_value.get());

  const auto cast_number = cf::cast<CFNumberRef>(type_value);
  EXPECT_FALSE(cast_number);
}

TEST(cf_tests, array_at_handles_success_and_error_paths) {
  const auto number = make_number(42);
  const auto label = make_string("array");
  ASSERT_TRUE(number);
  ASSERT_TRUE(label);

  const void *values[] = {number.get(), label.get()};
  const auto array = cf::adopt(CFArrayCreate(
      kCFAllocatorDefault, values, 2, &kCFTypeArrayCallBacks));
  ASSERT_TRUE(array);

  const auto number_value = cf::array_at<CFNumberRef>(cf::view{array.get()}, 0);
  ASSERT_TRUE(number_value);
  EXPECT_EQ(number_value.get(), number.get());

  EXPECT_FALSE(cf::array_at<CFStringRef>(cf::view{array.get()}, 0));
  EXPECT_FALSE(cf::array_at<CFNumberRef>(cf::view{array.get()}, -1));
  EXPECT_FALSE(cf::array_at<CFNumberRef>(cf::view{array.get()}, 2));
  EXPECT_FALSE(cf::array_at<CFNumberRef>(cf::view{static_cast<CFArrayRef>(nullptr)}, 0));
}

TEST(cf_tests, dictionary_find_handles_success_and_error_paths) {
  const auto number = make_number(42);
  const auto label = make_string("label");
  ASSERT_TRUE(number);
  ASSERT_TRUE(label);

  const auto value_key = cf::view{CFSTR("value")};
  const auto label_key = cf::view{CFSTR("label")};
  const void *keys[] = {value_key.get(), label_key.get()};
  const void *values[] = {number.get(), label.get()};
  const auto dictionary = cf::adopt(CFDictionaryCreate(
      kCFAllocatorDefault,
      keys,
      values,
      2,
      &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks));
  ASSERT_TRUE(dictionary);

  const auto found_number =
      cf::dictionary_find<CFNumberRef>(cf::view{dictionary.get()}, value_key);
  ASSERT_TRUE(found_number);
  EXPECT_EQ(found_number.get(), number.get());

  EXPECT_FALSE(cf::dictionary_find<CFStringRef>(cf::view{dictionary.get()}, value_key));
  EXPECT_FALSE(cf::dictionary_find<CFNumberRef>(
      cf::view{dictionary.get()}, cf::view{CFSTR("missing")}));
  EXPECT_FALSE(cf::dictionary_find<CFNumberRef>(
      cf::view{static_cast<CFDictionaryRef>(nullptr)}, value_key));
  EXPECT_FALSE(cf::dictionary_find<CFNumberRef>(
      cf::view{dictionary.get()}, cf::view{static_cast<CFStringRef>(nullptr)}));
}

TEST(cf_tests, number_get_supports_signed_unsigned_and_floating_types) {
  const auto integer_number = make_number(42);
  ASSERT_TRUE(integer_number);

  EXPECT_EQ(cf::number_get<std::int8_t>(cf::view{integer_number.get()}), 42);
  EXPECT_EQ(cf::number_get<std::int16_t>(cf::view{integer_number.get()}), 42);
  EXPECT_EQ(cf::number_get<std::int32_t>(cf::view{integer_number.get()}), 42);
  EXPECT_EQ(cf::number_get<std::int64_t>(cf::view{integer_number.get()}), 42);
  EXPECT_EQ(cf::number_get<std::uint8_t>(cf::view{integer_number.get()}), 42U);
  EXPECT_EQ(cf::number_get<std::uint16_t>(cf::view{integer_number.get()}), 42U);
  EXPECT_EQ(cf::number_get<std::uint32_t>(cf::view{integer_number.get()}), 42U);
  EXPECT_EQ(cf::number_get<std::uint64_t>(cf::view{integer_number.get()}), 42U);

  const auto double_number = make_double_number(3.5);
  ASSERT_TRUE(double_number);
  EXPECT_FLOAT_EQ(*cf::number_get<float>(cf::view{double_number.get()}), 3.5F);
  EXPECT_DOUBLE_EQ(*cf::number_get<double>(cf::view{double_number.get()}), 3.5);
}

TEST(cf_tests, number_get_rejects_invalid_unsigned_conversions) {
  const auto negative_number = make_number(-1);
  ASSERT_TRUE(negative_number);
  EXPECT_FALSE(cf::number_get<std::uint8_t>(cf::view{negative_number.get()}));

  const auto large_number = make_number(300);
  ASSERT_TRUE(large_number);
  EXPECT_FALSE(cf::number_get<std::uint8_t>(cf::view{large_number.get()}));
  EXPECT_FALSE(cf::number_get<std::uint16_t>(cf::view{static_cast<CFNumberRef>(nullptr)}));
}

TEST(cf_tests, type_wrappers_adopt_retain_move_and_cast_runtime_values) {
  const cf::type_view null_view{};
  EXPECT_FALSE(null_view);
  EXPECT_EQ(null_view.get(), nullptr);
  EXPECT_EQ(null_view.type_id(), CFTypeID{});

  auto string = make_string("typed-value");
  ASSERT_TRUE(string);

  const cf::type_view viewed{static_cast<CFTypeRef>(string.get())};
  ASSERT_TRUE(viewed);
  EXPECT_EQ(viewed.type_id(), CFStringGetTypeID());
  EXPECT_TRUE(viewed.is<CFTypeRef>());
  EXPECT_TRUE(viewed.is<CFStringRef>());
  EXPECT_FALSE(viewed.is<CFNumberRef>());

  const auto viewed_string = viewed.cast<CFStringRef>();
  ASSERT_TRUE(viewed_string);
  EXPECT_EQ(viewed_string.get(), string.get());

  const auto retained = cf::type::retain(viewed);
  ASSERT_TRUE(retained);
  EXPECT_EQ(retained.type_id(), CFStringGetTypeID());

  auto adopted = cf::type::adopt(static_cast<CFTypeRef>(string.release()));
  ASSERT_TRUE(adopted);
  EXPECT_TRUE(adopted.is<CFStringRef>());

  auto moved = std::move(adopted);
  EXPECT_FALSE(adopted);
  ASSERT_TRUE(moved);
  EXPECT_EQ(moved.cast<CFStringRef>().get(), viewed_string.get());
}

TEST(cf_tests, string_wrappers_round_trip_utf8_and_compare_values) {
  const cf::string_view null_view{};
  EXPECT_FALSE(null_view);
  EXPECT_FALSE(null_view.to_utf8());

  const auto string = cf::string::from_utf8("spacehound");
  ASSERT_TRUE(string);
  EXPECT_EQ(string.length(), 10);
  EXPECT_FALSE(string.empty());
  EXPECT_TRUE(string.equals(cf::string_view{CFSTR("spacehound")}));
  EXPECT_FALSE(string.equals(cf::string_view{CFSTR("different")}));

  const auto viewed = string.view();
  ASSERT_TRUE(viewed);
  EXPECT_EQ(viewed.get(), string.get());
  EXPECT_EQ(viewed.length(), 10);
  EXPECT_FALSE(viewed.empty());

  const auto utf8 = string.to_utf8();
  ASSERT_TRUE(utf8);
  EXPECT_EQ(*utf8, std::string("spacehound"));

  const auto retained = cf::string::retain(viewed);
  ASSERT_TRUE(retained);
  EXPECT_TRUE(retained.equals(viewed));

  auto adopted = cf::string::adopt(CFStringCreateWithCString(
      kCFAllocatorDefault, "adopted", kCFStringEncodingUTF8));
  ASSERT_TRUE(adopted);
  EXPECT_EQ(*adopted.to_utf8(), std::string("adopted"));
}

TEST(cf_tests, string_wrappers_handle_empty_and_non_ascii_utf8) {
  const auto empty = cf::string::from_utf8("");
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty.empty());
  ASSERT_TRUE(empty.to_utf8());
  EXPECT_EQ(*empty.to_utf8(), std::string());

  const std::string non_ascii = "caf\xC3\xA9 \xF0\x9F\x90\x87";
  const auto round_tripped = cf::string::from_utf8(non_ascii);
  ASSERT_TRUE(round_tripped);
  ASSERT_TRUE(round_tripped.to_utf8());
  EXPECT_EQ(*round_tripped.to_utf8(), non_ascii);
}

TEST(cf_tests, dictionary_wrappers_create_find_and_report_shape) {
  const auto number = make_number(42);
  const auto label = make_string("label");
  ASSERT_TRUE(number);
  ASSERT_TRUE(label);

  const std::array entries{
      cf::dictionary_entry{
          .key = cf::string_view{CFSTR("value")},
          .value = cf::type_view{static_cast<CFTypeRef>(number.get())}},
      cf::dictionary_entry{
          .key = cf::string_view{CFSTR("label")},
          .value = cf::type_view{static_cast<CFTypeRef>(label.get())}},
  };

  const auto dictionary = cf::dictionary::create(entries);
  ASSERT_TRUE(dictionary);
  EXPECT_EQ(dictionary.count(), 2);
  EXPECT_FALSE(dictionary.empty());
  EXPECT_TRUE(dictionary.contains(cf::string_view{CFSTR("value")}));
  EXPECT_FALSE(dictionary.contains(cf::string_view{CFSTR("missing")}));

  const auto found_number = dictionary.find<CFNumberRef>(cf::string_view{CFSTR("value")});
  ASSERT_TRUE(found_number);
  EXPECT_EQ(found_number.get(), number.get());

  const auto found_label = dictionary.find<CFStringRef>(cf::string_view{CFSTR("label")});
  ASSERT_TRUE(found_label);
  EXPECT_EQ(found_label.get(), label.get());

  EXPECT_FALSE(dictionary.find<CFStringRef>(cf::string_view{CFSTR("value")}));

  const auto retained = cf::dictionary::retain(dictionary.view());
  ASSERT_TRUE(retained);
  EXPECT_EQ(retained.count(), 2);

  auto moved = cf::dictionary::adopt(static_cast<CFDictionaryRef>(
      retained.view().get() == nullptr ? nullptr : CFRetain(retained.get())));
  ASSERT_TRUE(moved);
  EXPECT_EQ(moved.count(), 2);
}

TEST(cf_tests, dictionary_wrappers_handle_empty_and_invalid_entries) {
  const std::array<cf::dictionary_entry, 0> empty_entries{};
  const auto empty_dictionary = cf::dictionary::create(empty_entries);
  ASSERT_TRUE(empty_dictionary);
  EXPECT_TRUE(empty_dictionary.empty());
  EXPECT_EQ(empty_dictionary.count(), 0);

  const auto number = make_number(7);
  ASSERT_TRUE(number);

  const std::array null_key_entries{
      cf::dictionary_entry{
          .key = {},
          .value = cf::type_view{static_cast<CFTypeRef>(number.get())}},
  };
  EXPECT_FALSE(cf::dictionary::create(null_key_entries));

  const std::array null_value_entries{
      cf::dictionary_entry{
          .key = cf::string_view{CFSTR("value")},
          .value = {}},
  };
  EXPECT_FALSE(cf::dictionary::create(null_value_entries));

  const cf::dictionary_view null_view{};
  EXPECT_FALSE(null_view);
  EXPECT_TRUE(null_view.empty());
  EXPECT_EQ(null_view.count(), 0);
  EXPECT_FALSE(null_view.contains(cf::string_view{CFSTR("value")}));
  EXPECT_FALSE(null_view.find<CFNumberRef>(cf::string_view{CFSTR("value")}));
}

TEST(cf_tests, run_loop_helpers_and_uuid_string_are_callable) {
  const auto run_loop = cf::current_run_loop();
  ASSERT_TRUE(run_loop);

  CFRunLoopSourceContext context{};
  const auto source =
      cf::adopt(CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &context));
  ASSERT_TRUE(source);

  cf::add_source(
      run_loop, cf::view{source.get()}, cf::string_view{kCFRunLoopDefaultMode});
  EXPECT_TRUE(CFRunLoopContainsSource(
      run_loop.get(), source.get(), kCFRunLoopDefaultMode));
  CFRunLoopRemoveSource(run_loop.get(), source.get(), kCFRunLoopDefaultMode);

  const auto uuid = cf::adopt(CFUUIDCreate(kCFAllocatorDefault));
  ASSERT_TRUE(uuid);

  const auto uuid_string = cf::uuid_create_string(cf::view{uuid.get()});
  ASSERT_TRUE(uuid_string);
  EXPECT_FALSE(uuid_string.empty());

  const auto run_function = &cf::run;
  ASSERT_NE(run_function, nullptr);
}

}  // namespace

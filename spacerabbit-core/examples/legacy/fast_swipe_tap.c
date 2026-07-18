#include <ApplicationServices/ApplicationServices.h>

#include <stdio.h>
#include <stdlib.h>

enum sr_gesture_phase {
  SR_GESTURE_PHASE_BEGIN = 1,
  SR_GESTURE_PHASE_UPDATE = 2,
  SR_GESTURE_PHASE_END = 4
};

enum sr_direction {
  SR_DIRECTION_LEFT,
  SR_DIRECTION_RIGHT,
  SR_DIRECTION_UP,
  SR_DIRECTION_DOWN
};

typedef struct sr_tap_context {
  CFMachPortRef tap_ref;
  CGEventSourceRef synthetic_source_ref;
} sr_tap_context;

typedef struct sr_space_bounds {
  int64_t current_index;
  int64_t num_spaces;
} sr_space_bounds;

static const CGEventField SR_EVENT_TYPE_FIELD = (CGEventField)0x37;
static const CGEventField SR_GESTURE_SUBTYPE_FIELD = (CGEventField)0x6e;
static const CGEventField SR_GESTURE_TYPE_FIELD = (CGEventField)0x7b;
static const CGEventField SR_GESTURE_DELTA_FIELD = (CGEventField)0x7c;
static const CGEventField SR_GESTURE_MOMENTUM_X_FIELD = (CGEventField)0x81;
static const CGEventField SR_GESTURE_MOMENTUM_Y_FIELD = (CGEventField)0x82;
static const CGEventField SR_GESTURE_PHASE_FIELD = (CGEventField)0x84;
static const CGEventField SR_GESTURE_PHASE_MIRROR_FIELD = (CGEventField)0x86;
static const CGEventField SR_GESTURE_FINGER_COUNT_FIELD = (CGEventField)0x8a;
static const CGEventField SR_GESTURE_TYPE_MIRROR_FIELD = (CGEventField)0xa5;
static const int64_t SR_SYNTHETIC_EVENT_MARKER = 0x5352544150494E47LL;

extern int CGSMainConnectionID(void);
extern CFArrayRef CGSCopyManagedDisplaySpaces(int connection_id);
extern CFStringRef CGSCopyActiveMenuBarDisplayIdentifier(int connection_id);

static const char *sr_direction_name(enum sr_direction direction) {
  switch (direction) {
    case SR_DIRECTION_LEFT:
      return "left";
    case SR_DIRECTION_RIGHT:
      return "right";
    case SR_DIRECTION_UP:
      return "up";
    case SR_DIRECTION_DOWN:
      return "down";
  }

  return "unknown";
}

static int sr_direction_sign(enum sr_direction direction) {
  switch (direction) {
    case SR_DIRECTION_RIGHT:
    case SR_DIRECTION_UP:
      return 1;
    case SR_DIRECTION_LEFT:
    case SR_DIRECTION_DOWN:
      return -1;
  }

  return 0;
}

static int sr_gesture_type(enum sr_direction direction) {
  switch (direction) {
    case SR_DIRECTION_LEFT:
    case SR_DIRECTION_RIGHT:
      return 1;
    case SR_DIRECTION_UP:
    case SR_DIRECTION_DOWN:
      return 2;
  }

  return 0;
}

static int sr_is_horizontal_direction(enum sr_direction direction) {
  return direction == SR_DIRECTION_LEFT || direction == SR_DIRECTION_RIGHT;
}

static int sr_get_cf_number_int64(CFDictionaryRef dict_ref, CFStringRef key_ref, int64_t *out_value) {
  CFNumberRef number_ref;

  if (dict_ref == NULL || key_ref == NULL || out_value == NULL) {
    return 0;
  }

  number_ref = (CFNumberRef)CFDictionaryGetValue(dict_ref, key_ref);
  if (number_ref == NULL || CFGetTypeID(number_ref) != CFNumberGetTypeID()) {
    return 0;
  }

  return CFNumberGetValue(number_ref, kCFNumberSInt64Type, out_value);
}

static int sr_get_active_display_space_bounds(sr_space_bounds *out_bounds) {
  CFArrayRef managed_spaces_ref;
  CFStringRef active_display_ref;
  const CFStringRef display_identifier_key = CFSTR("Display Identifier");
  const CFStringRef spaces_key = CFSTR("Spaces");
  const CFStringRef current_space_key = CFSTR("Current Space");
  const CFStringRef managed_space_id_key = CFSTR("ManagedSpaceID");
  int connection_id;
  int found = 0;
  CFIndex display_index;
  CFIndex display_count;

  if (out_bounds == NULL) {
    return 0;
  }

  connection_id = CGSMainConnectionID();
  active_display_ref = CGSCopyActiveMenuBarDisplayIdentifier(connection_id);
  if (active_display_ref == NULL) {
    return 0;
  }

  managed_spaces_ref = CGSCopyManagedDisplaySpaces(connection_id);
  if (managed_spaces_ref == NULL) {
    CFRelease(active_display_ref);
    return 0;
  }

  display_count = CFArrayGetCount(managed_spaces_ref);
  for (display_index = 0; display_index < display_count; ++display_index) {
    CFDictionaryRef display_dict_ref;
    CFStringRef display_identifier_ref;
    CFArrayRef spaces_ref;
    CFDictionaryRef current_space_ref;
    CFIndex space_index;
    CFIndex space_count;
    int64_t current_space_id;

    display_dict_ref = (CFDictionaryRef)CFArrayGetValueAtIndex(managed_spaces_ref, display_index);
    if (display_dict_ref == NULL || CFGetTypeID(display_dict_ref) != CFDictionaryGetTypeID()) {
      continue;
    }

    display_identifier_ref =
        (CFStringRef)CFDictionaryGetValue(display_dict_ref, display_identifier_key);
    if (display_identifier_ref == NULL ||
        CFGetTypeID(display_identifier_ref) != CFStringGetTypeID() ||
        !CFEqual(display_identifier_ref, active_display_ref)) {
      continue;
    }

    spaces_ref = (CFArrayRef)CFDictionaryGetValue(display_dict_ref, spaces_key);
    current_space_ref = (CFDictionaryRef)CFDictionaryGetValue(display_dict_ref, current_space_key);
    if (spaces_ref == NULL || current_space_ref == NULL ||
        CFGetTypeID(spaces_ref) != CFArrayGetTypeID() ||
        CFGetTypeID(current_space_ref) != CFDictionaryGetTypeID()) {
      break;
    }

    if (!sr_get_cf_number_int64(current_space_ref, managed_space_id_key, &current_space_id)) {
      break;
    }

    space_count = CFArrayGetCount(spaces_ref);
    for (space_index = 0; space_index < space_count; ++space_index) {
      CFDictionaryRef space_dict_ref;
      int64_t space_id;

      space_dict_ref = (CFDictionaryRef)CFArrayGetValueAtIndex(spaces_ref, space_index);
      if (space_dict_ref == NULL || CFGetTypeID(space_dict_ref) != CFDictionaryGetTypeID()) {
        continue;
      }

      if (!sr_get_cf_number_int64(space_dict_ref, managed_space_id_key, &space_id)) {
        continue;
      }

      if (space_id == current_space_id) {
        out_bounds->current_index = (int64_t)space_index;
        out_bounds->num_spaces = (int64_t)space_count;
        found = 1;
        break;
      }
    }

    break;
  }

  CFRelease(managed_spaces_ref);
  CFRelease(active_display_ref);
  return found;
}

static int sr_decode_direction(CGEventRef event_ref, enum sr_direction *out_direction) {
  const int64_t gesture_type =
      CGEventGetIntegerValueField(event_ref, SR_GESTURE_TYPE_FIELD);
  const double delta = CGEventGetDoubleValueField(event_ref, SR_GESTURE_DELTA_FIELD);

  if (out_direction == NULL) {
    return 0;
  }

  switch (gesture_type) {
    case 1:
      *out_direction = (delta > 0.0) ? SR_DIRECTION_RIGHT : SR_DIRECTION_LEFT;
      return 1;
    case 2:
      *out_direction = (delta > 0.0) ? SR_DIRECTION_UP : SR_DIRECTION_DOWN;
      return 1;
    default:
      return 0;
  }
}

static int sr_decode_phase(CGEventRef event_ref, enum sr_gesture_phase *out_phase) {
  const int64_t phase = CGEventGetIntegerValueField(event_ref, SR_GESTURE_PHASE_FIELD);

  if (out_phase == NULL) {
    return 0;
  }

  switch (phase) {
    case SR_GESTURE_PHASE_BEGIN:
      *out_phase = SR_GESTURE_PHASE_BEGIN;
      return 1;
    case SR_GESTURE_PHASE_UPDATE:
      *out_phase = SR_GESTURE_PHASE_UPDATE;
      return 1;
    case SR_GESTURE_PHASE_END:
      *out_phase = SR_GESTURE_PHASE_END;
      return 1;
    default:
      return 0;
  }
}

static void sr_populate_gesture_event(
    CGEventRef event_ref,
    enum sr_gesture_phase phase,
    enum sr_direction direction) {
  const int64_t gesture_type = (int64_t)sr_gesture_type(direction);
  const double direction_sign = (double)sr_direction_sign(direction);

  CGEventSetIntegerValueField(event_ref, SR_EVENT_TYPE_FIELD, 30);
  CGEventSetIntegerValueField(event_ref, SR_GESTURE_SUBTYPE_FIELD, 0x17);
  CGEventSetIntegerValueField(event_ref, SR_GESTURE_PHASE_FIELD, (int64_t)phase);
  CGEventSetIntegerValueField(
      event_ref, SR_GESTURE_PHASE_MIRROR_FIELD, (int64_t)phase);
  CGEventSetIntegerValueField(event_ref, SR_GESTURE_FINGER_COUNT_FIELD, 0x03);
  CGEventSetIntegerValueField(event_ref, SR_GESTURE_TYPE_FIELD, gesture_type);
  CGEventSetIntegerValueField(
      event_ref, SR_GESTURE_TYPE_MIRROR_FIELD, gesture_type);

  if (phase == SR_GESTURE_PHASE_END) {
    CGEventSetDoubleValueField(
        event_ref, SR_GESTURE_DELTA_FIELD, 1.0 * direction_sign);
    CGEventSetDoubleValueField(
        event_ref, SR_GESTURE_MOMENTUM_X_FIELD, 29100.0 * direction_sign);
    CGEventSetDoubleValueField(
        event_ref, SR_GESTURE_MOMENTUM_Y_FIELD, 29100.0 * direction_sign);
  } else {
    CGEventSetDoubleValueField(
        event_ref, SR_GESTURE_DELTA_FIELD, 0.0001 * direction_sign);
  }
}

static int sr_post_gesture_phase(
    sr_tap_context *context,
    CGEventTapProxy proxy,
    enum sr_gesture_phase phase,
    enum sr_direction direction) {
  CGEventRef event_ref;

  if (context == NULL || context->synthetic_source_ref == NULL) {
    return 0;
  }

  event_ref = CGEventCreate(context->synthetic_source_ref);
  if (event_ref == NULL) {
    return 0;
  }

  sr_populate_gesture_event(event_ref, phase, direction);
  CGEventTapPostEvent(proxy, event_ref);
  CFRelease(event_ref);
  return 1;
}

static int sr_post_fast_swipe(sr_tap_context *context, CGEventTapProxy proxy, enum sr_direction direction) {
  return sr_post_gesture_phase(context, proxy, SR_GESTURE_PHASE_BEGIN, direction) &&
         sr_post_gesture_phase(context, proxy, SR_GESTURE_PHASE_UPDATE, direction) &&
         sr_post_gesture_phase(context, proxy, SR_GESTURE_PHASE_END, direction);
}

static CGEventRef sr_event_tap_callback(
    CGEventTapProxy proxy,
    CGEventType type,
    CGEventRef event_ref,
    void *user_info) {
  sr_tap_context *context = (sr_tap_context *)user_info;
  enum sr_gesture_phase phase;
  enum sr_direction direction;
  const int64_t marker =
      CGEventGetIntegerValueField(event_ref, kCGEventSourceUserData);

  if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
    const char *reason =
        (type == kCGEventTapDisabledByTimeout) ? "timeout" : "user input";

    if (context != NULL && context->tap_ref != NULL) {
      fprintf(stderr, "Event tap disabled by %s, re-enabling.\n", reason);
      CGEventTapEnable(context->tap_ref, true);
    }
    return event_ref;
  }

  if (type != (CGEventType)30) {
    return event_ref;
  }

  if (marker == SR_SYNTHETIC_EVENT_MARKER) {
    return event_ref;
  }

  if (!sr_decode_phase(event_ref, &phase) || !sr_decode_direction(event_ref, &direction)) {
    return event_ref;
  }

  if (phase == SR_GESTURE_PHASE_BEGIN) {
    if (sr_is_horizontal_direction(direction)) {
      sr_space_bounds bounds;

      if (sr_get_active_display_space_bounds(&bounds)) {
        if (direction == SR_DIRECTION_LEFT && bounds.current_index == 0) {
          fprintf(stdout, "Ignoring left begin gesture at first space.\n");
          fflush(stdout);
          return NULL;
        }

        if (direction == SR_DIRECTION_RIGHT &&
            bounds.current_index >= bounds.num_spaces - 1) {
          fprintf(stdout, "Ignoring right begin gesture at last space.\n");
          fflush(stdout);
          return NULL;
        }
      } else {
        fprintf(stderr, "Failed to determine active display space bounds; replaying gesture.\n");
      }
    }

    fprintf(stdout, "Intercepted begin gesture: %s\n", sr_direction_name(direction));
    fflush(stdout);

    if (!sr_post_fast_swipe(context, proxy, direction)) {
      fprintf(stderr, "Failed to replay synthetic fast swipe for %s.\n",
          sr_direction_name(direction));
      return event_ref;
    }
  }

  return NULL;
}

int main(int argc, char **argv) {
  sr_tap_context context;
  CFMachPortRef tap_ref;
  CFRunLoopSourceRef run_loop_source_ref;
  CGEventMask event_mask;

  if (argc != 1) {
    fprintf(stderr, "Usage: %s\n", argv[0]);
    return EXIT_FAILURE;
  }

  if (!AXIsProcessTrusted()) {
    fprintf(
        stderr,
        "Accessibility permission is required. Enable it in System Settings "
        "> Privacy & Security > Accessibility and try again.\n");
    return EXIT_FAILURE;
  }

  context.tap_ref = NULL;
  context.synthetic_source_ref = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
  if (context.synthetic_source_ref == NULL) {
    fprintf(stderr, "Failed to create synthetic CoreGraphics event source.\n");
    return EXIT_FAILURE;
  }

  CGEventSourceSetUserData(context.synthetic_source_ref, SR_SYNTHETIC_EVENT_MARKER);

  event_mask = CGEventMaskBit((CGEventType)30);
  tap_ref = CGEventTapCreate(
      kCGHIDEventTap,
      kCGHeadInsertEventTap,
      kCGEventTapOptionDefault,
      event_mask,
      sr_event_tap_callback,
      &context);
  if (tap_ref == NULL) {
    fprintf(stderr, "Failed to create HID event tap.\n");
    CFRelease(context.synthetic_source_ref);
    return EXIT_FAILURE;
  }
  context.tap_ref = tap_ref;

  run_loop_source_ref = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap_ref, 0);
  if (run_loop_source_ref == NULL) {
    fprintf(stderr, "Failed to create run loop source for event tap.\n");
    CFRelease(tap_ref);
    CFRelease(context.synthetic_source_ref);
    return EXIT_FAILURE;
  }

  CFRunLoopAddSource(CFRunLoopGetCurrent(), run_loop_source_ref, kCFRunLoopCommonModes);
  CGEventTapEnable(tap_ref, true);

  fprintf(stdout, "fast_swipe_tap listening for begin gesture events.\n");
  fflush(stdout);

  CFRunLoopRun();

  CFRelease(run_loop_source_ref);
  CFRelease(tap_ref);
  CFRelease(context.synthetic_source_ref);
  return EXIT_SUCCESS;
}

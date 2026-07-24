#include <ApplicationServices/ApplicationServices.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum sh_gesture_phase {
  SH_GESTURE_PHASE_BEGIN = 1,
  SH_GESTURE_PHASE_UPDATE = 2,
  SH_GESTURE_PHASE_END = 4
};

enum sh_direction {
  SH_DIRECTION_LEFT,
  SH_DIRECTION_RIGHT,
  SH_DIRECTION_UP,
  SH_DIRECTION_DOWN
};

static const CGEventField SH_EVENT_TYPE_FIELD = (CGEventField)0x37;
static const CGEventField SH_GESTURE_SUBTYPE_FIELD = (CGEventField)0x6e;
static const CGEventField SH_GESTURE_TYPE_FIELD = (CGEventField)0x7b;
static const CGEventField SH_GESTURE_DELTA_FIELD = (CGEventField)0x7c;
static const CGEventField SH_GESTURE_MOMENTUM_X_FIELD = (CGEventField)0x81;
static const CGEventField SH_GESTURE_MOMENTUM_Y_FIELD = (CGEventField)0x82;
static const CGEventField SH_GESTURE_PHASE_FIELD = (CGEventField)0x84;
static const CGEventField SH_GESTURE_PHASE_MIRROR_FIELD = (CGEventField)0x86;
static const CGEventField SH_GESTURE_FINGER_COUNT_FIELD = (CGEventField)0x8a;
static const CGEventField SH_GESTURE_TYPE_MIRROR_FIELD = (CGEventField)0xa5;

static void sh_print_usage(const char *program_name) {
  fprintf(stderr, "Usage: %s left|right|up|down\n", program_name);
}

static int sh_direction_sign(enum sh_direction direction) {
  switch (direction) {
    case SH_DIRECTION_RIGHT:
    case SH_DIRECTION_UP:
      return 1;
    case SH_DIRECTION_LEFT:
    case SH_DIRECTION_DOWN:
      return -1;
  }

  return 0;
}

static int sh_gesture_type(enum sh_direction direction) {
  switch (direction) {
    case SH_DIRECTION_LEFT:
    case SH_DIRECTION_RIGHT:
      return 1;
    case SH_DIRECTION_UP:
    case SH_DIRECTION_DOWN:
      return 2;
  }

  return 0;
}

static int sh_parse_direction(
    const char *argument,
    enum sh_direction *out_direction,
    const char **out_label) {
  if (argument == NULL || out_direction == NULL || out_label == NULL) {
    return 0;
  }

  if (strcmp(argument, "left") == 0) {
    *out_direction = SH_DIRECTION_LEFT;
    *out_label = "left";
    return 1;
  }

  if (strcmp(argument, "right") == 0) {
    *out_direction = SH_DIRECTION_RIGHT;
    *out_label = "right";
    return 1;
  }

  if (strcmp(argument, "up") == 0) {
    *out_direction = SH_DIRECTION_UP;
    *out_label = "up";
    return 1;
  }

  if (strcmp(argument, "down") == 0) {
    *out_direction = SH_DIRECTION_DOWN;
    *out_label = "down";
    return 1;
  }

  return 0;
}

static void sh_populate_gesture_event(
    CGEventRef event_ref,
    enum sh_gesture_phase phase,
    enum sh_direction direction) {
  const int64_t gesture_type = (int64_t)sh_gesture_type(direction);
  const double direction_sign = (double)sh_direction_sign(direction);

  CGEventSetIntegerValueField(event_ref, SH_EVENT_TYPE_FIELD, 30);
  CGEventSetIntegerValueField(event_ref, SH_GESTURE_SUBTYPE_FIELD, 0x17);
  CGEventSetIntegerValueField(event_ref, SH_GESTURE_PHASE_FIELD, (int64_t)phase);
  CGEventSetIntegerValueField(
      event_ref, SH_GESTURE_PHASE_MIRROR_FIELD, (int64_t)phase);
  CGEventSetIntegerValueField(event_ref, SH_GESTURE_FINGER_COUNT_FIELD, 0x03);
  CGEventSetIntegerValueField(event_ref, SH_GESTURE_TYPE_FIELD, gesture_type);
  CGEventSetIntegerValueField(
      event_ref, SH_GESTURE_TYPE_MIRROR_FIELD, gesture_type);

  if (phase == SH_GESTURE_PHASE_END) {
    CGEventSetDoubleValueField(
        event_ref, SH_GESTURE_DELTA_FIELD, 1.0 * direction_sign);
    CGEventSetDoubleValueField(
        event_ref, SH_GESTURE_MOMENTUM_X_FIELD, 29100.0 * direction_sign);
    CGEventSetDoubleValueField(
        event_ref, SH_GESTURE_MOMENTUM_Y_FIELD, 29100.0 * direction_sign);
  } else {
    CGEventSetDoubleValueField(
        event_ref, SH_GESTURE_DELTA_FIELD, 0.0001 * direction_sign);
  }
}

static int sh_post_gesture_phase(
    CGEventSourceRef source_ref,
    enum sh_gesture_phase phase,
    enum sh_direction direction) {
  CGEventRef event_ref = CGEventCreate(source_ref);

  if (event_ref == NULL) {
    return 0;
  }

  sh_populate_gesture_event(event_ref, phase, direction);
  CGEventPost(kCGHIDEventTap, event_ref);
  CFRelease(event_ref);

  return 1;
}

static int sh_post_swipe(enum sh_direction direction) {
  CGEventSourceRef source_ref =
      CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
  int ok = 0;

  if (source_ref == NULL) {
    fprintf(stderr, "Failed to create CoreGraphics event source.\n");
    return 0;
  }

  ok = sh_post_gesture_phase(source_ref, SH_GESTURE_PHASE_BEGIN, direction) &&
       sh_post_gesture_phase(source_ref, SH_GESTURE_PHASE_UPDATE, direction) &&
       sh_post_gesture_phase(source_ref, SH_GESTURE_PHASE_END, direction);

  CFRelease(source_ref);
  return ok;
}

int main(int argc, char **argv) {
  enum sh_direction direction;
  const char *direction_label = NULL;

  if (argc != 2) {
    sh_print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (!sh_parse_direction(argv[1], &direction, &direction_label)) {
    sh_print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (!AXIsProcessTrusted()) {
    fprintf(
        stderr,
        "Accessibility permission is required. Enable it in System Settings "
        "> Privacy & Security > Accessibility and try again.\n");
    return EXIT_FAILURE;
  }

  if (!sh_post_swipe(direction)) {
    fprintf(stderr, "Failed to create or post synthetic swipe gesture events.\n");
    return EXIT_FAILURE;
  }

  printf(
      "Posted synthetic Fast Swipe gesture (%s). macOS may still ignore it "
      "depending on system settings.\n",
      direction_label);
  return EXIT_SUCCESS;
}

#include <ApplicationServices/ApplicationServices.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void sr_print_usage(const char *program_name) {
  fprintf(stderr, "Usage: %s left|right|up|down\n", program_name);
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

static int sr_parse_direction(
    const char *argument,
    enum sr_direction *out_direction,
    const char **out_label) {
  if (argument == NULL || out_direction == NULL || out_label == NULL) {
    return 0;
  }

  if (strcmp(argument, "left") == 0) {
    *out_direction = SR_DIRECTION_LEFT;
    *out_label = "left";
    return 1;
  }

  if (strcmp(argument, "right") == 0) {
    *out_direction = SR_DIRECTION_RIGHT;
    *out_label = "right";
    return 1;
  }

  if (strcmp(argument, "up") == 0) {
    *out_direction = SR_DIRECTION_UP;
    *out_label = "up";
    return 1;
  }

  if (strcmp(argument, "down") == 0) {
    *out_direction = SR_DIRECTION_DOWN;
    *out_label = "down";
    return 1;
  }

  return 0;
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
    CGEventSourceRef source_ref,
    enum sr_gesture_phase phase,
    enum sr_direction direction) {
  CGEventRef event_ref = CGEventCreate(source_ref);

  if (event_ref == NULL) {
    return 0;
  }

  sr_populate_gesture_event(event_ref, phase, direction);
  CGEventPost(kCGHIDEventTap, event_ref);
  CFRelease(event_ref);

  return 1;
}

static int sr_post_swipe(enum sr_direction direction) {
  CGEventSourceRef source_ref =
      CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
  int ok = 0;

  if (source_ref == NULL) {
    fprintf(stderr, "Failed to create CoreGraphics event source.\n");
    return 0;
  }

  ok = sr_post_gesture_phase(source_ref, SR_GESTURE_PHASE_BEGIN, direction) &&
       sr_post_gesture_phase(source_ref, SR_GESTURE_PHASE_UPDATE, direction) &&
       sr_post_gesture_phase(source_ref, SR_GESTURE_PHASE_END, direction);

  CFRelease(source_ref);
  return ok;
}

int main(int argc, char **argv) {
  enum sr_direction direction;
  const char *direction_label = NULL;

  if (argc != 2) {
    sr_print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (!sr_parse_direction(argv[1], &direction, &direction_label)) {
    sr_print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (!AXIsProcessTrusted()) {
    fprintf(
        stderr,
        "Accessibility permission is required. Enable it in System Settings "
        "> Privacy & Security > Accessibility and try again.\n");
    return EXIT_FAILURE;
  }

  if (!sr_post_swipe(direction)) {
    fprintf(stderr, "Failed to create or post synthetic swipe gesture events.\n");
    return EXIT_FAILURE;
  }

  printf(
      "Posted synthetic Fast Swipe gesture (%s). macOS may still ignore it "
      "depending on system settings.\n",
      direction_label);
  return EXIT_SUCCESS;
}

#import "SRSettingsStore.h"

#include <spacerabbit/settings.hpp>

namespace {

NSString *const SRSettingsStoreErrorDomain = @"com.animaslabs.SpaceRabbit.Settings";

enum settings_store_error_code : NSInteger {
  settings_store_error_invalid_root = 1,
  settings_store_error_invalid_hotkeys = 2,
  settings_store_error_invalid_modifier = 3,
  settings_store_error_invalid_hotkey = 4,
};

struct known_hotkey_metadata final {
  const char *action_id;
  const char *section_title;
  const char *display_name;
};

constexpr known_hotkey_metadata known_hotkeys[] = {
    {"switch_space_left", "Workspace", "Switch Space Left"},
    {"switch_space_right", "Workspace", "Switch Space Right"},
    {"switch_space_1", "Workspace", "Switch To Space 1"},
    {"switch_space_2", "Workspace", "Switch To Space 2"},
    {"switch_space_3", "Workspace", "Switch To Space 3"},
    {"switch_space_4", "Workspace", "Switch To Space 4"},
    {"switch_space_5", "Workspace", "Switch To Space 5"},
    {"switch_space_6", "Workspace", "Switch To Space 6"},
    {"switch_space_7", "Workspace", "Switch To Space 7"},
    {"switch_space_8", "Workspace", "Switch To Space 8"},
    {"switch_space_9", "Workspace", "Switch To Space 9"},
    {"switch_space_10", "Workspace", "Switch To Space 10"},
    {"switch_display_left", "Display", "Switch Display Left"},
    {"switch_display_right", "Display", "Switch Display Right"},
    {"switch_display_1", "Display", "Switch To Display 1"},
    {"switch_display_2", "Display", "Switch To Display 2"},
    {"switch_display_3", "Display", "Switch To Display 3"},
    {"switch_display_4", "Display", "Switch To Display 4"},
    {"switch_display_5", "Display", "Switch To Display 5"},
    {"switch_display_6", "Display", "Switch To Display 6"},
    {"switch_display_7", "Display", "Switch To Display 7"},
    {"switch_display_8", "Display", "Switch To Display 8"},
    {"switch_display_9", "Display", "Switch To Display 9"},
    {"switch_display_10", "Display", "Switch To Display 10"},
    {"window_focus_next", "Window Focus", "Focus Next Window"},
    {"window_focus_prev", "Window Focus", "Focus Previous Window"},
    {"mission_control_toggle", "System", "Toggle Mission Control"},
    {"expose_toggle", "System", "Toggle App Expose"},
};

auto make_error(NSInteger code, NSString *description) -> NSError * {
  return [NSError errorWithDomain:SRSettingsStoreErrorDomain
                             code:code
                         userInfo:@{NSLocalizedDescriptionKey : description}];
}

auto trim_string(NSString *value) -> NSString * {
  return [value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

auto string_from_std(const std::string &value) -> NSString * {
  return [NSString stringWithUTF8String:value.c_str()];
}

auto make_default_settings_dictionary() -> NSDictionary<NSString *, id> * {
  return @{
    @"version" : @"1.0",
    @"workspaceWrap" : @NO,
    @"displayWrap" : @NO,
    @"trayScroll" : @YES,
    @"trayScrollInverted" : @NO,
    @"hotkeys" : @{},
    @"fastSwipe" : @YES,
    @"telemetryEnabled" : @YES,
  };
}

auto settings_file_path(NSURL *url) -> std::filesystem::path {
  return std::filesystem::path([url fileSystemRepresentation]);
}

auto mutable_root_dictionary(NSURL *settingsURL, NSError **error)
    -> NSMutableDictionary<NSString *, id> * {
  NSData *settingsData = [NSData dataWithContentsOfURL:settingsURL options:0 error:error];
  if (settingsData == nil) {
    return nil;
  }

  id rawSettings = [NSJSONSerialization JSONObjectWithData:settingsData
                                                   options:NSJSONReadingMutableContainers
                                                     error:error];
  if (rawSettings == nil) {
    return nil;
  }

  if (![rawSettings isKindOfClass:[NSDictionary class]]) {
    if (error != NULL) {
      *error = make_error(settings_store_error_invalid_root,
                          @"Settings file must contain a top-level JSON object.");
    }
    return nil;
  }

  return [(NSDictionary *)rawSettings mutableCopy];
}

auto normalized_modifiers(NSString *value, NSError **error) -> NSArray<NSString *> * {
  NSString *trimmed = trim_string(value);
  if (trimmed.length == 0) {
    return @[];
  }

  NSArray<NSString *> *parts =
      [trimmed componentsSeparatedByCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@",+"]];
  NSMutableArray<NSString *> *modifiers = [NSMutableArray array];
  NSMutableSet<NSString *> *seen = [NSMutableSet set];

  for (NSString *part in parts) {
    NSString *candidate = [trim_string(part) lowercaseString];
    if (candidate.length == 0) {
      continue;
    }

    NSString *normalized = nil;
    if ([candidate isEqualToString:@"ctrl"] || [candidate isEqualToString:@"control"]) {
      normalized = @"ctrl";
    } else if ([candidate isEqualToString:@"option"] || [candidate isEqualToString:@"alt"]) {
      normalized = @"option";
    } else if ([candidate isEqualToString:@"cmd"] || [candidate isEqualToString:@"command"] ||
               [candidate isEqualToString:@"meta"]) {
      normalized = @"cmd";
    } else if ([candidate isEqualToString:@"shift"]) {
      normalized = @"shift";
    }

    if (normalized == nil) {
      if (error != NULL) {
        *error = make_error(
            settings_store_error_invalid_modifier,
            [NSString stringWithFormat:
                          @"Unsupported modifier \"%@\". Use ctrl, option, cmd, or shift.",
                          part]);
      }
      return nil;
    }

    if (![seen containsObject:normalized]) {
      [seen addObject:normalized];
      [modifiers addObject:normalized];
    }
  }

  return modifiers;
}

}  // namespace

@implementation SRHotkeyItem
@end

@implementation SRSettingsDocument
@end

@implementation SRSettingsStore

+ (NSURL *)settingsFileURL:(NSError **)error {
  NSFileManager *fileManager = [NSFileManager defaultManager];
  NSURL *applicationSupportDirectory =
      [[fileManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask] firstObject];
  if (applicationSupportDirectory == nil) {
    if (error != NULL) {
      *error = [NSError errorWithDomain:NSCocoaErrorDomain
                                   code:NSFileNoSuchFileError
                               userInfo:@{NSLocalizedDescriptionKey : @"Application Support directory not found."}];
    }
    return nil;
  }

  NSURL *spaceRabbitDirectory = [applicationSupportDirectory URLByAppendingPathComponent:@"SpaceRabbit"
                                                                              isDirectory:YES];
  if (![fileManager createDirectoryAtURL:spaceRabbitDirectory
             withIntermediateDirectories:YES
                              attributes:nil
                                   error:error]) {
    return nil;
  }

  NSURL *settingsURL = [spaceRabbitDirectory URLByAppendingPathComponent:@"settings.json"];
  if (![fileManager fileExistsAtPath:settingsURL.path]) {
    NSData *settingsData = [NSJSONSerialization dataWithJSONObject:make_default_settings_dictionary()
                                                           options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys
                                                             error:error];
    if (settingsData == nil) {
      return nil;
    }

    NSMutableData *mutableData = [settingsData mutableCopy];
    [mutableData appendData:[@"\n" dataUsingEncoding:NSUTF8StringEncoding]];
    if (![mutableData writeToURL:settingsURL options:NSDataWritingAtomic error:error]) {
      return nil;
    }
  }

  return settingsURL;
}

+ (SRSettingsDocument *)loadDocument:(NSError **)error {
  NSURL *settingsURL = [self settingsFileURL:error];
  if (settingsURL == nil) {
    return nil;
  }

  const auto loaded = spacerabbit::settings::load(settings_file_path(settingsURL));
  if (!loaded.has_value()) {
    if (error != NULL) {
      NSString *message = loaded.error().message.empty()
                              ? @"Failed to load settings."
                              : string_from_std(loaded.error().message);
      *error = make_error(NSFileReadUnknownError, message);
    }
    return nil;
  }

  const spacerabbit::settings::document &parsed = *loaded;
  SRSettingsDocument *document = [[SRSettingsDocument alloc] init];
  document.version = string_from_std(parsed.version);
  document.workspaceWrap = parsed.workspace_wrap;
  document.displayWrap = parsed.display_wrap;
  document.trayScroll = parsed.tray_scroll;
  document.trayScrollInverted = parsed.tray_scroll_inverted;
  document.fastSwipe = parsed.fast_swipe;
  document.telemetryEnabled = parsed.telemetry_enabled;

  NSMutableArray<SRHotkeyItem *> *hotkeys = [NSMutableArray arrayWithCapacity:std::size(known_hotkeys)];
  for (const auto &metadata : known_hotkeys) {
    SRHotkeyItem *item = [[SRHotkeyItem alloc] init];
    item.actionID = @(metadata.action_id);
    item.sectionTitle = @(metadata.section_title);
    item.displayName = @(metadata.display_name);
    item.key = @"";
    item.modifiersText = @"";
    item.enabled = NO;

    const auto hotkeyIt = parsed.hotkeys.find(metadata.action_id);
    if (hotkeyIt != parsed.hotkeys.end() && hotkeyIt->second.has_value()) {
      item.key = string_from_std(hotkeyIt->second->key);

      NSMutableArray<NSString *> *modifiers = [NSMutableArray arrayWithCapacity:hotkeyIt->second->modifiers.size()];
      for (const std::string &modifier : hotkeyIt->second->modifiers) {
        [modifiers addObject:string_from_std(modifier)];
      }
      item.modifiersText = [modifiers componentsJoinedByString:@", "];
      item.enabled = hotkeyIt->second->enabled;
    }

    [hotkeys addObject:item];
  }

  document.hotkeys = [hotkeys copy];
  return document;
}

+ (BOOL)saveDocument:(SRSettingsDocument *)document error:(NSError **)error {
  NSURL *settingsURL = [self settingsFileURL:error];
  if (settingsURL == nil) {
    return NO;
  }

  NSMutableDictionary<NSString *, id> *root = mutable_root_dictionary(settingsURL, error);
  if (root == nil) {
    return NO;
  }

  NSMutableDictionary<NSString *, id> *hotkeys = nil;
  id hotkeysValue = root[@"hotkeys"];
  if (hotkeysValue == nil || [hotkeysValue isKindOfClass:[NSDictionary class]]) {
    hotkeys = hotkeysValue != nil ? [(NSDictionary *)hotkeysValue mutableCopy] : [NSMutableDictionary dictionary];
  } else {
    if (error != NULL) {
      *error = make_error(settings_store_error_invalid_hotkeys,
                          @"The hotkeys field must be a JSON object.");
    }
    return NO;
  }

  for (SRHotkeyItem *item in document.hotkeys) {
    NSString *key = trim_string(item.key ?: @"");
    NSArray<NSString *> *modifiers = normalized_modifiers(item.modifiersText ?: @"", error);
    if (modifiers == nil) {
      return NO;
    }

    if (item.enabled && key.length == 0) {
      if (error != NULL) {
        *error = make_error(
            settings_store_error_invalid_hotkey,
            [NSString stringWithFormat:@"Hotkey \"%@\" must have a key when enabled.",
                                       item.displayName]);
      }
      return NO;
    }

    if (key.length == 0 && modifiers.count > 0) {
      if (error != NULL) {
        *error = make_error(
            settings_store_error_invalid_hotkey,
            [NSString stringWithFormat:@"Hotkey \"%@\" cannot have modifiers without a key.",
                                       item.displayName]);
      }
      return NO;
    }

    if (key.length == 0) {
      hotkeys[item.actionID] = NSNull.null;
      continue;
    }

    hotkeys[item.actionID] = @{
      @"key" : key,
      @"modifiers" : modifiers,
      @"enabled" : @(item.enabled),
    };
  }

  root[@"version"] = document.version.length > 0 ? document.version : @"1.0";
  root[@"workspaceWrap"] = @(document.workspaceWrap);
  root[@"displayWrap"] = @(document.displayWrap);
  root[@"trayScroll"] = @(document.trayScroll);
  root[@"trayScrollInverted"] = @(document.trayScrollInverted);
  root[@"fastSwipe"] = @(document.fastSwipe);
  root[@"telemetryEnabled"] = @(document.telemetryEnabled);
  root[@"hotkeys"] = hotkeys;

  NSData *settingsData = [NSJSONSerialization dataWithJSONObject:root
                                                         options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys
                                                           error:error];
  if (settingsData == nil) {
    return NO;
  }

  NSMutableData *mutableData = [settingsData mutableCopy];
  [mutableData appendData:[@"\n" dataUsingEncoding:NSUTF8StringEncoding]];
  return [mutableData writeToURL:settingsURL options:NSDataWritingAtomic error:error];
}

@end

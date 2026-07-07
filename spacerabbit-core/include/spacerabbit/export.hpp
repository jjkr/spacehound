#pragma once

#if defined(_WIN32)
// Marks symbols that should be exported from or imported into a shared library.
#  if defined(SPACERABBIT_SHARED_BUILD)
#    define SPACERABBIT_EXPORT __declspec(dllexport)
#  elif defined(SPACERABBIT_SHARED)
#    define SPACERABBIT_EXPORT __declspec(dllimport)
#  else
#    define SPACERABBIT_EXPORT
#  endif
// Marks symbols that should remain hidden from the shared-library interface.
#  define SPACERABBIT_LOCAL
#else
// Marks symbols that should be exported from a shared library on ELF/Mach-O targets.
#  if defined(SPACERABBIT_SHARED_BUILD) || defined(SPACERABBIT_SHARED)
#    define SPACERABBIT_EXPORT __attribute__((visibility("default")))
#  else
#    define SPACERABBIT_EXPORT
#  endif
// Marks symbols that should remain hidden from the shared-library interface.
#  define SPACERABBIT_LOCAL __attribute__((visibility("hidden")))
#endif

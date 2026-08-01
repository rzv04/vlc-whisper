#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <path_to_plugin>\n", argv[0]);
    return 1;
  }

  const char *plugin_path = argv[1];
  printf("Loading plugin: %s\n", plugin_path);

#ifdef _WIN32
  HMODULE handle = LoadLibraryA(plugin_path);
  if (!handle) {
    fprintf(stderr, "Failed to load %s. Error: %lu\n", plugin_path, GetLastError());
    return 1;
  }
  void *sym = (void *)GetProcAddress(handle, "vlc_entry__3_0_0f");
  if (!sym) {
    fprintf(stderr, "Failed to find vlc_entry__3_0_0f. Error: %lu\n", GetLastError());
    FreeLibrary(handle);
    return 1;
  }
  FreeLibrary(handle);
#else
  void *handle = dlopen(plugin_path, RTLD_LAZY);
  if (!handle) {
    fprintf(stderr, "Failed to load plugin %s: %s\n", plugin_path, dlerror());
    return 1;
  }
  void *sym = dlsym(handle, "vlc_entry__3_0_0f");
  if (!sym) {
    fprintf(stderr, "Failed to find vlc_entry__3_0_0f in %s: %s\n", plugin_path, dlerror());
    dlclose(handle);
    return 1;
  }
  dlclose(handle);
#endif

  printf("Successfully loaded VLC module and resolved entry point.\n");
  return 0;
}

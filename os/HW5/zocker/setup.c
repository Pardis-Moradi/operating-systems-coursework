#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <string.h>

#include "setup.h"

static int setup_bin_dir(const char container_dir[256]);
static int setup_lib_dir(const char container_dir[256]);

int setup_zocker_dir(void) {
  struct stat st;
  char prefix[64];

  if (snprintf(prefix, sizeof(prefix), "%s", ZOCKER_PREFIX) < 0) {
    return 1;
  }

  if (stat(prefix, &st) == -1) {
    if (errno == ENOENT) {
      fprintf(stderr, "[ERR] ZOCKER_PREFIX %s does not exists\n",
              ZOCKER_PREFIX);
    }
    return 1;
  }

  if (!S_ISDIR(st.st_mode)) {
    fprintf(stderr, "[ERR] ZOCKER_PREFIX %s is not a directory\n",
            ZOCKER_PREFIX);
    return 1;
  }

  return 0;
}

int setup_container_dir(const char id[64], char container_dir[256]) {
  struct stat st;
  const size_t buffer_size = 256;

  if (snprintf(container_dir, buffer_size, "%s/%s", ZOCKER_PREFIX, id) < 0) {
    return 1;
  }

  if (stat(container_dir, &st) == -1) {
    if (errno != ENOENT) {
      return 1;
    }

    if (mkdir(container_dir, 0755) == -1) {
      fprintf(stderr, "[ERR] Failed to create container directory %s\n",
              container_dir);
      return 1;
    }
  } else if (!S_ISDIR(st.st_mode)) {
    fprintf(stderr, "[ERR] Path %s is not a directory\n", container_dir);
    return 1;
  }

  if (setup_bin_dir(container_dir) != 0) {
    fprintf(stderr, "[ERR] Failed to setup bin directory for container %s\n",
            id);
    return 1;
  }

  if (setup_lib_dir(container_dir) != 0) {
    fprintf(stderr, "[ERR] Failed to setup lib directory for container %s\n",
            id);
    return 1;
  }

  return 0;
}

static int setup_bin_dir(const char container_dir[256]) {
  char bin_dir[256];
  char cmd[512];

  if (snprintf(bin_dir, sizeof(bin_dir), "%s/bin", container_dir) < 0) {
    return 1;
  }
  if (mkdir(bin_dir, 0755) == -1) {
    fprintf(stderr, "[ERR] Failed to create bin directory %s\n", bin_dir);
    return 1;
  }

  // Copy /bin/sh into container/bin/sh
    snprintf(cmd, sizeof(cmd), "cp /bin/sh %s/sh", bin_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to copy /bin/sh into container\n");
        return 1;
    }

    // Copy /bin/ls into container/bin/ls
    snprintf(cmd, sizeof(cmd), "cp /bin/ls %s/ls", bin_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "[ERR] Failed to copy /bin/ls into container\n");
    }
    
    // Copy /bin/cat into container/bin/cat
    snprintf(cmd, sizeof(cmd), "cp /bin/cat %s/cat", bin_dir);
    system(cmd);
    
    // Copy /bin/pwd into container/bin/pwd
    snprintf(cmd, sizeof(cmd), "cp /bin/pwd %s/pwd", bin_dir);
    system(cmd);

    // Copy /bin/cd into container/bin/cd

  return 0;
}

static int setup_lib_dir(const char container_dir[256]) {
  char lib_dir[256];
  char cmd[512];
  FILE *fp;
  char line[512];

  if (snprintf(lib_dir, sizeof(lib_dir), "%s/lib", container_dir) < 0) {
    return 1;
  }
  if (mkdir(lib_dir, 0755) == -1) {
    fprintf(stderr, "[ERR] Failed to create lib directory %s\n", lib_dir);
    return 1;
  }

  // EXPLICITLY COPY DYNAMIC LINKER
  snprintf(cmd, sizeof(cmd), "cp -L /lib/ld-linux-aarch64.so.1 %s/", lib_dir);
  if (system(cmd) != 0) {
      fprintf(stderr, "[ERR] Failed to copy dynamic linker into container\n");
      return 1;
  }

  // Run ldd on all essential binaries copied in setup_bin_dir
  fp = popen("ldd /bin/sh /bin/ls /bin/cat /bin/pwd", "r");
  if (!fp) {
      fprintf(stderr, "[ERR] Failed to run ldd\n");
      return 1;
  }

  while (fgets(line, sizeof(line), fp)) {
      char *path_start = line;
      
      while (*path_start == ' ' || *path_start == '\t') {
          path_start++;
      }
      
      size_t len = strlen(path_start);
      if (len > 1 && path_start[len - 2] == ':') { // -2 accounts for potential newline
          continue; // Skip the line entirely
      }
      
      char *path = strstr(line, "=>");
      if (!path) {
          if (line[0] == '/') {
              path = strtok(line, " ");   // strip address
          } else {
              continue;
          }
      } else {
          // standard format: "library.so => /path/to/library.so (0x...)"
          path = strstr(line, "/");
      }

      if (!path) continue;

      path[strcspn(path, " \n")] = '\0'; // trim newline/spaces

      // Use -L to dereference symlinks and copy the actual file
      snprintf(cmd, sizeof(cmd), "cp -L %s %s/", path, lib_dir);
      
      system(cmd); 
  }

  pclose(fp);

  printf("ended successfully!\n");

  return 0;
}

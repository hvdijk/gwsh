#include <sys/stat.h>

int main(void) {
  static struct stat stat;
  return stat.st_mtim ? 1 : 0;
}

#include <inttypes.h>

int main(void) {
  static intmax_t i;
  static long ll;
  0 ? &i : &ll;
}

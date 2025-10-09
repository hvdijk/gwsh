int main(void) {
  void *l = &&label;
  goto *l;
label:
  return 0;
}

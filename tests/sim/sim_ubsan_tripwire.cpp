int main() {
    volatile int maximum = 2147483647;
    volatile int one = 1;
    volatile int overflow = maximum + one;
    return overflow == 0 ? 0 : 0;
}

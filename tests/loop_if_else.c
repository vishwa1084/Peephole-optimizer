int main() {
    int i = 0;
    int total = 0;
    while (i < 3) {
        if (i < 1) {
            total = total + 1;
        } else {
            total = total + 2;
        }
        i = i + 1;
    }
    return total;
}

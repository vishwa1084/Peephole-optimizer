int main() {
    int count = 0;
    for (int i = 0; i < 3; i = i + 1) {
        int j = 0;
        while (j < 2) {
            count = count + 1;
            j = j + 1;
        }
    }
    return count;
}

int main() {
    int a = 10;
    int b = 20;
    int unused = 99;

    int result = a;
    result = a;
    result = result + b;

    return result;
}
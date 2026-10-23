int main() {
    int a=4; int b=5; int c=0;
    if (a < b) c=c+1;
    if (a > b) c=c+2;
    if (a == b) c=c+4;
    if (a != b) c=c+2;
    if (a <= b) c=c+4;
    if (a >= b) c=c+8;
    return c;
}

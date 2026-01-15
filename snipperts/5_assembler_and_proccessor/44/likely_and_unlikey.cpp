int summa(int values[]) {
    int sum = 0;
    int sum1 = 0;
    for (int i = 0; i < 10; ++i) {
        if (values[i] >= 128) [[unlikely]] {
            sum += values[i];
        } else [[likely]] {
            sum1 += values[i];
        }
    }
    return sum - sum1;
}

int main() {
    int values[10];
    for (int i = 0; i < 10; ++i) {
        values[i] = 50 + i * i * i * i;
    }
    summa(values);
}
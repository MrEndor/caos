int main() {
    if (data[i] >= 128) [[likely]] {
        sum += data[i];
    }

    if (error) [[unlikely]] {
        handle_error();
    }

    if (__builtin_expect(data[i] >= 128, 1)) {  // 1 = true вероятнее
        sum += data[i];
    }

    if (__builtin_expect(error, 0)) {  // 0 = false вероятнее
        handle_error();
    }
}

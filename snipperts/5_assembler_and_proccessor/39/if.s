# if (x > 0) {
#     print("positive");
# } else {
#     print("negative");
# }

movq x(%rip), %rax      # rax = x
testq %rax, %rax        # установить флаги по rax
jle negative            # если <= 0, прыгнуть на negative

# положительное число
# ... код ...
jmp end

negative:
# ... код ...

end:
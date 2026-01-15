#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

// Timeout для прерывания зависания
void timeout_handler(int sig) {
    fprintf(stderr, "\n⏱️  ЗАВИСАНИЕ ОБНАРУЖЕНО! (timeout 3 сек)\n");
    exit(EXIT_FAILURE);
}

void setup_timeout(int seconds) {
    signal(SIGALRM, timeout_handler);
    alarm(seconds);
}

// ============================================================================
// ДЕМО 1: НЕПРАВИЛЬНО - забыл close(write end) в родителе
// ============================================================================

void demo1_parent_forgot_close(void) {
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║ ДЕМО 1: Родитель забыл close(pipefd[1])                   ║\n");
    printf("║ Результат: cat зависает на EOF                            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    int pipefd[2];
    pid_t pid1, pid2;

    pipe(pipefd);

    printf("Создан pipe: fd[0]=%d (read), fd[1]=%d (write)\n", pipefd[0], pipefd[1]);

    // ===== Process 1: echo =====
    pid1 = fork();
    if (pid1 == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        printf("echo: Пишу 'hello' в pipe\n");
        execlp("echo", "echo", "hello", NULL);
        exit(1);
    }
    printf("Fork процесс 1 (echo): PID=%d\n", pid1);

    // ===== Process 2: cat =====
    pid2 = fork();
    if (pid2 == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        printf("cat: Жду данные из pipe\n");
        execlp("cat", "cat", NULL);
        exit(1);
    }
    printf("Fork процесс 2 (cat): PID=%d\n", pid2);

    // ===== Родитель: ОШИБКА - забыли close(pipefd[1])! =====
    close(pipefd[0]);
    // ❌ ЗАБЫЛИ: close(pipefd[1]);
    printf("⚠️  Родитель: ЗАБЫЛИ close(pipefd[1])=%d!\n", pipefd[1]);
    printf("   Pipe write end всё ещё открыт в родителе!\n");
    printf("   cat не получит EOF и зависнет...\n\n");

    printf("Ожидание завершения процессов (timeout 3 сек)...\n");
    setup_timeout(3);

    int status1, status2;
    waitpid(pid1, &status1, 0);
    waitpid(pid2, &status2, 0);  // Здесь зависнет!

    printf("✓ Оба процесса завершились\n");
}

// ============================================================================
// ДЕМО 2: ПРАВИЛЬНО - close(write end) везде
// ============================================================================

void demo2_correct_close(void) {
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║ ДЕМО 2: Правильное закрытие всех дескрипторов             ║\n");
    printf("║ Результат: всё работает корректно                         ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    int pipefd[2];
    pid_t pid1, pid2;

    pipe(pipefd);

    printf("Создан pipe: fd[0]=%d (read), fd[1]=%d (write)\n", pipefd[0], pipefd[1]);

    // ===== Process 1: echo =====
    pid1 = fork();
    if (pid1 == 0) {
        close(pipefd[0]);  // ✓ Закрыли read end
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);  // ✓ Закрыли write end
        execlp("echo", "echo", "hello world", NULL);
        exit(1);
    }
    printf("Fork процесс 1 (echo): PID=%d\n", pid1);

    // ===== Process 2: wc =====
    pid2 = fork();
    if (pid2 == 0) {
        close(pipefd[1]);  // ✓ Закрыли write end
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);  // ✓ Закрыли read end
        execlp("wc", "wc", "-w", NULL);
        exit(1);
    }
    printf("Fork процесс 2 (wc): PID=%d\n", pid2);

    // ===== Родитель: ПРАВИЛЬНО =====
    close(pipefd[0]);  // ✓ Закрыли read end
    close(pipefd[1]);  // ✓ Закрыли write end
    printf("✓ Родитель: правильно закрыл оба дескриптора\n");
    printf("   cat получит EOF и завершится\n\n");

    printf("Ожидание завершения процессов...\n");

    int status1, status2;
    waitpid(pid1, &status1, 0);
    waitpid(pid2, &status2, 0);

    printf("✓ Оба процесса завершились корректно\n");
    printf("Результат:\n  echo 'hello world' | wc -w  →  2\n");
}

// ============================================================================
// ДЕМО 3: НЕПРАВИЛЬНО - оба процесса читают (DEADLOCK)
// ============================================================================

void demo3_both_read_deadlock(void) {
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║ ДЕМО 3: Оба процесса читают из pipe (DEADLOCK)             ║\n");
    printf("║ Результат: оба зависают, ждут данные                      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    int pipefd[2];
    pid_t pid1, pid2;

    pipe(pipefd);

    printf("Создан pipe: fd[0]=%d (read), fd[1]=%d (write)\n", pipefd[0], pipefd[1]);

    // ===== Process 1: читает из pipe =====
    pid1 = fork();
    if (pid1 == 0) {
        close(pipefd[1]);  // Закрыли write end
        // ❌ ЗАБЫЛИ: close(pipefd[0]);
        dup2(pipefd[0], STDIN_FILENO);
        printf("Process1: Читаю из pipe, жду данные...\n");
        fflush(stdout);
        char buf[100];
        read(STDIN_FILENO, buf, sizeof(buf));  // Зависит здесь!
        exit(1);
    }
    printf("Fork процесс 1 (reader): PID=%d\n", pid1);

    // ===== Process 2: также читает из pipe =====
    pid2 = fork();
    if (pid2 == 0) {
        close(pipefd[1]);  // Закрыли write end
        dup2(pipefd[0], STDIN_FILENO);
        printf("Process2: Читаю из pipe, жду данные...\n");
        fflush(stdout);
        char buf[100];
        read(STDIN_FILENO, buf, sizeof(buf));  // Зависит здесь!
        exit(1);
    }
    printf("Fork процесс 2 (reader): PID=%d\n", pid2);

    // ===== Родитель: никто не пишет! =====
    close(pipefd[0]);  // Закрыли оба конца
    close(pipefd[1]);
    printf("⚠️  Проблема: оба процесса читают, но никто не пишет!\n");
    printf("   DEADLOCK: оба ждут данные, которые никогда не придут\n\n");

    printf("Ожидание завершения процессов (timeout 3 сек)...\n");
    setup_timeout(3);

    int status1, status2;
    waitpid(pid1, &status1, 0);
    waitpid(pid2, &status2, 0);  // Здесь зависнет!

    printf("✓ (Не должны были дойти сюда)\n");
}

// ============================================================================
// ДЕМО 4: НЕПРАВИЛЬНО - оба процесса пишут (RACE CONDITION)
// ============================================================================

void demo4_both_write_race(void) {
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║ ДЕМО 4: Оба процесса пишут в pipe (RACE CONDITION)         ║\n");
    printf("║ Результат: данные смешиваются                             ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    int pipefd[2];
    pid_t pid1, pid2;

    pipe(pipefd);

    printf("Создан pipe: fd[0]=%d (read), fd[1]=%d (write)\n", pipefd[0], pipefd[1]);

    // ===== Process 1: пишет =====
    pid1 = fork();
    if (pid1 == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        for (int i = 0; i < 5; i++) {
            printf("From process1: %d\n", i);
            fflush(stdout);
            usleep(100000);  // 100ms
        }
        exit(0);
    }
    printf("Fork процесс 1 (writer): PID=%d\n", pid1);

    // ===== Process 2: ТАКЖЕ пишет (ОШИБКА) =====
    pid2 = fork();
    if (pid2 == 0) {
        close(pipefd[0]);
        // ❌ ДОЛЖНЫ БЫЛИ close(pipefd[1]) ПЕРЕД dup2!
        // Оставляем write end открытым!
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        for (int i = 0; i < 5; i++) {
            printf("From process2: %d\n", i);
            fflush(stdout);
            usleep(150000);  // 150ms
        }
        exit(0);
    }
    printf("Fork процесс 2 (also writer): PID=%d\n", pid2);

    // ===== Process 3: читает из pipe =====
    pid_t pid3 = fork();
    if (pid3 == 0) {
        close(pipefd[1]);  // Только читаем
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        // Читаем всё, что напишут process1 и process2
        char buf[100];
        while (read(STDIN_FILENO, buf, sizeof(buf)) > 0) {
            printf("[READER] %s", buf);
        }
        exit(0);
    }
    printf("Fork процесс 3 (reader): PID=%d\n", pid3);

    // ===== Родитель =====
    close(pipefd[0]);
    close(pipefd[1]);
    printf("⚠️  Проблема: два процесса пишут одновременно!\n");
    printf("   Данные будут перемешиваться (race condition)\n\n");

    int status1, status2, status3;
    printf("Ожидание завершения...\n");
    waitpid(pid1, &status1, 0);
    waitpid(pid2, &status2, 0);
    waitpid(pid3, &status3, 0);

    printf("\n✓ Все процессы завершились\n");
    printf("⚠️  Заметь: строки process1 и process2 перемешались!\n");
}

// ============================================================================
// ГЛАВНОЕ МЕНЮ
// ============================================================================

int main(int argc, char *argv[]) {
    if (argc > 1) {
        int demo = atoi(argv[1]);
        switch (demo) {
            case 1:
                demo1_parent_forgot_close();
                break;
            case 2:
                demo2_correct_close();
                break;
            case 3:
                demo3_both_read_deadlock();
                break;
            case 4:
                demo4_both_write_race();
                break;
            default:
                printf("Неизвестная демонстрация: %d\n", demo);
                return 1;
        }
    } else {
        printf("╔════════════════════════════════════════════════════════════╗\n");
        printf("║         Демонстрация ошибок при работе с pipe              ║\n");
        printf("╚════════════════════════════════════════════════════════════╝\n\n");
        printf("Использование: %s <номер_демо>\n\n", argv[0]);
        printf("Доступные демонстрации:\n");
        printf("  1 - Родитель забыл close(write end) → ЗАВИСАНИЕ\n");
        printf("  2 - Правильное закрытие дескрипторов → OK\n");
        printf("  3 - Оба процесса читают → DEADLOCK\n");
        printf("  4 - Оба процесса пишут → RACE CONDITION\n");
        printf("\nПримеры запуска:\n");
        printf("  gcc -o pipe_demo pipe_both_sides_demo.c\n");
        printf("  ./pipe_demo 1  # Демонстрация зависания\n");
        printf("  ./pipe_demo 2  # Правильная реализация\n");
    }

    return EXIT_SUCCESS;
}
#include <sys/capability.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    cap_t caps = cap_get_proc();

    if (cap_set_flag(caps, CAP_INHERITABLE, 1, (const cap_value_t[]){CAP_NET_RAW}, CAP_SET) == -1) {
        perror("cap_set_flag");
        exit(1);
    }
    if (cap_set_proc(caps) == -1) {
        perror("cap_set_proc");
        exit(1);
    }

    printf("Parent - Inheritable capabilities set\n");

    pid_t pid = fork();

    if (pid == 0) {
        cap_t child_caps = cap_get_proc();

        cap_flag_value_t val;
        cap_get_flag(child_caps, CAP_NET_RAW, CAP_INHERITABLE, &val);
        printf("Child - CAP_NET_RAW in Inheritable: %s\n", (val == CAP_SET) ? "yes" : "no");
        char *text = cap_to_text(child_caps, NULL);
        printf("Current Effective: %s\n", text);

        cap_free(child_caps);
        exit(0);
    } else {
        wait(NULL);
    }

    cap_free(caps);
}
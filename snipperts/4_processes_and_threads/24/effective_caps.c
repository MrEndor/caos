#include <sys/capability.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    cap_t caps = cap_get_proc();

    char *text = cap_to_text(caps, NULL);
    printf("Current Effective: %s\n", text);

    cap_set_flag(caps, CAP_EFFECTIVE, 1, (const cap_value_t[]){CAP_NET_RAW}, CAP_SET);
    cap_set_proc(caps);

    printf("After adding CAP_NET_RAW to Effective\n");
    text = cap_to_text(caps, NULL);
    printf("Current Effective: %s\n", text);

    cap_set_flag(caps, CAP_EFFECTIVE, 1, (const cap_value_t[]){CAP_NET_RAW}, CAP_CLEAR);
    cap_set_proc(caps);

    printf("After removing CAP_NET_RAW from Effective\n");
    text = cap_to_text(caps, NULL);
    printf("Current Effective: %s\n", text);

    cap_free(caps);
}
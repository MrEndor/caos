# eBPF

**eBPF (extended Berkeley Packet Filter)** — виртуальная машина внутри ядра Linux, в которую можно загружать программы, проверенные верификатором, и привязывать их к множеству kernel- и userspace-точек: системные вызовы, сетевые пакеты, файловые операции, LSM-хуки. Программы выполняются в kernel-режиме, общаются с userspace через map'ы и ringbuf'ы, и дают возможность реализовать observability, networking и security без модификации исходного кода ядра.

Раздел разделён на четыре статьи: общая архитектура VM и инструментарий, и три глубоких разреза по основным применениям.

## Темы

| Страница | Что рассматривается |
|----------|---------------------|
| [Основы eBPF](foundations.md) | BPF VM (64-bit RISC, 10 регистров), verifier, JIT, helpers, maps (HASH/ARRAY/RINGBUF/...), program types, libbpf + CO-RE через BTF, bpftrace, реальные продукты |
| [eBPF tracing](tracing.md) | Attach points (kprobe/uprobe/tracepoint/USDT/perf_event/fentry), bpftrace DSL, bcc tools, libbpf+skeleton, Pixie/Parca/Pyroscope |
| [eBPF в сетевом стеке](networking.md) | XDP (native/offloaded/generic), TC ingress/egress, sockops, sk_msg/sk_skb, cgroup-bpf, AF_XDP zero-copy, Cilium, Katran |
| [eBPF для безопасности](security.md) | LSM-BPF (KRSI), 250+ хуков, Tetragon, Falco, `bpf_send_signal`, CVE history, CAP_BPF split |

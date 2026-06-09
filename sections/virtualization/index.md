# Виртуализация

Виртуализация — это набор hardware и software механизмов, которые позволяют запускать одну ОС внутри другой (или несколько одновременно), с производительностью близкой к native и при этом с полной изоляцией. На ней построены публичные облака (AWS, GCP, Azure), serverless-платформы (Lambda, Cloud Run), современная CI-инфраструктура. Раздел разбирает все слои: от hardware-расширений Intel VT-x/AMD-V до user-space инструментов (QEMU, libvirt) и production-стэков (Firecracker, gVisor, KubeVirt).

## Темы

| Страница | Что рассматривается |
|----------|---------------------|
| [Основы виртуализации](foundations.md) | Type 1/2 гипервизоры, история (CP-67 → Xen → KVM), trap-and-emulate vs binary translation vs HW-assisted, KVM+QEMU стек, VM vs контейнеры |
| [CPU virtualization](cpu_virtualization.md) | VMX/SVM, VMCS (4 KB), VM-entry/exit с типами и стоимостью, MSR/IO bitmaps, posted interrupts, APICv, VPID, KVM paravirt (kvm-clock, kvm-steal-time, async page faults) |
| [Memory virtualization](memory_virtualization.md) | Shadow page tables (legacy) vs EPT (Intel)/NPT (AMD), 2D page walk, KSM (Kernel Same-page Merging), memory ballooning, hugepages в госте, vNUMA |
| [KVM API](kvm_api.md) | `/dev/kvm` ioctls (KVM_CREATE_VM/VCPU/RUN), vCPU run loop, обработка exits, минимальный hypervisor в 200 строк C |
| [QEMU internals](qemu_internals.md) | TCG (binary translation x86→ARM), device models, machine types (q35, microvm), QMP/HMP, qcow2, threading, snapshots |
| [virtio](virtio.md) | vring layout (split + packed), virtio-net/blk/scsi/fs/gpu, vhost-net, vhost-user, vDPA, indirect descriptors, event suppression |
| [I/O passthrough](io_passthrough.md) | VFIO, IOMMU (VT-d/AMD-Vi/SMMU), IOMMU groups, SR-IOV (PF + VFs), mdev/vGPU, GPU passthrough, NUMA-aware passthrough |
| [Live migration](live_migration.md) | Pre-copy, post-copy, hybrid; dirty page tracking (EPT D-bit, PML, write-protect); userfaultfd; RDMA transfer; CRIU как альтернатива |
| [Nested virtualization](nested_virtualization.md) | L0/L1/L2, VMCS shadowing (Intel), Nested SVM (AMD), nested EPT composition, cloud-provider support, KubeVirt |
| [MicroVMs](microvms.md) | Firecracker (AWS Lambda), Cloud Hypervisor, Kata Containers, gVisor (Sentry + Platform), сравнение overhead/startup/security, AWS Nitro |
| [Confidential Computing](confidential_computing.md) | TEE принципы (encryption + integrity + attestation), Intel SGX (enclaves, EPC, DCAP), Intel TDX, AMD SEV/SEV-ES/SEV-SNP, CoCo |

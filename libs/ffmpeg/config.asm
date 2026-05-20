%ifdef __i386__
%define ARCH_AARCH64          0
%define ARCH_ARM              0
%define ARCH_X86              1
%define ARCH_X86_32           1
%define ARCH_X86_64           0
%define HAVE_ALIGNED_STACK    0
%elifdef __x86_64__
%define ARCH_AARCH64          0
%define ARCH_ARM              0
%define ARCH_X86              1
%define ARCH_X86_32           0
%define ARCH_X86_64           1
%define HAVE_ALIGNED_STACK    1
%else
%error "Unsupported platform"
%endif

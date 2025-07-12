***This document records major architectural decisions made in the development of this OS. The purpose is: Document the reasoning, context, and tradeoffs behind critical changes, including past and future, as project evolves.

July 11th 2025

vmm.c refactoring plan: vmm.c to a modular memory subsystem

memory/
├── pmm.c              // Physical Memory Manager
├── vmm.c              // Virtual Memory Manager (MMU-dependent)
├── memory_core.c      // Common utilities & hardware access
├── address_space.c    // Addressing abstraction layer
├── memory_config.h    // Runtime configuration switches
└── memory_debug.c     // Debugging & verification tools

Detailed diagrams:
https://drive.google.com/file/d/1DynSIErjU63gyJtckzyGzf7rj0fC1naF/view?usp=sharing


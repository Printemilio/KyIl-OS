# Allocation/zeroing and serial logging — one-navigation comparison

Normal kernel changes: kernel memset uses non-SIMD REP STOSQ plus an exact byte tail; raw allocator zero_bytes delegates to it. Dedicated allocation policy, page alignment, full clearing, ownership and rollback are unchanged. Page-isolated payloads are used by ORA/process-memory projections; pooling these into ordinary unaligned blocks would break their contract.

The UART detects enabled/functional 16550 FIFO support at initialization. Counted debug_log_write batches at most 16 bytes per THRE readiness check (1 on non-FIFO UARTs); each bounded burst is IRQ/SMP protected. stderr is mirrored with one counted logger call per write, with no diagnostic suppression. FPU logger probes execute only when explicitly armed. Successful mmap hint-fallback traces require VEX_COMPAT_TRACE; failure diagnostics remain enabled.

## Exactly one real navigation

VirtualBox, 1 vCPU, 4096 MiB, native /webkit, native search `pomme fruit`, DuckDuckGo HTTPS. No renderer/JSC/network/scheduler change. Sampling: VirtualBox guestsample, 50 ms, five 2-second windows starting about 2.1 seconds after Enter, 200 root-PC samples; same fixed interval as previous run, includes some post-completion activity. This is sampling attribution, not an exact duration measurement.

| Metric | Previous guard-after run | allocator-after run |
|---|---:|---:|
| DOMInteractive | 5014 ms | 4589 ms |
| FCP | 6588 ms | 6225 ms |
| domComplete | 11211 ms | 10574 ms |
| loadEventEnd | 11231 ms | 10592 ms |
| allocator + kernel zeroing | 74/200 = 37.0% | 79/200 = 39.5% |
| kernel zeroing alone (zero_bytes or memset) | 66/200 = 33.0% | 63/200 = 31.5% |
| actual UART logger | 29/200 = 14.5% | 7/200 = 3.5% |

The earlier 26.5% referred only to zero_bytes under allocation, excluding separate memset samples and allocator indexing. After delegation, those samples appear under memset; comparing 26.5% directly to a missing zero_bytes symbol would be misleading. CPU shares are exclusive root-PC buckets. The broad category parser labels one PIC EOI outb as serial; that sample is excluded from the actual UART figure above.

Top five function hotspots: kernel memset 31.5%; userspace vk_spv_run 9.0%; ka_hash_insert 6.5%; find_inventory_region 5.0%; interrupt_stub_48 4.5%. Inline allocator hash-entry allocation adds another 1.5% to the hash insertion path; other shader functions bring the common software-renderer category to 12%.

TTFB 71 ms, transfer 437 ms. Page bytes/resources differ slightly between live server responses (decoded 24112 vs 24640, 99 vs 89 resource starts). The single run cannot isolate all timing differences or establish a statistically significant zeroing improvement. Serial cost clearly decreased; allocator/initialization remains the major system cost. No further optimization performed after this comparison.

## Regression evidence

Hosted UART tests: FIFO and non-FIFO capacity bounds, exact binary stderr, CRLF, ring wrap, fatal diagnostic preservation PASS. Hosted ORA tests: sharing/rebind/collisions/mutable independence/lifetime/views/no-rehash/OOM/concurrent ingestion PASS.

Actual VM: kernel_memory_init_test PASS (unaligned stores, zero length, word/page tails, sentinels, reused dedicated allocations fully zero, page projection). kernel_guard_test PASS. User validation, native syscall domains, memory ownership, thread context and JIT W^X all exit 0. No kernel panic or new abort observed. Screenshot page-final.png confirms real search text and images. Existing five shader rejections remain, unchanged and out of scope.

Artifacts: build/allocator-after/navigation.log, sample-01..05.txt, sample-windows.csv, symbols.json, maps.txt and page-final.png. Normal kernel and launcher restored in staging; ISO repacked without autorun/test instrumentation. VM powered off.

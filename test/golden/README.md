# Golden wire captures
18 scenarios (6 paper sizes at 1-bit, 3-page simplex, 8-bit gray halftone, long/short-edge duplex, odd page counts, draft/density/no-smooth
options, `BindEdge=Top`) recorded with `tools/baseline/run_baseline.py` against the Linux filter before the multi-platform refactor.
Each file holds every non-poll host->printer CAPT frame (command, size, sha256) plus the filter's INFO/ERROR/PAGE lines, with a pinned clock
and host name.  Verified reproducible and sensitive to a one-value change in the halftone matrix.

    tools/baseline/run_baseline.py run --bin cups --out /some/dir [--emu qemu-ppc --filter-bin PPC_FILTER]
    tools/baseline/run_baseline.py compare test/golden /some/dir      # RESULT: IDENTICAL
    make -C test/ppc check-ppc                                        # same comparison on emulated big-endian PPC32

# cBench

Simple framebuffer bench for the SharkDeck.

```bash
cd /home/working/cBench
make
./cBench
```

| | |
|--|--|
| ↑ ↓ | pick test |
| Enter | run one (~0.6 s) |
| A | run all |
| X | quit |

Results append to `~/cbench.log`.

Tests: integer ops, float+sin, memcpy MB/s, pointer-chase ns, fb_clear fps, px() Mpx/s.

# Header budget experiments

Host-only framing experiments. They are not part of the realtime core and do not
change the CPB1 wire format.

## bit-align-17 domain sweep

```sh
cc -O2 -std=c99 -Wall -Wextra -Werror -o /tmp/sweep17 sweep17.c
/tmp/sweep17
```

Measures the stable domain of the candidate 17-byte DATA card: random legal
cards, one-step field cliffs, expansion knees, sample-counter lifetime, and CRC
cost versus payload size. See `ROADMAP.md` for the recorded conclusions.

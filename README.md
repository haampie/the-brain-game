# The Brain game simulator

Cooperative card game with 36 cards by Peter Jürgensen. Every player gets 5
cards from the pile.  Each card has a color, type and action. In a player's
turn, a card has to be played on the table, and it's action has to be
performed:

- give card from hand to other player;
- take pile from table back in hand (if it doesn't contain a take card itself);
- cover pile on table;
- plus one: add a second card on the table w/o playing its action;
- remove all piles of a color or type (the top card of a pile determines its color/type).

After a turn, the player takes a card from the draw pile. If 5 or more piles
are on the table after the player's turn, the game is lost.


## Build the project

```
make # or make native for -march flags.
make test
```

compiles and runs a number of randomized two player games and reports win
rates. From about 500 games I got:

- hard (< 5 piles): 86%
- medium (< 6 piles): 96%
- easy (< 7 piles): 99%

## How it works

Since the branching factor is high and the game is stochastic, the
implementation is a monte carlo simulation with search. It's not monte carlo
tree search (the branching factor is pretty high, and wins by random play are
very rare). In a simulation, the other player's unknown cards and the pile are
shuffled and the other player receives new cards, and from there a win is
searched where the game is played with open cards. Search has a relatively
small budget of max number of nodes to explore, since exhaustive search is
rather slow. Search is a simple heuristically best-first depth-first search to
find a solution quickly. The move with most wins from simulations is picked as
the best move.

The advantage of best-first dfs is that it require very little memory, and it
seems to work alright in practice. The whole thing could be parallelized over
monte carlo simulations.

It's unclear if an 86% win rate is optimal.


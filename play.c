#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_PILES 5
#define NUM_START 5
#define MAX_NODES_PER_SIMULATION 250
#define TOTAL_GAMES 10
#define TOTAL_SIMULATIONS 5000

/* random numbers */
static inline uint64_t rotl(const uint64_t x, int k) {
  return (x << k) | (x >> (64 - k));
}

static uint64_t s[4] = {111, 222, 333, 444};

uint64_t random_next(void) {
  const uint64_t result = rotl(s[0] + s[3], 23) + s[0];
  const uint64_t t = s[1] << 17;
  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];
  s[2] ^= t;
  s[3] = rotl(s[3], 45);
  return result;
}

enum card_color { GREEN, RED, GRAY, PURPLE, BLUE, YELLOW };
enum card_action {
  REMOVE_TYPE,
  REMOVE_COLOR,
  COVER,
  GIVE,
  TAKE,
  PLUS_ONE,
};
enum card_type {
  DRAGON,
  GOOSE,
  CAT,
  UNICORN,
  FROG,
  ZOMBIE
}; /* type = (color - action + 5) % 6 */

char *card_color_esc[] = {
    "\033[;42m", "\033[;41m", "\033[;100m",
    "\033[;45m", "\033[;44m", "\033[;43m",
};
char *card_color_str[] = {"GREEN", "RED", "GRAY", "PURPLE", "BLUE", "YELLOW"};
char *card_action_str[] = {"REMOVE_TYPE", "REMOVE_COLOR", "COVER",
                           "GIVE",        "TAKE",         "PLUS_ONE"};
char *card_type_str[] = {"DRAGON", "GOOSE", "CAT", "UNICORN", "FROG", "ZOMBIE"};

typedef struct card {
  struct card *right;
  struct card *down;
  enum card_color color;
  enum card_action action;
  enum card_type type;

  /* set if type is REMOVE_COLOR / REMOVE_TYPE */
  enum card_color remove_color;
  enum card_type remove_type;

  /* 1 if taken or given */
  int open;
} card;

typedef struct move {
  /* card from hand that's played */
  card **hand;
  /* the +1'd or given card; or the covered or taken pile */
  card **extra;
} move;

typedef struct saved_move {
  card *hand;
  card *extra;
} saved_move;

typedef struct game_state {
  card cards[36];
  uint8_t draw_pile_size;
  card *hands[2];
  card *table;
  card *pile[36];
  saved_move stack[100];
  uint64_t nodes;
  int depth;

  int cards_left;             /* number of non-discarded cards */
  int left_of_color_type[36]; /* bool matrix[i, j] where i is color, j type */
  int pile_count;             /* number of piles on the table */
  int count_cover;            /* number of non-discarded cover cards */
  int can_remove_color[6];    /* whether removal of color is not discarded */
  int can_remove_type[6];     /* whether removal of type is not discarded */
} game_state;

static void remove_card(game_state *s, card *c) {
  switch (c->action) {
  case COVER:
    --s->count_cover;
    break;
  case REMOVE_TYPE:
    s->can_remove_type[c->remove_type] = 0;
    break;
  case REMOVE_COLOR:
    s->can_remove_color[c->remove_color] = 0;
    break;
  default:
    break;
  }

  s->left_of_color_type[c->color * 6 + c->type] = 0;
  --s->cards_left;
}

static void add_card(game_state *s, card *c) {
  switch (c->action) {
  case COVER:
    ++s->count_cover;
    break;
  case REMOVE_TYPE:
    s->can_remove_type[c->remove_type] = 1;
    break;
  case REMOVE_COLOR:
    s->can_remove_color[c->remove_color] = 1;
    break;
  default:
    break;
  }

  s->left_of_color_type[c->color * 6 + c->type] = 1;
  ++s->cards_left;
}

static void print_card(FILE *stream, card *p, int show_open) {
  fprintf(stream, "%s%s %s", card_color_esc[p->color], card_type_str[p->type],
          card_action_str[p->action]);
  if (p->action == REMOVE_TYPE)
    fprintf(stream, ":%s", card_type_str[p->remove_type]);
  else if (p->action == REMOVE_COLOR)
    fprintf(stream, ":%s%s", card_color_esc[p->remove_color],
            card_color_str[p->remove_color]);
  fprintf(stream, "\033[0m");
  if (show_open && p->open)
    fprintf(stream, " [visible]");
}

static int winnable(game_state *s) {
  int total_left = s->cards_left;
  for (int color = 0; color < 6; ++color)
    for (int type = 0; type < 6; ++type)
      total_left -= (s->can_remove_color[color] | s->can_remove_type[type]) &
                    s->left_of_color_type[6 * color + type];

  /* every cover card can remove at best one other card */
  return total_left - s->count_cover < MAX_PILES;
}

static void indent(FILE *stream, int depth) {
  for (int i = 0; i < depth; ++i)
    fprintf(stream, "  ");
}

static void print_state(FILE *stream, game_state *s, int depth) {
  indent(stream, depth);
  fprintf(stream, "total cards left: %d\n", s->cards_left);
  indent(stream, depth);
  fprintf(stream, "          ");
  for (int color = 0; color < 6; ++color)
    fprintf(stream, "%s%-10s", s->can_remove_color[color] ? "*" : " ",
            card_color_str[color]);
  fprintf(stream, "\n");
  for (int type = 0; type < 6; ++type) {
    indent(stream, depth);
    fprintf(stream, "%s%-10s", s->can_remove_type[type] ? "*" : " ",
            card_type_str[type]);
    for (int color = 0; color < 6; ++color) {
      fprintf(stream, "%d          ", s->left_of_color_type[6 * color + type]);
    }
    fprintf(stream, "\n");
  }
  fprintf(stream, "\n");

  indent(stream, depth);
  fprintf(stream, "table (%d piles):\n", s->pile_count);
  for (card *p = s->table; p; p = p->right) {
    indent(stream, depth);
    for (card *q = p; q; q = q->down) {
      fprintf(stream, "  ");
      print_card(stream, q, 0);
    }
    fprintf(stream, "\n");
  }

  for (int player = 0; player < 2; ++player) {
    indent(stream, depth);
    fprintf(stream, "player %d\n", player + 1);
    for (card *p = s->hands[player]; p; p = p->down) {
      indent(stream, depth);
      fprintf(stream, "  ");
      print_card(stream, p, 1);
      fprintf(stream, "\n");
    }
  }
  indent(stream, depth);
  fprintf(stream, "pile size = %d\n", s->draw_pile_size);
  for (int i = 0; i < s->draw_pile_size; ++i) {
    indent(stream, depth);
    fprintf(stream, "  ");
    print_card(stream, s->pile[i], 0);
    fprintf(stream, "\n");
  }
}

static int verbose = 0;

static int play(game_state *s, int player, int static_check, uint64_t max_nodes,
                int forced_move) {
  ++s->nodes;

  if (verbose) {
    indent(stderr, s->depth);
    fprintf(stderr, "nodes: %" PRIu64 ". depth = %d\n", s->nodes, s->depth);
    print_state(stderr, s, s->depth);
    fprintf(stderr, "\n");
    fflush(stderr);
  }

  if (s->pile_count >= MAX_PILES)
    return 0;

  /* no cards to play, skip to next player */
  if (s->hands[player] == NULL)
    player = !player;

  /* both players are done, game is won */
  if (s->hands[player] == NULL) {
    if (verbose) {
      fprintf(stdout, "[%d] ", s->depth);
      print_state(stdout, s, 0);
      fprintf(stdout, "\n");
      fflush(stdout);
    }
    return 1;
  }

  if (s->nodes >= max_nodes)
    return -1;

  int cards_in_hand = 0;
  for (card *h = s->hands[player]; h; h = h->down)
    ++cards_in_hand;

  int other = !player;

  move moves[300];
  int legal_moves = 0;

  /* check if too few removal cards remain to win */
  if (static_check && !winnable(s))
    return 0;

  /* count what's removable on table */
  int color_count[6] = {0};
  int type_count[6] = {0};
  for (card *x = s->table; x; x = x->right) {
    card *y = x;
    while (y->down)
      y = y->down;
    ++color_count[y->color];
    ++type_count[y->type];
  }

  /* generate all moves */
  int hand_idx = 0;
  int piles_left = MAX_PILES - s->pile_count;
  for (card **h = &s->hands[player]; *h; h = &(*h)->down, ++hand_idx) {
    card *c = *h;
    /* avoid creating more piles than allowed */
    if (c->action == GIVE && piles_left <= 0)
      continue;
    else if (c->action == REMOVE_COLOR && piles_left <= 0 &&
             color_count[c->remove_color] == 0)
      continue;
    else if (c->action == REMOVE_TYPE && piles_left <= 0 &&
             type_count[c->remove_type] == 0)
      continue;
    else if (c->action == PLUS_ONE &&
             piles_left <= (cards_in_hand == 1 ? 1 : 2))
      continue;

    /* generate cards to play extra */
    if (c->action == GIVE || c->action == PLUS_ONE) {
      /* temporarily remove current card from hand */
      card *tmp = *h;
      *h = (*h)->down;

      int extra_idx = 0;

      int pairs = 0;
      for (card **e = &s->hands[player]; *e; e = &(*e)->down, ++extra_idx) {
        ++pairs;
        /* only enqueue (A, B), (B, A) once if A == B on PLUS_ONE actions  */
        if (c->action == PLUS_ONE && (*e)->action == PLUS_ONE &&
            extra_idx < hand_idx)
          continue;
        move *m = &moves[legal_moves++];
        m->hand = h;
        m->extra = e;
      }
      /* the card cannot be played with an extra */
      if (pairs == 0) {
        move *m = &moves[legal_moves++];
        m->hand = h;
        m->extra = NULL;
      }

      *h = tmp;

    } else if (c->action == COVER || c->action == TAKE) {
      int pairs = 0;
      for (card **e = &s->table; *e; e = &(*e)->right) {
        /* cannot take a pile with take back card */
        if (c->action == TAKE && (*e)->action == TAKE)
          continue;
        move *m = &moves[legal_moves++];
        m->hand = h;
        m->extra = e;
        ++pairs;
      }
      /* the card cannot be played with an extra */
      if (pairs == 0 && s->pile_count < MAX_PILES - 1) {
        move *m = &moves[legal_moves++];
        m->hand = h;
        m->extra = NULL;
      }
    } else {
      /* removal cards */
      move *m = &moves[legal_moves++];
      m->hand = h;
      m->extra = NULL;
    }
  }

  if (forced_move >= 0) {
    /* force the dictated move */
    if (legal_moves > 0) {
      moves[0] = moves[forced_move % legal_moves];
      legal_moves = 1;
    }
  } else {
    /* reorder moves best-first */
    for (int good = 0, bad = legal_moves - 1, i = 0; i < bad;) {
      /* move +1 with +1 to front, move cards that remove >= 2 to front, move
       * take removal cards to front */
      move m = moves[i];
      enum card_action action = (*m.hand)->action;
      if ((action == PLUS_ONE && m.extra &&
           (m.hand == m.extra ? (*m.hand)->down : *m.extra)->action ==
               PLUS_ONE) ||
          (action == REMOVE_TYPE && type_count[(*m.hand)->remove_type] >= 2) ||
          (action == REMOVE_COLOR &&
           color_count[(*m.hand)->remove_color] >= 2) ||
          (action == TAKE && m.extra &&
           ((*m.extra)->action == REMOVE_TYPE ||
            (*m.extra)->action == REMOVE_COLOR))) {
        moves[i] = moves[good];
        moves[good] = m;
        ++good;
        ++i;
      }
      /* move take back +1 to back, move remove nothing to back */
      else if ((action == TAKE && m.extra && (*m.extra)->action == PLUS_ONE) ||
               (action == REMOVE_TYPE &&
                type_count[(*m.hand)->remove_type] == 0) ||
               (action == REMOVE_COLOR &&
                color_count[(*m.hand)->remove_color] == 0)) {
        moves[i] = moves[bad];
        moves[bad] = m;
        --bad;
      } else {
        ++i;
      }
    }
  }

  int won = 0;

  /* iterate over all moves in hand */
  for (int i = 0; i < legal_moves; ++i) {
    move m = moves[i];
    card *c = *m.hand;

    s->stack[s->depth].hand = c;

    /* remove from hand */
    *m.hand = c->down;
    c->down = NULL;

    s->stack[s->depth].extra = m.extra ? *m.extra : NULL;

    card **covered_card = NULL;    /* covered card's down pointer */
    card **pile_taken_hand = NULL; /* location of pile in hand */

    /* removed piles (type / color) and their location */
    card *removed[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    card **removed_table[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    int num_removed = 0;

    if (c->action == REMOVE_COLOR || c->action == REMOVE_TYPE) {
      /* remove other piles with same color or type */
      for (card **p = &s->table; *p;) {
        card *q = *p;
        while (q->down)
          q = q->down;
        if ((c->action == REMOVE_COLOR && q->color == c->remove_color) ||
            (c->action == REMOVE_TYPE && q->type == c->remove_type)) {

          /* keep track of removed piles */
          removed[num_removed] = *p;
          removed_table[num_removed] = p;
          ++num_removed;

          /* keep track of what is removed */
          --s->pile_count;
          for (card *r = *p; r; r = r->down)
            remove_card(s, r);

          /* remove from table (instead of advancing p) */
          *p = (*p)->right;
        } else {
          p = &(*p)->right;
        }
      }
    }

    if (m.extra) {
      switch (c->action) {
      case COVER: {
        covered_card = m.extra;
        while (*covered_card)
          covered_card = &(*covered_card)->down;
        *covered_card = c;
        break;
      }

      case TAKE: {
        /* iterate to tail of the hand */
        card **h = &s->hands[player];
        while (*h)
          h = &(*h)->down;
        pile_taken_hand = h;

        /* move pile to hand, and replace pile on table with card c */
        card *tmp = *m.extra;
        *m.extra = c;
        c->right = tmp->right;
        *h = tmp;
        break;
      }

      case PLUS_ONE: {
        card *second = *m.extra;
        /* put it on the table */
        second->right = s->table;
        s->table = second;
        /* remove it from the hand */
        *m.extra = second->down;
        second->down = NULL;
        ++s->pile_count;
        break;
      }

      case GIVE: {
        card *give = *m.extra;
        /* put it in the other player's hand */
        card *tmp = s->hands[other];
        s->hands[other] = give;
        *m.extra = give->down;
        give->down = tmp;
        break;
      }
      default:
        break;
      }
    }

    /* put card on the table */
    if (!(m.extra && (c->action == COVER || c->action == TAKE))) {
      c->right = s->table;
      s->table = c;
      ++s->pile_count;
    }

    /* take a card from the pile */
    int draw_card = s->draw_pile_size > 0;

    if (draw_card) {
      card *drawn = s->pile[--s->draw_pile_size];
      drawn->down = s->hands[player];
      s->hands[player] = drawn;
    }

    /* next turn */
    ++s->depth;
    won = play(s, other, c->action == REMOVE_TYPE || c->action == REMOVE_COLOR,
               max_nodes, -1);
    --s->depth;

    /* put card back on the pile */
    if (draw_card) {
      ++s->draw_pile_size;
      card *drawn = s->hands[player];
      s->hands[player] = drawn->down;
      drawn->down = NULL;
    }

    /* remove card from table */
    if (!(m.extra && (c->action == COVER || c->action == TAKE))) {
      s->table = s->table->right;
      --s->pile_count;
    }

    /* reinsert removed piles */
    for (int i = num_removed - 1; i >= 0; --i) {
      card *tmp = *removed_table[i];
      *removed_table[i] = removed[i];
      removed[i]->right = tmp;

      ++s->pile_count;
      for (card *r = removed[i]; r; r = r->down)
        add_card(s, r);
    }

    /* undo action */
    if (m.extra) {
      switch (c->action) {
      case COVER:
        *covered_card = NULL;
        break;
      case TAKE: {
        /* return taken pile to table if any */
        card *tmp = *m.extra;
        *m.extra = *pile_taken_hand;
        (*m.extra)->right = tmp->right;
        /* remove taken pile from hand */
        *pile_taken_hand = NULL;
        break;
      }
      case PLUS_ONE: {
        /* put from table in hand */
        card *tmp = *m.extra;
        *m.extra = s->table;
        s->table->down = tmp;
        /* remove from table */
        s->table = s->table->right;
        (*m.extra)->right = NULL; /* optional */
        --s->pile_count;
        break;
      }
      case GIVE: {
        card *tmp = *m.extra;
        *m.extra = s->hands[other];
        s->hands[other] = s->hands[other]->down;
        (*m.extra)->down = tmp;
        break;
      }
      default:
        break;
      }
    }

    /* put played card back in hand */
    c->right = NULL;
    c->down = *m.hand;
    *m.hand = c;

    if (won != 0)
      break;

    /* hack */
    if (s->depth > 0) {
      s->stack[s->depth].hand = NULL;
      s->stack[s->depth].extra = NULL;
    }
  }

  if (won == 1) {
    if (verbose) {
      fprintf(stdout, "[%d] ", s->depth);
      print_state(stdout, s, 0);
      fprintf(stdout, "\n");
      fflush(stdout);
    }

    return 1;
  }

  return won;
}

static void init_state(game_state *s) {
  /* init all cards */
  for (int i = 0; i < 36; ++i) {
    s->cards[i].right = NULL;
    s->cards[i].down = NULL;
    s->cards[i].color = i % 6;
    s->cards[i].action = i / 6;
    s->cards[i].type = (s->cards[i].color - s->cards[i].action + 6) % 6;
    s->cards[i].remove_color = (s->cards[i].color + 1) % 6;
    s->cards[i].remove_type = (s->cards[i].type + 5) % 6;
    s->cards[i].open = 0;
  }

  s->table = NULL;
  s->nodes = 0;
  s->depth = 0;
  s->draw_pile_size = 36;

  for (int player = 0; player < 2; ++player)
    s->hands[player] = NULL;

  /* no moves considered */
  for (int i = 0; i < 100; ++i) {
    s->stack[i].hand = NULL;
    s->stack[i].extra = NULL;
  }
  /* create a pile that can be shuffled */
  for (int i = 0; i < 36; ++i)
    s->pile[i] = &s->cards[i];

  s->cards_left = 36;
  for (int i = 0; i < 36; ++i)
    s->left_of_color_type[i] = 1;
  s->pile_count = 0;
  s->count_cover = 6;

  for (int i = 0; i < 6; ++i)
    s->can_remove_color[i] = 1;
  for (int i = 0; i < 6; ++i)
    s->can_remove_type[i] = 1;
}

static void random_init(game_state *s) {
  init_state(s);

  /* shuffle */
  for (int i = 0; i < s->draw_pile_size; ++i) {
    int j = i + random_next() % (s->draw_pile_size - i);
    card *tmp = s->pile[j];
    s->pile[j] = s->pile[i];
    s->pile[i] = tmp;
  }

  /* deal from the end of the deck */
  for (int player = 0; player < 2; ++player) {
    for (int i = 0; i < NUM_START; ++i) {
      card *c = s->pile[--s->draw_pile_size];
      c->down = s->hands[player];
      s->hands[player] = c;
    }
  }
}

static void copy_game_state(game_state *src, game_state *dst) {
  for (int i = 0; i < 36; ++i) {
    dst->pile[i] = dst->cards + (src->pile[i] - src->cards);
    dst->cards[i].open = src->cards[i].open;
    dst->cards[i].right = src->cards[i].right
                              ? dst->cards + (src->cards[i].right - src->cards)
                              : NULL;
    dst->cards[i].down = src->cards[i].down
                             ? dst->cards + (src->cards[i].down - src->cards)
                             : NULL;
  }

  for (int player = 0; player < 2; ++player)
    dst->hands[player] = src->hands[player]
                             ? dst->cards + (src->hands[player] - src->cards)
                             : NULL;

  for (int i = 0; i < 100; ++i) {
    dst->stack[i].hand = src->stack[i].hand
                             ? dst->cards + (src->stack[i].hand - src->cards)
                             : NULL;
    dst->stack[i].extra = src->stack[i].extra
                              ? dst->cards + (src->stack[i].extra - src->cards)
                              : NULL;
  }

  dst->depth = src->depth;
  dst->draw_pile_size = src->draw_pile_size;
  dst->nodes = src->nodes;
  dst->table = src->table ? dst->cards + (src->table - src->cards) : NULL;

  dst->cards_left = src->cards_left;
  for (int i = 0; i < 36; ++i)
    dst->left_of_color_type[i] = src->left_of_color_type[i];
  dst->pile_count = src->pile_count;
  dst->count_cover = src->count_cover;

  for (int i = 0; i < 6; ++i)
    dst->can_remove_color[i] = src->can_remove_color[i];
  for (int i = 0; i < 6; ++i)
    dst->can_remove_type[i] = src->can_remove_type[i];
}

static saved_move idx_to_move(game_state *s, int idx) {
  int hand = idx / 37;
  int extra = idx % 37;
  saved_move m = {.hand = s->cards + hand,
                  .extra = extra == 36 ? NULL : s->cards + extra};

  return m;
}

int main(void) {
  game_state simulation;
  game_state game;

  int games_won = 0;
  init_state(&simulation);

  /* hand * (extra or NULL) */
  int win_count[36 * 37];
  int loss_count[36 * 37];
  int unknown_count[36 * 37];

  /* number of games */
  for (int g = 0; g < TOTAL_GAMES; ++g) {
    printf("\n\nGAME %d\n", g);

    random_init(&game);

    int player = 0;
    for (int turn = 0;; ++turn) {
      print_state(stdout, &game, 1);

      /* determine if there are any cards to play */
      if (!game.hands[player])
        player = !player;

      if (!game.hands[player]) {
        ++games_won;
        break;
      }

      int other = !player;

      copy_game_state(&game, &simulation);

      printf("\n\nTURN %d (player %d)\n", turn, player + 1);

      for (int i = 0; i < 36 * 37; ++i) {
        win_count[i] = 0;
        loss_count[i] = 0;
        unknown_count[i] = 0;
      }

      int wins = 0, losses = 0;

      /* if there are no cards to draw we have perfect information: no need for
       * monte carlo */
      if (simulation.draw_pile_size == 0) {
        simulation.nodes = 0;

        int result = play(&simulation, player, 0, -1, -1);

        if (result != 1) {
          ++losses;
        } else {
          saved_move m = simulation.stack[0];
          int card_idx = (m.hand - simulation.cards) * 37 +
                         (m.extra ? m.extra - simulation.cards : 36);
          ++win_count[card_idx];
        }
      } else {
        /* do a monte carlo simulation */
        for (int run = 0; run < TOTAL_SIMULATIONS; ++run) {
          simulation.nodes = 0;

          /* put the other player's cards back in the pile */
          int num_cards_other_player = 0;
          card **other_hand = &simulation.hands[other];
          while (*other_hand) {
            /* retain visible cards */
            if ((*other_hand)->open) {
              other_hand = &(*other_hand)->down;
              continue;
            }
            /* put non-visible cards back in the pile */
            simulation.pile[simulation.draw_pile_size] = *other_hand;
            *other_hand = (*other_hand)->down;
            simulation.pile[simulation.draw_pile_size]->down = NULL;
            ++simulation.draw_pile_size;
            ++num_cards_other_player;
          }

          /* shuffle the deck */
          for (int i = 0; i < simulation.draw_pile_size; ++i) {
            int j = i + random_next() % (simulation.draw_pile_size - i);
            card *tmp = simulation.pile[j];
            simulation.pile[j] = simulation.pile[i];
            simulation.pile[i] = tmp;
          }

          /* deal the other player new cards */
          for (int i = 0; i < num_cards_other_player; ++i) {
            card *c = simulation.pile[--simulation.draw_pile_size];
            c->down = simulation.hands[other];
            simulation.hands[other] = c;
          }

          int result =
              play(&simulation, player, 0, MAX_NODES_PER_SIMULATION, run);

          saved_move m = simulation.stack[0];
          int card_idx = (m.hand - simulation.cards) * 37 +
                         (m.extra ? m.extra - simulation.cards : 36);
          if (result == 0) {
            ++losses;
            ++loss_count[card_idx];
          } else if (result == 1) {
            ++wins;
            /* increment count and early exit if we certainly play this */
            if (++win_count[card_idx] > TOTAL_SIMULATIONS / 2)
              break;
          } else {
            ++unknown_count[card_idx];
          }
        }
      }

      printf("losses = %d. wins = %d\n", losses, wins);

      int best_move = 0;
      int best_move_idx = -1;
      for (int i = 0; i < 36 * 37; ++i) {
        /* assume that no solution found is 50% chance of winning */
        int win_factor = 2 * win_count[i] + unknown_count[i];
        if (win_factor > best_move) {
          best_move = win_factor;
          best_move_idx = i;
        }
      }
      for (int i = 0; i < 36 * 37; ++i) {
        if (win_count[i] == 0 && unknown_count[i] == 0)
          continue;
        saved_move mi = idx_to_move(&game, i);
        print_card(stdout, mi.hand, 0);
        if (mi.extra) {
          printf(" ");
          print_card(stdout, mi.extra, 0);
        }
        printf("\n");
        int win_factor = 2 * win_count[i] + unknown_count[i];
        for (int j = 0; j < (70.0 * win_factor) / best_move; ++j)
          printf("*");
        printf(" (%d)", win_factor);
        if (i == best_move_idx)
          printf(" !!");
        printf("\n");
      }

      if (best_move_idx == -1) {
        printf("no win found\n");
        break;
      }

      saved_move best = idx_to_move(&game, best_move_idx);

      /* remove from hand */
      move m = {NULL, NULL};
      card *c = best.hand;
      for (m.hand = &game.hands[player]; *m.hand != best.hand;
           m.hand = &(*m.hand)->down)
        ;
      *m.hand = (*m.hand)->down;
      best.hand->down = NULL;

      if (best.extra) {
        /* locate other card in hand */
        if (best.hand->action == GIVE || best.hand->action == PLUS_ONE) {
          for (m.extra = &game.hands[player]; *m.extra != best.extra;
               m.extra = &(*m.extra)->down)
            ;
        }
        /* locate pile */
        if (best.hand->action == COVER || best.hand->action == TAKE) {
          for (m.extra = &game.table; *m.extra != best.extra;
               m.extra = &(*m.extra)->right)
            ;
        }
      }

      /* todo: DRY playing a move */
      if (c->action == REMOVE_COLOR || c->action == REMOVE_TYPE) {
        for (card **p = &game.table; *p;) {
          card *q = *p;
          while (q->down)
            q = q->down;
          if ((c->action == REMOVE_COLOR && q->color == c->remove_color) ||
              (c->action == REMOVE_TYPE && q->type == c->remove_type)) {
            /* keep track of what is removed */
            --game.pile_count;
            for (card *r = *p; r; r = r->down)
              remove_card(&game, r);
            *p = (*p)->right;
          } else {
            p = &(*p)->right;
          }
        }
      }

      if (m.extra) {
        switch (c->action) {
        case COVER: {
          card **covered_card = m.extra;
          while (*covered_card)
            covered_card = &(*covered_card)->down;
          *covered_card = c;
          break;
        }

        case TAKE: {
          /* iterate to tail of the hand */
          card **h = &game.hands[player];
          while (*h)
            h = &(*h)->down;

          /* move pile to hand, and replace pile on table with card c */
          card *tmp = *m.extra;
          *m.extra = c;
          c->right = tmp->right;
          *h = tmp;

          /* mark the cards as open */
          while (*h) {
            (*h)->open = 1;
            h = &(*h)->down;
          }

          break;
        }

        case PLUS_ONE: {
          card *second = *m.extra;
          /* put it on the table */
          second->right = game.table;
          game.table = second;
          /* remove it from the hand */
          *m.extra = second->down;
          second->down = NULL;
          ++game.pile_count;
          break;
        }

        case GIVE: {
          card *give = *m.extra;
          /* put it in the other player's hand */
          card *tmp = game.hands[other];
          game.hands[other] = give;
          *m.extra = give->down;
          give->down = tmp;
          /* mark as open */
          give->open = 1;
          break;
        }
        default:
          break;
        }
      }

      /* put card on the table */
      if (!(m.extra && (c->action == COVER || c->action == TAKE))) {
        c->right = game.table;
        game.table = c;
        ++game.pile_count;
      }

      /* take a card from the pile */
      int draw_card = game.draw_pile_size > 0;

      if (draw_card) {
        card *drawn = game.pile[--game.draw_pile_size];
        drawn->down = game.hands[player];
        game.hands[player] = drawn;
      }

      /* next player */
      player = !player;
    }
    printf("games won = %d / %d\n", games_won, g + 1);
  }
}

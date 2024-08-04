#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* random numbers */
static inline uint64_t rotl(const uint64_t x, int k) {
  return (x << k) | (x >> (64 - k));
}

static uint64_t s[4] = {3, 2, 1, 4};

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
} card;

typedef struct move {
  /* card from hand that's played */
  card **hand;
  /* the +1'd or given card; or the covered or taken pile */
  card **extra;
} move;

static void print_card(FILE *stream, card *p) {
  fprintf(stream, "%s %s %s", card_color_str[p->color], card_type_str[p->type],
          card_action_str[p->action]);
  if (p->action == REMOVE_TYPE)
    fprintf(stream, ":%s", card_type_str[p->remove_type]);
  else if (p->action == REMOVE_COLOR)
    fprintf(stream, ":%s", card_color_str[p->remove_color]);
}

static uint8_t draw_pile_size = 36;
static card cards[36];
static card *hands[2] = {NULL, NULL};
static card *table = NULL;
static card *pile[36];

static void print_state(FILE *stream) {
  fprintf(stream, "table:\n");
  for (card *p = table; p; p = p->right) {
    for (card *q = p; q; q = q->down) {
      fprintf(stream, "  ");
      print_card(stream, q);
    }
    fprintf(stream, "\n");
  }

  for (int player = 0; player < 2; ++player) {
    fprintf(stream, "player %d\n", player + 1);
    for (card *p = hands[player]; p; p = p->down) {
      fprintf(stream, "  ");
      print_card(stream, p);
      fprintf(stream, "\n");
    }
  }
  fprintf(stream, "pile size = %d\n", draw_pile_size);
}

static uint64_t nodes = 0;

#define MAX_PILES 5

static int play(int depth) {
  int player = depth % 2;

  if (++nodes % 100000000 == 0) {
    fprintf(stderr, "nodes: %llu. depth = %d\n", nodes, depth);
    print_state(stderr);
    fprintf(stderr, "\n");
    fflush(stderr);
  }

  /* early exit if get to 5 cards or more */
  int piles_on_table = 0;
  for (card *t = table; t; t = t->right)
    if (++piles_on_table == MAX_PILES)
      return 0;

  /* no cards to play, skip to next player */
  if (hands[player] == NULL)
    player = (player + 1) % 2;

  /* both players are done, game is won */
  if (hands[player] == NULL) {
    fprintf(stdout, "[%d] ", depth);
    print_state(stdout);
    fprintf(stdout, "\n");
    return 1;
  }

  int cards_in_hand = 0;
  for (card *h = hands[player]; h; h = h->right)
    ++cards_in_hand;

  int other = (player + 1) % 2;

  move moves[300];
  int legal_moves = 0;

  /* generate all moves */
  for (card **h = &hands[player]; *h; h = &(*h)->down) {
    card *c = *h;
    /* avoid illegal moves */
    if (piles_on_table >= MAX_PILES - 1 && c->action == GIVE)
      continue;
    else if (piles_on_table >= MAX_PILES - (cards_in_hand == 1 ? 1 : 2) &&
             c->action == PLUS_ONE)
      continue;

    /* generate cards to play extra */
    if (c->action == GIVE || c->action == PLUS_ONE) {
      /* temporarily remove current card from hand */
      card *tmp = *h;
      *h = (*h)->down;

      int pairs = 0;
      for (card **e = &hands[player]; *e; e = &(*e)->down) {
        move *m = &moves[legal_moves++];
        m->hand = h;
        m->extra = e;
        ++pairs;
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
      for (card **e = &table; *e; e = &(*e)->right) {
        /* cannot take a pile with take back card */
        if (c->action == TAKE && (*e)->action == TAKE)
          continue;
        move *m = &moves[legal_moves++];
        m->hand = h;
        m->extra = e;
        ++pairs;
      }
      /* the card cannot be played with an extra */
      if (pairs == 0 && piles_on_table < MAX_PILES - 1) {
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

  int won = 0;

  /* iterate over all moves in hand */
  for (int i = 0; i < legal_moves; ++i) {
    move m = moves[i];

    /* current card */
    card *c = *m.hand;

    /* remove from hand */
    *m.hand = c->down;
    c->down = NULL;

    /* down link for covered card */
    card **covered_card = NULL;

    /* location of pile taken in (tail of) hand */
    card **pile_taken_hand = NULL;

    /* removed piles (type / color) and their location */
    card *removed[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    card **removed_table[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    int num_removed = 0;

    switch (c->action) {
    case REMOVE_COLOR:
    case REMOVE_TYPE: {
      /* remove other piles with same color or type */
      for (card **p = &table; *p;) {
        card *q = *p;
        while (q->down)
          q = q->down;
        if ((c->action == REMOVE_COLOR && q->color == c->remove_color) ||
            (c->action == REMOVE_TYPE && q->type == c->remove_type)) {

          /* keep track of removed piles */
          removed[num_removed] = *p;
          removed_table[num_removed] = p;
          ++num_removed;

          /* remove from table (instead of advancing p) */
          *p = (*p)->right;
        } else {
          p = &(*p)->right;
        }
      }
      break;
    }

    case COVER: {
      covered_card = m.extra;
      while (*covered_card)
        covered_card = &(*covered_card)->down;
      *covered_card = c;
      break;
    }

    case TAKE: {
      if (m.extra) {
        /* iterate to tail of the hand */
        card **h = &hands[player];
        while (*h)
          h = &(*h)->down;
        pile_taken_hand = h;

        /* move pile to hand, and replace pile on table with card c */
        card *tmp = *m.extra;
        *m.extra = c;
        c->right = tmp->right;
        *h = tmp;
      }
      break;
    }

    case PLUS_ONE: {
      if (m.extra) {
        card *second = *m.extra;
        /* put it on the table */
        second->right = table;
        table = second;
        /* remove it from the hand */
        *m.extra = second->down;
        second->down = NULL;
      }
      break;
    }

    case GIVE: {
      if (m.extra) {
        card *give = *m.extra;
        /* put it in the other player's hand */
        card *tmp = hands[other];
        hands[other] = give;
        *m.extra = give->down;
        give->down = tmp;
      }
      break;
    }
    }

    /* put card on the table */
    if (!(m.extra && (c->action == COVER || c->action == TAKE))) {
      c->right = table;
      table = c;
    }

    /* take a card from the pile */
    int draw_card = draw_pile_size > 0;

    if (draw_card) {
      card *drawn = pile[--draw_pile_size];
      drawn->down = hands[player];
      hands[player] = drawn;
    }

    /* next turn */
    won = play(depth + 1);

    /* put card back on the pile */
    if (draw_card) {
      ++draw_pile_size;
      card *drawn = hands[player];
      hands[player] = drawn->down;
      drawn->down = NULL;
    }

    /* remove card from table */
    if (!(m.extra && (c->action == COVER || c->action == TAKE))) {
      table = table->right;
    }

    /* undo action */
    switch (c->action) {
    case COVER:
      /* todo? cover card not covering anything? */
      *covered_card = NULL;
      break;
    case TAKE: {
      if (m.extra) {
        /* return taken pile to table if any */
        card *tmp = *m.extra;
        *m.extra = *pile_taken_hand;
        (*m.extra)->right = tmp->right;
        /* remove taken pile from hand */
        *pile_taken_hand = NULL;
      }
      break;
    }
    case PLUS_ONE: {
      if (m.extra) {
        /* put from table in hand */
        card *tmp = *m.extra;
        *m.extra = table;
        table->down = tmp;
        /* remove from table */
        table = table->right;
        (*m.extra)->right = NULL; /* optional */
      }
      break;
    }
    case GIVE: {
      if (m.extra) {
        card *tmp = *m.extra;
        *m.extra = hands[other];
        hands[other] = hands[other]->down;
        (*m.extra)->down = tmp;
      }
      break;
    }
    case REMOVE_TYPE:
    case REMOVE_COLOR: {
      /* reinsert removed piles */
      for (int i = num_removed - 1; i >= 0; --i) {
        card *tmp = *removed_table[i];
        *removed_table[i] = removed[i];
        removed[i]->right = tmp;
      }
      break;
    }
    }

    /* put played card back in hand */
    c->right = NULL;
    c->down = *m.hand;
    *m.hand = c;

    if (won)
      break;
  }

  if (won) {
    fprintf(stdout, "[%d] ", depth);
    print_state(stdout);
    fprintf(stdout, "\n");
    return 1;
  }

  return 0;
}

int main(void) {

  /* init all cards */
  for (int i = 0; i < 36; ++i) {
    cards[i].right = NULL;
    cards[i].down = NULL;
    cards[i].color = i % 6;
    cards[i].action = i / 6;
    cards[i].type = (cards[i].color - cards[i].action + 6) % 6;
    cards[i].remove_color = (cards[i].color + 1) % 6;
    cards[i].remove_type = (cards[i].type + 5) % 6;
  }

  /* create a pile that can be shuffeled */
  for (int i = 0; i < 36; ++i)
    pile[i] = &cards[i];

  /* shuffle */
  for (int i = 0; i < draw_pile_size; ++i) {
    int j = i + random_next() % (draw_pile_size - i);
    card *tmp = pile[j];
    pile[j] = pile[i];
    pile[i] = tmp;
  }

  /* deal from the end of the deck */
  for (int player = 0; player < 2; ++player) {
    for (int i = 0; i < 5; ++i) {
      card *c = pile[--draw_pile_size];
      c->down = hands[player];
      hands[player] = c;
    }
  }

  for (int simulation = 0; simulation < 10; ++simulation) {
    print_state(stdout);
    fprintf(stdout, "\n");
    fprintf(stdout, "simulation %d\n", simulation);
    play(0);

    /* remove hand from player 2 */
    card *p = hands[1];
    while (p) {
      card *tmp = p->down;
      p->down = NULL;
      p = tmp;
      ++draw_pile_size;
    }
    hands[1] = NULL;

    /* shuffle the deck again (excluding player 1's hand) */
    for (int i = 0; i < draw_pile_size; ++i) {
      int j = i + random_next() % (draw_pile_size - i);
      card *tmp = pile[j];
      pile[j] = pile[i];
      pile[i] = tmp;
    }

    /* deal player 2 cards */
    for (int i = 0; i < 5; ++i) {
      card *c = pile[--draw_pile_size];
      c->down = hands[1];
      hands[1] = c;
    }
  }
}

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

static void print_card(FILE * stream, card *p) {
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

static void print_state(FILE * stream) {
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

  int other = (player + 1) % 2;

  /* iterate over all cards in hand */
  for (card **played_hand = &hands[player]; *played_hand;
       played_hand = &(*played_hand)->down) {

    /* current card */
    card *c = *played_hand;

    /* avoid playing all branches of losing moves */
    if (piles_on_table >= MAX_PILES - 1 && c->action == GIVE)
        continue;
    else if (piles_on_table >= MAX_PILES - 2 && c->action == PLUS_ONE)
        continue;

    /* remove from hand */
    *played_hand = c->down;
    c->down = NULL;

    /* down link for covered card */
    card **covered_pile = &table;
    card **covered_card = NULL;

    /* location of pile taken on table and in hand (tail) */
    card **pile_taken_hand = NULL;
    card **pile_taken_table = NULL;

    /* location in hand of additional card played (+1 or give) */
    card **extra_from_hand = &hands[player];

    /* removed piles (type / color) and their location */
    card *removed[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    card **removed_table[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    int num_removed = 0;

    int won = 0;

    /* sub moves (for PLUS_ONE, COVER, TAKE, GIVE) */
    while (1) {
      int extra_played = 0;
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
        /* put it on the next pile on the table */
        covered_card = covered_pile;
        while (*covered_card)
          covered_card = &(*covered_card)->down;
        *covered_card = c;
        break;
      }

      case TAKE: {
        /* find the first pile that has no take back card */
        card **p;
        for (p = &table; *p; p = &(*p)->right) {
          if ((*p)->action == TAKE)
            continue;
          break;
        }
        pile_taken_table = p;

        /* iterate to tail of the hand */
        card **h = &hands[player];
        while (*h)
          h = &(*h)->down;
        pile_taken_hand = h;

        /* move pile to hand, and replace pile on table with card c */
        card *tmp = *p;
        *p = c;
        c->right = tmp ? tmp->right : NULL;
        // tmp->right = NULL; /* unnecessary? */
        *h = tmp;
        break;
      }

      case PLUS_ONE: {
        if (*extra_from_hand) {
          card *second = *extra_from_hand;
          /* put it on the table */
          second->right = table;
          table = second;
          /* remove it from the hand */
          *extra_from_hand = second->down;
          second->down = NULL;
          extra_played = 1;
        }
        break;
      }

      case GIVE: {
        if (*extra_from_hand) {
          card *give = *extra_from_hand;
          /* put it in the other player's hand */
          card *tmp = hands[other];
          hands[other] = give;
          *extra_from_hand = give->down;
          give->down = tmp;
          extra_played = 1;
        }
        break;
      }
      }

      /* put card on the table */
      if (c->action != COVER && c->action != TAKE) {
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

      /* remove from table */
      switch (c->action) {
      case COVER:
        *covered_card = NULL;
        break;
      case TAKE: {
        /* return taken pile to table if any */
        card *tmp = *pile_taken_table;
        *pile_taken_table = *pile_taken_hand;
        if (*pile_taken_table)
          (*pile_taken_table)->right = tmp->right;
        /* remove taken pile from hand */
        *pile_taken_hand = NULL;
        break;
      }
      default: {
        table = table->right;
        break;
      }
      }

      if (c->action == PLUS_ONE && extra_played) {
        /* put from table in hand */
        card *tmp = *extra_from_hand;
        *extra_from_hand = table;
        table->down = tmp;
        /* remove from table */
        table = table->right;
        (*extra_from_hand)->right = NULL; /* optional */
      }

      /* take back the card given to the other player */
      else if (c->action == GIVE && extra_played) {
        card *tmp = *extra_from_hand;
        *extra_from_hand = hands[other];
        hands[other] = hands[other]->down;
        (*extra_from_hand)->down = tmp;
      }

      /* reinsert removed piles */
      for (int i = num_removed - 1; i >= 0; --i) {
        card *tmp = *removed_table[i];
        *removed_table[i] = removed[i];
        removed[i]->right = tmp;
      }

      if (won)
        break;

      if (c->action == COVER && *covered_pile) {
        /* advance the pile for next cover move */
        covered_pile = &(*covered_pile)->right;
        continue;
      } else if (extra_played && (*extra_from_hand)->down) {
        /* advance next card in hand to be +1'ed */
        extra_from_hand = &(*extra_from_hand)->down;
        continue;
      }
      break;
    }

    /* put played card back in hand */
    c->right = NULL;
    c->down = *played_hand;
    *played_hand = c;

    if (won) {
      fprintf(stdout, "[%d] ", depth);
      print_state(stdout);
      fprintf(stdout, "\n");
      return 1;
    }
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

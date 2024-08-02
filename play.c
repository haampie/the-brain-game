#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* random numbers */
static inline uint64_t rotl(const uint64_t x, int k) {
  return (x << k) | (x >> (64 - k));
}

static uint64_t s[4] = {1, 2, 3, 4};

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
  DUCK,
  CAT,
  UNICORN,
  FROG,
  ZOMBIE
}; /* type = (color - action + 5) % 6 */

char *card_color_str[] = {"GREEN", "RED", "GRAY", "PURPLE", "BLUE", "YELLOW"};
char *card_action_str[] = {"REMOVE_TYPE", "REMOVE_COLOR", "COVER",
                           "GIVE",        "TAKE",         "PLUS_ONE"};
char *card_type_str[] = {"DRAGON", "DUCK", "CAT", "UNICORN", "FROG", "ZOMBIE"};

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

static void print_card(card *p) {
  printf("%s %s %s", card_color_str[p->color], card_type_str[p->type],
         card_action_str[p->action]);
  if (p->action == REMOVE_TYPE)
    printf(":%s", card_type_str[p->remove_type]);
  else if (p->action == REMOVE_COLOR)
    printf(":%s", card_color_str[p->remove_color]);
}

static uint8_t pile_size = 36;
static card cards[36];
static card *hands[2] = {NULL, NULL};
static card *table = NULL;
static card *pile[36];

static void print_state(void) {
  printf("table:\n");
  for (card *p = table; p; p = p->right) {
    for (card *q = p; q; q = q->down) {
      printf("  ");
      print_card(q);
    }
    printf("\n");
  }

  for (int player = 0; player < 2; ++player) {
    printf("player %d\n", player + 1);
    for (card *p = hands[player]; p; p = p->down) {
      printf("  ");
      print_card(p);
      printf("\n");
    }
  }
}

static void random_play(int depth) {
  printf("[%d] ", depth);
  print_state();

  int player = depth % 2;
  int other = (depth + 1) % 2;

  /* removed piles, either type or color */
  card *removed[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
  card **removed_from[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
  int num_removed = 0;

  /* what pile was a cover card put on */
  card **cover_parent = NULL;

  /* what pile was taken, and where is it in the hand */
  card **pile_taken_hand = NULL;
  card **pile_taken_table = NULL;

  /* plus one played */
  card **plus_one_hand = NULL;

  /* location of given card */
  card **give_hand = NULL;

  /* everybody is done */
  if (hands[0] == NULL && hands[1] == NULL)
    return;

  card *c = hands[player];

  /* curent player has no cards left, let the other player go */
  if (c == NULL) {
    random_play(depth + 1);
    return;
  }

  /* remove from hand */
  hands[player] = c->down;
  c->down = NULL;

  /* remove other piles with same color or type */
  if (c->action == REMOVE_COLOR || c->action == REMOVE_TYPE) {
    for (card **p = &table; *p;) {
      card *q = *p;
      while (q->down)
        q = q->down;
      if ((c->action == REMOVE_COLOR && q->color == c->remove_color) ||
          (c->action == REMOVE_TYPE && q->type == c->remove_type)) {

        /* keep track of removed piles */
        removed[num_removed] = *p;
        removed_from[num_removed] = p;
        ++num_removed;

        /* remove from table (instead of advancing p) */
        *p = (*p)->right;
      } else {
        p = &(*p)->right;
      }
    }
  }

  else if (c->action == COVER) {
    /* put it on the first pile on the table */
    cover_parent = &table;
    while (*cover_parent)
      cover_parent = &(*cover_parent)->down;
    *cover_parent = c;
  }

  else if (c->action == TAKE) {
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
    c->right = tmp->right;
    tmp->right = NULL; /* unnecessary? */
    *h = tmp;
  }

  else if (c->action == PLUS_ONE) {
    /* just play the first card in hand */
    if (hands[player]) {
      plus_one_hand = &hands[player];
      card *extra = hands[player];
      /* put it on the table */
      extra->right = table;
      table = extra;
      /* remove it from the hand */
      hands[player] = extra->down;
      extra->down = NULL;
    }
  }

  else if (c->action == GIVE) {
    /* just give the first card in hand */
    if (hands[player]) {
      give_hand = &hands[player];
      card *give = hands[player];
      /* put it in the other player's hand */
      card *tmp = hands[other];
      hands[other] = give;
      hands[player] = give->down;
      give->down = tmp;
    }
  }

  /* put card on the table */
  if (c->action != COVER && c->action != TAKE) {
    c->right = table;
    table = c;
  }

  /* next turn */
  random_play(depth + 1);

  /* remove from table */
  if (c->action == COVER) {
    *cover_parent = NULL;
  } else if (c->action == TAKE) {
    /* return taken pile to table */
    card *tmp = *pile_taken_table;
    *pile_taken_table = *pile_taken_hand;
    (*pile_taken_table)->right = tmp->right;
    /* remove taken pile from hand */
    *pile_taken_hand = NULL;
  } else {
    table = table->right;
  }

  /* put extra card from table in hand and remove from table */
  if (plus_one_hand) {
    card *tmp = *plus_one_hand;
    *plus_one_hand = table;
    table->down = tmp;
    table = table->right;
    (*plus_one_hand)->right = NULL;
  }

  /* take back the card given to the other player */
  if (give_hand) {
    card *tmp = hands[player];
    hands[player] = hands[other];
    hands[other] = hands[other]->down;
    hands[player]->down = tmp;
  }

  /* put played card back in hand */
  c->right = NULL;
  c->down = hands[player];
  hands[player] = c;

  /* reinsert removed piles */
  for (int i = 0; i < num_removed; ++i) {
    card *tmp = *removed_from[i];
    *removed_from[i] = removed[i];
    removed[i]->right = tmp;
  }

  printf("[%d] ", depth);
  print_state();
}

int main(int argc, char **argv) {

  for (int i = 0; i < 36; ++i) {
    cards[i].right = NULL;
    cards[i].down = NULL;
    cards[i].color = i % 6;
    cards[i].action = i / 6;
    cards[i].type = (cards[i].color - cards[i].action + 6) % 6;
    cards[i].remove_color = (cards[i].color + 1) % 6;
    cards[i].remove_type = (cards[i].type + 5) % 6;
  }

  for (int i = 0; i < pile_size; ++i)
    pile[i] = &cards[i];

  /* take random cards from the pile for both players */
  for (int player = 0; player < 2; ++player) {
    for (int i = 0; i < 10; ++i) {
      unsigned int idx = random_next() % pile_size;
      card *c = pile[idx];
      c->down = hands[player];
      hands[player] = c;
      pile[idx] = pile[--pile_size];
    }
  }

  random_play(0);
}
